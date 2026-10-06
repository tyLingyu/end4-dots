#pragma once

// Quickshell.Services.Notifications: an org.freedesktop.Notifications server.

#include "compat/object_model.h"
#include "runtime/js.h"
#include "runtime/object.h"
#include "runtime/property.h"

#include <string>
#include <vector>

namespace ii::qs {

  struct NotificationUrgency {
    enum Enum { Low = 0, Normal = 1, Critical = 2 };
  };

  class NotificationAction : public Object {
  public:
    Property<std::string> identifier;
    Property<std::string> text;
    void invoke();
  };

  class Notification : public Object {
  public:
    Property<int> id;
    Property<bool> tracked;
    Property<bool> lastGeneration;
    Property<double> expireTimeout;
    Property<std::string> appName;
    Property<std::string> appIcon;
    Property<std::string> summary;
    Property<std::string> body;
    Property<NotificationUrgency::Enum> urgency{NotificationUrgency::Normal};
    Property<std::vector<NotificationAction*>> actions;
    Property<bool> hasActionIcons;
    Property<bool> resident;
    Property<bool> transient;
    Property<std::string> desktopEntry;
    Property<std::string> image;
    Property<bool> hasInlineReply;
    Property<std::string> inlineReplyPlaceholder;
    Property<js::Json> hints;
    Signal<int> closed;

    void expire();
    void dismiss();
    void sendInlineReply(const std::string& text);
  };

  class NotificationServer : public Object {
  public:
    NotificationServer();

    Property<bool> keepOnReload{true};
    Property<bool> persistenceSupported;
    Property<bool> bodySupported{true};
    Property<bool> bodyMarkupSupported;
    Property<bool> bodyHyperlinksSupported;
    Property<bool> bodyImagesSupported;
    Property<bool> actionsSupported;
    Property<bool> actionIconsSupported;
    Property<bool> imageSupported;
    Property<bool> inlineReplySupported;
    Property<UntypedObjectModel*> trackedNotifications;
    Property<std::vector<std::string>> extraHints;
    Signal<Notification*> notification;

  protected:
    void componentComplete() override;
  };

} // namespace ii::qs
