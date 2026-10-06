#include "compat/notifications.h"

#include "core/log.h"

namespace ii::qs {

  namespace {
    constexpr Logger kLog("notifications");
  }

  // The D-Bus server (org.freedesktop.Notifications) comes with the notification UI; the OSD never
  // creates a NotificationServer.
  NotificationServer::NotificationServer() { trackedNotifications.set(create<UntypedObjectModel>()); }

  void NotificationServer::componentComplete() {
    kLog.warn("NotificationServer is not implemented yet: no notifications will be received");
  }

  void NotificationAction::invoke() {}
  void Notification::expire() {}
  void Notification::dismiss() {}
  void Notification::sendInlineReply(const std::string& text) { (void)text; }

} // namespace ii::qs
