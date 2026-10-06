#include "runtime/mouse_area.h"

#include "render/scene/input_area.h"

#include <linux/input-event-codes.h>

namespace ii {

  namespace {

    int qtButton(std::uint32_t linuxButton) {
      switch (linuxButton) {
      case BTN_LEFT: return MouseButton::LeftButton;
      case BTN_RIGHT: return MouseButton::RightButton;
      case BTN_MIDDLE: return MouseButton::MiddleButton;
      case BTN_SIDE: return MouseButton::BackButton;
      case BTN_EXTRA: return MouseButton::ForwardButton;
      default: return MouseButton::NoButton;
      }
    }

    std::uint32_t linuxButtons(int qtButtons) {
      std::uint32_t mask = 0;
      constexpr std::pair<int, int> kButtons[] = {{BTN_LEFT, MouseButton::LeftButton}, {BTN_RIGHT, MouseButton::RightButton},
                                                  {BTN_MIDDLE, MouseButton::MiddleButton}, {BTN_SIDE, MouseButton::BackButton},
                                                  {BTN_EXTRA, MouseButton::ForwardButton}};
      for (const auto& [code, flag] : kButtons) {
        if ((qtButtons & flag) != 0) {
          mask |= InputArea::buttonMask(static_cast<std::uint32_t>(code));
        }
      }
      return mask;
    }

  } // namespace

  MouseArea::MouseArea() : Item(std::make_unique<InputArea>()) {
    InputArea& input = area();
    input.setAcceptedButtons(linuxButtons(acceptedButtons.peek()));
    acceptedButtons.changed().connectForever([this] { area().setAcceptedButtons(linuxButtons(acceptedButtons.peek())); });
    enabled.changed().connectForever([this] { area().setEnabled(enabled.peek()); });
    cursorShape.changed().connectForever([this] { area().setCursorShape(static_cast<std::uint32_t>(cursorShape.peek())); });

    input.setOnEnter([this](const InputArea::PointerData& data) {
      mouseX.writeDirect(data.localX);
      mouseY.writeDirect(data.localY);
      setHovered(true);
    });
    input.setOnLeave([this] { setHovered(false); });
    input.setOnMotion([this](const InputArea::PointerData& data) {
      mouseX.writeDirect(data.localX);
      mouseY.writeDirect(data.localY);
      if (hoverEnabled.peek() || pressed.peek()) {
        emitMouse(positionChanged, data.localX, data.localY, MouseButton::NoButton);
      }
    });
    input.setOnPress([this](const InputArea::PointerData& data) {
      const int button = qtButton(data.button);
      mouseX.writeDirect(data.localX);
      mouseY.writeDirect(data.localY);
      if (data.pressed) {
        pressedButtons.writeDirect(pressedButtons.peek() | button);
        pressed.writeDirect(true);
        setHovered(m_hovered);
        emitMouse(pressedSignal, data.localX, data.localY, button);
      } else {
        pressedButtons.writeDirect(pressedButtons.peek() & ~button);
        pressed.writeDirect(pressedButtons.peek() != 0);
        emitMouse(released, data.localX, data.localY, button);
        setHovered(m_hovered);
      }
    });
    input.setOnClick([this](const InputArea::PointerData& data) {
      emitMouse(clicked, data.localX, data.localY, qtButton(data.button));
    });
    input.setOnCancel([this] {
      pressedButtons.writeDirect(0);
      pressed.writeDirect(false);
      canceled.emit();
    });
    input.setOnAxisHandler([this](const InputArea::PointerData& data) {
      if (wheel.empty()) {
        return false;
      }
      WheelEvent event;
      event.x.set(data.localX);
      event.y.set(data.localY);
      // 120 per notch; axis 0 is vertical (wl_pointer), positive scrolls down, Qt's delta up.
      const double delta = data.axisValue120 != 0 ? -static_cast<double>(data.axisValue120) : -data.axisValue * 8.0;
      event.angleDelta.set(data.axis == 0 ? Point{0.0, delta} : Point{delta, 0.0});
      event.buttons.set(pressedButtons.peek());
      wheel.emit(&event);
      return event.accepted.peek();
    });
  }

  InputArea& MouseArea::area() const { return *static_cast<InputArea*>(node()); }

  void MouseArea::setHovered(bool hovered) {
    m_hovered = hovered;
    // containsMouse (and entered/exited) track the pointer only with hover enabled or a button held.
    const bool contains = hovered && (hoverEnabled.peek() || pressed.peek());
    if (contains == containsMouse.peek()) {
      return;
    }
    containsMouse.writeDirect(contains);
    (contains ? entered : exited).emit();
  }

  void MouseArea::emitMouse(Signal<MouseEvent*>& signal, double px, double py, int button) {
    if (signal.empty()) {
      return;
    }
    MouseEvent event;
    event.x.set(px);
    event.y.set(py);
    event.button.set(button);
    event.buttons.set(pressedButtons.peek());
    signal.emit(&event);
  }

} // namespace ii
