// Runtime behaviour of the item tree after construction: the diff test covers the first frame,
// this covers what happens when things change (and must stay clean under ASan).

#include "render/scene/node.h"
#include "runtime/item.h"
#include "runtime/layout.h"
#include "runtime/positioner.h"
#include "runtime/rectangle.h"

#include "../check.h"

#include <memory>

using namespace ii;

TEST("anchors: anchored child follows its parent's resize") {
  Item root;
  root.width.set(100);
  root.height.set(50);
  auto* child = root.add<Item>();
  child->anchors().fill.set(&root);
  child->anchors().margins.set(5);
  root.width.set(200);
  CHECK_EQ(child->width.peek(), 190.0);
  child->anchors().margins.set(1);
  CHECK_EQ(child->x.peek(), 1.0);
  CHECK_EQ(child->width.peek(), 198.0);
}

TEST("anchors: removing an anchor keeps x and restores the implicit width") {
  Item root;
  root.width.set(100);
  auto* child = root.add<Item>();
  child->implicitWidth.set(7);
  child->anchors().left.set(root.leftLine());
  child->anchors().right.set(root.rightLine());
  child->anchors().leftMargin.set(10);
  CHECK_EQ(child->width.peek(), 90.0);
  // Removing right first leaves a left-anchored item (x = 10); removing left then keeps that x.
  // (The other order passes through a right-anchored state, x = 100 - 7, exactly as QML does.)
  child->anchors().right.set({});
  child->anchors().left.set({});
  CHECK_EQ(child->x.peek(), 10.0);
  CHECK_EQ(child->width.peek(), 7.0);
  root.width.set(300);
  CHECK_EQ(child->width.peek(), 7.0);
}

TEST("anchors: a user binding installed later is never removed by anchors") {
  Item root;
  auto* child = root.add<Item>();
  child->anchors().centerIn.set(&root);
  Property<double> source(42);
  child->x.bind([&] { return source.get(); });
  child->anchors().centerIn.set(nullptr);
  CHECK(child->x.hasBinding());
  source.set(43);
  CHECK_EQ(child->x.peek(), 43.0);
}

TEST("tree: reparenting moves the scene node and bumps childrenRevision") {
  Item a;
  Item b;
  auto* child = a.add<Item>();
  const auto revA = a.childrenRevision.peek();
  child->parent.set(&b);
  CHECK(child->node()->parent() == b.node());
  CHECK(a.childItems().empty());
  CHECK_EQ(b.childItems().size(), std::size_t{1});
  CHECK(a.childrenRevision.peek() != revA);
  child->parent.set(nullptr);
  CHECK(child->node()->parent() == nullptr);
}

TEST("tree: effectiveVisible follows ancestors") {
  Item root;
  auto* mid = root.add<Item>();
  auto* leaf = mid->add<Item>();
  root.visible.set(false);
  CHECK(!leaf->effectiveVisible.peek());
  root.visible.set(true);
  CHECK(leaf->effectiveVisible.peek());
  leaf->parent.set(nullptr);
  mid->visible.set(false);
  CHECK(leaf->effectiveVisible.peek());
}

TEST("tree: destroying a parent leaves children owned elsewhere alive and detached") {
  Item owner;
  auto* stray = owner.create<Rectangle>();
  {
    Item visualParent;
    stray->parent.set(&visualParent);
    CHECK(stray->node()->parent() == visualParent.node());
  }
  CHECK(stray->parent.peek() == nullptr);
  CHECK(stray->node()->parent() == nullptr);
  stray->width.set(5); // node must still be alive
}

TEST("polish: an item destroyed while queued is skipped") {
  Item root;
  auto* row = root.add<RowLayout>();
  row->add<Rectangle>()->implicitWidth.set(10);
  CHECK(hasPendingPolish());
  std::unique_ptr<Object> released = root.release(row);
  released.reset();
  flushPolish();
  CHECK(!hasPendingPolish());
}

TEST("layout: relayouts when a child's implicit size, visibility or the child list changes") {
  RowLayout row;
  row.spacing.set(0);
  auto* a = row.add<Rectangle>();
  a->implicitWidth.set(10);
  auto* b = row.add<Rectangle>();
  b->implicitWidth.set(20);
  flushPolish();
  CHECK_EQ(b->x.peek(), 10.0);
  CHECK_EQ(row.implicitWidth.peek(), 30.0);

  a->implicitWidth.set(15);
  CHECK_EQ(row.implicitWidth.peek(), 35.0); // implicit size is immediate
  flushPolish();
  CHECK_EQ(a->width.peek(), 15.0);
  CHECK_EQ(b->x.peek(), 15.0);

  a->visible.set(false);
  flushPolish();
  CHECK_EQ(b->x.peek(), 0.0);
  CHECK_EQ(row.implicitWidth.peek(), 20.0);

  auto* c = row.add<Rectangle>();
  c->implicitWidth.set(5);
  flushPolish();
  CHECK_EQ(c->x.peek(), 20.0);
  CHECK_EQ(row.implicitWidth.peek(), 25.0);
}

TEST("layout: nested layouts propagate size changes both ways") {
  Item root;
  root.width.set(200);
  root.height.set(100);
  auto* column = root.add<ColumnLayout>();
  column->anchors().fill.set(&root);
  auto* inner = column->add<RowLayout>();
  auto* filler = inner->add<Rectangle>();
  filler->layout().fillWidth.set(true);
  filler->implicitHeight.set(10);
  flushPolish();
  // An inner layout with a filling child fills by default (no explicit Layout.fillWidth).
  CHECK_EQ(inner->width.peek(), 200.0);
  CHECK_EQ(filler->width.peek(), 200.0);
  root.width.set(120);
  flushPolish();
  CHECK_EQ(filler->width.peek(), 120.0);
}

TEST("positioner: re-stacks when a child's size changes") {
  Column column;
  column.spacing.set(2);
  auto* a = column.add<Rectangle>();
  a->width.set(10);
  a->height.set(10);
  auto* b = column.add<Rectangle>();
  b->width.set(10);
  b->height.set(10);
  flushPolish();
  CHECK_EQ(b->y.peek(), 12.0);
  a->height.set(30);
  flushPolish();
  CHECK_EQ(b->y.peek(), 32.0);
  CHECK_EQ(column.implicitHeight.peek(), 42.0);
}

TEST_MAIN()
