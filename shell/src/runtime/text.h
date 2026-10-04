#pragma once

#include "runtime/color.h"
#include "runtime/item.h"
#include "runtime/text_layout.h"

#include <climits>
#include <map>
#include <string>

class TextNode;

namespace ii {

  // The application font a Text inherits for anything it doesn't set (QGuiApplication::font();
  // on KDE that is kdeglobals' `font=`). The shell sets this at startup.
  [[nodiscard]] FontSpec& defaultFont();

  // QQuickText. Implicit size comes from ii::measureText (Pango, calibrated against Qt in
  // tests/diff/cases/text_metrics.qml): implicitWidth is the unwrapped natural width,
  // implicitHeight is the laid-out height at the current width, rounded up to whole pixels.
  class Text : public Item {
  public:
    Text();

    Property<std::string> text;
    Property<Color> color{Color{0.0F, 0.0F, 0.0F, 1.0F}};

    struct Font {
      // Empty / negative / zero means "inherit from defaultFont()".
      Property<std::string> family;
      Property<double> pixelSize{-1.0};
      Property<double> pointSize{-1.0};
      Property<int> weight{0};
      Property<bool> bold;
      Property<bool> italic;
      Property<std::map<std::string, double>> variableAxes;
    } font;

    Property<int> horizontalAlignment;
    Property<int> verticalAlignment;
    Property<Elide> elide{Elide::None};
    Property<WrapMode> wrapMode{WrapMode::NoWrap};
    Property<int> maximumLineCount{INT_MAX};
    // StyledText / RichText are drawn as Pango markup.
    Property<bool> markup;

    // Read-only results of the current layout.
    Property<double> contentWidth;
    Property<double> contentHeight;
    Property<int> lineCount;

    // The resolved font this Text draws with.
    [[nodiscard]] FontSpec resolvedFont() const;

  private:
    [[nodiscard]] TextLayoutRequest request(double availableWidth) const;
    void syncNode();

    TextNode* m_textNode = nullptr;
    Property<int> m_tracker;
    int m_trackerCount = 0;
  };

} // namespace ii
