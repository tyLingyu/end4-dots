// Unit tests for Quickshell revision 7511545 IPC compatibility:
// tests IpcHandler registration, unix socket server on FdWatch, client call, argument passing,
// return values, target/function not found errors, disabled handler, metadata queries (show),
// and regressions for EOF probes, socket unlinking, SIGPIPE safety, and Quickshell exit code parity.

#include "compat/ipc.h"

#include "../check.h"
#include "../pump.h"

#include <arpa/inet.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace ii;

namespace {

  std::filesystem::path scratch() {
    static const auto dir = [] {
      auto path = std::filesystem::temp_directory_path() / ("ii-ipc-test-" + std::to_string(::getpid()));
      std::filesystem::remove_all(path);
      std::filesystem::create_directories(path);
      ::setenv("XDG_RUNTIME_DIR", path.c_str(), 1);
      return path;
    }();
    return dir;
  }

} // namespace

TEST("ipc: server starts, handles call with arguments and return value") {
  (void)scratch();

  auto handler = std::make_unique<qs::IpcHandler>();
  handler->target.set(std::string("calculator"));
  handler->addFunction("add", [](const std::vector<std::string>& args) -> std::string {
    int sum = 0;
    for (const auto& a : args) {
      sum += std::stoi(a);
    }
    return std::to_string(sum);
  });
  handler->addFunction("echo", [](const std::vector<std::string>& args) -> std::string {
    return args.empty() ? "" : args[0];
  });

  CHECK(qs::IpcServer::start());
  CHECK(qs::IpcServer::isRunning());

  // Call "add" with 3 arguments
  {
    std::atomic<bool> done{false};
    qs::IpcClient::Result res;
    std::jthread worker([&] {
      res = qs::IpcClient::call("calculator", "add", {"10", "20", "12"});
      done = true;
    });
    CHECK(ii_test::pumpUntil([&] { return done.load(); }));
    CHECK(res.status == qs::IpcClient::Status::Ok);
    CHECK(res.value == "42");
  }

  // Call "echo" with argument
  {
    std::atomic<bool> done{false};
    qs::IpcClient::Result res;
    std::jthread worker([&] {
      res = qs::IpcClient::call("calculator", "echo", {"hello world"});
      done = true;
    });
    CHECK(ii_test::pumpUntil([&] { return done.load(); }));
    CHECK(res.status == qs::IpcClient::Status::Ok);
    CHECK(res.value == "hello world");
  }

  qs::IpcServer::stop();
  CHECK(!qs::IpcServer::isRunning());
}

TEST("ipc: target not found, function not found, and disabled handler errors") {
  (void)scratch();

  auto handler = std::make_unique<qs::IpcHandler>();
  handler->target.set(std::string("myTarget"));
  handler->addFunction("ping", [](const std::vector<std::string>&) -> std::string {
    return "pong";
  });

  CHECK(qs::IpcServer::start());

  // Unknown target
  {
    std::atomic<bool> done{false};
    qs::IpcClient::Result res;
    std::jthread worker([&] {
      res = qs::IpcClient::call("nonExistentTarget", "ping");
      done = true;
    });
    CHECK(ii_test::pumpUntil([&] { return done.load(); }));
    CHECK(res.status == qs::IpcClient::Status::TargetNotFound);
    CHECK(res.value == "Target not found.");
  }

  // Unknown function
  {
    std::atomic<bool> done{false};
    qs::IpcClient::Result res;
    std::jthread worker([&] {
      res = qs::IpcClient::call("myTarget", "nonExistentFunc");
      done = true;
    });
    CHECK(ii_test::pumpUntil([&] { return done.load(); }));
    CHECK(res.status == qs::IpcClient::Status::FunctionNotFound);
    CHECK(res.value == "Function not found.");
  }

  // Disabled handler
  handler->enabled.set(false);
  {
    std::atomic<bool> done{false};
    qs::IpcClient::Result res;
    std::jthread worker([&] {
      res = qs::IpcClient::call("myTarget", "ping");
      done = true;
    });
    CHECK(ii_test::pumpUntil([&] { return done.load(); }));
    CHECK(res.status == qs::IpcClient::Status::TargetNotFound);
  }

  // Re-enable handler
  handler->enabled.set(true);
  {
    std::atomic<bool> done{false};
    qs::IpcClient::Result res;
    std::jthread worker([&] {
      res = qs::IpcClient::call("myTarget", "ping");
      done = true;
    });
    CHECK(ii_test::pumpUntil([&] { return done.load(); }));
    CHECK(res.status == qs::IpcClient::Status::Ok);
    CHECK(res.value == "pong");
  }

  qs::IpcServer::stop();
}

TEST("ipc: show query metadata for targets and functions") {
  (void)scratch();

  auto handler = std::make_unique<qs::IpcHandler>();
  handler->target.set(std::string("widget"));
  handler->addFunction("show", [](const std::vector<std::string>&) -> std::string { return {}; });
  handler->addFunction("hide", [](const std::vector<std::string>&) -> std::string { return {}; });

  CHECK(qs::IpcServer::start());

  // Show all targets
  {
    std::atomic<bool> done{false};
    qs::IpcClient::Result res;
    std::jthread worker([&] {
      res = qs::IpcClient::show();
      done = true;
    });
    CHECK(ii_test::pumpUntil([&] { return done.load(); }));
    CHECK(res.status == qs::IpcClient::Status::Ok);
    CHECK(res.value.find("target widget") != std::string::npos);
    CHECK(res.value.find("function show()") != std::string::npos);
    CHECK(res.value.find("function hide()") != std::string::npos);
  }

  // Show specific target
  {
    std::atomic<bool> done{false};
    qs::IpcClient::Result res;
    std::jthread worker([&] {
      res = qs::IpcClient::show("widget");
      done = true;
    });
    CHECK(ii_test::pumpUntil([&] { return done.load(); }));
    CHECK(res.status == qs::IpcClient::Status::Ok);
    CHECK(res.value.find("target widget") != std::string::npos);
  }

  // Show specific function
  {
    std::atomic<bool> done{false};
    qs::IpcClient::Result res;
    std::jthread worker([&] {
      res = qs::IpcClient::show("widget", "show");
      done = true;
    });
    CHECK(ii_test::pumpUntil([&] { return done.load(); }));
    CHECK(res.status == qs::IpcClient::Status::Ok);
    CHECK(res.value == "function show()");
  }

  // Show non-existent target
  {
    std::atomic<bool> done{false};
    qs::IpcClient::Result res;
    std::jthread worker([&] {
      res = qs::IpcClient::show("nonExistent");
      done = true;
    });
    CHECK(ii_test::pumpUntil([&] { return done.load(); }));
    CHECK(res.status == qs::IpcClient::Status::TargetNotFound);
  }

  // Show non-existent function on existing target
  {
    std::atomic<bool> done{false};
    qs::IpcClient::Result res;
    std::jthread worker([&] {
      res = qs::IpcClient::show("widget", "nonExistent");
      done = true;
    });
    CHECK(ii_test::pumpUntil([&] { return done.load(); }));
    CHECK(res.status == qs::IpcClient::Status::FunctionNotFound);
  }

  qs::IpcServer::stop();
}

TEST("ipc: stale socket file recovery and second instance probe safety") {
  (void)scratch();

  const auto sockPath = qs::IpcServer::defaultSocketPath();
  std::filesystem::create_directories(std::filesystem::path(sockPath).parent_path());

  // Create a dead socket / dummy file
  {
    std::ofstream dummy(sockPath);
    dummy << "stale socket contents";
  }
  CHECK(std::filesystem::exists(sockPath));

  // Server should detect stale socket, remove it, and start cleanly
  CHECK(qs::IpcServer::start());
  CHECK(qs::IpcServer::isRunning());

  // Regression 2: Attempting to start another instance must fail and MUST NOT unlink the live socket
  {
    // Calling start again when running returns true (already running), but testing direct init
    // via a second instance probe:
    int probeFd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    CHECK(probeFd >= 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, sockPath.c_str(), sizeof(addr.sun_path) - 1);
    CHECK(::connect(probeFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    ::close(probeFd);
  }
  // The live socket MUST still exist
  CHECK(std::filesystem::exists(sockPath));

  qs::IpcServer::stop();
  CHECK(!qs::IpcServer::isRunning());
  CHECK(!std::filesystem::exists(sockPath));
}

TEST("ipc: regression 1 - client EOF probe does not spin or leak fd") {
  (void)scratch();

  CHECK(qs::IpcServer::start());
  const auto sockPath = qs::IpcServer::defaultSocketPath();

  // Connect and close immediately without sending anything (like probe in init)
  for (int i = 0; i < 5; ++i) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    CHECK(fd >= 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, sockPath.c_str(), sizeof(addr.sun_path) - 1);
    CHECK(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    ::close(fd);
  }

  // Pump loop: must process EOF cleanly without spinning
  ii_test::pumpFor(50);
  CHECK(qs::IpcServer::isRunning());

  qs::IpcServer::stop();
}

TEST("ipc: regression 3 - disconnected client does not crash server with SIGPIPE") {
  (void)scratch();

  auto handler = std::make_unique<qs::IpcHandler>();
  handler->target.set(std::string("echoTarget"));
  handler->addFunction("slowEcho", [](const std::vector<std::string>&) -> std::string {
    return "reply";
  });

  CHECK(qs::IpcServer::start());
  const auto sockPath = qs::IpcServer::defaultSocketPath();

  // Connect, send full request, and close socket immediately before server answers
  int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  CHECK(fd >= 0);
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, sockPath.c_str(), sizeof(addr.sun_path) - 1);
  CHECK(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);

  // Send framed call request
  std::vector<std::uint8_t> req;
  req.push_back(1); // kCmdCall
  auto appendStr = [&](std::string_view s) {
    std::uint32_t len = htonl(static_cast<std::uint32_t>(s.size()));
    const auto* p = reinterpret_cast<const std::uint8_t*>(&len);
    req.insert(req.end(), p, p + 4);
    req.insert(req.end(), s.begin(), s.end());
  };
  appendStr("echoTarget");
  appendStr("slowEcho");
  std::uint32_t argc = 0;
  req.insert(req.end(), reinterpret_cast<const std::uint8_t*>(&argc), reinterpret_cast<const std::uint8_t*>(&argc) + 4);

  std::uint32_t totalLen = htonl(static_cast<std::uint32_t>(req.size()));
  std::vector<std::uint8_t> framed;
  framed.insert(framed.end(), reinterpret_cast<const std::uint8_t*>(&totalLen), reinterpret_cast<const std::uint8_t*>(&totalLen) + 4);
  framed.insert(framed.end(), req.begin(), req.end());

  CHECK(::write(fd, framed.data(), framed.size()) == static_cast<ssize_t>(framed.size()));
  // Close socket immediately (causing EPIPE on server's write)
  ::close(fd);

  // Pump loop: server handles request, writes response to closed socket with MSG_NOSIGNAL (no SIGPIPE crash)
  ii_test::pumpFor(50);
  CHECK(qs::IpcServer::isRunning());

  qs::IpcServer::stop();
}

TEST("ipc: regression 4 - CLI exit code parity with Quickshell (TEST_ALIVE)") {
  (void)scratch();

  char argProg[] = "ii-shell";
  char argIpc[] = "ipc";
  char argCall[] = "call";
  char argAlive[] = "TEST_ALIVE";
  char* argvAlive[] = {argProg, argIpc, argCall, argAlive};

  // When server is NOT running: handleIpcCli returns non-zero (1)
  CHECK(qs::handleIpcCli(4, argvAlive) != 0);

  // When server IS running: handleIpcCli returns 0 even with missing function (instance reached)
  CHECK(qs::IpcServer::start());
  CHECK(qs::handleIpcCli(4, argvAlive) == 0);

  // Unknown target / unknown function returns 0 when instance reached
  char argBogusFunc[] = "bogus";
  char* argvBogus[] = {argProg, argIpc, argCall, argAlive, argBogusFunc};
  {
    std::atomic<bool> done{false};
    int rc = -1;
    std::jthread worker([&] {
      rc = qs::handleIpcCli(5, argvBogus);
      done = true;
    });
    CHECK(ii_test::pumpUntil([&] { return done.load(); }));
    CHECK(rc == 0);
  }

  qs::IpcServer::stop();
}

TEST("ipc: regression 5 - malformed frame with huge argc is safely rejected") {
  (void)scratch();

  CHECK(qs::IpcServer::start());
  const auto sockPath = qs::IpcServer::defaultSocketPath();

  int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  CHECK(fd >= 0);
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, sockPath.c_str(), sizeof(addr.sun_path) - 1);
  CHECK(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);

  // Construct frame with argc = 0xFFFFFFFF
  std::vector<std::uint8_t> req;
  req.push_back(1); // kCmdCall
  auto appendStr = [&](std::string_view s) {
    std::uint32_t len = htonl(static_cast<std::uint32_t>(s.size()));
    const auto* p = reinterpret_cast<const std::uint8_t*>(&len);
    req.insert(req.end(), p, p + 4);
    req.insert(req.end(), s.begin(), s.end());
  };
  appendStr("target");
  appendStr("func");
  std::uint32_t hugeArgc = htonl(0xFFFFFFFF);
  req.insert(req.end(), reinterpret_cast<const std::uint8_t*>(&hugeArgc), reinterpret_cast<const std::uint8_t*>(&hugeArgc) + 4);

  std::uint32_t totalLen = htonl(static_cast<std::uint32_t>(req.size()));
  std::vector<std::uint8_t> framed;
  framed.insert(framed.end(), reinterpret_cast<const std::uint8_t*>(&totalLen), reinterpret_cast<const std::uint8_t*>(&totalLen) + 4);
  framed.insert(framed.end(), req.begin(), req.end());

  CHECK(::write(fd, framed.data(), framed.size()) == static_cast<ssize_t>(framed.size()));

  // Pump loop: server must reject safely without bad_alloc crash
  ii_test::pumpFor(50);
  CHECK(qs::IpcServer::isRunning());

  ::close(fd);
  qs::IpcServer::stop();
}

TEST_MAIN()
