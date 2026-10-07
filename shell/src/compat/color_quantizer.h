#pragma once

// Quickshell ColorQuantizer: the dominant colours of an image (2^depth of them).
// Ported from Quickshell revision 7511545: src/core/colorquantizer.hpp and src/core/colorquantizer.cpp

#include "runtime/color.h"
#include "runtime/object.h"
#include "runtime/property.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ii::qs {

  class ColorQuantizer : public Object {
  public:
    ColorQuantizer();
    ~ColorQuantizer() override;

    Property<std::vector<Color>> colors;  // read-only
    Property<std::string> source;
    Property<double> depth{0.0};
    Property<double> rescaleSize{0.0};

  protected:
    void componentComplete() override;

  private:
    void quantizeAsync();
    void cancelAsync();
    void operationFinished(std::vector<Color> result);

    struct WorkerContext {
      std::atomic<bool> cancelled{false};
    };

    struct LifeTracker {
      std::atomic<bool> alive{true};
      std::atomic<std::uint64_t> activeOpId{0};
      ColorQuantizer* owner = nullptr;
    };

    std::shared_ptr<WorkerContext> m_workerContext;
    std::shared_ptr<LifeTracker> m_lifeTracker;
    std::uint64_t m_nextOpId = 0;
  };

} // namespace ii::qs
