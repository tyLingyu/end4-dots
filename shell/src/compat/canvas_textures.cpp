#include "compat/canvas_textures.h"

#include "render/core/texture_manager.h"
#include "render/render_context.h"
#include "render/scene/image_node.h"
#include "runtime/canvas.h"

#include <algorithm>
#include <cairo.h>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ii::qs {

  namespace {

    class CanvasTextures final : public CanvasTextureSink {
    public:
      RenderContext* render = nullptr;

      void canvasPainted(Canvas& canvas) override {
        if (std::ranges::find(m_pending, &canvas) == m_pending.end()) {
          m_pending.push_back(&canvas);
        }
      }

      void canvasDestroyed(Canvas& canvas) override {
        std::erase(m_pending, &canvas);
        if (const auto it = m_textures.find(&canvas); it != m_textures.end()) {
          m_freed.push_back(it->second);
          m_textures.erase(it);
        }
      }

      void upload() {
        if (render == nullptr || (m_pending.empty() && m_freed.empty()) || !render->makeCurrentNoSurface()) {
          return;
        }
        TextureManager& textures = render->textureManager();
        for (TextureHandle& handle : m_freed) {
          textures.unload(handle);
        }
        m_freed.clear();
        for (Canvas* canvas : m_pending) {
          if (canvas->pixelsDirty() && canvas->surface() != nullptr) {
            upload(*canvas, textures);
          }
        }
        m_pending.clear();
      }

    private:
      // Cairo keeps premultiplied BGRA words; the image program takes straight RGBA.
      void upload(Canvas& canvas, TextureManager& textures) {
        cairo_surface_t* surface = canvas.surface();
        const int width = cairo_image_surface_get_width(surface);
        const int height = cairo_image_surface_get_height(surface);
        const int stride = cairo_image_surface_get_stride(surface);
        const unsigned char* data = cairo_image_surface_get_data(surface);
        m_rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
        for (int y = 0; y < height; ++y) {
          const auto* row = reinterpret_cast<const std::uint32_t*>(data + static_cast<std::ptrdiff_t>(y) * stride);
          std::uint8_t* out = m_rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4U;
          for (int x = 0; x < width; ++x) {
            const std::uint32_t argb = row[x];
            const std::uint32_t a = argb >> 24U;
            const auto unpremultiply = [a](std::uint32_t c) {
              return static_cast<std::uint8_t>(a == 0 ? 0 : std::min<std::uint32_t>(255U, (c * 255U + a / 2U) / a));
            };
            out[0] = unpremultiply((argb >> 16U) & 0xffU);
            out[1] = unpremultiply((argb >> 8U) & 0xffU);
            out[2] = unpremultiply(argb & 0xffU);
            out[3] = static_cast<std::uint8_t>(a);
            out += 4;
          }
        }
        TextureHandle& handle = m_textures[&canvas];
        if (handle.valid()) {
          textures.replace(handle, m_rgba.data(), width, height, TextureDataFormat::Rgba);
        } else {
          handle = textures.loadFromPixels(m_rgba.data(), width, height, TextureDataFormat::Rgba);
        }
        auto* node = static_cast<ImageNode*>(canvas.node());
        node->setTextureId(handle.id);
        node->setTextureSize(width, height);
        canvas.markPixelsUploaded();
      }

      std::vector<Canvas*> m_pending;
      std::unordered_map<Canvas*, TextureHandle> m_textures;
      std::vector<TextureHandle> m_freed;
      std::vector<std::uint8_t> m_rgba;
    };

    CanvasTextures& textures() {
      static CanvasTextures instance;
      return instance;
    }

  } // namespace

  void setCanvasTextureRenderer(RenderContext* render) {
    textures().render = render;
    setCanvasTextureSink(render != nullptr ? &textures() : nullptr);
  }

  void uploadCanvasTextures() { textures().upload(); }

} // namespace ii::qs
