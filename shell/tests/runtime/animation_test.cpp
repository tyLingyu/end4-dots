// Animations and Behavior on a manual clock.

#include "runtime/animation.h"
#include "runtime/item.h"

#include "../check.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>

using namespace ii;

namespace {

  double g_now = 0.0;

  void useManualClock() {
    g_now = 0.0;
    AnimationDriver::instance().setClock([] { return g_now; });
  }

  void advance(double ms) {
    g_now += ms;
    AnimationDriver::instance().tick();
  }

  bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

} // namespace

TEST("animation: NumberAnimation runs from/to with its easing and finishes") {
  useManualClock();
  Property<double> value(0);
  NumberAnimation anim;
  anim.target = &value;
  anim.from.set(10.0);
  anim.to.set(20.0);
  anim.duration.set(100);
  anim.easing.set(Easing{.type = Easing::Type::InQuad});
  std::string log;
  anim.started.connectForever([&] { log += "started "; });
  anim.stopped.connectForever([&] { log += "stopped "; });
  anim.finished.connectForever([&] { log += "finished"; });

  anim.start();
  CHECK(anim.running.peek());
  CHECK(near(value.peek(), 10.0));
  advance(50);
  CHECK(near(value.peek(), 12.5)); // 10 + 10 * 0.5^2
  advance(60);
  CHECK(near(value.peek(), 20.0));
  CHECK(!anim.running.peek());
  CHECK_EQ(log, "started stopped finished");
  CHECK(!AnimationDriver::instance().hasRunningAnimations());
}

TEST("animation: running can be bound, and stop() leaves the current value") {
  useManualClock();
  Property<double> value(0);
  Property<bool> active(false);
  NumberAnimation anim;
  anim.target = &value;
  anim.to.set(100.0);
  anim.duration.set(100);
  anim.running.bind([&] { return active.get(); });
  CHECK(!anim.running.peek());
  active.set(true);
  CHECK(anim.running.peek());
  advance(30);
  active.set(false);
  CHECK(!anim.running.peek());
  CHECK(near(value.peek(), 30.0));
  advance(50);
  CHECK(near(value.peek(), 30.0));
  CHECK(anim.running.hasBinding()); // start()/stop() from the binding did not break it
}

TEST("animation: loops and Infinite") {
  useManualClock();
  Property<double> rotation(0);
  NumberAnimation spin;
  spin.target = &rotation;
  spin.from.set(0.0);
  spin.to.set(360.0);
  spin.duration.set(1000);
  spin.loops.set(Animation::Infinite);
  spin.start();
  advance(2250);
  CHECK(near(rotation.peek(), 90.0));
  CHECK(spin.running.peek());

  Property<double> v(0);
  NumberAnimation twice;
  twice.target = &v;
  twice.from.set(0.0);
  twice.to.set(1.0);
  twice.duration.set(100);
  twice.loops.set(2);
  twice.start();
  advance(150);
  CHECK(near(v.peek(), 0.5));
  advance(100);
  CHECK(!twice.running.peek());
  CHECK(near(v.peek(), 1.0));
  spin.stop();
}

TEST("animation: ColorAnimation interpolates each channel") {
  useManualClock();
  Property<Color> color(Color{0.0F, 0.0F, 0.0F, 0.0F});
  ColorAnimation anim;
  anim.target = &color;
  anim.to.set(Color{1.0F, 0.5F, 0.0F, 1.0F});
  anim.duration.set(100);
  anim.start();
  advance(50);
  CHECK(near(color.peek().r, 0.5) && near(color.peek().g, 0.25) && near(color.peek().a, 0.5));
  advance(50);
}

TEST("sequential: actions fire once, children run in order") {
  useManualClock();
  Property<double> x(0);
  Property<double> y(0);
  int scripts = 0;
  SequentialAnimation seq;
  auto* setY = seq.add<PropertyAction<double>>();
  setY->target = &y;
  setY->value.set(5.0);
  auto* moveX = seq.add<NumberAnimation>();
  moveX->target = &x;
  moveX->to.set(100.0);
  moveX->duration.set(100);
  seq.add<PauseAnimation>()->duration.set(50);
  seq.add<ScriptAction>()->script = [&] { ++scripts; };

  seq.start();
  CHECK(near(y.peek(), 5.0));
  advance(50);
  CHECK(near(x.peek(), 50.0));
  CHECK_EQ(scripts, 0);
  advance(70); // in the pause
  CHECK(near(x.peek(), 100.0));
  CHECK_EQ(scripts, 0);
  advance(100);
  CHECK_EQ(scripts, 1);
  CHECK(!seq.running.peek());
}

TEST("sequential: an infinitely looping group keeps cycling") {
  useManualClock();
  Property<double> level(0);
  SequentialAnimation cycle;
  for (const auto& [from, to] : {std::pair{0.0, 1.0}, std::pair{1.0, 0.0}}) {
    auto* step = cycle.add<NumberAnimation>();
    step->target = &level;
    step->from.set(from);
    step->to.set(to);
    step->duration.set(100);
  }
  int scripts = 0;
  cycle.add<ScriptAction>()->script = [&] { ++scripts; };
  cycle.loops.set(Animation::Infinite);
  cycle.start();
  advance(50);
  CHECK(near(level.peek(), 0.5));
  advance(200); // second loop, 50 ms in
  CHECK(near(level.peek(), 0.5));
  CHECK_EQ(scripts, 1); // the first loop's trailing action fired once
  advance(150); // second loop, 200 ms in: going down
  CHECK(near(level.peek(), 0.0));
  advance(400);
  CHECK_EQ(scripts, 3);
  CHECK(cycle.running.peek());
  cycle.stop();
}

TEST("parallel: children share the timeline, the group lasts as long as the longest") {
  useManualClock();
  Property<double> a(0);
  Property<double> b(0);
  ParallelAnimation par;
  auto* fast = par.add<NumberAnimation>();
  fast->target = &a;
  fast->to.set(1.0);
  fast->duration.set(100);
  auto* slow = par.add<NumberAnimation>();
  slow->target = &b;
  slow->to.set(1.0);
  slow->duration.set(200);
  par.start();
  advance(150);
  CHECK(near(a.peek(), 1.0));
  CHECK(near(b.peek(), 0.75));
  CHECK(par.running.peek());
  advance(60);
  CHECK(!par.running.peek());
}

TEST("behavior: no animation before the component is complete") {
  useManualClock();
  Item item;
  auto* behavior = item.create<Behavior<double>>(item.opacity);
  behavior->setAnimation<NumberAnimation>()->duration.set(100);
  item.opacity.set(0.5);
  CHECK(near(item.opacity.peek(), 0.5));
  item.complete();
  item.opacity.set(0.0);
  CHECK(near(item.opacity.peek(), 0.5)); // animating now
  advance(50);
  CHECK(near(item.opacity.peek(), 0.25));
  advance(60);
  CHECK(near(item.opacity.peek(), 0.0));
}

TEST("behavior: retargeting starts from the current value; same target is ignored") {
  useManualClock();
  Item item;
  auto* behavior = item.create<Behavior<double>>(item.x);
  auto* anim = behavior->setAnimation<NumberAnimation>();
  anim->duration.set(100);
  item.complete();

  item.x.set(100);
  advance(50);
  CHECK(near(item.x.peek(), 50.0));
  item.x.set(100); // same target while running: keeps going
  advance(25);
  CHECK(near(item.x.peek(), 75.0));
  item.x.set(0); // retarget: from 75 back to 0 over a full duration
  CHECK(near(item.x.peek(), 75.0));
  advance(50);
  CHECK(near(item.x.peek(), 37.5));
  advance(60);
  CHECK(near(item.x.peek(), 0.0));
}

TEST("behavior: binding changes animate; disabled behaviors write directly") {
  useManualClock();
  Item item;
  Property<double> source(0);
  auto* behavior = item.create<Behavior<double>>(item.y);
  behavior->setAnimation<NumberAnimation>()->duration.set(100);
  item.y.bind([&] { return source.get() * 2.0; });
  item.complete();

  source.set(10);
  CHECK(near(item.y.peek(), 0.0));
  advance(100);
  CHECK(near(item.y.peek(), 20.0));
  CHECK(item.y.hasBinding()); // animation writes don't remove the binding

  behavior->enabled.set(false);
  source.set(1);
  CHECK(near(item.y.peek(), 2.0));
}

TEST("behavior: destroying the item mid-animation is safe") {
  useManualClock();
  {
    auto item = std::make_unique<Item>();
    auto* behavior = item->create<Behavior<double>>(item->opacity);
    behavior->setAnimation<NumberAnimation>()->duration.set(100);
    item->complete();
    item->opacity.set(0.0);
    CHECK(AnimationDriver::instance().hasRunningAnimations());
  }
  CHECK(!AnimationDriver::instance().hasRunningAnimations());
  advance(50);
}

TEST("behavior: a group takes the change where a bare PropertyAction sits (StyledText)") {
  // Behavior on text { SequentialAnimation { NumberAnimation { target: item; property: "opacity";
  // to: 0 } PropertyAction {} NumberAnimation { ...; to: 1 } } }
  useManualClock();
  struct Label : Item {
    Property<std::string> text;
  };
  Label label;
  auto* behavior = label.create<Behavior<std::string>>(label.text);
  auto* seq = behavior->setAnimation<SequentialAnimation>();
  auto* fadeOut = seq->add<NumberAnimation>();
  fadeOut->target = &label.opacity;
  fadeOut->to.set(0.0);
  fadeOut->duration.set(100);
  seq->add<PropertyAction<std::string>>();
  auto* fadeIn = seq->add<NumberAnimation>();
  fadeIn->target = &label.opacity;
  fadeIn->to.set(1.0);
  fadeIn->duration.set(100);
  label.text.set("old");
  label.complete();

  label.text.set("new");
  CHECK(label.text.peek() == "old");  // not yet: the action sits after the fade-out
  advance(50);
  CHECK(near(label.opacity.peek(), 0.5));
  CHECK(label.text.peek() == "old");
  advance(60);  // past the fade-out: the PropertyAction has written the new text
  CHECK(label.text.peek() == "new");
  CHECK(label.opacity.peek() < 0.2);
  advance(200);
  CHECK(near(label.opacity.peek(), 1.0));
  CHECK(label.text.peek() == "new");
}

TEST("behavior: a group without a target-less animation writes the value at once") {
  useManualClock();
  Item item;
  auto* behavior = item.create<Behavior<double>>(item.x);
  auto* seq = behavior->setAnimation<SequentialAnimation>();
  auto* other = seq->add<NumberAnimation>();
  other->target = &item.opacity;
  other->to.set(0.0);
  other->duration.set(100);
  item.complete();
  item.x.set(10.0);
  CHECK(near(item.x.peek(), 10.0));  // nobody took the change: written directly
  advance(50);
  CHECK(near(item.opacity.peek(), 0.5));  // the group still runs
}

TEST("smoothed: velocity sets the duration, in-out quad easing") {
  useManualClock();
  Item item;
  auto* behavior = item.create<Behavior<double>>(item.implicitWidth);
  auto* smooth = behavior->setAnimation<SmoothedAnimation>();
  smooth->velocity.set(100); // 100 px/s
  item.complete();
  item.implicitWidth.set(50); // 0.5 s
  advance(250);
  CHECK(near(item.implicitWidth.peek(), 25.0)); // midpoint of InOutQuad
  advance(260);
  CHECK(near(item.implicitWidth.peek(), 50.0));
}

TEST("rotation: Shortest direction goes the short way round") {
  useManualClock();
  Property<double> angle(350);
  RotationAnimation anim;
  anim.target = &angle;
  anim.to.set(10.0);
  anim.direction.set(RotationAnimation::Direction::Shortest);
  anim.duration.set(100);
  anim.start();
  advance(50);
  CHECK(near(angle.peek(), 360.0));
  advance(50);
}

TEST_MAIN()
