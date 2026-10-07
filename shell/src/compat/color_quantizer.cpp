#include "compat/color_quantizer.h"

// Quickshell ColorQuantizer: the dominant colours of an image (2^depth of them).
// Ported faithfully from Quickshell revision 7511545:
// src/core/colorquantizer.hpp and src/core/colorquantizer.cpp

#include "core/deferred_call.h"
#include "core/log.h"
#include "render/core/image_file_loader.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stb/stb_image_resize2.h>
#include <string_view>
#include <thread>
#include <utility>

namespace ii::qs {

  namespace {

    constexpr Logger kLog("colorquantizer");

    // Strips file:// scheme and decodes percent escapes, matching Qt's QUrl::toLocalFile().
    std::string urlToLocalPath(std::string_view url) {
      if (url.starts_with("file://")) {
        url.remove_prefix(7);
      }
      if (url.starts_with("localhost/")) {
        url.remove_prefix(9);
      }
      std::string path;
      path.reserve(url.size());
      for (std::size_t i = 0; i < url.size(); ++i) {
        if (url[i] == '%' && i + 2 < url.size()) {
          auto hexVal = [](char c) -> int {
            if (c >= '0' && c <= '9') {
              return c - '0';
            }
            if (c >= 'a' && c <= 'f') {
              return c - 'a' + 10;
            }
            if (c >= 'A' && c <= 'F') {
              return c - 'A' + 10;
            }
            return -1;
          };
          const int hi = hexVal(url[i + 1]);
          const int lo = hexVal(url[i + 2]);
          if (hi >= 0 && lo >= 0) {
            path.push_back(static_cast<char>((hi << 4) | lo));
            i += 2;
            continue;
          }
        }
        path.push_back(url[i]);
      }
      return path;
    }

    struct Rgb {
      int r = 0;
      int g = 0;
      int b = 0;
    };

    // As Quickshell's ColorQuantizerOperation::findBiggestColorRange in colorquantizer.cpp.
    char findBiggestColorRange(const std::vector<Rgb>& rgbValues) {
      if (rgbValues.empty()) {
        return 'r';
      }

      int rMin = 255;
      int gMin = 255;
      int bMin = 255;
      int rMax = 0;
      int gMax = 0;
      int bMax = 0;

      for (const auto& color : rgbValues) {
        rMin = std::min(rMin, color.r);
        gMin = std::min(gMin, color.g);
        bMin = std::min(bMin, color.b);

        rMax = std::max(rMax, color.r);
        gMax = std::max(gMax, color.g);
        bMax = std::max(bMax, color.b);
      }

      const int rRange = rMax - rMin;
      const int gRange = gMax - gMin;
      const int bRange = bMax - bMin;

      const int biggestRange = std::max(rRange, std::max(gRange, bRange));
      if (biggestRange == rRange) {
        return 'r';
      }
      if (biggestRange == gRange) {
        return 'g';
      }
      return 'b';
    }

    // Matches Qt's qRound(double).
    inline int qRound(double d) {
      return d >= 0.0 ? static_cast<int>(d + 0.5) : static_cast<int>(d - 0.5);
    }

    // As Quickshell's ColorQuantizerOperation::quantization in colorquantizer.cpp.
    template <typename CancelFn>
    std::vector<Rgb> quantization(
        std::vector<Rgb>& rgbValues,
        double currentDepth,
        double maxDepth,
        const CancelFn& shouldCancel
    ) {
      if (shouldCancel()) {
        return {};
      }

      if (currentDepth >= maxDepth || rgbValues.empty()) {
        if (rgbValues.empty()) {
          return {};
        }

        std::int64_t totalR = 0;
        std::int64_t totalG = 0;
        std::int64_t totalB = 0;

        for (const auto& color : rgbValues) {
          if (shouldCancel()) {
            return {};
          }
          totalR += color.r;
          totalG += color.g;
          totalB += color.b;
        }

        const double count = static_cast<double>(rgbValues.size());
        const int avgR = qRound(static_cast<double>(totalR) / count);
        const int avgG = qRound(static_cast<double>(totalG) / count);
        const int avgB = qRound(static_cast<double>(totalB) / count);

        return {Rgb{.r = avgR, .g = avgG, .b = avgB}};
      }

      const char dominantChannel = findBiggestColorRange(rgbValues);
      std::ranges::sort(rgbValues, [dominantChannel](const Rgb& a, const Rgb& b) {
        if (dominantChannel == 'r') {
          return a.r < b.r;
        }
        if (dominantChannel == 'g') {
          return a.g < b.g;
        }
        return a.b < b.b;
      });

      const std::size_t mid = rgbValues.size() / 2;
      std::vector<Rgb> leftHalf(rgbValues.begin(), rgbValues.begin() + static_cast<std::ptrdiff_t>(mid));
      std::vector<Rgb> rightHalf(rgbValues.begin() + static_cast<std::ptrdiff_t>(mid), rgbValues.end());

      auto result = quantization(leftHalf, currentDepth + 1.0, maxDepth, shouldCancel);
      const auto rightResult = quantization(rightHalf, currentDepth + 1.0, maxDepth, shouldCancel);
      result.insert(result.end(), rightResult.begin(), rightResult.end());
      return result;
    }

  } // namespace

  ColorQuantizer::ColorQuantizer()
      : m_lifeTracker(std::make_shared<LifeTracker>()) {
    m_lifeTracker->owner = this;

    source.changed().connectForever([this] {
      if (!isCompleted()) {
        return;
      }
      if (source.peek().empty()) {
        cancelAsync();
      } else {
        quantizeAsync();
      }
    });

    depth.changed().connectForever([this] {
      if (isCompleted() && !source.peek().empty()) {
        quantizeAsync();
      }
    });

    rescaleSize.changed().connectForever([this] {
      if (isCompleted() && !source.peek().empty()) {
        quantizeAsync();
      }
    });
  }

  ColorQuantizer::~ColorQuantizer() {
    destroyOwned();
    if (m_lifeTracker) {
      m_lifeTracker->alive.store(false, std::memory_order_release);
      m_lifeTracker->owner = nullptr;
    }
    cancelAsync();
  }

  void ColorQuantizer::componentComplete() {
    if (!source.peek().empty()) {
      quantizeAsync();
    }
  }

  // As Quickshell's tryCancel + disown: the running job is told to stop and its result ignored,
  // without waiting for it (a wallpaper decode would otherwise stall the main loop).
  void ColorQuantizer::cancelAsync() {
    if (m_workerContext) {
      m_workerContext->cancelled.store(true, std::memory_order_release);
      m_workerContext = nullptr;
    }
  }

  void ColorQuantizer::operationFinished(std::vector<Color> result) {
    colors.set(std::move(result));
  }

  void ColorQuantizer::quantizeAsync() {
    cancelAsync();

    if (source.peek().empty()) {
      return;
    }

    const std::string localPath = urlToLocalPath(source.peek());
    const double currentDepth = depth.peek();
    const double currentRescaleSize = rescaleSize.peek();
    const std::uint64_t opId = ++m_nextOpId;
    m_lifeTracker->activeOpId.store(opId, std::memory_order_release);

    auto worker = std::make_shared<WorkerContext>();
    m_workerContext = worker;

    std::shared_ptr<LifeTracker> tracker = m_lifeTracker;

    // Detached: it shares only `worker` and `tracker` with this object.
    std::thread([worker, tracker, opId, localPath, currentDepth, currentRescaleSize]() {
      if (worker->cancelled.load(std::memory_order_acquire)) {
        return;
      }

      std::vector<Color> resultColors;

      // Load image; targetSize = 0 leaves resizing to our Qt-parity logic below.
      auto loaded = loadImageFile(localPath, 0, false);
      if (!loaded) {
        kLog.warn("Failed to load image from {}", localPath);
      } else if (!worker->cancelled.load(std::memory_order_acquire)) {
        int w = loaded->width;
        int h = loaded->height;
        std::vector<std::uint8_t> pixels = std::move(loaded->rgba);

        // Rescale image if dimensions exceed rescaleSize, as Quickshell's:
        // image.scaled(rescaleSize, rescaleSize, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        if (currentRescaleSize > 0.0
            && (static_cast<double>(w) > currentRescaleSize || static_cast<double>(h) > currentRescaleSize)
            && w > 0 && h > 0) {
          const int rescale = static_cast<int>(currentRescaleSize);
          int targetW = w;
          int targetH = h;
          const std::int64_t rax =
              (static_cast<std::int64_t>(rescale) * static_cast<std::int64_t>(w)) / static_cast<std::int64_t>(h);
          if (rax <= static_cast<std::int64_t>(rescale)) {
            targetW = static_cast<int>(rax);
            targetH = rescale;
          } else {
            targetW = rescale;
            targetH = static_cast<int>(
                (static_cast<std::int64_t>(h) * static_cast<std::int64_t>(rescale)) / static_cast<std::int64_t>(w)
            );
          }

          if (targetW > 0 && targetH > 0 && (targetW != w || targetH != h)) {
            std::vector<std::uint8_t> resized(
                static_cast<std::size_t>(targetW) * static_cast<std::size_t>(targetH) * 4U
            );
            // STBIR_FILTER_BOX performs area-averaging matching Qt's SmoothTransformation downsampling.
            stbir_resize(
                pixels.data(), w, h, 0,
                resized.data(), targetW, targetH, 0,
                STBIR_RGBA, STBIR_TYPE_UINT8, STBIR_EDGE_CLAMP, STBIR_FILTER_BOX
            );
            pixels = std::move(resized);
            w = targetW;
            h = targetH;
          }
        }

        if (!worker->cancelled.load(std::memory_order_acquire)) {
          // As Quickshell: collect non-transparent pixels (qAlpha != 0).
          std::vector<Rgb> rgbPixels;
          rgbPixels.reserve(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
          for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
              const std::size_t idx =
                  (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4U;
              const std::uint8_t a = pixels[idx + 3];
              if (a == 0) {
                continue;
              }
              rgbPixels.push_back(Rgb{
                  .r = static_cast<int>(pixels[idx + 0]),
                  .g = static_cast<int>(pixels[idx + 1]),
                  .b = static_cast<int>(pixels[idx + 2]),
              });
            }
          }

          auto cancelCheck = [&worker]() -> bool {
            return worker->cancelled.load(std::memory_order_acquire);
          };

          auto rgbResult = quantization(rgbPixels, 0.0, currentDepth, cancelCheck);
          if (!worker->cancelled.load(std::memory_order_acquire)) {
            resultColors.reserve(rgbResult.size());
            for (const auto& c : rgbResult) {
              resultColors.push_back(Color{
                  .r = static_cast<float>(c.r) / 255.0F,
                  .g = static_cast<float>(c.g) / 255.0F,
                  .b = static_cast<float>(c.b) / 255.0F,
                  .a = 1.0F,
              });
            }
          }
        }
      }

      if (worker->cancelled.load(std::memory_order_acquire)) {
        return;
      }

      // Safely deliver results back to the main thread.
      DeferredCall::callLater([tracker, opId, colors = std::move(resultColors)]() mutable {
        if (!tracker->alive.load(std::memory_order_acquire)) {
          return;
        }
        if (tracker->activeOpId.load(std::memory_order_acquire) != opId) {
          return;
        }
        if (tracker->owner != nullptr) {
          tracker->owner->operationFinished(std::move(colors));
        }
      });
    }).detach();
  }

} // namespace ii::qs
