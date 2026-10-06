#pragma once

#include "runtime/object.h"
#include "runtime/property.h"

#include <cstdint>
#include <memory>

namespace ii {

  // QML Timer, as QQmlTimer behaves:
  // - nothing runs before the component is complete;
  // - start(), stop(), restart() and the timer stopping itself change `running` without breaking
  //   a binding on it (they are C++ calls in Qt); assigning `running` from JS does break it;
  // - changing interval or repeat while running restarts the countdown;
  // - triggeredOnStart fires once right after starting (deferred, as Qt posts an event), then on
  //   every expiry, so a non-repeating timer with it fires twice.
  class Timer : public Object {
  public:
    Timer();
    ~Timer() override;

    Property<int> interval{1000};
    Property<bool> running;
    Property<bool> repeat;
    Property<bool> triggeredOnStart;
    Signal<> triggered;

    void start() { running.writeDirect(true); }
    void stop() { running.writeDirect(false); }
    void restart();

  protected:
    void componentComplete() override;

  private:
    void update();
    void expired();

    std::uint64_t m_timerId = 0;
    bool m_firstTick = true;
    // Expires pending deferred triggers (stop() before they run, or destruction).
    std::shared_ptr<std::uint64_t> m_generation = std::make_shared<std::uint64_t>(0);
  };

} // namespace ii
