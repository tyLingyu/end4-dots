#pragma once

#include "runtime/color.h"
#include "runtime/easing.h"
#include "runtime/object.h"
#include "runtime/property.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace ii {

  class Animation;

  // The animation clock. The window system ticks it once per frame; tests drive it manually.
  class AnimationDriver {
  public:
    using Clock = std::function<double()>; // milliseconds, monotonic

    static AnimationDriver& instance();

    // Reads the clock and advances every running top-level animation.
    void tick();
    [[nodiscard]] double now() const;
    [[nodiscard]] bool hasRunningAnimations() const noexcept { return !m_running.empty(); }

    void setClock(Clock clock) { m_clock = std::move(clock); }
    // Called whenever an animation starts, so the window keeps producing frames.
    void setFrameRequestHandler(std::function<void()> handler) { m_requestFrame = std::move(handler); }

  private:
    friend class Animation;
    AnimationDriver();
    void add(Animation* animation);
    void remove(Animation* animation);

    Clock m_clock;
    std::function<void()> m_requestFrame;
    std::vector<Animation*> m_running;
    std::vector<Animation*>* m_ticking = nullptr;
  };

  // QQuickAbstractAnimation. Top-level animations run on the driver; animations inside a group
  // are driven by the group.
  class Animation : public Object {
  public:
    static constexpr int Infinite = -1;

    Animation();
    ~Animation() override;

    // Writable: binding `running` to a condition starts and stops the animation.
    Property<bool> running;
    Property<int> loops{1};
    // stop() (or running = false) lets the current loop finish first, as in Qt. A Behavior or a
    // group stops its animation outright (stopNow), which Qt does too.
    Property<bool> alwaysRunToEnd;
    Signal<> started;
    Signal<> stopped;
    Signal<> finished;

    void start();
    void stop();
    // Stops immediately, whatever alwaysRunToEnd says.
    void stopNow();
    void restart();
    // Jumps to the end values and stops.
    void complete();

    // Length of one loop in milliseconds.
    [[nodiscard]] virtual double loopDuration() const = 0;

  protected:
    friend class AnimationGroup;
    friend class SequentialAnimation;
    friend class ParallelAnimation;
    friend class AnimationDriver;

    // Start of a pass: capture start values.
    virtual void begin() {}
    // Sets the state `time` ms into the current loop, 0 <= time <= loopDuration().
    virtual void update(double time) = 0;
    // A new loop begins. Groups restart their children (which then re-capture start values as
    // they run, like Qt's group loops); a single animation keeps the values it started with.
    virtual void loopRestart() {}

  private:
    void tick(double now);
    void finish();
    void setRunningInternally(bool value);
    static void onRunningChanged(void* self);

    double m_startTime = 0.0;
    int m_loop = 0;
    int m_stopAfterLoop = -1;  // alwaysRunToEnd: finish this loop, then stop
    bool m_registered = false;
    bool m_settingRunning = false;
    Animation* m_group = nullptr;
  };

  // An animation a Behavior can run between two values of its property.
  template <typename T> class BehaviorAnimation : public Animation {
  public:
    virtual void setTarget(Property<T>* target) = 0;
    // Animate from `from` to `to` unless the animation's own from/to say otherwise.
    virtual void animate(const T& from, const T& to) = 0;
  };

  template <typename T> T interpolate(const T& a, const T& b, double progress);

  template <> inline double interpolate(const double& a, const double& b, double progress) {
    return a + (b - a) * progress;
  }

  // Per channel, like QVariantAnimation's QColor interpolator.
  template <> inline Color interpolate(const Color& a, const Color& b, double progress) {
    const auto lerp = [progress](float x, float y) {
      return static_cast<float>(x + (y - x) * progress);
    };
    return Color{lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), lerp(a.a, b.a)};
  }

  // QQuickPropertyAnimation: NumberAnimation (double), ColorAnimation (Color).
  template <typename T> class PropertyAnimation : public BehaviorAnimation<T> {
  public:
    Property<double> duration{250.0};
    Property<std::optional<T>> from;
    Property<std::optional<T>> to;
    Property<Easing> easing;
    // Resolved statically from QML `target` + `property` by the translator.
    Property<T>* target = nullptr;

    void setTarget(Property<T>* t) override { target = t; }

    void animate(const T& fromValue, const T& toValue) override {
      m_actionFrom = fromValue;
      m_actionTo = toValue;
      this->start();
    }

    [[nodiscard]] double loopDuration() const override { return std::max(duration.peek(), 0.0); }

  protected:
    void begin() override {
      if (target == nullptr) {
        return;
      }
      m_start = from.peek().value_or(m_actionFrom.value_or(target->peek()));
      m_end = to.peek().value_or(m_actionTo.value_or(target->peek()));
    }

    void update(double time) override {
      if (target == nullptr) {
        return;
      }
      const double length = loopDuration();
      const double progress = length > 0.0 ? easing.peek().value(std::clamp(time / length, 0.0, 1.0)) : 1.0;
      target->writeDirect(this->interpolateValue(m_start, m_end, progress));
    }

    [[nodiscard]] virtual T interpolateValue(const T& a, const T& b, double progress) const {
      return interpolate(a, b, progress);
    }

  private:
    std::optional<T> m_actionFrom;
    std::optional<T> m_actionTo;
    T m_start{};
    T m_end{};
  };

  using NumberAnimation = PropertyAnimation<double>;
  using ColorAnimation = PropertyAnimation<Color>;

  // QQuickRotationAnimation: a NumberAnimation in degrees that can take the short way round.
  class RotationAnimation : public PropertyAnimation<double> {
  public:
    enum class Direction : std::uint8_t { Numerical, Clockwise, Counterclockwise, Shortest };
    Property<Direction> direction{Direction::Numerical};

  protected:
    [[nodiscard]] double interpolateValue(const double& a, const double& b, double progress) const override;
  };

  // QQuickSmoothedAnimation (as used in ii: inside a Behavior, with a velocity). Moves at
  // `velocity` units per second with an in-out quadratic ease.
  class SmoothedAnimation : public BehaviorAnimation<double> {
  public:
    Property<double> velocity{200.0};

    void setTarget(Property<double>* t) override { m_target = t; }
    void animate(const double& from, const double& to) override;
    [[nodiscard]] double loopDuration() const override { return m_duration; }

  protected:
    void update(double time) override;

  private:
    Property<double>* m_target = nullptr;
    double m_from = 0.0;
    double m_to = 0.0;
    double m_duration = 0.0;
  };

  class PauseAnimation : public Animation {
  public:
    Property<double> duration{250.0};
    [[nodiscard]] double loopDuration() const override { return std::max(duration.peek(), 0.0); }

  protected:
    void update(double /*time*/) override {}
  };

  // Instantaneous steps inside groups.
  template <typename T> class PropertyAction : public Animation {
  public:
    Property<T>* target = nullptr;
    Property<T> value;
    [[nodiscard]] double loopDuration() const override { return 0.0; }

  protected:
    void update(double /*time*/) override {
      if (target != nullptr) {
        target->writeDirect(value.peek());
      }
    }
  };

  class ScriptAction : public Animation {
  public:
    std::function<void()> script;
    [[nodiscard]] double loopDuration() const override { return 0.0; }

  protected:
    void begin() override { m_ran = false; }
    void update(double /*time*/) override {
      if (!m_ran && script) {
        m_ran = true;
        script();
      }
    }

  private:
    bool m_ran = false;
  };

  class AnimationGroup : public Animation {
  public:
    template <typename A, typename... Args> A* add(Args&&... args) {
      A* child = this->create<A>(std::forward<Args>(args)...);
      child->m_group = this;
      m_children.push_back(child);
      return child;
    }

  protected:
    std::vector<Animation*> m_children;
  };

  class SequentialAnimation : public AnimationGroup {
  public:
    [[nodiscard]] double loopDuration() const override;

  protected:
    void begin() override;
    void update(double time) override;
    void loopRestart() override { begin(); }

  private:
    std::size_t m_current = 0;
    bool m_currentBegun = false;
  };

  class ParallelAnimation : public AnimationGroup {
  public:
    [[nodiscard]] double loopDuration() const override;

  protected:
    void begin() override;
    void update(double time) override;
    void loopRestart() override { begin(); }
  };

  // QQuickBehavior: writes to the target property are turned into animations once the
  // component is complete (QQuickBehavior::write). Writes before that, or while disabled, are
  // stored directly; a write equal to the running animation's target is ignored.
  template <typename T> class Behavior : public Object, public PropertyInterceptor<T> {
  public:
    explicit Behavior(Property<T>& target) : m_target(target) { m_target.setInterceptor(this); }

    ~Behavior() override {
      if (m_target.interceptor() == this) {
        m_target.setInterceptor(nullptr);
      }
    }

    Property<bool> enabled{true};

    template <typename A, typename... Args> A* setAnimation(Args&&... args) {
      A* animation = this->create<A>(std::forward<Args>(args)...);
      animation->setTarget(&m_target);
      m_animation = animation;
      return animation;
    }

    // Takes an animation already created as this behavior's child (e.g. from a Component).
    template <typename A> A* adoptAnimation(A* animation) {
      if (animation != nullptr) {
        animation->setTarget(&m_target);
        m_animation = animation;
      }
      return animation;
    }

    [[nodiscard]] BehaviorAnimation<T>* animation() const noexcept { return m_animation; }

    bool intercept(const T& value) override {
      if (m_animation == nullptr || !enabled.peek() || !isCompleted()) {
        if (m_animation != nullptr) {
          m_animation->stopNow();
        }
        m_targetValue = value;
        return false;
      }
      const bool active = m_animation->running.peek();
      if (active && m_targetValue == value) {
        return true;
      }
      m_targetValue = value;
      if (active) {
        m_animation->stopNow();
      }
      const T current = m_target.peek();
      if (!active && current == value) {
        return false;
      }
      m_animation->animate(current, value);
      return true;
    }

  private:
    Property<T>& m_target;
    BehaviorAnimation<T>* m_animation = nullptr;
    std::optional<T> m_targetValue;
  };

  // QML FrameAnimation: triggered once per frame while running (on the animation driver), with
  // frame timing in seconds as QQuickFrameAnimation reports it.
  class FrameAnimation : public Animation {
  public:
    Property<bool> paused;
    Property<int> currentFrame;
    Property<double> frameTime;
    Property<double> smoothFrameTime;
    Property<double> elapsedTime;
    Signal<> triggered;

    void reset();

  protected:
    [[nodiscard]] double loopDuration() const override { return std::numeric_limits<double>::infinity(); }
    void begin() override;
    void update(double time) override;

  private:
    double m_lastTime = -1.0;
    double m_pausedFor = 0.0;
  };

} // namespace ii
