#include "runtime/text.h"

#include "render/scene/text_node.h"
#include "runtime/layout.h"

#include <cmath>
#include <format>

namespace ii {

  namespace {
    constexpr double kPixelsPerPoint = 96.0 / 72.0;

    TextEllipsize toEllipsize(Elide elide) {
      switch (elide) {
      case Elide::Left:
        return TextEllipsize::Start;
      case Elide::Middle:
        return TextEllipsize::Middle;
      case Elide::Right:
      case Elide::None:
        break;
      }
      return TextEllipsize::End;
    }

    TextAlign toTextAlign(int alignment) {
      if ((alignment & Align::Right) != 0) {
        return TextAlign::End;
      }
      if ((alignment & Align::HCenter) != 0) {
        return TextAlign::Center;
      }
      return TextAlign::Start;
    }
  } // namespace

  FontSpec& defaultFont() {
    // Qt's own fallback (10pt) until the shell loads the desktop's application font.
    static FontSpec font{"", 10.0 * kPixelsPerPoint, 400, false, ""};
    return font;
  }

  Text::Text() {
    m_textNode = static_cast<TextNode*>(node()->addChild(std::make_unique<TextNode>()));

    implicitWidth.bind(
        [this] {
          if (text.get().empty()) {
            return 0.0;
          }
          return measureText(request(0.0)).width;
        },
        "Text.implicitWidth"
    );
    implicitHeight.bind(
        [this] {
          if (text.get().empty()) {
            return std::ceil(measureEmptyLine(resolvedFont()).height);
          }
          // Only wrapping text depends on the width it is given.
          const double available = wrapMode.get() != WrapMode::NoWrap ? width.get() : 0.0;
          return std::ceil(measureText(request(available)).height);
        },
        "Text.implicitHeight"
    );
    // Everything the drawn text depends on; re-lays out and updates the node on any change.
    m_tracker.bind(
        [this] {
          (void)request(width.get());
          (void)height.get();
          (void)color.get();
          (void)horizontalAlignment.get();
          (void)verticalAlignment.get();
          syncNode();
          return ++m_trackerCount;
        },
        "Text (node tracker)"
    );
  }

  FontSpec Text::resolvedFont() const {
    FontSpec spec = defaultFont();
    if (!font.family.get().empty()) {
      spec.family = font.family.get();
    }
    if (font.pixelSize.get() > 0.0) {
      spec.pixelSize = font.pixelSize.get();
    } else if (font.pointSize.get() > 0.0) {
      spec.pixelSize = font.pointSize.get() * kPixelsPerPoint;
    }
    if (font.weight.get() > 0) {
      spec.weight = font.weight.get();
    }
    if (font.bold.get()) {
      spec.weight = 700;
    }
    spec.italic = font.italic.get();
    std::string variations;
    for (const auto& [axis, value] : font.variableAxes.get()) {
      variations += std::format("{}{}={}", variations.empty() ? "" : ",", axis, value);
    }
    if (!variations.empty()) {
      spec.variations = std::move(variations);
    }
    return spec;
  }

  TextLayoutRequest Text::request(double availableWidth) const {
    TextLayoutRequest r;
    r.text = text.get();
    r.font = resolvedFont();
    r.width = availableWidth;
    r.wrap = wrapMode.get();
    r.elide = elide.get();
    const int maxLines = maximumLineCount.get();
    r.maximumLineCount = maxLines == INT_MAX ? 0 : maxLines;
    r.markup = markup.get();
    return r;
  }

  void Text::syncNode() {
    const double w = width.peek();
    const double h = height.peek();
    const bool constrained = w > 0.0 && (wrapMode.peek() != WrapMode::NoWrap || elide.peek() != Elide::None);
    const TextLayoutRequest r = request(constrained ? w : 0.0);
    const TextLayoutMetrics metrics = r.text.empty() ? TextLayoutMetrics{} : measureText(r);

    contentWidth.set(metrics.width);
    contentHeight.set(std::ceil(metrics.height));
    lineCount.set(metrics.lineCount);

    std::string family = r.font.family;
    if (const std::string variations = resolvedVariations(r.font); !variations.empty()) {
      family += "@" + variations;
    }
    m_textNode->setText(std::string(r.text));
    m_textNode->setFontFamily(std::move(family));
    m_textNode->setFontSize(static_cast<float>(r.font.pixelSize));
    m_textNode->setFontWeight(static_cast<FontWeight>(r.font.weight));
    m_textNode->setColor(color.peek());
    m_textNode->setUseMarkup(r.markup);
    m_textNode->setMaxWidth(constrained ? static_cast<float>(w) : 0.0F);
    m_textNode->setMaxLines(elide.peek() != Elide::None && wrapMode.peek() == WrapMode::NoWrap ? 1 : r.maximumLineCount);
    m_textNode->setEllipsize(toEllipsize(elide.peek()));
    m_textNode->setTextAlign(toTextAlign(horizontalAlignment.peek()));

    // Pango aligns lines inside maxWidth; an unconstrained line is aligned here.
    double offsetX = 0.0;
    if (!constrained) {
      const int align = horizontalAlignment.peek();
      if ((align & Align::Right) != 0) {
        offsetX = w - metrics.width;
      } else if ((align & Align::HCenter) != 0) {
        offsetX = (w - metrics.width) / 2.0;
      }
    }
    double offsetY = 0.0;
    const int valign = verticalAlignment.peek();
    if ((valign & Align::Bottom) != 0) {
      offsetY = h - metrics.height;
    } else if ((valign & Align::VCenter) != 0) {
      offsetY = (h - metrics.height) / 2.0;
    }
    // TextNode's origin is the first baseline.
    m_textNode->setPosition(static_cast<float>(offsetX), static_cast<float>(offsetY + metrics.baseline));
  }

} // namespace ii
