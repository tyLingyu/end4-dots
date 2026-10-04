#include "runtime/property.h"

#include "core/log.h"

#include <algorithm>

namespace ii {

  namespace {
    constexpr Logger kLog("binding");
    thread_local Binding* t_currentBinding = nullptr;
  } // namespace

  // ── Binding ─────────────────────────────────────────────────────────────────

  Binding::~Binding() {
    clearDependencies();
    if (m_destroyed != nullptr) {
      *m_destroyed = true;
    }
  }

  Binding* Binding::current() noexcept { return t_currentBinding; }

  void Binding::evaluate() {
    if (m_evaluating) {
      kLog.warn("binding loop detected for {}", name());
      return;
    }
    m_evaluating = true;
    bool destroyed = false;
    m_destroyed = &destroyed;

    clearDependencies();
    Binding* const outer = t_currentBinding;
    t_currentBinding = this;
    const bool ok = compute();
    t_currentBinding = outer;

    // Storing notifies other bindings and handlers, any of which may destroy this binding
    // (by assigning to the target, or by destroying the object that owns it). m_evaluating
    // stays set across the store so a binding that depends on its own target is a loop.
    if (ok) {
      store();
    }
    if (destroyed) {
      return;
    }
    m_destroyed = nullptr;
    m_evaluating = false;
  }

  void Binding::reportError(const std::exception* error) const noexcept {
    kLog.warn("binding {} failed: {}", name(), error != nullptr ? error->what() : "unknown exception");
  }

  void Binding::addDependency(PropertyBase* property) {
    if (std::ranges::find(m_dependencies, property) != m_dependencies.end()) {
      return;
    }
    m_dependencies.push_back(property);
    property->subscribe(this);
  }

  void Binding::dependencyDestroyed(PropertyBase* property) noexcept { std::erase(m_dependencies, property); }

  void Binding::clearDependencies() noexcept {
    for (PropertyBase* property : m_dependencies) {
      property->unsubscribe(this);
    }
    m_dependencies.clear();
  }

  // ── PropertyBase ────────────────────────────────────────────────────────────

  PropertyBase::~PropertyBase() {
    // Our own binding first: it may depend on this very property and unsubscribe from it.
    m_binding.reset();
    if (m_extra != nullptr) {
      for (Binding* binding : m_extra->subscribers) {
        binding->dependencyDestroyed(this);
      }
      if (m_extra->destroyedDuringNotify != nullptr) {
        *m_extra->destroyedDuringNotify = true;
      }
    }
  }

  PropertyBase::Extra& PropertyBase::extra() {
    if (m_extra == nullptr) {
      m_extra = std::make_unique<Extra>();
    }
    return *m_extra;
  }

  void PropertyBase::recordRead() const {
    if (Binding* binding = Binding::current()) {
      binding->addDependency(const_cast<PropertyBase*>(this));
    }
  }

  void PropertyBase::subscribe(Binding* binding) { extra().subscribers.push_back(binding); }

  void PropertyBase::unsubscribe(Binding* binding) noexcept {
    if (m_extra != nullptr) {
      std::erase(m_extra->subscribers, binding);
    }
  }

  void PropertyBase::notifyChanged() {
    if (m_extra == nullptr) {
      return;
    }
    bool destroyed = false;
    bool* const outer = m_extra->destroyedDuringNotify;
    m_extra->destroyedDuringNotify = &destroyed;

    // Re-evaluating a binding re-records its dependencies, and any binding may be destroyed
    // along the way, so walk a snapshot and skip entries that are no longer subscribed.
    const std::vector<Binding*> snapshot = m_extra->subscribers;
    for (Binding* binding : snapshot) {
      if (std::ranges::find(m_extra->subscribers, binding) == m_extra->subscribers.end()) {
        continue;
      }
      binding->evaluate();
      if (destroyed) {
        if (outer != nullptr) {
          *outer = true;
        }
        return;
      }
    }
    m_extra->destroyedDuringNotify = outer;
    m_extra->changed.emit();
  }

  void PropertyBase::installBinding(std::unique_ptr<Binding> binding) {
    m_binding = std::move(binding);
    m_binding->evaluate();
  }

} // namespace ii
