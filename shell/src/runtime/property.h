#pragma once

#include "runtime/signal.h"

#include <concepts>
#include <exception>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

// QML property and binding semantics (design decision Q21):
//  1. Dependencies are tracked dynamically: every evaluation re-records which properties it read.
//  2. When a dependency changes, dependent bindings re-evaluate synchronously, then the
//     property's changed() handlers run. Layout and painting are coalesced elsewhere, per frame.
//  3. set() (imperative assignment) removes the property's binding; bind() installs a new one,
//     which is also how `Qt.binding(...)` / re-binding after an assignment is expressed.
//  4. Storing a value equal to the current one notifies nobody.
//  5. A binding re-entered while it evaluates is a binding loop: it is reported and cut.

namespace ii {

  class PropertyBase;

  class Binding {
  public:
    Binding(PropertyBase& target, const char* name) : m_target(target), m_name(name) {}
    virtual ~Binding();

    Binding(const Binding&) = delete;
    Binding& operator=(const Binding&) = delete;

    void evaluate();

    [[nodiscard]] std::size_t dependencyCount() const noexcept { return m_dependencies.size(); }
    [[nodiscard]] const char* name() const noexcept { return m_name != nullptr ? m_name : "<unnamed>"; }

    // The binding whose compute() is running on this thread, if any.
    [[nodiscard]] static Binding* current() noexcept;

  protected:
    // Computes the new value with dependency tracking on. Returns false if it failed
    // (the target then keeps its value, like a QML binding that throws).
    virtual bool compute() noexcept = 0;
    // Writes the computed value into the target, with tracking off.
    virtual void store() = 0;

    void reportError(const std::exception* error) const noexcept;

    PropertyBase& m_target;

  private:
    friend class PropertyBase;

    void addDependency(PropertyBase* property);
    void dependencyDestroyed(PropertyBase* property) noexcept;
    void clearDependencies() noexcept;

    const char* m_name;
    std::vector<PropertyBase*> m_dependencies;
    bool m_evaluating = false;
    bool* m_destroyed = nullptr;
  };

  class PropertyBase {
  public:
    PropertyBase() = default;
    ~PropertyBase();

    PropertyBase(const PropertyBase&) = delete;
    PropertyBase& operator=(const PropertyBase&) = delete;

    [[nodiscard]] bool hasBinding() const noexcept { return m_binding != nullptr; }
    [[nodiscard]] const Binding* binding() const noexcept { return m_binding.get(); }
    void clearBinding() noexcept { m_binding.reset(); }

    // `onFooChanged`. Emitted after dependent bindings have re-evaluated.
    [[nodiscard]] Signal<>& changed() { return extra().changed; }

    // Runtime-internal change hook (e.g. pushing geometry into the scene node), called before
    // dependent bindings. A plain function pointer, so it allocates nothing beyond Extra.
    using Hook = void (*)(void* context);
    void setHook(Hook hook, void* context) {
      extra().hook = hook;
      extra().hookContext = context;
    }

  protected:
    void recordRead() const;
    void notifyChanged();
    void installBinding(std::unique_ptr<Binding> binding);

  private:
    friend class Binding;

    // Allocated on first subscription or handler, so an unobserved property costs two pointers.
    struct Extra {
      std::vector<Binding*> subscribers;
      Signal<> changed;
      Hook hook = nullptr;
      void* hookContext = nullptr;
      bool* destroyedDuringNotify = nullptr;
    };

    Extra& extra();
    void subscribe(Binding* binding);
    void unsubscribe(Binding* binding) noexcept;

    std::unique_ptr<Binding> m_binding;
    std::unique_ptr<Extra> m_extra;
  };

  template <typename T> class Property : public PropertyBase {
  public:
    using ValueType = T;

    Property() = default;
    explicit Property(T initial) : m_value(std::move(initial)) {}

    // Reads the value; inside a binding this makes the property a dependency.
    [[nodiscard]] const T& get() const {
      recordRead();
      return m_value;
    }

    // Reads the value without becoming a dependency.
    [[nodiscard]] const T& peek() const noexcept { return m_value; }

    // Imperative assignment: drops the binding, then stores.
    void set(T value) {
      clearBinding();
      store(std::move(value));
    }

    template <typename F>
      requires std::invocable<F&> && std::convertible_to<std::invoke_result_t<F&>, T>
    void bind(F&& compute, const char* name = nullptr);

    // Handler owned by the property itself (QML `onFooChanged:` in the declaring component).
    template <typename F> void onChanged(F&& handler) { changed().connectForever(std::forward<F>(handler)); }

  private:
    template <typename, typename> friend class FunctionBinding;

    void store(T value) {
      if constexpr (std::equality_comparable<T>) {
        if (m_value == value) {
          return;
        }
      }
      m_value = std::move(value);
      notifyChanged();
    }

    T m_value{};
  };

  template <typename T, typename F> class FunctionBinding final : public Binding {
  public:
    FunctionBinding(Property<T>& target, F compute, const char* name)
        : Binding(target, name), m_compute(std::move(compute)) {}

  protected:
    bool compute() noexcept override {
      try {
        m_pending.emplace(m_compute());
        return true;
      } catch (const std::exception& error) {
        reportError(&error);
      } catch (...) {
        reportError(nullptr);
      }
      return false;
    }

    void store() override {
      auto& target = static_cast<Property<T>&>(m_target);
      T value = std::move(*m_pending);
      m_pending.reset();
      target.store(std::move(value));
    }

  private:
    F m_compute;
    std::optional<T> m_pending;
  };

  template <typename T>
  template <typename F>
    requires std::invocable<F&> && std::convertible_to<std::invoke_result_t<F&>, T>
  void Property<T>::bind(F&& compute, const char* name) {
    installBinding(std::make_unique<FunctionBinding<T, std::decay_t<F>>>(*this, std::forward<F>(compute), name));
  }

} // namespace ii
