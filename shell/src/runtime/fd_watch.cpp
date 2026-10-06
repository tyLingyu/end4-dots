#include "runtime/fd_watch.h"

#include "app/poll_source.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace ii {

  namespace {

    struct Entry {
      FdWatch::Id id;
      int fd;
      short events;
      std::shared_ptr<FdWatch::Callback> callback;  // kept alive while it runs
    };

    class Source final : public PollSource {
    public:
      std::vector<Entry> entries;
      std::vector<FdWatch::Id> polled;  // ids in the order their fds were added this iteration
      FdWatch::Id nextId = 1;

      void dispatch(const std::vector<pollfd>& fds, std::size_t startIdx) override {
        const auto ids = polled;
        for (std::size_t i = 0; i < ids.size() && startIdx + i < fds.size(); ++i) {
          const short revents = fds[startIdx + i].revents;
          if (revents == 0) {
            continue;
          }
          // Look the entry up again: an earlier callback may have removed it.
          auto it = std::ranges::find(entries, ids[i], &Entry::id);
          if (it == entries.end()) {
            continue;
          }
          const auto callback = it->callback;
          (*callback)(revents);
        }
      }

    protected:
      void doAddPollFds(std::vector<pollfd>& fds) override {
        polled.clear();
        for (const Entry& e : entries) {
          fds.push_back(pollfd{e.fd, e.events, 0});
          polled.push_back(e.id);
        }
      }
    };

    Source& source() {
      static Source instance;
      return instance;
    }

  } // namespace

  FdWatch::Id FdWatch::watch(int fd, short events, Callback callback) {
    const Id id = source().nextId++;
    source().entries.push_back({id, fd, events, std::make_shared<Callback>(std::move(callback))});
    return id;
  }

  void FdWatch::unwatch(Id id) { std::erase_if(source().entries, [id](const Entry& e) { return e.id == id; }); }

  void FdWatch::setEvents(Id id, short events) {
    auto it = std::ranges::find(source().entries, id, &Entry::id);
    if (it != source().entries.end()) {
      it->events = events;
    }
  }

  PollSource& FdWatch::pollSource() { return source(); }

} // namespace ii
