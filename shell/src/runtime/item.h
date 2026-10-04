#pragma once

#include "runtime/anchors.h"
#include "runtime/object.h"
#include "runtime/property.h"

#include <memory>
#include <vector>

class Node;

namespace ii {

  // QQuickItem. Owns a scene Node and keeps it in sync with its properties. The node lives in
  // the parent item's node while the item has a visual parent, and in m_detachedNode otherwise.
  class Item : public Object {
  public:
    Item();
    ~Item() override;

    Property<double> x;
    Property<double> y;
    Property<double> z;
    // Follow implicitWidth/implicitHeight until assigned or bound (QML default-size rule).
    Property<double> width;
    Property<double> height;
    Property<double> implicitWidth;
    Property<double> implicitHeight;
    Property<bool> visible{true};
    Property<double> opacity{1.0};
    Property<bool> clip{false};
    Property<bool> enabled{true};
    // Visual parent. Assigning reparents the item (and its node).
    Property<Item*> parent;
    // `item.visible` as QML reads it: own visibility and every ancestor's.
    Property<bool> effectiveVisible{true};

    // Created on first use, so un-anchored items pay nothing.
    [[nodiscard]] Anchors& anchors();
    [[nodiscard]] Anchors* anchorsIfAny() const noexcept { return m_anchors.get(); }

    [[nodiscard]] AnchorLine leftLine() noexcept { return {this, AnchorEdge::Left}; }
    [[nodiscard]] AnchorLine rightLine() noexcept { return {this, AnchorEdge::Right}; }
    [[nodiscard]] AnchorLine horizontalCenterLine() noexcept { return {this, AnchorEdge::HorizontalCenter}; }
    [[nodiscard]] AnchorLine topLine() noexcept { return {this, AnchorEdge::Top}; }
    [[nodiscard]] AnchorLine bottomLine() noexcept { return {this, AnchorEdge::Bottom}; }
    [[nodiscard]] AnchorLine verticalCenterLine() noexcept { return {this, AnchorEdge::VerticalCenter}; }

    // Creates a child item owned by this item and visually parented to it.
    template <typename T, typename... Args> T* add(Args&&... args) {
      T* child = create<T>(std::forward<Args>(args)...);
      child->parent.set(this);
      return child;
    }

    [[nodiscard]] const std::vector<Item*>& childItems() const noexcept { return m_childItems; }
    [[nodiscard]] Node* node() const noexcept { return m_node; }

    // Layout-style deferred work, run before the next frame (or by flushPolish() in tests).
    void polishLater();
    virtual void polish() {}

  protected:
    // Derived items supply their own node type (RectNode, TextNode, ...).
    explicit Item(std::unique_ptr<Node> node);

    // Called after width or height changed and the node was resized.
    virtual void geometryChanged() {}

  private:
    void init();
    void reparent(Item* newParent);
    static void syncPosition(void* self);
    static void syncSize(void* self);
    static void syncZ(void* self);
    static void syncVisible(void* self);
    static void syncOpacity(void* self);
    static void syncClip(void* self);
    static void onParentChanged(void* self);

    Node* m_node = nullptr;
    std::unique_ptr<Node> m_detachedNode;
    Item* m_parentItem = nullptr;
    std::vector<Item*> m_childItems;
    std::unique_ptr<Anchors> m_anchors;
    bool m_polishPending = false;

    friend void flushPolish();
  };

  // Runs pending polish() calls until none are left.
  void flushPolish();
  [[nodiscard]] bool hasPendingPolish() noexcept;

} // namespace ii
