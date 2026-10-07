#include "runtime/property.h"
#include "runtime/signal.h"

#include "../check.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using ii::CreationScope;
using ii::Property;
using ii::Signal;

// ── Signal ────────────────────────────────────────────────────────────────────

TEST("signal: handlers run in order, connection disconnects on destruction") {
  Signal<int> sig;
  std::string log;
  sig.connectForever([&](int v) { log += "a" + std::to_string(v); });
  {
    auto conn = sig.connect([&](int v) { log += "b" + std::to_string(v); });
    sig.emit(1);
  }
  sig.emit(2);
  CHECK_EQ(log, "a1b1a2");
}

TEST("signal: disconnecting a later handler during emit skips it") {
  Signal<> sig;
  int second = 0;
  ii::Connection conn;
  sig.connectForever([&] { conn.disconnect(); });
  conn = sig.connect([&] { ++second; });
  sig.emit();
  CHECK_EQ(second, 0);
}

TEST("signal: destroying the signal from its own handler is safe") {
  auto sig = std::make_unique<Signal<>>();
  int after = 0;
  sig->connectForever([&] { sig.reset(); });
  sig->connectForever([&] { ++after; });
  sig->emit();
  CHECK(sig == nullptr);
  CHECK_EQ(after, 0);
}

// ── Property basics ───────────────────────────────────────────────────────────

TEST("property: set notifies once, equal value notifies nobody") {
  Property<int> p(1);
  int notified = 0;
  p.onChanged([&] { ++notified; });
  p.set(2);
  p.set(2);
  CHECK_EQ(p.peek(), 2);
  CHECK_EQ(notified, 1);
}

TEST("binding: initial value and update on dependency change") {
  Property<int> a(1);
  Property<int> b(2);
  Property<int> sum;
  sum.bind([&] { return a.get() + b.get(); });
  CHECK_EQ(sum.peek(), 3);
  a.set(10);
  CHECK_EQ(sum.peek(), 12);
  b.set(20);
  CHECK_EQ(sum.peek(), 30);
}

TEST("binding: chained bindings propagate") {
  Property<int> a(1);
  Property<int> b;
  Property<int> c;
  b.bind([&] { return a.get() * 2; });
  c.bind([&] { return b.get() + 1; });
  a.set(5);
  CHECK_EQ(b.peek(), 10);
  CHECK_EQ(c.peek(), 11);
}

TEST("binding: dependencies are re-recorded on every evaluation") {
  Property<bool> useA(true);
  Property<int> a(1);
  Property<int> b(2);
  int evaluations = 0;
  Property<int> out;
  out.bind([&] {
    ++evaluations;
    return useA.get() ? a.get() : b.get();
  });
  CHECK_EQ(evaluations, 1);
  b.set(3); // not a dependency while useA is true
  CHECK_EQ(evaluations, 1);
  useA.set(false);
  CHECK_EQ(out.peek(), 3);
  a.set(100); // no longer a dependency
  CHECK_EQ(evaluations, 2);
  b.set(4);
  CHECK_EQ(out.peek(), 4);
  CHECK_EQ(evaluations, 3);
}

TEST("binding: an equal result does not notify dependents") {
  Property<int> a(1);
  Property<bool> positive;
  positive.bind([&] { return a.get() > 0; });
  int dependentEvaluations = 0;
  Property<int> dependent;
  dependent.bind([&] {
    ++dependentEvaluations;
    return positive.get() ? 1 : 0;
  });
  a.set(2);
  a.set(3);
  CHECK_EQ(dependentEvaluations, 1);
}

// ── Assignment vs binding ─────────────────────────────────────────────────────

TEST("assignment: set() removes the binding, bind() restores one") {
  Property<int> a(1);
  Property<int> b;
  b.bind([&] { return a.get() + 1; });
  b.set(100);
  CHECK(!b.hasBinding());
  a.set(5);
  CHECK_EQ(b.peek(), 100);
  b.bind([&] { return a.get() + 1; });
  CHECK_EQ(b.peek(), 6);
}

// ── Ordering and tracking hygiene ─────────────────────────────────────────────

TEST("ordering: dependent bindings update before changed() handlers run") {
  Property<int> a(1);
  Property<int> b;
  b.bind([&] { return a.get() * 10; });
  int seen = 0;
  a.onChanged([&] { seen = b.peek(); });
  a.set(2);
  CHECK_EQ(seen, 20);
}

TEST("tracking: reads inside changed() handlers are not recorded by the binding") {
  Property<int> source(1);
  Property<int> unrelated(0);
  int evaluations = 0;
  Property<int> target;
  target.onChanged([&] { (void)unrelated.get(); });
  target.bind([&] {
    ++evaluations;
    return source.get();
  });
  source.set(2); // runs target's handler, which reads `unrelated`
  CHECK_EQ(evaluations, 2);
  unrelated.set(1);
  CHECK_EQ(evaluations, 2);
}

// ── Binding loops ─────────────────────────────────────────────────────────────

TEST("loop: self-dependent binding is cut") {
  Property<int> a(1);
  a.bind([&] { return a.get() + 1; });
  CHECK_EQ(a.peek(), 2);
}

TEST("loop: mutual bindings terminate") {
  Property<int> a(0);
  Property<int> b(0);
  a.bind([&] { return b.get() + 1; });
  b.bind([&] { return a.get() + 1; });
  // Exact values follow QML's "cut at re-entry" rule; what matters is termination.
  CHECK(a.peek() > 0);
  CHECK(b.peek() > 0);
}

// ── Errors ────────────────────────────────────────────────────────────────────

TEST("error: a throwing binding keeps the old value and stays live") {
  Property<bool> fail(false);
  Property<int> value(1);
  Property<int> out;
  out.bind([&] {
    if (fail.get()) {
      throw std::runtime_error("expected test failure");
    }
    return value.get();
  });
  fail.set(true);
  CHECK_EQ(out.peek(), 1);
  fail.set(false);
  value.set(7);
  CHECK_EQ(out.peek(), 7);
}

// ── Lifetime ──────────────────────────────────────────────────────────────────

TEST("lifetime: destroying a dependency detaches it from the binding") {
  auto source = std::make_unique<Property<int>>(3);
  Property<int> out;
  out.bind([&] { return source != nullptr ? source->get() : -1; });
  CHECK_EQ(out.peek(), 3);
  source.reset();
  CHECK_EQ(out.peek(), 3); // nothing re-evaluates; the binding just lost a dependency
}

TEST("lifetime: destroying a bound property unsubscribes it from its sources") {
  Property<int> source(1);
  auto out = std::make_unique<Property<int>>();
  out->bind([&] { return source.get(); });
  out.reset();
  source.set(2); // must not touch the destroyed binding
  CHECK_EQ(source.peek(), 2);
}

TEST("lifetime: a handler destroying a sibling dependent during notify") {
  Property<int> source(1);
  auto first = std::make_unique<Property<int>>();
  auto second = std::make_unique<Property<int>>();
  first->bind([&] { return source.get(); });
  second->bind([&] { return source.get(); });
  first->onChanged([&] { second.reset(); });
  source.set(2);
  CHECK(second == nullptr);
  CHECK_EQ(first->peek(), 2);
}

TEST("lifetime: a property destroyed by its own changed() handler") {
  auto p = std::make_unique<Property<int>>(0);
  p->onChanged([&] { p.reset(); });
  p->set(1);
  CHECK(p == nullptr);
}

TEST("lifetime: a binding destroyed while storing its own value") {
  Property<int> source(1);
  auto owner = std::make_unique<Property<int>>();
  owner->bind([&] { return source.get(); });
  owner->onChanged([&] { owner.reset(); });
  source.set(2);
  CHECK(owner == nullptr);
  source.set(3);
}

TEST("lifetime: assignment from a handler replaces the binding mid-evaluation") {
  Property<int> source(1);
  Property<int> out;
  out.bind([&] { return source.get(); });
  out.onChanged([&] {
    if (out.peek() == 2) {
      out.set(42);
    }
  });
  source.set(2);
  CHECK_EQ(out.peek(), 42);
  CHECK(!out.hasBinding());
  source.set(3);
  CHECK_EQ(out.peek(), 42);
}

// QML: `property string a: src + "_CN"; onAChanged: ...` runs the handler for the initial value
// (bindings are evaluated after creation, with handlers connected); a literal value does not.
TEST("creation: a binding's initial value reaches handlers connected later in the scope") {
  Property<std::string> src{"zh"};
  Property<std::string> a;
  Property<std::string> literal;
  std::vector<std::string> seen;
  {
    CreationScope creation;
    literal.set("lit");
    a.bind([&] { return src.get() + "_CN"; });
    CHECK(a.peek().empty());  // not evaluated while the tree is being built
    a.onChanged([&] { seen.push_back("a=" + a.peek()); });
    literal.onChanged([&] { seen.push_back("literal"); });
    creation.finish();
  }
  CHECK(seen == std::vector<std::string>{"a=zh_CN"});
  src.set("ja");
  CHECK_EQ(a.peek(), "ja_CN");
}

TEST("creation: last installed is evaluated first; replaced or destroyed bindings are skipped") {
  std::vector<int> order;
  Property<int> first;
  Property<int> second;
  auto gone = std::make_unique<Property<int>>();
  {
    CreationScope creation;
    first.bind([&] { order.push_back(1); return 1; });
    gone->bind([&] { order.push_back(9); return 9; });
    second.bind([&] { order.push_back(2); return 2; });
    first.set(10);  // an initial property replaces the component's binding
    gone.reset();
  }
  CHECK(order == std::vector<int>{2});
  CHECK_EQ(first.peek(), 10);
  CHECK_EQ(second.peek(), 2);
}

TEST("creation: nested scopes evaluate their own bindings when they finish") {
  Property<int> outer;
  Property<int> inner;
  CreationScope outerScope;
  outer.bind([&] { return inner.get() + 1; });
  {
    CreationScope innerScope;
    inner.bind([] { return 5; });
  }
  CHECK_EQ(inner.peek(), 5);
  CHECK_EQ(outer.peek(), 0);
  outerScope.finish();
  CHECK_EQ(outer.peek(), 6);
}

TEST_MAIN()
