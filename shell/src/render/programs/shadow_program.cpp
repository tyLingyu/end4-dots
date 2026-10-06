#include "render/programs/shadow_program.h"

#include "render/scene/shadow_node.h"

#include <array>
#include <stdexcept>

namespace {

  // The fragment math is Qt's rectangularshadow.frag (qtdeclarative v6.11.2,
  // src/effects/data/shaders, GPL-3.0-only option): a rounded-box distance field, faded over
  // `blur` with smoothstep and squared. fragCoord is the position in the effect area, as in Qt.

  constexpr char kVertexShaderSource[] = R"(
precision highp float;

attribute vec2 a_position;
uniform vec2 u_surface_size;
uniform vec2 u_quad_size;
uniform mat3 u_transform;
varying vec2 v_frag_coord;

vec2 to_ndc(vec2 pixel_pos) {
    vec2 normalized = pixel_pos / u_surface_size;
    return vec2(normalized.x * 2.0 - 1.0, 1.0 - normalized.y * 2.0);
}

void main() {
    vec2 local = a_position * u_quad_size;
    vec3 pixel = u_transform * vec3(local, 1.0);
    v_frag_coord = local;
    gl_Position = vec4(to_ndc(pixel.xy), 0.0, 1.0);
}
)";

  constexpr char kFragmentShaderSource[] = R"(
precision highp float;

uniform vec2 u_quad_size;
uniform vec4 u_color;
uniform vec2 u_rect_size;
uniform float u_radius;
uniform float u_blur;
varying vec2 v_frag_coord;

float roundedBox(vec2 centerPos, vec2 size, float radii) {
    return length(max(abs(centerPos) - size + radii, 0.0)) - radii;
}

void main() {
    float box = roundedBox(v_frag_coord - u_quad_size * 0.5, u_rect_size, u_radius);
    float a = 1.0 - smoothstep(0.0, u_blur, box);
    // Qt passes the colour premultiplied.
    gl_FragColor = vec4(u_color.rgb * u_color.a, u_color.a) * a * a;
}
)";

} // namespace

void ShadowProgram::ensureInitialized() {
  if (m_program.isValid()) {
    return;
  }
  m_program.create(kVertexShaderSource, kFragmentShaderSource);
  m_positionLocation = glGetAttribLocation(m_program.id(), "a_position");
  m_surfaceSizeLocation = glGetUniformLocation(m_program.id(), "u_surface_size");
  m_quadSizeLocation = glGetUniformLocation(m_program.id(), "u_quad_size");
  m_transformLocation = glGetUniformLocation(m_program.id(), "u_transform");
  m_colorLocation = glGetUniformLocation(m_program.id(), "u_color");
  m_rectSizeLocation = glGetUniformLocation(m_program.id(), "u_rect_size");
  m_radiusLocation = glGetUniformLocation(m_program.id(), "u_radius");
  m_blurLocation = glGetUniformLocation(m_program.id(), "u_blur");
  if (m_positionLocation < 0 || m_surfaceSizeLocation < 0 || m_quadSizeLocation < 0 || m_transformLocation < 0
      || m_colorLocation < 0 || m_rectSizeLocation < 0 || m_radiusLocation < 0 || m_blurLocation < 0) {
    throw std::runtime_error("failed to query shadow shader locations");
  }
}

void ShadowProgram::destroy() {
  m_program.destroy();
  m_positionLocation = m_surfaceSizeLocation = m_quadSizeLocation = m_transformLocation = -1;
  m_colorLocation = m_rectSizeLocation = m_radiusLocation = m_blurLocation = -1;
}

void ShadowProgram::abandon() noexcept { m_program.abandon(); }

void ShadowProgram::draw(
    float surfaceWidth, float surfaceHeight, float width, float height, const ShadowStyle& style, const Mat3& transform
) const {
  if (!m_program.isValid() || width <= 0.0F || height <= 0.0F || style.color.a <= 0.0F) {
    return;
  }
  const std::array<GLfloat, 12> vertices = {
      0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F, 1.0F, 0.0F, 1.0F, 1.0F,
  };
  glUseProgram(m_program.id());
  glUniform2f(m_surfaceSizeLocation, surfaceWidth, surfaceHeight);
  glUniform2f(m_quadSizeLocation, width, height);
  glUniformMatrix3fv(m_transformLocation, 1, GL_FALSE, transform.m.data());
  glUniform4f(m_colorLocation, style.color.r, style.color.g, style.color.b, style.color.a);
  glUniform2f(m_rectSizeLocation, style.rectWidth, style.rectHeight);
  glUniform1f(m_radiusLocation, style.radius);
  // smoothstep(0, 0, x) is undefined in GLSL; a zero blur is a hard edge.
  glUniform1f(m_blurLocation, style.blur > 0.0F ? style.blur : 1e-4F);
  const auto posAttr = static_cast<GLuint>(m_positionLocation);
  glVertexAttribPointer(posAttr, 2, GL_FLOAT, GL_FALSE, 0, vertices.data());
  glEnableVertexAttribArray(posAttr);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glDisableVertexAttribArray(posAttr);
}
