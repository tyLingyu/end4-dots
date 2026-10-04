#include "runtime/item.h"

#include "render/scene/node.h"
#include "runtime/layout.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace ii {

  namespace {
    std::vector<Item*>& polishQueue() {
      static std::vector<Item*> queue;
      return queue;
    }
    // The batch flushPolish() is walking, so a destroyed item can null itself out of it.
    std::vector<Item*>* g_polishBatch = nullptr;
    std::function<void()> g_polishRequested;
  } // namespace

  Item::Item() : Item(std::make_unique<Node>()) {}

  Item::Item(std::unique_ptr<Node> node) : m_detachedNode(std::move(node)) {
    m_node = m_detachedNode.get();
    init();
  }

  void Item::init() {
    m_node->setUserData(this);

    width.bind([this] { return implicitWidth.get(); }, kImplicitWidthBindingName);
    height.bind([this] { return implicitHeight.get(); }, kImplicitHeightBindingName);
    effectiveVisible.bind(
        [this] {
          Item* p = parent.get();
          return visible.get() && (p == nullptr || p->effectiveVisible.get());
        },
        "Item.effectiveVisible"
    );

    x.setHook(&syncPosition, this);
    y.setHook(&syncPosition, this);
    width.setHook(&syncSize, this);
    height.setHook(&syncSize, this);
    z.setHook(&syncZ, this);
    visible.setHook(&syncVisible, this);
    opacity.setHook(&syncOpacity, this);
    clip.setHook(&syncClip, this);
    rotation.setHook(&syncRotation, this);
    scale.setHook(&syncScale, this);
    parent.setHook(&onParentChanged, this);
  }

  Item::~Item() {
    if (m_polishPending) {
      std::ranges::replace(polishQueue(), this, nullptr);
      if (g_polishBatch != nullptr) {
        std::ranges::replace(*g_polishBatch, this, nullptr);
      }
    }
    // Owned children first: their nodes live inside ours.
    destroyOwned();
    // Visual children owned elsewhere outlive us; take them (and their nodes) out first.
    while (!m_childItems.empty()) {
      m_childItems.back()->parent.set(nullptr);
    }
    m_anchors.reset();
    if (m_parentItem != nullptr) {
      std::erase(m_parentItem->m_childItems, this);
      (void)m_parentItem->m_node->removeChild(m_node);
    }
  }

  Anchors& Item::anchors() {
    if (m_anchors == nullptr) {
      m_anchors = std::make_unique<Anchors>(*this);
    }
    return *m_anchors;
  }

  LayoutAttached& Item::layout() {
    if (m_layout == nullptr) {
      m_layout = std::make_unique<LayoutAttached>();
    }
    return *m_layout;
  }

  bool Item::followsImplicitSize(Axis axis) const noexcept {
    const Binding* binding = axis == Axis::Horizontal ? width.binding() : height.binding();
    const char* expected = axis == Axis::Horizontal ? kImplicitWidthBindingName : kImplicitHeightBindingName;
    return binding != nullptr && binding->name() == expected;
  }

  void Item::polishLater() {
    if (!m_polishPending) {
      m_polishPending = true;
      const bool wasEmpty = polishQueue().empty();
      polishQueue().push_back(this);
      if (wasEmpty && g_polishRequested) {
        g_polishRequested();
      }
    }
  }

  void Item::reparent(Item* newParent) {
    if (newParent == m_parentItem) {
      return;
    }
    std::unique_ptr<Node> node;
    if (m_parentItem != nullptr) {
      std::erase(m_parentItem->m_childItems, this);
      node = m_parentItem->m_node->removeChild(m_node);
      m_parentItem->childrenRevision.set(m_parentItem->childrenRevision.peek() + 1);
    } else {
      node = std::move(m_detachedNode);
    }
    m_parentItem = newParent;
    if (newParent != nullptr) {
      newParent->m_childItems.push_back(this);
      newParent->m_node->addChild(std::move(node));
      newParent->childrenRevision.set(newParent->childrenRevision.peek() + 1);
    } else {
      m_detachedNode = std::move(node);
    }
  }

  void Item::syncPosition(void* self) {
    auto* item = static_cast<Item*>(self);
    item->m_node->setPosition(static_cast<float>(item->x.peek()), static_cast<float>(item->y.peek()));
  }

  void Item::syncSize(void* self) {
    auto* item = static_cast<Item*>(self);
    item->m_node->setFrameSize(static_cast<float>(item->width.peek()), static_cast<float>(item->height.peek()));
    item->geometryChanged();
  }

  void Item::syncZ(void* self) {
    auto* item = static_cast<Item*>(self);
    item->m_node->setZIndex(static_cast<std::int32_t>(std::lround(item->z.peek())));
  }

  void Item::syncVisible(void* self) {
    auto* item = static_cast<Item*>(self);
    item->m_node->setVisible(item->visible.peek());
  }

  void Item::syncOpacity(void* self) {
    auto* item = static_cast<Item*>(self);
    item->m_node->setOpacity(static_cast<float>(item->opacity.peek()));
  }

  void Item::syncClip(void* self) {
    auto* item = static_cast<Item*>(self);
    item->m_node->setClipChildren(item->clip.peek());
  }

  void Item::syncRotation(void* self) {
    auto* item = static_cast<Item*>(self);
    item->m_node->setRotation(static_cast<float>(item->rotation.peek() * std::numbers::pi / 180.0));
  }

  void Item::syncScale(void* self) {
    auto* item = static_cast<Item*>(self);
    item->m_node->setScale(static_cast<float>(item->scale.peek()));
  }

  void Item::onParentChanged(void* self) {
    auto* item = static_cast<Item*>(self);
    item->reparent(item->parent.peek());
  }

  void flushPolish() {
    // polish() may schedule more polish (a layout resizing a nested layout); bound the rounds.
    for (int round = 0; round < 64 && !polishQueue().empty(); ++round) {
      std::vector<Item*> batch = std::exchange(polishQueue(), {});
      g_polishBatch = &batch;
      for (Item*& item : batch) {
        if (item == nullptr) {
          continue;
        }
        Item* current = item;
        current->m_polishPending = false;
        current->polish();
      }
      g_polishBatch = nullptr;
    }
  }

  bool hasPendingPolish() noexcept { return !polishQueue().empty(); }

  void setPolishRequestHandler(std::function<void()> handler) { g_polishRequested = std::move(handler); }

} // namespace ii
