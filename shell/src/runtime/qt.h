#pragma once

// Qt.* functions for generated code, in namespace ii::qt. Colours (Qt.rgba, Qt.hsla, the HSV/HSL
// accessors) live in runtime/color.h.

#include "runtime/color.h"
#include "runtime/datetime.h"
#include "runtime/geometry.h"

#include <string>
#include <string_view>
#include <vector>

namespace ii {

  // Root of the QML shell ii-shell was translated from (Quickshell's shellPath()): $II_SHELL_ROOT,
  // else ~/.config/quickshell/ii. Assets, scripts and translations are read from there.
  [[nodiscard]] const std::string& shellRoot();

} // namespace ii

namespace ii::qt {

  // Qt.vector2d(x, y)
  [[nodiscard]] inline Point vector2d(double x, double y) { return {x, y}; }

  // Qt.resolvedUrl(url) in the QML file `qmlFile` (relative to the shell root): a relative URL
  // resolves against that file's URL; absolute paths and URLs with a scheme become file:// URLs or
  // stay as they are.
  [[nodiscard]] std::string resolvedUrl(std::string_view url, std::string_view qmlFile);

  // Qt.locale(): the system locale (LC_ALL, else LC_NUMERIC, else LANG, as QLocale::system()).
  struct Locale {
    std::string code;  // "en_US"
    [[nodiscard]] const std::string& name() const noexcept { return code; }
  };
  [[nodiscard]] Locale locale();

  // Qt.locale().toString(date, format) / Qt.formatDateTime: Qt's date format (d dd ddd dddd M MM
  // MMM MMMM yy yyyy h hh H HH m mm s ss z zzz AP ap t, 'quoted text') in local time. Day and month
  // names come from the LC_TIME locale.
  [[nodiscard]] std::string formatDateTime(const DateTime& date, std::string_view format);

  // StandardPaths.standardLocations(type) of Qt.labs.platform: file:// URLs, no trailing slash.
  // Application locations use Quickshell's name, so ii-shell finds the same state and caches.
  namespace StandardPaths {
    enum StandardLocation {
      HomeLocation,
      ConfigLocation,
      StateLocation,
      CacheLocation,
      GenericCacheLocation,
      DocumentsLocation,
      DownloadLocation,
      PicturesLocation,
      MusicLocation,
      MoviesLocation,
    };
    [[nodiscard]] std::vector<std::string> standardLocations(StandardLocation location);
  } // namespace StandardPaths

} // namespace ii::qt
