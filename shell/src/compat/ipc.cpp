#include "compat/ipc.h"

#include <algorithm>

namespace ii::qs {

  namespace {
    std::vector<IpcHandler*>& handlers() {
      static std::vector<IpcHandler*> all;
      return all;
    }
  } // namespace

  IpcHandler::IpcHandler() { handlers().push_back(this); }

  IpcHandler::~IpcHandler() { std::erase(handlers(), this); }

  void IpcHandler::addFunction(std::string name, Function function) { m_functions[std::move(name)] = std::move(function); }

  const IpcHandler::Function* IpcHandler::find(const std::string& target, const std::string& function) {
    for (IpcHandler* handler : handlers()) {
      if (handler->enabled.peek() && handler->target.peek() == target) {
        const auto it = handler->m_functions.find(function);
        return it == handler->m_functions.end() ? nullptr : &it->second;
      }
    }
    return nullptr;
  }

} // namespace ii::qs
