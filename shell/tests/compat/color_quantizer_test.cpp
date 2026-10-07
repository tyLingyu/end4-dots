// Tests for Quickshell ColorQuantizer parity (revision 7511545).
// Checks source URL handling, median-cut quantization, depth-based splits,
// rescaleSize downsampling, transparent pixel filtering, and componentComplete lifecycle.

#include "compat/color_quantizer.h"

#include "../check.h"
#include "../pump.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <stb/stb_image_write.h>
#include <string>
#include <unistd.h>
#include <vector>

using namespace ii;

namespace {

  std::filesystem::path scratchDir() {
    static const auto dir = [] {
      auto path = std::filesystem::temp_directory_path() / ("ii-colorquant-test-" + std::to_string(::getpid()));
      std::filesystem::remove_all(path);
      std::filesystem::create_directories(path);
      return path;
    }();
    return dir;
  }

  void writePng(const std::filesystem::path& path, int w, int h, const std::vector<std::uint8_t>& rgba) {
    const int ok = stbi_write_png(path.string().c_str(), w, h, 4, rgba.data(), w * 4);
    (void)ok;
  }

  struct RgbByte {
    int r;
    int g;
    int b;
  };

  RgbByte toRgb(const Color& c) {
    return RgbByte{
        static_cast<int>(std::round(c.r * 255.0F)),
        static_cast<int>(std::round(c.g * 255.0F)),
        static_cast<int>(std::round(c.b * 255.0F)),
    };
  }

} // namespace

// Reference values for this 216-pixel color cube were verified against real Quickshell
// 0.2.1 (revision 7511545ee20664e3b8b8d3322c0ffe7567c56f7a) run via `qs -p test.qml`
// with QT_FORCE_STDERR_LOGGING=1.
TEST("colorquantizer: synthetic color cube produces exact reference values as Quickshell 0.2.1") {
  const auto imgPath = scratchDir() / "cube.png";

  // 18x12 image: 6x6x6 color cube (r, g, b in {0, 50, 100, 150, 200, 250})
  std::vector<std::uint8_t> rgba;
  rgba.reserve(18 * 12 * 4);
  for (int r = 0; r < 256; r += 50) {
    for (int g = 0; g < 256; g += 50) {
      for (int b = 0; b < 256; b += 50) {
        rgba.push_back(static_cast<std::uint8_t>(r));
        rgba.push_back(static_cast<std::uint8_t>(g));
        rgba.push_back(static_cast<std::uint8_t>(b));
        rgba.push_back(255);
      }
    }
  }
  writePng(imgPath, 18, 12, rgba);

  // Depth 0 -> 1 color (average of all pixels: 125, 125, 125)
  {
    qs::ColorQuantizer cq;
    cq.source.set("file://" + imgPath.string());
    cq.depth.set(0.0);
    cq.rescaleSize.set(0.0);
    cq.complete();

    CHECK(ii_test::pumpUntil([&] { return cq.colors.get().size() == 1; }));
    const auto rgb = toRgb(cq.colors.get()[0]);
    CHECK(rgb.r == 125);
    CHECK(rgb.g == 125);
    CHECK(rgb.b == 125);
  }

  // Depth 1 -> 2 colors: [0]=(50, 125, 125), [1]=(200, 125, 125)
  {
    qs::ColorQuantizer cq;
    cq.source.set("file://" + imgPath.string());
    cq.depth.set(1.0);
    cq.rescaleSize.set(0.0);
    cq.complete();

    CHECK(ii_test::pumpUntil([&] { return cq.colors.get().size() == 2; }));
    const auto colors = cq.colors.get();
    const auto c0 = toRgb(colors[0]);
    const auto c1 = toRgb(colors[1]);
    CHECK(c0.r == 50 && c0.g == 125 && c0.b == 125);
    CHECK(c1.r == 200 && c1.g == 125 && c1.b == 125);
  }

  // Depth 2 -> 4 colors:
  // [0]=(50, 50, 125), [1]=(50, 200, 125), [2]=(200, 50, 125), [3]=(200, 200, 125)
  {
    qs::ColorQuantizer cq;
    cq.source.set("file://" + imgPath.string());
    cq.depth.set(2.0);
    cq.rescaleSize.set(0.0);
    cq.complete();

    CHECK(ii_test::pumpUntil([&] { return cq.colors.get().size() == 4; }));
    const auto colors = cq.colors.get();
    const auto c0 = toRgb(colors[0]);
    const auto c1 = toRgb(colors[1]);
    const auto c2 = toRgb(colors[2]);
    const auto c3 = toRgb(colors[3]);
    CHECK(c0.r == 50 && c0.g == 50 && c0.b == 125);
    CHECK(c1.r == 50 && c1.g == 200 && c1.b == 125);
    CHECK(c2.r == 200 && c2.g == 50 && c2.b == 125);
    CHECK(c3.r == 200 && c3.g == 200 && c3.b == 125);
  }

  // Depth 3 -> 8 colors:
  // [0]=(50, 50, 50),   [1]=(50, 50, 200),  [2]=(50, 200, 50),  [3]=(50, 200, 200),
  // [4]=(200, 50, 50),  [5]=(200, 50, 200), [6]=(200, 200, 50), [7]=(200, 200, 200)
  {
    qs::ColorQuantizer cq;
    cq.source.set("file://" + imgPath.string());
    cq.depth.set(3.0);
    cq.rescaleSize.set(0.0);
    cq.complete();

    CHECK(ii_test::pumpUntil([&] { return cq.colors.get().size() == 8; }));
    const auto colors = cq.colors.get();
    const auto c0 = toRgb(colors[0]);
    const auto c1 = toRgb(colors[1]);
    const auto c2 = toRgb(colors[2]);
    const auto c3 = toRgb(colors[3]);
    const auto c4 = toRgb(colors[4]);
    const auto c5 = toRgb(colors[5]);
    const auto c6 = toRgb(colors[6]);
    const auto c7 = toRgb(colors[7]);
    CHECK(c0.r == 50 && c0.g == 50 && c0.b == 50);
    CHECK(c1.r == 50 && c1.g == 50 && c1.b == 200);
    CHECK(c2.r == 50 && c2.g == 200 && c2.b == 50);
    CHECK(c3.r == 50 && c3.g == 200 && c3.b == 200);
    CHECK(c4.r == 200 && c4.g == 50 && c4.b == 50);
    CHECK(c5.r == 200 && c5.g == 50 && c5.b == 200);
    CHECK(c6.r == 200 && c6.g == 200 && c6.b == 50);
    CHECK(c7.r == 200 && c7.g == 200 && c7.b == 200);
  }
}

// Rescaling test with reference values verified against Quickshell 0.2.1.
// 20x20 image downsampled to 10x10 with KeepAspectRatio + SmoothTransformation.
TEST("colorquantizer: rescaleSize downscales and matches Quickshell 0.2.1") {
  const auto imgPath = scratchDir() / "quadrants_20x20.png";

  std::vector<std::uint8_t> rgba(20 * 20 * 4);
  for (int row = 0; row < 20; ++row) {
    for (int col = 0; col < 20; ++col) {
      const std::size_t idx = (static_cast<std::size_t>(row) * 20U + static_cast<std::size_t>(col)) * 4U;
      if (row < 10 && col < 10) {
        // Red
        rgba[idx] = 255;
        rgba[idx + 1] = 0;
        rgba[idx + 2] = 0;
      } else if (row < 10) {
        // Green
        rgba[idx] = 0;
        rgba[idx + 1] = 255;
        rgba[idx + 2] = 0;
      } else if (col < 10) {
        // Blue
        rgba[idx] = 0;
        rgba[idx + 1] = 0;
        rgba[idx + 2] = 255;
      } else {
        // Yellow
        rgba[idx] = 255;
        rgba[idx + 1] = 255;
        rgba[idx + 2] = 0;
      }
      rgba[idx + 3] = 255;
    }
  }
  writePng(imgPath, 20, 20, rgba);

  // Depth 0 with rescaleSize=10: reference Quickshell output is [0] = (128, 128, 64)
  {
    qs::ColorQuantizer cq;
    cq.source.set("file://" + imgPath.string());
    cq.depth.set(0.0);
    cq.rescaleSize.set(10.0);
    cq.complete();

    CHECK(ii_test::pumpUntil([&] { return cq.colors.get().size() == 1; }));
    const auto rgb = toRgb(cq.colors.get()[0]);
    CHECK(rgb.r == 128);
    CHECK(rgb.g == 128);
    CHECK(rgb.b == 64);
  }

  // Depth 1 with rescaleSize=10: reference Quickshell output is (0, 128, 128) and (255, 128, 0)
  {
    qs::ColorQuantizer cq;
    cq.source.set("file://" + imgPath.string());
    cq.depth.set(1.0);
    cq.rescaleSize.set(10.0);
    cq.complete();

    CHECK(ii_test::pumpUntil([&] { return cq.colors.get().size() == 2; }));
    const auto colors = cq.colors.get();
    const auto c0 = toRgb(colors[0]);
    const auto c1 = toRgb(colors[1]);
    CHECK(c0.r == 0 && c0.g == 128 && c0.b == 128);
    CHECK(c1.r == 255 && c1.g == 128 && c1.b == 0);
  }

  // Depth 2 with rescaleSize=10: reference Quickshell output is
  // (0, 0, 255), (0, 255, 0), (255, 0, 0), (255, 255, 0)
  {
    qs::ColorQuantizer cq;
    cq.source.set("file://" + imgPath.string());
    cq.depth.set(2.0);
    cq.rescaleSize.set(10.0);
    cq.complete();

    CHECK(ii_test::pumpUntil([&] { return cq.colors.get().size() == 4; }));
    const auto colors = cq.colors.get();
    const auto c0 = toRgb(colors[0]);
    const auto c1 = toRgb(colors[1]);
    const auto c2 = toRgb(colors[2]);
    const auto c3 = toRgb(colors[3]);
    CHECK(c0.r == 0 && c0.g == 0 && c0.b == 255);
    CHECK(c1.r == 0 && c1.g == 255 && c1.b == 0);
    CHECK(c2.r == 255 && c2.g == 0 && c2.b == 0);
    CHECK(c3.r == 255 && c3.g == 255 && c3.b == 0);
  }
}

TEST("colorquantizer: source URL handling with file:// prefix and percent escaping") {
  // Create an image with spaces in the filename
  const auto spacedDir = scratchDir() / "path with spaces";
  std::filesystem::create_directories(spacedDir);
  const auto imgFile = spacedDir / "test image.png";

  std::vector<std::uint8_t> rgba{255, 0, 0, 255};
  writePng(imgFile, 1, 1, rgba);

  // Test 1: Percent-escaped URL: file://.../path%20with%20spaces/test%20image.png
  {
    qs::ColorQuantizer cq;
    std::string escapedUrl = "file://" + spacedDir.string() + "/test%20image.png";
    // Replace spaces in dir path as well
    std::string finalUrl;
    for (char ch : escapedUrl) {
      if (ch == ' ') {
        finalUrl += "%20";
      } else {
        finalUrl += ch;
      }
    }

    cq.source.set(finalUrl);
    cq.depth.set(0.0);
    cq.complete();

    CHECK(ii_test::pumpUntil([&] { return cq.colors.get().size() == 1; }));
    const auto rgb = toRgb(cq.colors.get()[0]);
    CHECK(rgb.r == 255 && rgb.g == 0 && rgb.b == 0);
  }

  // Test 2: Raw filesystem path
  {
    qs::ColorQuantizer cq;
    cq.source.set(imgFile.string());
    cq.depth.set(0.0);
    cq.complete();

    CHECK(ii_test::pumpUntil([&] { return cq.colors.get().size() == 1; }));
    const auto rgb = toRgb(cq.colors.get()[0]);
    CHECK(rgb.r == 255 && rgb.g == 0 && rgb.b == 0);
  }
}

TEST("colorquantizer: invalid or missing image results in empty colors") {
  qs::ColorQuantizer cq;
  cq.source.set("file:///nonexistent/path/that/does/not/exist.png");
  cq.depth.set(1.0);
  cq.complete();

  // Quantization should finish and result in empty colors
  bool finished = ii_test::pumpUntil([&] {
    return cq.colors.get().empty();
  }, 500);
  CHECK(finished);
  CHECK(cq.colors.get().empty());
}

TEST("colorquantizer: transparent pixels (alpha = 0) are excluded from quantization") {
  const auto imgPath = scratchDir() / "transparent.png";

  // 2x1 image: pixel 0 is red (255, 0, 0, 255), pixel 1 is transparent black (0, 0, 0, 0)
  std::vector<std::uint8_t> rgba{
      255, 0, 0, 255,
      0, 0, 0, 0,
  };
  writePng(imgPath, 2, 1, rgba);

  qs::ColorQuantizer cq;
  cq.source.set("file://" + imgPath.string());
  cq.depth.set(0.0);
  cq.complete();

  CHECK(ii_test::pumpUntil([&] { return cq.colors.get().size() == 1; }));
  // If transparent pixel was included, average would be (128, 0, 0).
  // Because it is excluded (as in Quickshell), average is pure red (255, 0, 0).
  const auto rgb = toRgb(cq.colors.get()[0]);
  CHECK(rgb.r == 255);
  CHECK(rgb.g == 0);
  CHECK(rgb.b == 0);
}

TEST("colorquantizer: componentComplete ordering and property updates") {
  const auto imgPath = scratchDir() / "lifecycle.png";
  // 2x1 image with green and red pixels so depth 0 produces 1 color and depth 1 produces 2.
  std::vector<std::uint8_t> rgba{
      0, 255, 0, 255,
      255, 0, 0, 255,
  };
  writePng(imgPath, 2, 1, rgba);
  qs::ColorQuantizer cq;
  cq.source.set("file://" + imgPath.string());
  cq.depth.set(0.0);

  // Before complete(), no computation runs
  ii_test::pumpFor(50);
  CHECK(cq.colors.get().empty());

  // complete() triggers initial computation
  cq.complete();
  CHECK(ii_test::pumpUntil([&] { return cq.colors.get().size() == 1; }));
  const auto rgb = toRgb(cq.colors.get()[0]);
  CHECK(rgb.r == 128 && rgb.g == 128 && rgb.b == 0);

  // Modifying depth after completion triggers recomputation to 2 colors
  cq.depth.set(1.0);
  CHECK(ii_test::pumpUntil([&] { return cq.colors.get().size() == 2; }));
  const auto colors = cq.colors.get();
  const auto c0 = toRgb(colors[0]);
  const auto c1 = toRgb(colors[1]);
  CHECK(c0.r == 0 && c0.g == 255 && c0.b == 0);
  CHECK(c1.r == 255 && c1.g == 0 && c1.b == 0);
}

TEST_MAIN()
