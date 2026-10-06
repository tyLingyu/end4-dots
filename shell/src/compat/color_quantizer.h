#pragma once

// Quickshell ColorQuantizer: the dominant colours of an image (2^depth of them).

#include "runtime/color.h"
#include "runtime/object.h"
#include "runtime/property.h"

#include <string>
#include <vector>

namespace ii::qs {

  class ColorQuantizer : public Object {
  public:
    ColorQuantizer();

    Property<std::vector<Color>> colors;  // read-only
    Property<std::string> source;
    Property<double> depth;
    Property<double> rescaleSize;

  private:
    void quantize();
  };

} // namespace ii::qs
