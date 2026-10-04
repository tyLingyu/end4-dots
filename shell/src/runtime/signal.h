#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace ii {

  namespace detail {
    struct SlotBase {
      bool connected = true;
    };
  } // namespace detail

  // Owning handle for one signal connection. Disconnects when destroyed, so a handler that
  // captures an object should be stored in that object (or something with the same lifetime).
  class Connection {
  public:
    Connection() = default;
    explicit Connection(std::weak_ptr<detail::SlotBase> slot) : m_slot(std::move(slot)) {}
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&& other) noexcept = default;
    Connection& operator=(Connection&& other) noexcept {
      if (this != &other) {
        disconnect();
        m_slot = std::move(other.m_slot);
      }
      return *this;
    }
    ~Connection() { disconnect(); }

    void disconnect() {
      if (auto slot = m_slot.lock()) {
        slot->connected = false;
      }
      m_slot.reset();
    }

    [[nodiscard]] bool connected() const {
      auto slot = m_slot.lock();
      return slot != nullptr && slot->connected;
    }

  private:
    std::weak_ptr<detail::SlotBase> m_slot;
  };

  // QML-style signal. Handlers run synchronously in connection order. Handlers may connect,
  // disconnect, or destroy the object that owns the signal while it is being emitted.
  template <typename... Args> class Signal {
  public:
    using Handler = std::function<void(Args...)>;

    Signal() = default;
    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;
    ~Signal() {
      for (auto& slot : m_slots) {
        slot->connected = false;
      }
      if (m_destroyedDuringEmit != nullptr) {
        *m_destroyedDuringEmit = true;
      }
    }

    // Connection lives as long as the returned handle.
    [[nodiscard]] Connection connect(Handler handler) {
      auto slot = std::make_shared<Slot>(std::move(handler));
      m_slots.push_back(slot);
      return Connection(slot);
    }

    // Connection lives as long as the signal. For handlers owned by the signal's own object
    // (QML `onFooChanged:` inside the declaring component).
    void connectForever(Handler handler) { m_slots.push_back(std::make_shared<Slot>(std::move(handler))); }

    void emit(Args... args) {
      if (m_slots.empty()) {
        return;
      }
      bool destroyed = false;
      bool* const outerFlag = m_destroyedDuringEmit;
      m_destroyedDuringEmit = &destroyed;
      const auto snapshot = m_slots;
      for (const auto& slot : snapshot) {
        if (slot->connected) {
          slot->handler(args...);
          if (destroyed) {
            if (outerFlag != nullptr) {
              *outerFlag = true;
            }
            return;
          }
        }
      }
      m_destroyedDuringEmit = outerFlag;
      if (outerFlag == nullptr) {
        std::erase_if(m_slots, [](const auto& slot) { return !slot->connected; });
      }
    }

    [[nodiscard]] bool empty() const noexcept { return m_slots.empty(); }

  private:
    struct Slot : detail::SlotBase {
      explicit Slot(Handler h) : handler(std::move(h)) {}
      Handler handler;
    };

    std::vector<std::shared_ptr<Slot>> m_slots;
    bool* m_destroyedDuringEmit = nullptr;
  };

} // namespace ii
