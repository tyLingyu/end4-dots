#include "runtime/animation.h"

#include <chrono>
#include <numeric>

namespace ii {

  // ── AnimationDriver ─────────────────────────────────────────────────────────

  AnimationDriver::AnimationDriver() {
    m_clock = [] {
      using namespace std::chrono;
      return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
    };
  }

  AnimationDriver& AnimationDriver::instance() {
    static AnimationDriver driver;
    return driver;
  }

  double AnimationDriver::now() const { return m_clock(); }

  void AnimationDriver::add(Animation* animation) {
    m_running.push_back(animation);
    if (m_requestFrame) {
      m_requestFrame();
    }
  }

  void AnimationDriver::remove(Animation* animation) {
    std::erase(m_running, animation);
    if (m_ticking != nullptr) {
      std::ranges::replace(*m_ticking, animation, nullptr);
    }
  }

  void AnimationDriver::tick() {
    const double time = now();
    // Animations may stop, start or destroy each other while being advanced.
    std::vector<Animation*> batch = m_running;
    std::vector<Animation*>* const outer = m_ticking;
    m_ticking = &batch;
    for (Animation*& animation : batch) {
      if (animation != nullptr) {
        animation->tick(time);
      }
    }
    m_ticking = outer;
  }

  // ── Animation ───────────────────────────────────────────────────────────────

  Animation::Animation() { running.setHook(&onRunningChanged, this); }

  Animation::~Animation() {
    if (m_registered) {
      AnimationDriver::instance().remove(this);
    }
  }

  void Animation::setRunningInternally(bool value) {
    m_settingRunning = true;
    running.writeDirect(value);
    m_settingRunning = false;
  }

  void Animation::onRunningChanged(void* self) {
    auto* animation = static_cast<Animation*>(self);
    if (animation->m_settingRunning) {
      return;
    }
    // Someone assigned or bound `running`.
    if (animation->running.peek()) {
      animation->m_settingRunning = true; // already true; start() must not write it again
      animation->start();
      animation->m_settingRunning = false;
    } else {
      animation->stopNow();
    }
  }

  void Animation::start() {
    if (m_registered) {
      return;
    }
    auto& driver = AnimationDriver::instance();
    m_startTime = driver.now();
    m_loop = 0;
    m_stopAfterLoop = -1;
    m_registered = true;
    driver.add(this);
    if (!m_settingRunning) {
      setRunningInternally(true);
    }
    begin();
    update(0.0);
    started.emit();
    if (loopDuration() <= 0.0 && m_registered) {
      finish();
    }
  }

  void Animation::stop() {
    if (!m_registered) {
      return;
    }
    if (alwaysRunToEnd.peek()) {
      m_stopAfterLoop = m_loop;  // tick() finishes when this loop ends
      return;
    }
    stopNow();
  }

  void Animation::stopNow() {
    if (!m_registered) {
      return;
    }
    m_registered = false;
    AnimationDriver::instance().remove(this);
    setRunningInternally(false);
    stopped.emit();
  }

  void Animation::restart() {
    stopNow();
    start();
  }

  void Animation::complete() {
    if (!m_registered) {
      return;
    }
    update(loopDuration());
    finish();
  }

  void Animation::finish() {
    m_registered = false;
    AnimationDriver::instance().remove(this);
    setRunningInternally(false);
    stopped.emit();
    finished.emit();
  }

  void Animation::tick(double now) {
    const double length = loopDuration();
    const double elapsed = std::max(now - m_startTime, 0.0);
    const int loopCount = loops.peek();
    if (length <= 0.0 || (loopCount != Infinite && elapsed >= length * std::max(loopCount, 1))) {
      update(length);
      finish();
      return;
    }
    const int loop = static_cast<int>(elapsed / length);
    if (m_stopAfterLoop >= 0 && loop > m_stopAfterLoop) {
      update(length);
      finish();
      return;
    }
    if (loop != m_loop) {
      update(length); // finish the previous loop (fires trailing actions)
      m_loop = loop;
      loopRestart();
    }
    update(std::fmod(elapsed, length));
  }

  // ── RotationAnimation ───────────────────────────────────────────────────────

  double RotationAnimation::interpolateValue(const double& a, const double& b, double progress) const {
    double delta = b - a;
    switch (direction.peek()) {
    case Direction::Numerical:
      break;
    case Direction::Clockwise:
      if (delta < 0.0) {
        delta = std::fmod(delta, 360.0) + 360.0;
      }
      break;
    case Direction::Counterclockwise:
      if (delta > 0.0) {
        delta = std::fmod(delta, 360.0) - 360.0;
      }
      break;
    case Direction::Shortest:
      delta = std::fmod(delta, 360.0);
      if (delta > 180.0) {
        delta -= 360.0;
      } else if (delta < -180.0) {
        delta += 360.0;
      }
      break;
    }
    return a + delta * progress;
  }

  // ── SmoothedAnimation ───────────────────────────────────────────────────────

  void SmoothedAnimation::animate(const double& from, const double& to) {
    m_from = from;
    m_to = to;
    const double speed = velocity.peek();
    m_duration = speed > 0.0 ? std::fabs(to - from) / speed * 1000.0 : 0.0;
    start();
  }

  void SmoothedAnimation::update(double time) {
    if (m_target == nullptr) {
      return;
    }
    const double progress = m_duration > 0.0 ? std::clamp(time / m_duration, 0.0, 1.0) : 1.0;
    const Easing inOutQuad{.type = Easing::Type::InOutQuad};
    m_target->writeDirect(interpolate(m_from, m_to, inOutQuad.value(progress)));
  }

  // ── SequentialAnimation ─────────────────────────────────────────────────────

  double SequentialAnimation::loopDuration() const {
    return std::accumulate(m_children.begin(), m_children.end(), 0.0, [](double sum, const Animation* child) {
      return sum + child->loopDuration() * std::max(child->loops.peek(), 1);
    });
  }

  void SequentialAnimation::begin() {
    m_current = 0;
    m_currentBegun = false;
  }

  void SequentialAnimation::update(double time) {
    // Walk forward through children whose span ended before `time` (finishing each one, so
    // instantaneous actions fire exactly once), then position the child `time` falls in.
    double start = 0.0;
    for (std::size_t i = 0; i < m_current; ++i) {
      start += m_children[i]->loopDuration() * std::max(m_children[i]->loops.peek(), 1);
    }
    while (m_current < m_children.size()) {
      Animation* child = m_children[m_current];
      const double length = child->loopDuration() * std::max(child->loops.peek(), 1);
      if (!m_currentBegun) {
        child->begin();
        m_currentBegun = true;
      }
      if (time < start + length || (length > 0.0 && time == start + length && m_current + 1 == m_children.size())) {
        const double local = time - start;
        const double loopLength = child->loopDuration();
        child->update(loopLength > 0.0 && local < length ? std::fmod(local, loopLength) : loopLength);
        return;
      }
      child->update(child->loopDuration());
      start += length;
      ++m_current;
      m_currentBegun = false;
    }
  }

  // ── ParallelAnimation ───────────────────────────────────────────────────────

  double ParallelAnimation::loopDuration() const {
    double longest = 0.0;
    for (const Animation* child : m_children) {
      longest = std::max(longest, child->loopDuration() * std::max(child->loops.peek(), 1));
    }
    return longest;
  }

  void ParallelAnimation::begin() {
    for (Animation* child : m_children) {
      child->begin();
    }
  }

  void ParallelAnimation::update(double time) {
    for (Animation* child : m_children) {
      const double loopLength = child->loopDuration();
      const double length = loopLength * std::max(child->loops.peek(), 1);
      child->update(time >= length || loopLength <= 0.0 ? loopLength : std::fmod(time, loopLength));
    }
  }

  // ── FrameAnimation ──────────────────────────────────────────────────────────

  void FrameAnimation::reset() {
    m_lastTime = -1.0;
    currentFrame.writeDirect(0);
    elapsedTime.writeDirect(0.0);
    frameTime.writeDirect(0.0);
    smoothFrameTime.writeDirect(0.0);
  }

  void FrameAnimation::begin() {
    reset();
    m_pausedFor = 0.0;
  }

  void FrameAnimation::update(double time) {
    if (paused.peek()) {
      if (m_lastTime >= 0.0) {
        m_pausedFor += time - m_lastTime;
        m_lastTime = time;
      }
      return;
    }
    const double frame = m_lastTime < 0.0 ? 0.0 : (time - m_lastTime) / 1000.0;
    m_lastTime = time;
    frameTime.writeDirect(frame);
    // QQuickFrameAnimation smooths with a factor of 0.1.
    smoothFrameTime.writeDirect(currentFrame.peek() == 0 ? frame : smoothFrameTime.peek() * 0.9 + frame * 0.1);
    elapsedTime.writeDirect((time - m_pausedFor) / 1000.0);
    currentFrame.writeDirect(currentFrame.peek() + 1);
    triggered.emit();
  }

} // namespace ii
