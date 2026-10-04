#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ii {

  struct FontSpec {
    std::string family;
    double pixelSize = 0.0;
    int weight = 400;
    bool italic = false;
    // Pango variations string, e.g. "wght=450,wdth=100" (QML font.variableAxes).
    std::string variations;

    friend bool operator==(const FontSpec&, const FontSpec&) = default;
  };

  enum class WrapMode : std::uint8_t { NoWrap, WordWrap, WrapAnywhere, Wrap };
  enum class Elide : std::uint8_t { None, Left, Middle, Right };

  struct TextLayoutRequest {
    std::string_view text;
    FontSpec font;
    double width = 0.0; // available width; <= 0 means unconstrained
    WrapMode wrap = WrapMode::NoWrap;
    Elide elide = Elide::None;
    int maximumLineCount = 0; // 0 = unlimited
    bool markup = false;
  };

  struct TextLayoutMetrics {
    double width = 0.0;    // advance width of the widest line
    double height = 0.0;   // height of all lines
    double baseline = 0.0; // first baseline, from the top of the layout
    int lineCount = 0;
  };

  // The variations to request from Pango for this font. Pango derives `opsz` from the point
  // size, while Qt (FreeType) keeps the font's default optical size; to match Qt the default is
  // pinned explicitly unless the caller set opsz. Measuring and drawing must both use this.
  [[nodiscard]] std::string resolvedVariations(const FontSpec& font);

  // Lays out text with Pango the way QQuickText would, without any GL context: Text items
  // measure synchronously when text or font change, exactly like QML.
  [[nodiscard]] TextLayoutMetrics measureText(const TextLayoutRequest& request);

  // Height of one empty line in this font (QML gives empty text a one-line implicit height).
  [[nodiscard]] TextLayoutMetrics measureEmptyLine(const FontSpec& font);

} // namespace ii
