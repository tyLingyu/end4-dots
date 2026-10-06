#include "compat/color_quantizer.h"

#include "core/log.h"

namespace ii::qs {

  ColorQuantizer::ColorQuantizer() {
    source.changed().connectForever([this] { quantize(); });
    depth.changed().connectForever([this] { quantize(); });
    rescaleSize.changed().connectForever([this] { quantize(); });
  }

  void ColorQuantizer::quantize() {
    // Quickshell's algorithm is ported in stage 3b; until then no colours are reported.
    static constexpr Logger kLog("colorquantizer");
    kLog.debug("ColorQuantizer is not implemented yet (stage 3b)");
  }

} // namespace ii::qs
