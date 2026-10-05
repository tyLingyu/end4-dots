"""How QML and Qt types map onto the ii-shell runtime (shell/src/runtime) and generated code."""
from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

from .registry import SHELL_ROOT, BuiltinRef, ComponentRef

DATA = Path(__file__).resolve().parent.parent / "data"

# QML property type names.
QML_BASIC = {
    "real": "double",
    "double": "double",
    "int": "int",
    "bool": "bool",
    "string": "std::string",
    "color": "Color",
    "url": "std::string",
    "point": "Point",
    "size": "Size",
    "rect": "Rect",
    "date": "DateTime",
}

# C++ types in .qmltypes.
QT_CPP = {
    "double": "double",
    "float": "double",
    "qreal": "double",
    "int": "int",
    "uint": "int",
    "qsizetype": "int",
    "bool": "bool",
    "QString": "std::string",
    "QUrl": "std::string",
    "QColor": "Color",
    "QVariant": "nlohmann::json",
    "QJSValue": "nlohmann::json",
    "QStringList": "std::vector<std::string>",
    "QPointF": "Point",
    "QSizeF": "Size",
    "QRectF": "Rect",
    "QDateTime": "DateTime",
}


@dataclass(frozen=True)
class Runtime:
    """A built-in QML type as implemented by the runtime."""

    cpp: str  # class name in namespace ii
    header: str  # include path
    visual: bool  # an Item: children are added with add<T>() and become visual children


# Built-in QML types -> runtime classes. Types not implemented yet are listed with their planned
# header: generated code then fails to compile at exactly the pieces stage 3 has to provide.
RUNTIME: dict[str, Runtime] = {
    "QtQuick/Item": Runtime("Item", "runtime/item.h", True),
    "QtQuick/Rectangle": Runtime("Rectangle", "runtime/rectangle.h", True),
    "QtQuick/Text": Runtime("Text", "runtime/text.h", True),
    "QtQuick/Row": Runtime("Row", "runtime/positioner.h", True),
    "QtQuick/Column": Runtime("Column", "runtime/positioner.h", True),
    "QtQuick.Layouts/RowLayout": Runtime("RowLayout", "runtime/layout.h", True),
    "QtQuick.Layouts/ColumnLayout": Runtime("ColumnLayout", "runtime/layout.h", True),
    "QtQuick/Behavior": Runtime("Behavior", "runtime/animation.h", False),
    "QtQuick/NumberAnimation": Runtime("NumberAnimation", "runtime/animation.h", False),
    "QtQuick/ColorAnimation": Runtime("ColorAnimation", "runtime/animation.h", False),
    "QtQuick/RotationAnimation": Runtime("RotationAnimation", "runtime/animation.h", False),
    "QtQuick/SmoothedAnimation": Runtime("SmoothedAnimation", "runtime/animation.h", False),
    "QtQuick/SequentialAnimation": Runtime("SequentialAnimation", "runtime/animation.h", False),
    "QtQuick/ParallelAnimation": Runtime("ParallelAnimation", "runtime/animation.h", False),
    "QtQuick/PauseAnimation": Runtime("PauseAnimation", "runtime/animation.h", False),
    "QtQuick/PropertyAction": Runtime("PropertyAction", "runtime/animation.h", False),
    "QtQuick/ScriptAction": Runtime("ScriptAction", "runtime/animation.h", False),
    "QML/QtObject": Runtime("Object", "runtime/object.h", False),
    "QtQml/QtObject": Runtime("Object", "runtime/object.h", False),
    # Planned (stage 3 and later).
    "QtQml/Timer": Runtime("Timer", "runtime/timer.h", False),
    "QtQml/Connections": Runtime("Connections", "runtime/connections.h", False),
    "QtQuick/Loader": Runtime("Loader", "runtime/loader.h", True),
    "QtQuick/MouseArea": Runtime("MouseArea", "runtime/mouse_area.h", True),
    "QtQuick/Canvas": Runtime("Canvas", "runtime/canvas.h", True),
    "QtQuick/FrameAnimation": Runtime("FrameAnimation", "runtime/animation.h", False),
    "QtQuick.Effects/RectangularShadow": Runtime("RectangularShadow", "runtime/effects.h", True),
    "QtQuick.Controls/ProgressBar": Runtime("ProgressBar", "runtime/controls.h", True),
    "Quickshell/Scope": Runtime("Scope", "compat/scope.h", False),
    "Quickshell/Singleton": Runtime("Singleton", "compat/scope.h", False),
    "Quickshell/PanelWindow": Runtime("PanelWindow", "compat/panel_window.h", False),
    "Quickshell/Region": Runtime("Region", "compat/panel_window.h", False),
    "Quickshell/Variants": Runtime("Variants", "compat/variants.h", False),
    "Quickshell/SystemClock": Runtime("SystemClock", "compat/system_clock.h", False),
    "Quickshell/ColorQuantizer": Runtime("ColorQuantizer", "compat/color_quantizer.h", False),
    "Quickshell.Io/Process": Runtime("Process", "compat/process.h", False),
    "Quickshell.Io/StdioCollector": Runtime("StdioCollector", "compat/process.h", False),
    "Quickshell.Io/SplitParser": Runtime("SplitParser", "compat/process.h", False),
    "Quickshell.Io/FileView": Runtime("FileView", "compat/file_view.h", False),
    "Quickshell.Io/JsonAdapter": Runtime("JsonAdapter", "compat/file_view.h", False),
    "Quickshell.Io/JsonObject": Runtime("JsonObject", "compat/file_view.h", False),
    "Quickshell.Io/IpcHandler": Runtime("IpcHandler", "compat/ipc.h", False),
    "Quickshell.Hyprland/GlobalShortcut": Runtime("GlobalShortcut", "compat/hyprland.h", False),
    "Quickshell.Services.Pipewire/PwObjectTracker": Runtime("PwObjectTracker", "compat/pipewire.h", False),
    "Quickshell.Services.Notifications/NotificationServer": Runtime(
        "NotificationServer", "compat/notifications.h", False
    ),
}


def runtime_for(ref: BuiltinRef) -> Runtime | None:
    return RUNTIME.get(f"{ref.module}/{ref.name}") or RUNTIME.get(f"QtQuick/{ref.name}")


# ── Generated components ─────────────────────────────────────────────────────


def namespace_parts(ref: ComponentRef) -> list[str]:
    """Directory of a component -> C++ namespace below `ii`: modules/ii/bar -> ["bar"]."""
    parts = list(ref.path.parent.relative_to(SHELL_ROOT).parts)
    if parts[:2] == ["modules", "ii"]:
        parts = parts[2:]
    elif parts[:1] == ["modules"]:
        parts = parts[1:]
    return parts


def qualified_class(ref: ComponentRef) -> str:
    parts = ["ii", *namespace_parts(ref), ref.path.stem]
    if ref.inline:
        parts.append(ref.inline)
    return "::".join(parts)


def generated_header(ref: ComponentRef) -> str:
    """Include path of a component's generated header, mirroring the QML tree under ii/."""
    rel = ref.path.relative_to(SHELL_ROOT).with_suffix(".h")
    return f"ii/{rel}"


# ── `property var` types (tools/qml2cpp/data/var-types.json) ────────────────


class VarTypes:
    def __init__(self, path: Path = DATA / "var-types.json"):
        data = json.loads(path.read_text())
        self.structs: dict[str, dict[str, str]] = data["structs"]
        self.by_location: dict[tuple[str, str], str] = {}
        for p in data["properties"]:
            self.by_location[(p["file"], p["name"])] = p["cpp_type"]

    def lookup(self, ref: ComponentRef, name: str) -> str | None:
        rel = str(ref.path.relative_to(SHELL_ROOT))
        return self.by_location.get((rel, name))
