// QML colour parsing and the Qt.* colour functions against values sampled from Qt
// (tests/diff/color_dump.qml). QColor stores 16-bit channels, hence the 1e-4 tolerance.

#include "runtime/color.h"

#include "../check.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <string>

using namespace ii;

namespace {

  nlohmann::json expected() {
    std::ifstream in(std::string(II_DIFF_EXPECTED_DIR) + "/color.json");
    return nlohmann::json::parse(in);
  }

  bool near(double a, double b) { return std::fabs(a - b) < 1e-4; }

} // namespace

TEST("color: QML literals, alpha first in #AARRGGBB") {
  const Color c = qmlColor("#80ff0000");
  CHECK(near(c.r, 1.0) && near(c.g, 0.0) && near(c.a, 128.0 / 255.0));
  CHECK(qmlColor("transparent").a == 0.0F);
  CHECK(near(qmlColor("#abc").g, 0xbb / 255.0));
}

TEST("color: HSV/HSL accessors match QColor (hue -1 when achromatic)") {
  const auto data = expected();
  for (const auto& [text, values] : data["accessors"].items()) {
    const Color c = qmlColor(text);
    const double ours[] = {qt::hsvHue(c),  qt::hsvSaturation(c), qt::hsvValue(c),
                           qt::hslHue(c),  qt::hslSaturation(c), qt::hslLightness(c)};
    for (std::size_t i = 0; i < 6; ++i) {
      if (!near(ours[i], values[i].get<double>())) {
        std::fprintf(stderr, "  %s[%zu]: %f, Qt %f\n", text.c_str(), i, ours[i], values[i].get<double>());
        CHECK(false);
      }
    }
  }
}

TEST("color: Qt.hsva / Qt.hsla match Qt") {
  const auto data = expected();
  for (const char* fn : {"hsva", "hsla"}) {
    for (const auto& row : data[fn]) {
      const double h = row[0], s = row[1], v = row[2], a = row[3];
      const Color c = std::string(fn) == "hsva" ? qt::hsva(h, s, v, a) : qt::hsla(h, s, v, a);
      const bool ok = near(c.r, row[4]) && near(c.g, row[5]) && near(c.b, row[6]) && near(c.a, row[7]);
      if (!ok) {
        std::fprintf(stderr, "  %s(%g, %g, %g, %g) = %f %f %f, Qt %s\n", fn, h, s, v, a, c.r, c.g, c.b, row.dump().c_str());
      }
      CHECK(ok);
    }
  }
}

TEST_MAIN()
