// Quickshell revision 7511545 IPC compatibility layer:
// - Server: /tmp/qs/q/src/ipc/ipc.cpp (IpcServer, IpcServerConnection)
// - Dispatch & CLI: /tmp/qs/q/src/io/ipccomm.cpp (QueryMetadataCommand, StringCallCommand, callFunction, queryMetadata)
// - Handlers: /tmp/qs/q/src/io/ipchandler.cpp (IpcHandler, IpcHandlerRegistry)

#include "compat/ipc.h"
#include "core/log.h"
#include "runtime/fd_watch.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <poll.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

namespace ii::qs {

  namespace {

    constexpr Logger kLog("compat.ipc");
    constexpr std::size_t kMaxMessageSize = 1024 * 1024; // 1 MiB

    // Command types matching Quickshell IPC operations
    constexpr std::uint8_t kCmdCall = 1;
    constexpr std::uint8_t kCmdShow = 2;

    // Response statuses matching Quickshell ipccomm.cpp results
    constexpr std::uint8_t kRespOk = 0;
    constexpr std::uint8_t kRespTargetNotFound = 1;
    constexpr std::uint8_t kRespFunctionNotFound = 2;
    constexpr std::uint8_t kRespError = 3;

    std::vector<IpcHandler*>& handlers() {
      static std::vector<IpcHandler*> all;
      return all;
    }

    void appendU32(std::vector<std::uint8_t>& buf, std::uint32_t val) {
      const std::uint32_t netVal = htonl(val);
      const auto* ptr = reinterpret_cast<const std::uint8_t*>(&netVal);
      buf.insert(buf.end(), ptr, ptr + 4);
    }

    void appendString(std::vector<std::uint8_t>& buf, std::string_view s) {
      appendU32(buf, static_cast<std::uint32_t>(s.size()));
      buf.insert(buf.end(), s.begin(), s.end());
    }

    struct BufferReader {
      const std::uint8_t* data{nullptr};
      std::size_t size{0};
      std::size_t offset{0};

      [[nodiscard]] std::size_t remaining() const { return size >= offset ? size - offset : 0; }

      bool readU8(std::uint8_t& out) {
        if (remaining() < 1) {
          return false;
        }
        out = data[offset++];
        return true;
      }

      bool readU32(std::uint32_t& out) {
        if (remaining() < 4) {
          return false;
        }
        std::uint32_t netVal = 0;
        std::memcpy(&netVal, data + offset, 4);
        offset += 4;
        out = ntohl(netVal);
        return true;
      }

      bool readString(std::string& out) {
        std::uint32_t len = 0;
        if (!readU32(len)) {
          return false;
        }
        if (remaining() < len) {
          return false;
        }
        out.assign(reinterpret_cast<const char*>(data + offset), len);
        offset += len;
        return true;
      }
    };

    std::vector<std::uint8_t> framePayload(const std::vector<std::uint8_t>& payload) {
      std::vector<std::uint8_t> framed;
      framed.reserve(4 + payload.size());
      appendU32(framed, static_cast<std::uint32_t>(payload.size()));
      framed.insert(framed.end(), payload.begin(), payload.end());
      return framed;
    }

    std::vector<std::uint8_t> makeErrorResponse(std::string_view message) {
      std::vector<std::uint8_t> resp;
      resp.push_back(kRespError);
      appendString(resp, message);
      return resp;
    }

    std::vector<std::uint8_t> handleCommand(const std::uint8_t* payload, std::size_t len) {
      BufferReader reader{.data = payload, .size = len, .offset = 0};
      std::uint8_t cmdType = 0;
      if (!reader.readU8(cmdType)) {
        return makeErrorResponse("malformed command packet");
      }

      if (cmdType == kCmdCall) {
        std::string target;
        std::string function;
        std::uint32_t argc = 0;
        if (!reader.readString(target) || !reader.readString(function) || !reader.readU32(argc)) {
          return makeErrorResponse("malformed call request");
        }
        // Bound wire argc against remaining bytes (each string needs at least 4 bytes of length prefix)
        // to prevent bad_alloc on malformed packets.
        if (argc > reader.remaining() / 4) {
          return makeErrorResponse("malformed call argument count");
        }
        std::vector<std::string> args;
        args.reserve(argc);
        for (std::uint32_t i = 0; i < argc; ++i) {
          std::string arg;
          if (!reader.readString(arg)) {
            return makeErrorResponse("malformed call argument");
          }
          args.push_back(std::move(arg));
        }

        if (!IpcHandler::hasTarget(target)) {
          return {kRespTargetNotFound};
        }

        const auto* fn = IpcHandler::find(target, function);
        if (!fn) {
          return {kRespFunctionNotFound};
        }

        std::string retVal;
        try {
          retVal = (*fn)(args);
        } catch (const std::exception& e) {
          return makeErrorResponse(std::string("handler threw exception: ") + e.what());
        }

        std::vector<std::uint8_t> resp;
        resp.push_back(kRespOk);
        appendString(resp, retVal);
        return resp;
      }

      if (cmdType == kCmdShow) {
        std::string target;
        std::string function;
        if (!reader.readString(target) || !reader.readString(function)) {
          return makeErrorResponse("malformed show request");
        }

        if (target.empty()) {
          // As Quickshell's QueryMetadataCommand wireTargets()
          const auto active = IpcHandler::activeTargets();
          std::string text;
          for (const auto& t : active) {
            if (!text.empty()) {
              text += '\n';
            }
            text += "target " + t.name;
            for (const auto& f : t.functions) {
              text += "\n  function " + f + "()";
            }
          }
          std::vector<std::uint8_t> resp;
          resp.push_back(kRespOk);
          appendString(resp, text);
          return resp;
        }

        if (!IpcHandler::hasTarget(target)) {
          return {kRespTargetNotFound};
        }

        if (function.empty()) {
          // As Quickshell's QueryMetadataCommand handler wireDef()
          const auto funcs = IpcHandler::targetFunctions(target);
          std::string text = "target " + target;
          for (const auto& f : funcs) {
            text += "\n  function " + f + "()";
          }
          std::vector<std::uint8_t> resp;
          resp.push_back(kRespOk);
          appendString(resp, text);
          return resp;
        }

        const auto* fn = IpcHandler::find(target, function);
        if (!fn) {
          return {kRespFunctionNotFound};
        }

        // As Quickshell's QueryMetadataCommand function wireDef()
        const std::string text = "function " + function + "()";
        std::vector<std::uint8_t> resp;
        resp.push_back(kRespOk);
        appendString(resp, text);
        return resp;
      }

      return makeErrorResponse("unknown command type");
    }

    struct ClientConn {
      int fd{-1};
      FdWatch::Id watchId{0};
      std::vector<std::uint8_t> inBuf;
      std::vector<std::uint8_t> outBuf;
      std::size_t outPos{0};
    };

    class ServerInstance {
    public:
      ~ServerInstance() { shutdown(); }

      bool init(const std::string& path) {
        const std::filesystem::path fsPath(path);
        std::error_code ec;
        std::filesystem::create_directories(fsPath.parent_path(), ec);
        if (ec) {
          kLog.error("failed to create directory for socket {}: {}", path, ec.message());
          return false;
        }
        ::chmod(fsPath.parent_path().c_str(), 0700);

        // Check for existing socket file (live instance vs stale leftover)
        struct stat st{};
        if (::stat(path.c_str(), &st) == 0) {
          const int probeFd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
          if (probeFd >= 0) {
            sockaddr_un probeAddr{};
            probeAddr.sun_family = AF_UNIX;
            std::strncpy(probeAddr.sun_path, path.c_str(), sizeof(probeAddr.sun_path) - 1);
            if (::connect(probeFd, reinterpret_cast<sockaddr*>(&probeAddr), sizeof(probeAddr)) == 0) {
              ::close(probeFd);
              kLog.error("another instance is already listening on {}", path);
              // Return false without setting m_socketPath so destructor does NOT unlink the live socket!
              return false;
            }
            ::close(probeFd);
          }
          // Connect failed: leftover socket from dead instance, remove it
          kLog.info("removing stale socket file {}", path);
          ::unlink(path.c_str());
        }

        m_listenFd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (m_listenFd < 0) {
          kLog.error("failed to create socket: {}", std::strerror(errno));
          return false;
        }

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (path.size() >= sizeof(addr.sun_path)) {
          kLog.error("socket path too long: {}", path);
          ::close(m_listenFd);
          m_listenFd = -1;
          return false;
        }
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

        if (::bind(m_listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
          kLog.error("failed to bind to {}: {}", path, std::strerror(errno));
          ::close(m_listenFd);
          m_listenFd = -1;
          return false;
        }

        if (::listen(m_listenFd, SOMAXCONN) < 0) {
          kLog.error("failed to listen on {}: {}", path, std::strerror(errno));
          ::unlink(path.c_str());
          ::close(m_listenFd);
          m_listenFd = -1;
          return false;
        }

        // Only assign m_socketPath after successful bind/listen so shutdown() only unlinks our own socket
        m_socketPath = path;

        m_listenWatchId = FdWatch::watch(m_listenFd, POLLIN, [this](short revents) {
          if (revents & POLLIN) {
            onAccept();
          }
        });

        kLog.info("listening on {}", m_socketPath);
        return true;
      }

      void shutdown() {
        for (const auto& [clientFd, conn] : m_clients) {
          FdWatch::unwatch(conn.watchId);
          ::close(clientFd);
        }
        m_clients.clear();

        if (m_listenWatchId != 0) {
          FdWatch::unwatch(m_listenWatchId);
          m_listenWatchId = 0;
        }
        if (m_listenFd >= 0) {
          ::close(m_listenFd);
          m_listenFd = -1;
        }
        if (!m_socketPath.empty()) {
          ::unlink(m_socketPath.c_str());
          m_socketPath.clear();
        }
      }

    private:
      void onAccept() {
        while (true) {
          sockaddr_un clientAddr{};
          socklen_t addrLen = sizeof(clientAddr);
          const int clientFd = ::accept4(m_listenFd, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen,
                                        SOCK_NONBLOCK | SOCK_CLOEXEC);
          if (clientFd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              break;
            }
            if (errno == EINTR) {
              continue;
            }
            kLog.warn("accept failed: {}", std::strerror(errno));
            break;
          }

          ClientConn conn;
          conn.fd = clientFd;
          conn.watchId = FdWatch::watch(clientFd, POLLIN, [this, clientFd](short revents) {
            onClientEvent(clientFd, revents);
          });
          m_clients.emplace(clientFd, std::move(conn));
        }
      }

      void onClientEvent(int clientFd, short revents) {
        auto it = m_clients.find(clientFd);
        if (it == m_clients.end()) {
          return;
        }
        auto& conn = it->second;

        if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
          if (!(revents & POLLIN)) {
            closeClient(clientFd);
            return;
          }
        }

        bool clientEof = false;
        if (revents & POLLIN) {
          std::uint8_t buf[4096];
          while (true) {
            const ssize_t n = ::read(clientFd, buf, sizeof(buf));
            if (n > 0) {
              conn.inBuf.insert(conn.inBuf.end(), buf, buf + n);
              if (conn.inBuf.size() > kMaxMessageSize) {
                kLog.warn("client {} exceeded max message size", clientFd);
                closeClient(clientFd);
                return;
              }
            } else if (n == 0) {
              clientEof = true;
              break;
            } else {
              if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
              }
              if (errno == EINTR) {
                continue;
              }
              closeClient(clientFd);
              return;
            }
          }

          while (conn.inBuf.size() >= 4) {
            std::uint32_t payloadLen = 0;
            std::memcpy(&payloadLen, conn.inBuf.data(), 4);
            payloadLen = ntohl(payloadLen);

            if (payloadLen > kMaxMessageSize) {
              closeClient(clientFd);
              return;
            }

            if (conn.inBuf.size() < 4 + payloadLen) {
              break;
            }

            const std::uint8_t* payloadData = conn.inBuf.data() + 4;
            const auto respPayload = handleCommand(payloadData, payloadLen);
            const auto framedResp = framePayload(respPayload);

            conn.outBuf.insert(conn.outBuf.end(), framedResp.begin(), framedResp.end());
            conn.inBuf.erase(conn.inBuf.begin(), conn.inBuf.begin() + 4 + payloadLen);

            // Once a response is queued, stop reading/parsing more input (one-shot lifecycle)
            FdWatch::setEvents(conn.watchId, POLLOUT);
            break;
          }
        }

        // If client hit EOF and no response was queued, close immediately (prevents CPU spin / fd leak)
        if (clientEof && conn.outBuf.empty()) {
          closeClient(clientFd);
          return;
        }

        if ((revents & POLLOUT) || !conn.outBuf.empty()) {
          trySend(conn);
        }
      }

      void trySend(ClientConn& conn) {
        while (conn.outPos < conn.outBuf.size()) {
          // Use MSG_NOSIGNAL to avoid SIGPIPE if client disconnected before reading response
          const ssize_t n = ::send(conn.fd, conn.outBuf.data() + conn.outPos,
                                   conn.outBuf.size() - conn.outPos, MSG_NOSIGNAL);
          if (n > 0) {
            conn.outPos += static_cast<std::size_t>(n);
          } else if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              FdWatch::setEvents(conn.watchId, POLLOUT);
              return;
            }
            if (errno == EINTR) {
              continue;
            }
            // EPIPE, ECONNRESET, etc.
            closeClient(conn.fd);
            return;
          }
        }

        // Response completed; as Quickshell's IpcServerConnection one-shot lifecycle
        closeClient(conn.fd);
      }

      void closeClient(int clientFd) {
        auto it = m_clients.find(clientFd);
        if (it != m_clients.end()) {
          FdWatch::unwatch(it->second.watchId);
          ::close(it->second.fd);
          m_clients.erase(it);
        }
      }

      std::string m_socketPath;
      int m_listenFd{-1};
      FdWatch::Id m_listenWatchId{0};
      std::map<int, ClientConn> m_clients;
    };

    std::unique_ptr<ServerInstance> g_serverInstance;

    bool probeInstanceAlive(const std::string& socketPath) {
      const std::string path = socketPath.empty() ? IpcServer::defaultSocketPath() : socketPath;
      const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
      if (fd < 0) {
        return false;
      }
      sockaddr_un addr{};
      addr.sun_family = AF_UNIX;
      if (path.size() >= sizeof(addr.sun_path)) {
        ::close(fd);
        return false;
      }
      std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
      const bool alive = (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
      ::close(fd);
      return alive;
    }

    IpcClient::Result sendAndReceive(const std::vector<std::uint8_t>& requestPayload,
                                     const std::string& socketPath) {
      const std::string path = socketPath.empty() ? IpcServer::defaultSocketPath() : socketPath;
      const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
      if (fd < 0) {
        return IpcClient::Result{
            .status = IpcClient::Status::ConnectionFailed,
            .value = std::string("socket() failed: ") + std::strerror(errno),
        };
      }

      sockaddr_un addr{};
      addr.sun_family = AF_UNIX;
      if (path.size() >= sizeof(addr.sun_path)) {
        ::close(fd);
        return IpcClient::Result{
            .status = IpcClient::Status::ConnectionFailed,
            .value = "socket path too long",
        };
      }
      std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

      if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        const std::string err = std::strerror(errno);
        ::close(fd);
        return IpcClient::Result{
            .status = IpcClient::Status::ConnectionFailed,
            .value = "Could not connect to ii-shell IPC at " + path + ": " + err,
        };
      }

      // Quickshell waits indefinitely for responses; avoid artificial timeouts
      const auto framedReq = framePayload(requestPayload);
      std::size_t written = 0;
      while (written < framedReq.size()) {
        const ssize_t n = ::send(fd, framedReq.data() + written, framedReq.size() - written, MSG_NOSIGNAL);
        if (n <= 0) {
          if (n < 0 && errno == EINTR) {
            continue;
          }
          const std::string err = std::strerror(errno);
          ::close(fd);
          return IpcClient::Result{
              .status = IpcClient::Status::Error,
              .value = "write failed: " + err,
          };
        }
        written += static_cast<std::size_t>(n);
      }

      std::uint32_t payloadLen = 0;
      std::size_t headerRead = 0;
      std::uint8_t headerBuf[4];
      while (headerRead < 4) {
        const ssize_t n = ::read(fd, headerBuf + headerRead, 4 - headerRead);
        if (n <= 0) {
          if (n < 0 && errno == EINTR) {
            continue;
          }
          ::close(fd);
          return IpcClient::Result{
              .status = IpcClient::Status::Error,
              .value = "connection closed before response header received",
          };
        }
        headerRead += static_cast<std::size_t>(n);
      }
      std::memcpy(&payloadLen, headerBuf, 4);
      payloadLen = ntohl(payloadLen);

      if (payloadLen > kMaxMessageSize) {
        ::close(fd);
        return IpcClient::Result{
            .status = IpcClient::Status::Error,
            .value = "response exceeded maximum size",
        };
      }

      std::vector<std::uint8_t> respPayload(payloadLen);
      std::size_t bodyRead = 0;
      while (bodyRead < payloadLen) {
        const ssize_t n = ::read(fd, respPayload.data() + bodyRead, payloadLen - bodyRead);
        if (n <= 0) {
          if (n < 0 && errno == EINTR) {
            continue;
          }
          ::close(fd);
          return IpcClient::Result{
              .status = IpcClient::Status::Error,
              .value = "connection closed before response body received",
          };
        }
        bodyRead += static_cast<std::size_t>(n);
      }
      ::close(fd);

      BufferReader reader{.data = respPayload.data(), .size = respPayload.size(), .offset = 0};
      std::uint8_t status = 0;
      if (!reader.readU8(status)) {
        return IpcClient::Result{.status = IpcClient::Status::Error, .value = "empty response"};
      }

      if (status == kRespOk) {
        std::string val;
        if (!reader.readString(val)) {
          return IpcClient::Result{.status = IpcClient::Status::Error, .value = "corrupt response payload"};
        }
        return IpcClient::Result{.status = IpcClient::Status::Ok, .value = std::move(val)};
      }
      if (status == kRespTargetNotFound) {
        return IpcClient::Result{.status = IpcClient::Status::TargetNotFound, .value = "Target not found."};
      }
      if (status == kRespFunctionNotFound) {
        return IpcClient::Result{.status = IpcClient::Status::FunctionNotFound, .value = "Function not found."};
      }
      if (status == kRespError) {
        std::string err;
        reader.readString(err);
        return IpcClient::Result{.status = IpcClient::Status::Error, .value = std::move(err)};
      }

      return IpcClient::Result{.status = IpcClient::Status::Error, .value = "unknown response status"};
    }

    // In Quickshell (/tmp/qs/q/src/ipc/ipc.cpp:116 connect() and launch/command.cpp ipcCommand),
    // the callback return code is dropped and 0 is returned whenever the instance was reached.
    // Exit code is non-zero ONLY when no instance is reachable.
    int dispatchResult(const IpcClient::Result& res) {
      switch (res.status) {
        case IpcClient::Status::Ok:
          if (!res.value.empty()) {
            std::printf("%s\n", res.value.c_str());
          }
          return 0;
        case IpcClient::Status::TargetNotFound:
          std::fprintf(stderr, "Target not found.\n");
          return 0;
        case IpcClient::Status::FunctionNotFound:
          std::fprintf(stderr, "Function not found.\n");
          return 0;
        case IpcClient::Status::Error:
          std::fprintf(stderr, "%s\n", res.value.c_str());
          return 0;
        case IpcClient::Status::ConnectionFailed:
          std::fprintf(stderr, "%s\n", res.value.c_str());
          return 1;
      }
      return 1;
    }

  } // namespace

  IpcHandler::IpcHandler() { handlers().push_back(this); }

  IpcHandler::~IpcHandler() { std::erase(handlers(), this); }

  void IpcHandler::addFunction(std::string name, Function function) {
    m_functions[std::move(name)] = std::move(function);
  }

  const IpcHandler::Function* IpcHandler::find(const std::string& target, const std::string& function) {
    for (IpcHandler* handler : handlers()) {
      if (handler->enabled.peek() && handler->target.peek() == target) {
        const auto it = handler->m_functions.find(function);
        if (it != handler->m_functions.end()) {
          return &it->second;
        }
      }
    }
    return nullptr;
  }

  bool IpcHandler::hasTarget(const std::string& target) {
    for (IpcHandler* handler : handlers()) {
      if (handler->enabled.peek() && handler->target.peek() == target) {
        return true;
      }
    }
    return false;
  }

  std::vector<std::string> IpcHandler::targetFunctions(const std::string& target) {
    std::vector<std::string> result;
    for (IpcHandler* handler : handlers()) {
      if (handler->enabled.peek() && handler->target.peek() == target) {
        for (const auto& [name, _] : handler->m_functions) {
          result.push_back(name);
        }
      }
    }
    return result;
  }

  std::vector<IpcHandler::TargetInfo> IpcHandler::activeTargets() {
    std::map<std::string, std::vector<std::string>> targetMap;
    for (IpcHandler* handler : handlers()) {
      if (handler->enabled.peek()) {
        const std::string name = handler->target.peek();
        if (!name.empty()) {
          auto& funcs = targetMap[name];
          for (const auto& [fname, _] : handler->m_functions) {
            funcs.push_back(fname);
          }
        }
      }
    }
    std::vector<TargetInfo> result;
    result.reserve(targetMap.size());
    for (auto& [name, funcs] : targetMap) {
      result.push_back(TargetInfo{.name = std::move(name), .functions = std::move(funcs)});
    }
    return result;
  }

  std::string IpcServer::defaultSocketPath() {
    const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR");
    std::filesystem::path base;
    if (runtimeDir && *runtimeDir != '\0') {
      base = runtimeDir;
    } else {
      base = std::filesystem::temp_directory_path() / ("ii-shell-" + std::to_string(::getuid()));
    }
    return (base / "ii-shell" / "ipc.sock").string();
  }

  bool IpcServer::start(const std::string& socketPath) {
    if (g_serverInstance) {
      return true;
    }
    auto instance = std::make_unique<ServerInstance>();
    const std::string path = socketPath.empty() ? defaultSocketPath() : socketPath;
    if (!instance->init(path)) {
      return false;
    }
    g_serverInstance = std::move(instance);
    return true;
  }

  void IpcServer::stop() {
    if (g_serverInstance) {
      g_serverInstance->shutdown();
      g_serverInstance.reset();
    }
  }

  bool IpcServer::isRunning() {
    return g_serverInstance != nullptr;
  }

  IpcClient::Result IpcClient::call(const std::string& target, const std::string& function,
                                    const std::vector<std::string>& args,
                                    const std::string& socketPath) {
    std::vector<std::uint8_t> req;
    req.push_back(kCmdCall);
    appendString(req, target);
    appendString(req, function);
    appendU32(req, static_cast<std::uint32_t>(args.size()));
    for (const auto& a : args) {
      appendString(req, a);
    }
    return sendAndReceive(req, socketPath);
  }

  IpcClient::Result IpcClient::show(const std::string& target, const std::string& function,
                                    const std::string& socketPath) {
    std::vector<std::uint8_t> req;
    req.push_back(kCmdShow);
    appendString(req, target);
    appendString(req, function);
    return sendAndReceive(req, socketPath);
  }

  int handleIpcCli(int argc, char** argv) {
    // Mimicking Quickshell CLI:
    // qs ipc call <target> <function> [args...]
    // qs ipc show [target] [function]
    // qs msg <target> <function> [args...] (deprecated alias)
    if (argc < 2) {
      std::fprintf(stderr, "Usage: ii-shell ipc <call|show> ...\n");
      return 1;
    }

    const bool isMsg = (std::strcmp(argv[1], "msg") == 0);
    int subIdx = 2;
    std::string command = "call";

    if (!isMsg) {
      if (argc < 3) {
        std::fprintf(stderr, "Usage: ii-shell ipc <call|show> ...\n");
        return 1;
      }
      command = argv[2];
      subIdx = 3;
    }

    // Connect check: non-zero only when no instance is reachable
    if (!probeInstanceAlive("")) {
      std::fprintf(stderr, "Could not connect to ii-shell IPC at %s: %s\n",
                   IpcServer::defaultSocketPath().c_str(), "Connection refused or server not running");
      return 1;
    }

    if (command == "call") {
      if (argc <= subIdx) {
        std::fprintf(stderr, "Target required to send message.\n");
        return 0; // Instance reached (Quickshell drops error code)
      }
      if (argc <= subIdx + 1) {
        std::fprintf(stderr, "Function required to send message.\n");
        return 0; // Instance reached (Quickshell drops error code, keybinds test_alive relies on this)
      }
      const std::string target = argv[subIdx];
      const std::string function = argv[subIdx + 1];
      std::vector<std::string> args;
      for (int i = subIdx + 2; i < argc; ++i) {
        args.emplace_back(argv[i]);
      }
      const auto res = IpcClient::call(target, function, args);
      return dispatchResult(res);
    }

    if (command == "show") {
      const std::string target = (argc > subIdx) ? argv[subIdx] : "";
      const std::string function = (argc > subIdx + 1) ? argv[subIdx + 1] : "";
      const auto res = IpcClient::show(target, function);
      return dispatchResult(res);
    }

    std::fprintf(stderr, "Unknown ipc subcommand: %s\n", command.c_str());
    return 1;
  }

} // namespace ii::qs
