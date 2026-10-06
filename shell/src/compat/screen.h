#pragma once

// Quickshell ShellScreen (QuickshellScreenInfo): an output, as Quickshell.screens lists them.

#include "runtime/object.h"
#include "runtime/property.h"

#include <string>

namespace ii::qs {

  class ShellScreen : public Object {
  public:
    Property<std::string> name;
    Property<std::string> model;
    Property<std::string> serialNumber;
    Property<int> x;
    Property<int> y;
    Property<int> width;
    Property<int> height;
    Property<double> physicalPixelDensity;
    Property<double> logicalPixelDensity;
    Property<double> devicePixelRatio{1.0};
    Property<int> orientation;
    Property<int> primaryOrientation;

    // QML's String(screen): "ShellScreen(<name>)", as QuickshellScreenInfo::toString.
    [[nodiscard]] std::string toString() const { return "ShellScreen(" + name.peek() + ")"; }
  };

} // namespace ii::qs
