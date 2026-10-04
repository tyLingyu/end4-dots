#include "runtime/layout.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ii {

  namespace {
    constexpr double kInfinity = std::numeric_limits<double>::infinity();

    double snap(double value) { return std::floor(value + 0.5); }

    std::size_t index(Axis axis) { return static_cast<std::size_t>(axis); }

    Axis other(Axis axis) { return axis == Axis::Horizontal ? Axis::Vertical : Axis::Horizontal; }

    // Offset of a `size`-long item in `available` space. Qt's default is AlignLeft | AlignVCenter.
    double alignedOffset(int alignment, Axis axis, double available, double size) {
      const double slack = available - size;
      if (axis == Axis::Horizontal) {
        if ((alignment & Align::Right) != 0) {
          return slack;
        }
        if ((alignment & Align::HCenter) != 0) {
          return slack / 2.0;
        }
        return 0.0;
      }
      if ((alignment & Align::Top) != 0) {
        return 0.0;
      }
      if ((alignment & Align::Bottom) != 0) {
        return slack;
      }
      return slack / 2.0;
    }

    struct Cell {
      double minimum;
      double preferred;
      double maximum;
    };

    // QGridLayoutRowData::calculateGeometries for one row of cells, without stretch factors.
    std::vector<double> distribute(const std::vector<Cell>& cells, double target) {
      const std::size_t n = cells.size();
      double sumMin = 0.0;
      double sumPref = 0.0;
      double sumMax = 0.0;
      for (const Cell& c : cells) {
        sumMin += c.minimum;
        sumPref += c.preferred;
        sumMax += c.maximum;
      }

      std::vector<double> sizes(n);
      if (target < sumPref) {
        for (std::size_t i = 0; i < n; ++i) {
          sizes[i] = cells[i].minimum;
        }
        const double available = target - sumMin;
        const double sumDesired = sumPref - sumMin;
        if (available > 0.0 && sumDesired > 0.0) {
          std::vector<double> factors(n);
          double sumFactors = 0.0;
          for (std::size_t i = 0; i < n; ++i) {
            const double desired = cells[i].preferred - cells[i].minimum;
            // growthFactorBelowPreferredSize
            factors[i] = desired * std::pow(available / sumDesired, desired / sumDesired);
            sumFactors += factors[i];
          }
          if (sumFactors > 0.0) {
            for (std::size_t i = 0; i < n; ++i) {
              sizes[i] += available * factors[i] / sumFactors;
            }
          }
        }
        return sizes;
      }

      const bool beyondMaximum = target > sumMax;
      for (std::size_t i = 0; i < n; ++i) {
        sizes[i] = beyondMaximum ? cells[i].maximum : cells[i].preferred;
      }
      double available = target - (beyondMaximum ? sumMax : sumPref);
      if (available <= 0.0) {
        return sizes;
      }

      std::vector<double> factors(n, 0.0);
      for (std::size_t i = 0; i < n; ++i) {
        const bool canGrow = beyondMaximum || cells[i].maximum > cells[i].preferred;
        if (canGrow) {
          factors[i] = sizes[i] == 0.0 ? 1.0 : sizes[i];
        }
      }

      // Water-fill: an item whose share would pass its maximum is pinned there and the rest is
      // shared again. Beyond the maximum total, cells have no upper bound.
      std::vector<bool> pinned(n, false);
      bool pinnedAny = !beyondMaximum;
      while (pinnedAny && available > 0.0) {
        pinnedAny = false;
        double sumFactors = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
          if (!pinned[i]) {
            sumFactors += factors[i];
          }
        }
        if (sumFactors <= 0.0) {
          break;
        }
        for (std::size_t i = 0; i < n; ++i) {
          if (pinned[i] || factors[i] <= 0.0) {
            continue;
          }
          const double limit = std::max(cells[i].minimum, std::floor(cells[i].maximum));
          if (sizes[i] + available * factors[i] / sumFactors >= limit) {
            available -= limit - sizes[i];
            sizes[i] = limit;
            pinned[i] = true;
            pinnedAny = true;
          }
        }
      }

      double sumFactors = 0.0;
      for (std::size_t i = 0; i < n; ++i) {
        if (!pinned[i]) {
          sumFactors += factors[i];
        }
      }
      if (available > 0.0 && sumFactors > 0.0) {
        for (std::size_t i = 0; i < n; ++i) {
          if (!pinned[i]) {
            sizes[i] += available * factors[i] / sumFactors;
          }
        }
      }
      return sizes;
    }
  } // namespace

  // ── LayoutAttached ──────────────────────────────────────────────────────────

  LayoutAttached::LayoutAttached() {
    leftMargin.bind([this] { return margins.get(); }, "Layout.leftMargin (margins)");
    rightMargin.bind([this] { return margins.get(); }, "Layout.rightMargin (margins)");
    topMargin.bind([this] { return margins.get(); }, "Layout.topMargin (margins)");
    bottomMargin.bind([this] { return margins.get(); }, "Layout.bottomMargin (margins)");
  }

  // ── LinearLayout ────────────────────────────────────────────────────────────

  LinearLayout::LinearLayout(Axis orientation) : m_orientation(orientation) {
    implicitWidth.bind([this] { return contentHints(Axis::Horizontal).preferred; }, "Layout.implicitWidth");
    implicitHeight.bind([this] { return contentHints(Axis::Vertical).preferred; }, "Layout.implicitHeight");
    // Reads every input of the arrangement; any change schedules a polish.
    m_tracker.bind(
        [this] {
          (void)width.get();
          (void)height.get();
          (void)spacing.get();
          (void)entries();
          polishLater();
          return ++m_trackerCount;
        },
        "Layout (relayout tracker)"
    );
  }

  LinearLayout::Hints LinearLayout::hintsFor(Item& child, Axis axis) {
    const bool horizontal = axis == Axis::Horizontal;
    LayoutAttached* attached = child.layoutIfAny();

    double preferred = attached != nullptr
        ? (horizontal ? attached->preferredWidth.get() : attached->preferredHeight.get())
        : -1.0;
    if (preferred < 0.0) {
      preferred = horizontal ? child.implicitWidth.get() : child.implicitHeight.get();
      if (preferred <= 0.0) {
        attached = &child.layout();
        auto& explicitSize = attached->m_explicitSize[index(axis)];
        if (!explicitSize && !attached->m_sizedByLayout[index(axis)] && !child.followsImplicitSize(axis)) {
          explicitSize = horizontal ? child.width.peek() : child.height.peek();
        }
        if (explicitSize) {
          preferred = *explicitSize;
        }
      }
    }

    const auto* childLayout = dynamic_cast<const LinearLayout*>(&child);
    Hints content;
    if (childLayout != nullptr) {
      content = childLayout->contentHints(axis);
    }

    Hints hints;
    const std::optional<bool> fill =
        attached != nullptr ? (horizontal ? attached->fillWidth.get() : attached->fillHeight.get()) : std::nullopt;
    hints.fill = fill.value_or(childLayout != nullptr && content.fill);

    double minimum = attached != nullptr ? (horizontal ? attached->minimumWidth.get() : attached->minimumHeight.get())
                                         : 0.0;
    double maximum = attached != nullptr ? (horizontal ? attached->maximumWidth.get() : attached->maximumHeight.get())
                                         : kInfinity;
    if (childLayout != nullptr) {
      minimum = std::max(minimum, content.minimum);
      maximum = std::min(maximum, content.maximum);
    }
    // Qt snaps layouts to the pixel grid and rounds minimum and preferred hints up (calibrated:
    // tests/diff layout_fractional_hints); maxima are floored later, during distribution.
    preferred = std::ceil(preferred);
    minimum = std::ceil(minimum);
    maximum = std::max(maximum, minimum);

    if (hints.fill) {
      hints.minimum = minimum;
      hints.maximum = maximum;
      hints.preferred = std::clamp(preferred, minimum, maximum);
    } else {
      // Not filling: fixed at the preferred size.
      hints.preferred = std::clamp(preferred, minimum, maximum);
      hints.minimum = hints.preferred;
      hints.maximum = hints.preferred;
    }

    if (attached != nullptr) {
      hints.marginBefore = horizontal ? attached->leftMargin.get() : attached->topMargin.get();
      hints.marginAfter = horizontal ? attached->rightMargin.get() : attached->bottomMargin.get();
      hints.alignment = attached->alignment.get();
    }
    return hints;
  }

  std::vector<LinearLayout::Entry> LinearLayout::entries() const {
    (void)childrenRevision.get();
    std::vector<Entry> list;
    for (Item* child : childItems()) {
      if (!child->visible.get()) {
        continue;
      }
      list.push_back(Entry{child, hintsFor(*child, m_orientation), hintsFor(*child, other(m_orientation))});
    }
    return list;
  }

  LinearLayout::Hints LinearLayout::contentHints(Axis axis) const {
    const std::vector<Entry> list = entries();
    Hints out;
    if (list.empty()) {
      return out;
    }
    const bool mainAxis = axis == m_orientation;
    for (const Entry& e : list) {
      const Hints& h = mainAxis ? e.main : e.cross;
      const double margins = h.marginBefore + h.marginAfter;
      out.fill = out.fill || h.fill;
      if (mainAxis) {
        out.minimum += h.minimum + margins;
        out.preferred += h.preferred + margins;
        out.maximum += h.maximum + margins;
      } else {
        out.minimum = std::max(out.minimum, h.minimum + margins);
        out.preferred = std::max(out.preferred, h.preferred + margins);
        out.maximum = std::max(out.maximum, h.maximum + margins);
      }
    }
    if (mainAxis) {
      const double gaps = spacing.get() * static_cast<double>(list.size() - 1);
      out.minimum += gaps;
      out.preferred += gaps;
      out.maximum += gaps;
    }
    return out;
  }

  void LinearLayout::polish() {
    const std::vector<Entry> list = entries();
    if (list.empty()) {
      return;
    }
    const Axis mainAxis = m_orientation;
    const Axis crossAxis = other(m_orientation);
    const bool horizontal = mainAxis == Axis::Horizontal;
    const double mainLength = horizontal ? width.peek() : height.peek();
    const double crossLength = horizontal ? height.peek() : width.peek();
    const double gap = spacing.peek();

    std::vector<Cell> cells;
    cells.reserve(list.size());
    for (const Entry& e : list) {
      const double margins = e.main.marginBefore + e.main.marginAfter;
      cells.push_back({e.main.minimum + margins, e.main.preferred + margins, e.main.maximum + margins});
    }
    const double target = mainLength - gap * static_cast<double>(list.size() - 1);
    const std::vector<double> sizes = distribute(cells, target);

    double position = 0.0;
    for (std::size_t i = 0; i < list.size(); ++i) {
      const Entry& e = list[i];
      Item& child = *e.item;

      const double cellStart = snap(position);
      const double cellSize = snap(position + sizes[i]) - cellStart;
      position += sizes[i] + gap;

      const double mainAvailable = cellSize - e.main.marginBefore - e.main.marginAfter;
      const double mainSize = std::clamp(mainAvailable, e.main.minimum, e.main.maximum);
      const double mainPos =
          snap(cellStart + e.main.marginBefore + alignedOffset(e.main.alignment, mainAxis, mainAvailable, mainSize));

      const double crossAvailable = crossLength - e.cross.marginBefore - e.cross.marginAfter;
      const double crossSize =
          e.cross.fill ? std::clamp(crossAvailable, e.cross.minimum, e.cross.maximum) : e.cross.preferred;
      const double crossPos =
          snap(e.cross.marginBefore + alignedOffset(e.cross.alignment, crossAxis, crossAvailable, crossSize));

      LayoutAttached& attached = child.layout();
      attached.m_sizedByLayout = {true, true};
      if (horizontal) {
        child.x.set(mainPos);
        child.y.set(crossPos);
        child.width.set(mainSize);
        child.height.set(crossSize);
      } else {
        child.x.set(crossPos);
        child.y.set(mainPos);
        child.width.set(crossSize);
        child.height.set(mainSize);
      }
    }
  }

} // namespace ii
