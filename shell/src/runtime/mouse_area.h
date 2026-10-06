#pragma once

#include "runtime/geometry.h"
#include "runtime/item.h"

class InputArea;

namespace ii {

  // Qt::MouseButton values, as QML compares them (mouse.button === Qt.RightButton).
  namespace MouseButton {
    inline constexpr int NoButton = 0x00;
    inline constexpr int LeftButton = 0x01;
    inline constexpr int RightButton = 0x02;
    inline constexpr int MiddleButton = 0x04;
    inline constexpr int BackButton = 0x08;
    inline constexpr int ForwardButton = 0x10;
    inline constexpr int AllButtons = 0x07ffffff;
  } // namespace MouseButton

  // The `mouse` of MouseArea's handlers (QQuickMouseEvent).
  class MouseEvent : public Object {
  public:
    Property<double> x;
    Property<double> y;
    Property<int> button;
    Property<int> buttons;
    Property<int> modifiers;
    Property<bool> wasHeld;
    Property<bool> isClick;
    Property<bool> accepted{true};
  };

  // The `wheel` of onWheel (QQuickWheelEvent): angleDelta in eighths of a degree, 120 per notch.
  class WheelEvent : public Object {
  public:
    Property<double> x;
    Property<double> y;
    Property<Point> angleDelta;
    Property<Point> pixelDelta;
    Property<int> buttons;
    Property<int> modifiers;
    Property<bool> inverted;
    Property<bool> accepted{true};
  };

  // QML MouseArea over an InputArea node. entered/exited and containsMouse follow hover only with
  // hoverEnabled (or while a button is held), as in Qt.
  class MouseArea : public Item {
  public:
    MouseArea();

    Property<bool> hoverEnabled;
    Property<bool> containsMouse;  // read-only
    Property<bool> pressed;        // read-only
    Property<int> pressedButtons;  // read-only
    Property<int> acceptedButtons{MouseButton::LeftButton};
    Property<double> mouseX;  // read-only
    Property<double> mouseY;  // read-only
    Property<int> cursorShape;
    Property<bool> preventStealing;
    Property<bool> propagateComposedEvents;
    Property<bool> scrollGestureEnabled{true};

    Signal<> entered;
    Signal<> exited;
    Signal<MouseEvent*> pressedSignal;
    Signal<MouseEvent*> released;
    Signal<MouseEvent*> clicked;
    Signal<MouseEvent*> doubleClicked;
    Signal<MouseEvent*> pressAndHold;
    Signal<MouseEvent*> positionChanged;
    Signal<WheelEvent*> wheel;
    Signal<> canceled;

  private:
    [[nodiscard]] InputArea& area() const;
    void setHovered(bool hovered);
    void emitMouse(Signal<MouseEvent*>& signal, double px, double py, int button);

    bool m_hovered = false;
  };

} // namespace ii
