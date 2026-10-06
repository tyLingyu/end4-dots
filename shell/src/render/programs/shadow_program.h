#pragma once

// ii-shell: QtQuick.Effects' RectangularShadow shader (see shadow_program.cpp).

#include "render/core/mat3.h"
#include "render/core/shader_program.h"

#include <GLES2/gl2.h>

struct ShadowStyle;

class ShadowProgram {
public:
  ShadowProgram() = default;
  ~ShadowProgram() = default;

  ShadowProgram(const ShadowProgram&) = delete;
  ShadowProgram& operator=(const ShadowProgram&) = delete;

  void ensureInitialized();
  void destroy();
  void abandon() noexcept;

  void draw(
      float surfaceWidth, float surfaceHeight, float width, float height, const ShadowStyle& style,
      const Mat3& transform = Mat3::identity()
  ) const;

private:
  ShaderProgram m_program;
  GLint m_positionLocation = -1;
  GLint m_surfaceSizeLocation = -1;
  GLint m_quadSizeLocation = -1;
  GLint m_transformLocation = -1;
  GLint m_colorLocation = -1;
  GLint m_rectSizeLocation = -1;
  GLint m_radiusLocation = -1;
  GLint m_blurLocation = -1;
};
