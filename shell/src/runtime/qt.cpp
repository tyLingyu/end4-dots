#include "runtime/qt.h"

#include "runtime/js.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <locale.h>

namespace ii {

  namespace {

    std::string env(const char* name) {
      const char* value = std::getenv(name);
      return value != nullptr ? value : "";
    }

    std::string home() { return env("HOME"); }

    std::string xdg(const char* name, const char* fallback) {
      const std::string value = env(name);
      return value.empty() || value.front() != '/' ? home() + "/" + fallback : value;
    }

  } // namespace

  const std::string& shellRoot() {
    static const std::string root = [] {
      const std::string fromEnv = env("II_SHELL_ROOT");
      return fromEnv.empty() ? xdg("XDG_CONFIG_HOME", ".config") + "/quickshell/ii" : fromEnv;
    }();
    return root;
  }

  DateTime DateTime::now() {
    using namespace std::chrono;
    return {duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()};
  }

  namespace js {
    double dateNow() { return static_cast<double>(DateTime::now().msecsSinceEpoch); }
  } // namespace js

} // namespace ii

namespace ii::qt {

  namespace {

    // "a/./b/../c" -> "a/c", keeping a leading "/".
    std::string normalizePath(std::string_view path) {
      std::vector<std::string_view> parts;
      std::size_t start = 0;
      while (start <= path.size()) {
        const std::size_t end = std::min(path.find('/', start), path.size());
        const std::string_view part = path.substr(start, end - start);
        if (part == "..") {
          if (!parts.empty()) {
            parts.pop_back();
          }
        } else if (!part.empty() && part != ".") {
          parts.push_back(part);
        }
        start = end + 1;
      }
      std::string out;
      for (const auto part : parts) {
        out += "/";
        out += part;
      }
      if (!path.empty() && path.back() == '/' && !out.empty()) {
        out += "/";
      }
      return out.empty() ? "/" : out;
    }

    locale_t timeLocale() {
      static const locale_t locale = [] {
        locale_t loc = newlocale(LC_TIME_MASK, "", static_cast<locale_t>(nullptr));
        return loc != static_cast<locale_t>(nullptr) ? loc : newlocale(LC_TIME_MASK, "C", static_cast<locale_t>(nullptr));
      }();
      return locale;
    }

    std::string strftimeLocal(const char* format, const std::tm& tm) {
      char buffer[128];
      const std::size_t n = strftime_l(buffer, sizeof(buffer), format, &tm, timeLocale());
      return std::string(buffer, n);
    }

    std::string pad(int value, int width) {
      std::string out = std::to_string(value);
      while (static_cast<int>(out.size()) < width) {
        out.insert(out.begin(), '0');
      }
      return out;
    }

    // Whether the format shows AM/PM outside quoted text (then h/hh are 12-hour).
    bool hasAmPm(std::string_view format) {
      bool quoted = false;
      for (const char c : format) {
        if (c == '\'') {
          quoted = !quoted;
        } else if (!quoted && (c == 'A' || c == 'a')) {
          return true;
        }
      }
      return false;
    }

  } // namespace

  std::string resolvedUrl(std::string_view url, std::string_view qmlFile) {
    if (url.find("://") != std::string_view::npos) {
      return std::string(url);
    }
    if (!url.empty() && url.front() == '/') {
      return "file://" + normalizePath(url);
    }
    const std::string base = shellRoot() + "/" + std::string(qmlFile);
    if (url.empty()) {
      return "file://" + normalizePath(base);
    }
    const std::string dir = base.substr(0, base.rfind('/') + 1);
    return "file://" + normalizePath(dir + std::string(url));
  }

  Locale locale() {
    for (const char* name : {"LC_ALL", "LC_NUMERIC", "LANG"}) {
      std::string value = env(name);
      if (value.empty()) {
        continue;
      }
      value = value.substr(0, value.find_first_of(".@"));
      return {value == "POSIX" ? "C" : value};
    }
    return {"C"};
  }

  std::string formatDateTime(const DateTime& date, std::string_view format) {
    const std::time_t seconds = static_cast<std::time_t>(date.msecsSinceEpoch / 1000);
    const int msec = static_cast<int>(((date.msecsSinceEpoch % 1000) + 1000) % 1000);
    std::tm tm{};
    localtime_r(&seconds, &tm);
    const bool twelveHour = hasAmPm(format);

    std::string out;
    std::size_t i = 0;
    while (i < format.size()) {
      const char c = format[i];
      if (c == '\'') {
        // Quoted literal text; '' is a single quote.
        if (i + 1 < format.size() && format[i + 1] == '\'') {
          out += '\'';
          i += 2;
          continue;
        }
        const std::size_t end = format.find('\'', i + 1);
        out += format.substr(i + 1, (end == std::string_view::npos ? format.size() : end) - i - 1);
        i = end == std::string_view::npos ? format.size() : end + 1;
        continue;
      }
      std::size_t repeat = 1;
      while (i + repeat < format.size() && format[i + repeat] == c) {
        ++repeat;
      }
      switch (c) {
      case 'd':
        repeat = std::min<std::size_t>(repeat, 4);
        out += repeat == 1   ? std::to_string(tm.tm_mday)
               : repeat == 2 ? pad(tm.tm_mday, 2)
               : repeat == 3 ? strftimeLocal("%a", tm)
                             : strftimeLocal("%A", tm);
        break;
      case 'M':
        repeat = std::min<std::size_t>(repeat, 4);
        out += repeat == 1   ? std::to_string(tm.tm_mon + 1)
               : repeat == 2 ? pad(tm.tm_mon + 1, 2)
               : repeat == 3 ? strftimeLocal("%b", tm)
                             : strftimeLocal("%B", tm);
        break;
      case 'y':
        repeat = std::min<std::size_t>(repeat, 4);
        if (repeat == 4) {
          out += pad(tm.tm_year + 1900, 4);
        } else if (repeat >= 2) {
          repeat = 2;
          out += pad((tm.tm_year + 1900) % 100, 2);
        } else {
          out += c;
        }
        break;
      case 'h': {
        repeat = std::min<std::size_t>(repeat, 2);
        const int hour = twelveHour ? (tm.tm_hour % 12 == 0 ? 12 : tm.tm_hour % 12) : tm.tm_hour;
        out += repeat == 1 ? std::to_string(hour) : pad(hour, 2);
        break;
      }
      case 'H':
        repeat = std::min<std::size_t>(repeat, 2);
        out += repeat == 1 ? std::to_string(tm.tm_hour) : pad(tm.tm_hour, 2);
        break;
      case 'm':
        repeat = std::min<std::size_t>(repeat, 2);
        out += repeat == 1 ? std::to_string(tm.tm_min) : pad(tm.tm_min, 2);
        break;
      case 's':
        repeat = std::min<std::size_t>(repeat, 2);
        out += repeat == 1 ? std::to_string(tm.tm_sec) : pad(tm.tm_sec, 2);
        break;
      case 'z':
        if (repeat >= 3) {
          repeat = 3;
          out += pad(msec, 3);
        } else {
          // Qt 6: milliseconds without trailing zeroes.
          repeat = 1;
          std::string ms = pad(msec, 3);
          while (ms.size() > 1 && ms.back() == '0') {
            ms.pop_back();
          }
          out += ms;
        }
        break;
      case 'A':
      case 'a': {
        const bool pm = tm.tm_hour >= 12;
        std::string text = strftimeLocal("%p", tm);
        if (text.empty()) {
          text = pm ? "PM" : "AM";
        }
        out += c == 'A' ? js::toUpperCase(text) : js::toLowerCase(text);
        repeat = (i + 1 < format.size() && (format[i + 1] == 'P' || format[i + 1] == 'p')) ? 2 : 1;
        break;
      }
      case 't':
        repeat = 1;
        out += strftimeLocal("%Z", tm);
        break;
      default:
        repeat = 1;
        out += c;
        break;
      }
      i += repeat;
    }
    return out;
  }

  namespace StandardPaths {

    namespace {
      // XDG user directory from ~/.config/user-dirs.dirs, else Qt's default name under $HOME.
      std::string userDir(const char* key, const char* fallback) {
        std::ifstream in(xdg("XDG_CONFIG_HOME", ".config") + "/user-dirs.dirs");
        std::string line;
        const std::string prefix = std::string(key) + "=\"";
        while (std::getline(in, line)) {
          if (line.rfind(prefix, 0) != 0 || line.back() != '"') {
            continue;
          }
          std::string value = line.substr(prefix.size(), line.size() - prefix.size() - 1);
          if (value.rfind("$HOME", 0) == 0) {
            value = home() + value.substr(5);
          }
          if (!value.empty() && value.front() == '/') {
            return value;
          }
        }
        return home() + "/" + fallback;
      }
    } // namespace

    std::vector<std::string> standardLocations(StandardLocation location) {
      std::string path;
      switch (location) {
      case HomeLocation: path = home(); break;
      case ConfigLocation: path = xdg("XDG_CONFIG_HOME", ".config"); break;
      case StateLocation: path = xdg("XDG_STATE_HOME", ".local/state") + "/quickshell"; break;
      case CacheLocation: path = xdg("XDG_CACHE_HOME", ".cache") + "/quickshell"; break;
      case GenericCacheLocation: path = xdg("XDG_CACHE_HOME", ".cache"); break;
      case DocumentsLocation: path = userDir("XDG_DOCUMENTS_DIR", "Documents"); break;
      case DownloadLocation: path = userDir("XDG_DOWNLOAD_DIR", "Downloads"); break;
      case PicturesLocation: path = userDir("XDG_PICTURES_DIR", "Pictures"); break;
      case MusicLocation: path = userDir("XDG_MUSIC_DIR", "Music"); break;
      case MoviesLocation: path = userDir("XDG_VIDEOS_DIR", "Videos"); break;
      }
      while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
      }
      return {"file://" + path};
    }

  } // namespace StandardPaths

} // namespace ii::qt
