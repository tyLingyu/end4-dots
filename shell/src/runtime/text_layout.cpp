#include "runtime/text_layout.h"

#include <cairo.h>
#include <hb-ot.h>
#include <pango/pangocairo.h>

#include <format>
#include <map>
#include <optional>
#include <tuple>

namespace ii {

  namespace {
    // One context for all measurement, with the font options Noctalia's renderer uses
    // (grayscale AA, Fontconfig hinting) so measured and drawn text agree.
    PangoContext* context() {
      static PangoContext* ctx = [] {
        PangoFontMap* map = pango_cairo_font_map_new();
        PangoContext* c = pango_font_map_create_context(map);
        g_object_unref(map);
        cairo_font_options_t* options = cairo_font_options_create();
        cairo_font_options_set_antialias(options, CAIRO_ANTIALIAS_GRAY);
        cairo_font_options_set_hint_metrics(options, CAIRO_HINT_METRICS_OFF);
        pango_cairo_context_set_font_options(c, options);
        cairo_font_options_destroy(options);
        pango_context_set_round_glyph_positions(c, FALSE);
        return c;
      }();
      return ctx;
    }

    // Default optical size of the font this spec resolves to, if it has an opsz axis.
    std::optional<double> defaultOpticalSize(const FontSpec& font) {
      static std::map<std::tuple<std::string, int, bool>, std::optional<double>> cache;
      const auto key = std::make_tuple(font.family, font.weight, font.italic);
      if (auto it = cache.find(key); it != cache.end()) {
        return it->second;
      }
      std::optional<double> result;
      PangoFontDescription* desc = pango_font_description_new();
      pango_font_description_set_family(desc, font.family.c_str());
      pango_font_description_set_weight(desc, static_cast<PangoWeight>(font.weight));
      pango_font_description_set_style(desc, font.italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
      pango_font_description_set_absolute_size(desc, 16.0 * PANGO_SCALE);
      if (PangoFont* loaded = pango_context_load_font(context(), desc)) {
        hb_face_t* face = hb_font_get_face(pango_font_get_hb_font(loaded));
        hb_ot_var_axis_info_t axis;
        if (hb_ot_var_find_axis_info(face, HB_OT_TAG_VAR_AXIS_OPTICAL_SIZE, &axis)) {
          result = axis.default_value;
        }
        g_object_unref(loaded);
      }
      pango_font_description_free(desc);
      cache.emplace(key, result);
      return result;
    }

    PangoFontDescription* describe(const FontSpec& font) {
      PangoFontDescription* desc = pango_font_description_new();
      if (!font.family.empty()) {
        pango_font_description_set_family(desc, font.family.c_str());
      }
      pango_font_description_set_absolute_size(desc, font.pixelSize * PANGO_SCALE);
      pango_font_description_set_weight(desc, static_cast<PangoWeight>(font.weight));
      pango_font_description_set_style(desc, font.italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
      const std::string variations = resolvedVariations(font);
      if (!variations.empty()) {
        pango_font_description_set_variations(desc, variations.c_str());
      }
      return desc;
    }

    PangoWrapMode pangoWrap(WrapMode wrap) {
      switch (wrap) {
      case WrapMode::WrapAnywhere:
        return PANGO_WRAP_CHAR;
      case WrapMode::Wrap:
        return PANGO_WRAP_WORD_CHAR;
      case WrapMode::WordWrap:
      case WrapMode::NoWrap:
        break;
      }
      return PANGO_WRAP_WORD;
    }

    PangoEllipsizeMode pangoEllipsize(Elide elide) {
      switch (elide) {
      case Elide::Left:
        return PANGO_ELLIPSIZE_START;
      case Elide::Middle:
        return PANGO_ELLIPSIZE_MIDDLE;
      case Elide::Right:
        return PANGO_ELLIPSIZE_END;
      case Elide::None:
        break;
      }
      return PANGO_ELLIPSIZE_NONE;
    }

    TextLayoutMetrics metricsOf(PangoLayout* layout) {
      PangoRectangle logical;
      pango_layout_get_extents(layout, nullptr, &logical);
      TextLayoutMetrics m;
      m.width = static_cast<double>(logical.width) / PANGO_SCALE;
      m.height = static_cast<double>(logical.height) / PANGO_SCALE;
      m.baseline = static_cast<double>(pango_layout_get_baseline(layout) - logical.y) / PANGO_SCALE;
      m.lineCount = pango_layout_get_line_count(layout);
      return m;
    }
  } // namespace

  std::string resolvedVariations(const FontSpec& font) {
    if (font.variations.find("opsz") != std::string::npos || font.family.empty()) {
      return font.variations;
    }
    const std::optional<double> opsz = defaultOpticalSize(font);
    if (!opsz) {
      return font.variations;
    }
    return std::format("{}{}opsz={}", font.variations, font.variations.empty() ? "" : ",", *opsz);
  }

  TextLayoutMetrics measureText(const TextLayoutRequest& request) {
    PangoLayout* layout = pango_layout_new(context());
    PangoFontDescription* desc = describe(request.font);
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);

    const int length = static_cast<int>(request.text.size());
    if (request.markup) {
      pango_layout_set_markup(layout, request.text.data(), length);
    } else {
      pango_layout_set_text(layout, request.text.data(), length);
    }

    const bool constrained = request.width > 0.0;
    const bool wraps = constrained && request.wrap != WrapMode::NoWrap;
    const bool elides = constrained && request.elide != Elide::None;
    if (wraps || elides) {
      pango_layout_set_width(layout, static_cast<int>(request.width * PANGO_SCALE));
    }
    if (wraps) {
      pango_layout_set_wrap(layout, pangoWrap(request.wrap));
    }
    if (elides) {
      pango_layout_set_ellipsize(layout, pangoEllipsize(request.elide));
      // Without wrapping an elided text is one line; with wrapping, the line budget applies.
      pango_layout_set_height(layout, wraps && request.maximumLineCount > 0 ? -request.maximumLineCount : -1);
    }

    TextLayoutMetrics metrics = metricsOf(layout);
    g_object_unref(layout);
    return metrics;
  }

  TextLayoutMetrics measureEmptyLine(const FontSpec& font) {
    TextLayoutRequest request;
    request.text = " ";
    request.font = font;
    TextLayoutMetrics metrics = measureText(request);
    metrics.width = 0.0;
    return metrics;
  }

} // namespace ii
