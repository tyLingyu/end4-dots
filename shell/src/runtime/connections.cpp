#include "runtime/connections.h"

namespace ii {

  Connections::Connections() {
    target.changed().connectForever([this] { reconnect(); });
    enabled.changed().connectForever([this] { reconnect(); });
  }

  void Connections::setConnector(Connector connector) {
    m_connector = std::move(connector);
    reconnect();
  }

  void Connections::componentComplete() { reconnect(); }

  void Connections::reconnect() {
    m_connections.clear();
    if (!isCompleted() || !m_connector || !enabled.peek() || target.peek() == nullptr) {
      return;
    }
    m_connector(target.peek(), m_connections);
  }

} // namespace ii
