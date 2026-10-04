#include "runtime/item.h"

#include "render/scene/node.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ii {

  namespace {
    std::vector<Item*>& polishQueue() {
      static std::vector<Item*> queue;
      return queue;
    }
    // The batch flushPolish() is walking, so a destroyed item can null itself out of it.
    std::vector<Item*>* g_polishBatch = nullptr;
  } // namespace

  Item::Item() : Item(std::make_unique<Node>()) {}

  Item::Item(std::unique_ptr<Node> node) : m_detachedNode(std::move(node)) {
    m_node = m_detachedNode.get();
    init();
  }

  void Item::init() {
    m_node->setUserData(this);

    width.bind([this] { return implicitWidth.get(); }, "Item.width (implicit)");
    height.bind([this] { return implicitHeight.get(); }, "Item.height (implicit)");
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

  void Item::polishLater() {
    if (!m_polishPending) {
      m_polishPending = true;
      polishQueue().push_back(this);
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
    } else {
      node = std::move(m_detachedNode);
    }
    m_parentItem = newParent;
    if (newParent != nullptr) {
      newParent->m_childItems.push_back(this);
      newParent->m_node->addChild(std::move(node));
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

} // namespace ii
