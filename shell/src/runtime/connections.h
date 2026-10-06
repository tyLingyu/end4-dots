#pragma once

#include "runtime/object.h"
#include "runtime/property.h"

#include <functional>
#include <vector>

namespace ii {

  // QML Connections. Generated code binds `target` and supplies a connector that connects the
  // handlers to a target object (cast to its type). The handlers are reconnected whenever the
  // target changes, and only once the component is complete, as QQmlConnections does; a null
  // target, or enabled: false, connects nothing.
  class Connections : public Object {
  public:
    using Connector = std::function<void(Object* target, std::vector<Connection>& out)>;

    Connections();

    Property<Object*> target;
    Property<bool> enabled{true};
    Property<bool> ignoreUnknownSignals;

    void setConnector(Connector connector);

  protected:
    void componentComplete() override;

  private:
    void reconnect();

    Connector m_connector;
    std::vector<Connection> m_connections;
  };

} // namespace ii
