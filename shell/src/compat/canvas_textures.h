#pragma once

// Canvas pixels to GL textures: canvases report their paints (runtime/canvas.h), windows upload
// the pending ones before drawing a frame. Textures live in the shared EGL context, so any
// window's frame may upload any canvas.

class RenderContext;

namespace ii::qs {

  // Installs (or with nullptr removes, freeing nothing: the context goes with it) the sink.
  void setCanvasTextureRenderer(RenderContext* render);
  // Uploads canvases painted since the last call and frees textures of destroyed ones.
  void uploadCanvasTextures();

} // namespace ii::qs
