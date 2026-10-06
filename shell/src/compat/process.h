#pragma once

// Quickshell.Io Process, DataStreamParser, SplitParser, StdioCollector. Semantics follow
// Quickshell (src/io/process.cpp, datastream.cpp at 7511545): see the class comments.

#include "runtime/fd_watch.h"
#include "runtime/js.h"
#include "runtime/object.h"
#include "runtime/property.h"

#include <string>
#include <vector>

namespace ii::qs {

  // Receives a process's output. `buffer` holds what earlier calls left unconsumed; `incoming`
  // may be the buffer itself (a parser attached after output arrived).
  class DataStreamParser : public Object {
  public:
    virtual void parseBytes(std::string& incoming, std::string& buffer) = 0;
    virtual void streamEnded(std::string& buffer) = 0;
  };

  // Emits read() for each piece between markers; at the end, for what is left. An empty marker
  // passes every chunk through as it arrives.
  class SplitParser : public DataStreamParser {
  public:
    SplitParser();

    Property<std::string> splitMarker{"\n"};
    Signal<std::string> read;

    void parseBytes(std::string& incoming, std::string& buffer) override;
    void streamEnded(std::string& buffer) override;

  private:
    bool m_markerChanged = false;
  };

  // Collects all output. With waitForEnd (the default) `text` is set when the stream ends;
  // otherwise it follows the output as it arrives.
  class StdioCollector : public DataStreamParser {
  public:
    Property<std::string> text;  // read-only
    Property<bool> waitForEnd{true};
    Signal<> streamFinished;

    void parseBytes(std::string& incoming, std::string& buffer) override;
    void streamEnded(std::string& buffer) override;
  };

  // A child process. `running` is not a stored flag but Quickshell's: assigning true (even when
  // already true) asks for a start, which happens once the component is complete and the command
  // is set; a process still running starts again after it exits. Assigning false sends SIGTERM.
  // Reading it says whether a process exists. On exit the parsers get the end of the stream,
  // then exited(exitCode, exitStatus) (exitStatus 1 and the signal number for a killed process,
  // as QProcess reports), then running turns false.
  class Process : public Object {
  public:
    Process();
    ~Process() override;

    Property<bool> running;
    Property<std::vector<std::string>> command;
    Property<std::string> workingDirectory;
    Property<js::Json> environment;  // name -> value; null removes (or, cleared, inherits)
    Property<bool> clearEnvironment;
    Property<DataStreamParser*> stdout_;
    Property<DataStreamParser*> stderr_;
    Property<bool> stdinEnabled;
    Property<js::Json> processId;  // null when not running

    Signal<> started;
    Signal<int, int> exited;

    void exec(const std::vector<std::string>& command);
    void startDetached();
    void signal(int signal);
    void write(const std::string& data);

  protected:
    void componentComplete() override;

  private:
    class RunningInterceptor;
    struct Stream {
      int fd = -1;
      FdWatch::Id watch = 0;
      std::string buffer;
    };

    void startIfReady();
    void readStream(Stream& stream, DataStreamParser* parser, bool drain);
    void closeStream(Stream& stream);
    void onExited(int status);
    void flushStdin();

    std::unique_ptr<RunningInterceptor> m_interceptor;
    bool m_targetRunning = false;
    int m_pid = 0;
    int m_pidfd = -1;
    FdWatch::Id m_pidWatch = 0;
    Stream m_stdout;
    Stream m_stderr;
    int m_stdinFd = -1;
    FdWatch::Id m_stdinWatch = 0;
    std::string m_pendingStdin;
  };

} // namespace ii::qs
