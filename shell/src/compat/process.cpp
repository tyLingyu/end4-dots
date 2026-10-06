#include "compat/process.h"

#include "core/log.h"
#include "runtime/qt.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <poll.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace ii::qs {

  namespace {

    constexpr Logger kLog("process");

    // The environment when the program started (Quickshell's INITIAL_ENVIRONMENT).
    const std::map<std::string, std::string>& initialEnvironment() {
      static const std::map<std::string, std::string> env = [] {
        std::map<std::string, std::string> out;
        for (char** e = environ; e != nullptr && *e != nullptr; ++e) {
          const std::string entry(*e);
          const auto eq = entry.find('=');
          if (eq != std::string::npos) {
            out.emplace(entry.substr(0, eq), entry.substr(eq + 1));
          }
        }
        return out;
      }();
      return env;
    }
    [[maybe_unused]] const bool kEnvironmentCaptured = (initialEnvironment(), true);

    // qs::io::process::setupProcessEnvironment.
    std::vector<std::string> buildEnvironment(bool clear, const js::Json& changes) {
      const auto& sysenv = initialEnvironment();
      std::map<std::string, std::string> env = clear ? std::map<std::string, std::string>() : sysenv;
      if (changes.is_object()) {
        for (const auto& [name, value] : changes.items()) {
          if (value.is_null()) {
            if (clear) {
              if (auto it = sysenv.find(name); it != sysenv.end()) {
                env[name] = it->second;
              }
            } else {
              env.erase(name);
            }
          } else {
            env[name] = value.is_string() ? value.get<std::string>() : js::logString(value);
          }
        }
      }
      std::vector<std::string> out;
      for (const auto& [name, value] : env) {
        out.push_back(name + "=" + value);
      }
      return out;
    }

    int pidfdOpen(pid_t pid) { return static_cast<int>(syscall(SYS_pidfd_open, pid, 0)); }

    void setNonBlocking(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK); }

    void closeFd(int& fd) {
      if (fd >= 0) {
        ::close(fd);
        fd = -1;
      }
    }

    std::vector<char*> cStrings(std::vector<std::string>& strings) {
      std::vector<char*> out;
      for (auto& s : strings) {
        out.push_back(s.data());
      }
      out.push_back(nullptr);
      return out;
    }

    // Quickshell strips file:// and resolves root:// against the shell root.
    std::string program(const std::string& first) {
      if (first.starts_with("file://")) {
        return first.substr(7);
      }
      if (first.starts_with("root://")) {
        const std::string rest = first.substr(7);
        return shellRoot() + "/" + (rest.starts_with('/') ? rest.substr(1) : rest);
      }
      return first;
    }

  } // namespace

  // ── SplitParser ────────────────────────────────────────────────────────────

  SplitParser::SplitParser() {
    splitMarker.changed().connectForever([this] { m_markerChanged = true; });
  }

  void SplitParser::parseBytes(std::string& incoming, std::string& buffer) {
    // A literal port of Quickshell's SplitParser::parseBytes, including that the byte right
    // after a marker can't start the next one (the loop also increments after a match), so
    // "a\n\nb" reads "a", then "\nb" at the end.
    const std::string marker = splitMarker.peek();
    if (marker.empty()) {
      if (!buffer.empty()) {
        const std::string pending = buffer;
        buffer.clear();
        read.emit(pending);
      }
      read.emit(std::string(incoming));
      return;
    }
    if (m_markerChanged) {
      m_markerChanged = false;
      parseBytes(buffer, buffer);
    }
    const auto mlen = static_cast<long>(marker.size());
    const auto blen = static_cast<long>(buffer.size());
    const auto ilen = static_cast<long>(incoming.size());
    long start = &incoming == &buffer ? 0 : -blen;
    for (long readi = -std::min(blen, mlen - 1); readi <= ilen - mlen; ++readi) {
      bool matched = true;
      for (long marki = 0; marki < mlen; ++marki) {
        const char byte = readi + marki < 0 ? buffer[static_cast<std::size_t>(blen + readi + marki)]
                                            : incoming[static_cast<std::size_t>(readi + marki)];
        if (byte != marker[static_cast<std::size_t>(marki)]) {
          matched = false;
          break;
        }
      }
      if (!matched) {
        continue;
      }
      std::string slice;
      if (start < 0) {
        slice = buffer.substr(0, static_cast<std::size_t>(std::min(blen, blen + readi)));
      }
      if (readi > 0) {
        const long sstart = std::max(0L, start);
        slice.append(incoming, static_cast<std::size_t>(sstart), static_cast<std::size_t>(readi - sstart));
      }
      readi += mlen;
      start = readi;
      read.emit(slice);
    }
    if (start < 0) {
      buffer.append(incoming);
    } else {
      std::string rest = incoming.substr(static_cast<std::size_t>(start));
      buffer = std::move(rest);
    }
  }

  void SplitParser::streamEnded(std::string& buffer) {
    if (!buffer.empty()) {
      read.emit(std::string(buffer));
    }
  }

  // ── StdioCollector ─────────────────────────────────────────────────────────

  void StdioCollector::parseBytes(std::string& incoming, std::string& buffer) {
    buffer.append(incoming);
    if (!waitForEnd.peek()) {
      text.writeDirect(buffer);
    }
  }

  void StdioCollector::streamEnded(std::string& buffer) {
    if (waitForEnd.peek()) {
      text.writeDirect(buffer);
    }
    streamFinished.emit();
  }

  // ── Process ────────────────────────────────────────────────────────────────

  class Process::RunningInterceptor final : public PropertyInterceptor<bool> {
  public:
    explicit RunningInterceptor(Process& process) : m_process(process) {}

    bool intercept(const bool& value) override {
      m_process.m_targetRunning = value;
      if (value) {
        m_process.startIfReady();
      } else if (m_process.m_pid != 0) {
        ::kill(m_process.m_pid, SIGTERM);  // QProcess::terminate
      }
      return true;  // the state is the process's, not the value written
    }

  private:
    Process& m_process;
  };

  Process::Process() : m_interceptor(std::make_unique<RunningInterceptor>(*this)) {
    running.setInterceptor(m_interceptor.get());
    command.changed().connectForever([this] { startIfReady(); });
    stdinEnabled.changed().connectForever([this] {
      if (!stdinEnabled.peek() && m_stdinFd >= 0) {
        FdWatch::unwatch(m_stdinWatch);
        closeFd(m_stdinFd);
      }
    });
  }

  Process::~Process() {
    running.setInterceptor(nullptr);
    if (m_pid != 0) {
      ::kill(m_pid, SIGKILL);
      int status = 0;
      ::waitpid(m_pid, &status, 0);
    }
    FdWatch::unwatch(m_pidWatch);
    FdWatch::unwatch(m_stdout.watch);
    FdWatch::unwatch(m_stderr.watch);
    FdWatch::unwatch(m_stdinWatch);
    closeFd(m_pidfd);
    closeFd(m_stdout.fd);
    closeFd(m_stderr.fd);
    closeFd(m_stdinFd);
  }

  void Process::componentComplete() { startIfReady(); }

  void Process::exec(const std::vector<std::string>& cmd) {
    running.set(false);
    command.set(cmd);
    if (cmd.empty()) {
      kLog.warn("Cannot start process as command is empty.");
      return;
    }
    running.set(true);
  }

  void Process::startIfReady() {
    if (m_pid != 0 || !isCompleted() || !m_targetRunning || command.peek().empty()) {
      return;
    }
    m_targetRunning = false;

    std::vector<std::string> args = command.peek();
    args.front() = program(args.front());
    std::vector<std::string> env = buildEnvironment(clearEnvironment.peek(), environment.peek());
    auto argv = cStrings(args);
    auto envp = cStrings(env);
    const std::string cwd = workingDirectory.peek();

    int report[2];
    int out[2] = {-1, -1};
    int err[2] = {-1, -1};
    int in[2] = {-1, -1};
    if (pipe2(report, O_CLOEXEC) != 0) {
      kLog.warn("pipe failed: {}", std::strerror(errno));
      return;
    }
    if (stdout_.peek() != nullptr) {
      (void)pipe2(out, O_CLOEXEC);
    }
    if (stderr_.peek() != nullptr) {
      (void)pipe2(err, O_CLOEXEC);
    }
    if (stdinEnabled.peek()) {
      (void)pipe2(in, O_CLOEXEC);
    }

    const pid_t pid = ::fork();
    if (pid == 0) {
      // Child: default signal handling, the pipes (or /dev/null) as stdio, then exec.
      sigset_t all;
      sigemptyset(&all);
      sigprocmask(SIG_SETMASK, &all, nullptr);
      for (int s = 1; s < NSIG; ++s) {
        ::signal(s, SIG_DFL);
      }
      const int devnull = ::open("/dev/null", O_RDWR);
      ::dup2(in[0] >= 0 ? in[0] : devnull, STDIN_FILENO);
      ::dup2(out[1] >= 0 ? out[1] : devnull, STDOUT_FILENO);
      ::dup2(err[1] >= 0 ? err[1] : devnull, STDERR_FILENO);
      if (!cwd.empty() && ::chdir(cwd.c_str()) != 0) {
        const int e = errno;
        (void)::write(report[1], &e, sizeof(e));
        _exit(127);
      }
      ::execvpe(argv[0], argv.data(), envp.data());
      const int e = errno;
      (void)::write(report[1], &e, sizeof(e));
      _exit(127);
    }

    ::close(report[1]);
    for (int* end : {&out[1], &err[1], &in[0]}) {
      closeFd(*end);
    }
    int childErrno = 0;
    const bool failed = pid < 0 || ::read(report[0], &childErrno, sizeof(childErrno)) == sizeof(childErrno);
    ::close(report[0]);
    if (failed) {
      if (pid > 0) {
        int status = 0;
        ::waitpid(pid, &status, 0);
      }
      closeFd(out[0]);
      closeFd(err[0]);
      closeFd(in[1]);
      kLog.warn("Process failed to start, likely because the binary could not be found. Command: {} ({})",
                js::join(command.peek(), " "), std::strerror(pid < 0 ? errno : childErrno));
      running.writeDirect(false);
      return;
    }

    m_pid = pid;
    m_pidfd = pidfdOpen(pid);
    m_pidWatch = FdWatch::watch(m_pidfd, POLLIN, [this](short) {
      int status = 0;
      if (::waitpid(m_pid, &status, WNOHANG) == m_pid) {
        onExited(status);
      }
    });
    m_stdout = Stream{out[0], 0, {}};
    m_stderr = Stream{err[0], 0, {}};
    for (Stream* stream : {&m_stdout, &m_stderr}) {
      if (stream->fd >= 0) {
        setNonBlocking(stream->fd);
        const bool isOut = stream == &m_stdout;
        stream->watch = FdWatch::watch(stream->fd, POLLIN, [this, isOut](short) {
          readStream(isOut ? m_stdout : m_stderr, isOut ? stdout_.peek() : stderr_.peek(), false);
        });
      }
    }
    m_stdinFd = in[1];
    if (m_stdinFd >= 0) {
      setNonBlocking(m_stdinFd);
      flushStdin();
    }

    running.writeDirect(true);
    processId.writeDirect(js::Json(pid));
    started.emit();
  }

  void Process::readStream(Stream& stream, DataStreamParser* parser, bool drain) {
    char chunk[16384];
    while (stream.fd >= 0) {
      const ssize_t n = ::read(stream.fd, chunk, sizeof(chunk));
      if (n > 0) {
        std::string incoming(chunk, static_cast<std::size_t>(n));
        if (parser != nullptr) {
          parser->parseBytes(incoming, stream.buffer);
        }
        if (!drain) {
          return;  // one chunk per wakeup, like a readyRead
        }
        continue;
      }
      if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
        return;
      }
      closeStream(stream);  // EOF or error
    }
  }

  void Process::closeStream(Stream& stream) {
    FdWatch::unwatch(stream.watch);
    stream.watch = 0;
    closeFd(stream.fd);
  }

  void Process::onExited(int status) {
    // What the child wrote before exiting is still in the pipes.
    readStream(m_stdout, stdout_.peek(), true);
    readStream(m_stderr, stderr_.peek(), true);
    closeStream(m_stdout);
    closeStream(m_stderr);
    FdWatch::unwatch(m_stdinWatch);
    closeFd(m_stdinFd);
    m_pendingStdin.clear();
    FdWatch::unwatch(m_pidWatch);
    m_pidWatch = 0;
    closeFd(m_pidfd);
    m_pid = 0;

    // Quickshell's Process::onFinished order.
    if (stdout_.peek() != nullptr) {
      stdout_.peek()->streamEnded(m_stdout.buffer);
    }
    if (stderr_.peek() != nullptr) {
      stderr_.peek()->streamEnded(m_stderr.buffer);
    }
    m_stdout.buffer.clear();
    m_stderr.buffer.clear();
    const bool crashed = WIFSIGNALED(status);
    exited.emit(crashed ? WTERMSIG(status) : WEXITSTATUS(status), crashed ? 1 : 0);
    running.writeDirect(false);
    processId.writeDirect(js::Json(nullptr));
    startIfReady();  // `running = false; running = true`
  }

  void Process::flushStdin() {
    while (m_stdinFd >= 0 && !m_pendingStdin.empty()) {
      const ssize_t n = ::write(m_stdinFd, m_pendingStdin.data(), m_pendingStdin.size());
      if (n > 0) {
        m_pendingStdin.erase(0, static_cast<std::size_t>(n));
      } else if (n < 0 && errno == EINTR) {
        continue;
      } else {
        break;
      }
    }
    const bool pending = m_stdinFd >= 0 && !m_pendingStdin.empty();
    if (pending && m_stdinWatch == 0) {
      m_stdinWatch = FdWatch::watch(m_stdinFd, POLLOUT, [this](short) { flushStdin(); });
    } else if (!pending && m_stdinWatch != 0) {
      FdWatch::unwatch(m_stdinWatch);
      m_stdinWatch = 0;
    }
  }

  void Process::write(const std::string& data) {
    if (m_pid == 0) {
      return;
    }
    m_pendingStdin += data;
    flushStdin();
  }

  void Process::signal(int sig) {
    if (m_pid != 0) {
      ::kill(m_pid, sig);
    }
  }

  void Process::startDetached() {
    if (command.peek().empty()) {
      kLog.warn("Cannot start process as command is empty.");
      return;
    }
    std::vector<std::string> args = command.peek();
    std::vector<std::string> env = buildEnvironment(clearEnvironment.peek(), environment.peek());
    auto argv = cStrings(args);
    auto envp = cStrings(env);
    const std::string cwd = workingDirectory.peek();
    const pid_t pid = ::fork();
    if (pid == 0) {
      ::setsid();
      if (::fork() == 0) {
        const int devnull = ::open("/dev/null", O_RDWR);
        ::dup2(devnull, STDIN_FILENO);
        ::dup2(devnull, STDOUT_FILENO);
        ::dup2(devnull, STDERR_FILENO);
        if (!cwd.empty()) {
          (void)::chdir(cwd.c_str());
        }
        ::execvpe(argv[0], argv.data(), envp.data());
      }
      _exit(0);
    }
    if (pid > 0) {
      int status = 0;
      ::waitpid(pid, &status, 0);
    }
  }

} // namespace ii::qs
