"""How QML and Qt types map onto the ii-shell runtime (shell/src/runtime) and generated code."""
from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

from .registry import SHELL_ROOT, BuiltinRef, ComponentRef

DATA = Path(__file__).resolve().parent.parent / "data"

# Names that can't be C++ members: keywords, and the <cstdio>/<cerrno> macros.
CPP_RESERVED = set("""
alignas alignof and and_eq asm auto bitand bitor bool break case catch char char8_t char16_t char32_t class
compl concept const consteval constexpr constinit const_cast continue co_await co_return co_yield decltype
default delete do double dynamic_cast else enum explicit export extern false float for friend goto if inline
int long mutable namespace new noexcept not not_eq nullptr operator or or_eq private protected public register
reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch template
this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t
while xor xor_eq stdin stdout stderr errno assert NULL EOF
""".split())


def cpp_name(name: str) -> str:
    """A QML member name as a C++ member: reserved names get a `_` suffix (`stdout_`)."""
    return f"{name}_" if name in CPP_RESERVED else name


# Untyped values: JSON whose objects keep insertion order, as JS objects do (runtime/js.h).
JSON = "js::Json"

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
    "QVariant": JSON,
    "QJSValue": JSON,
    "QStringList": "std::vector<std::string>",
    "QVariantMap": JSON,
    # Quickshell's ProcessContext; ii always passes the command as a list.
    "qs::io::process::ProcessContext": "std::vector<std::string>",
    "QVariantList": f"std::vector<{JSON}>",
    "QPointF": "Point",
    "QSizeF": "Size",
    "QRectF": "Rect",
    "QDateTime": "DateTime",
    "QRect": "Rect",
    "QVector2D": "Point",
    "QProcess::ExitStatus": "int",
    "void": "void",
    # Enums (Text.AlignHCenter etc.) as the runtime stores them.
    "HAlignment": "int",
    "VAlignment": "int",
    "WrapMode": "WrapMode",
    "TextElideMode": "Elide",
}


@dataclass(frozen=True)
class Runtime:
    """A built-in QML type as implemented by the runtime."""

    cpp: str  # class name in namespace ii
    header: str  # include path
    visual: bool  # an Item: children are added with add<T>() and become visual children
    content: str | None = None  # a window: visual children go into this item ("contentItem()")


# Built-in QML types -> runtime classes (Quickshell's in namespace ii::qs, as var-types.json
# writes them). Types not implemented yet are listed with their planned
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
    "QtQuick/MouseEvent": Runtime("MouseEvent", "runtime/mouse_area.h", False),
    "QtQuick/WheelEvent": Runtime("WheelEvent", "runtime/mouse_area.h", False),
    "QtQuick/Canvas": Runtime("Canvas", "runtime/canvas.h", True),
    "QtQuick/FrameAnimation": Runtime("FrameAnimation", "runtime/animation.h", False),
    "QtQuick.Effects/RectangularShadow": Runtime("RectangularShadow", "runtime/effects.h", True),
    "QtQuick.Controls/ProgressBar": Runtime("ProgressBar", "runtime/controls.h", True),
    "Quickshell/Scope": Runtime("qs::Scope", "compat/scope.h", False),
    "Quickshell/Singleton": Runtime("qs::Singleton", "compat/scope.h", False),
    "Quickshell/PanelWindow": Runtime("qs::PanelWindow", "compat/panel_window.h", False, "contentItem()"),
    "Quickshell/Region": Runtime("qs::Region", "compat/panel_window.h", False),
    "Quickshell/ShellScreen": Runtime("qs::ShellScreen", "compat/screen.h", False),
    "Quickshell/Quickshell": Runtime("qs::Quickshell", "compat/quickshell.h", False),
    "Quickshell.Hyprland/Hyprland": Runtime("qs::Hyprland", "compat/hyprland.h", False),
    "Quickshell.Hyprland/HyprlandMonitor": Runtime("qs::HyprlandMonitor", "compat/hyprland.h", False),
    "Quickshell.Hyprland/HyprlandWorkspace": Runtime("qs::HyprlandWorkspace", "compat/hyprland.h", False),
    "Quickshell.Hyprland/HyprlandEvent": Runtime("qs::HyprlandEvent", "compat/hyprland.h", False),
    "Quickshell.Hyprland/HyprlandToplevel": Runtime("qs::HyprlandToplevel", "compat/hyprland.h", False),
    "Quickshell.Services.Pipewire/Pipewire": Runtime("qs::Pipewire", "compat/pipewire.h", False),
    "Quickshell.Services.Pipewire/PwNode": Runtime("qs::PwNode", "compat/pipewire.h", False),
    "Quickshell.Services.Pipewire/PwNodeAudio": Runtime("qs::PwNodeAudio", "compat/pipewire.h", False),
    "Quickshell.Io/DataStreamParser": Runtime("qs::DataStreamParser", "compat/process.h", False),
    "Quickshell/Variants": Runtime("qs::Variants", "compat/variants.h", False),
    "Quickshell/SystemClock": Runtime("qs::SystemClock", "compat/system_clock.h", False),
    "Quickshell/ColorQuantizer": Runtime("qs::ColorQuantizer", "compat/color_quantizer.h", False),
    "Quickshell.Io/Process": Runtime("qs::Process", "compat/process.h", False),
    "Quickshell.Io/StdioCollector": Runtime("qs::StdioCollector", "compat/process.h", False),
    "Quickshell.Io/SplitParser": Runtime("qs::SplitParser", "compat/process.h", False),
    "Quickshell.Io/FileView": Runtime("qs::FileView", "compat/file_view.h", False),
    "Quickshell.Io/JsonAdapter": Runtime("qs::JsonAdapter", "compat/file_view.h", False),
    "Quickshell.Io/JsonObject": Runtime("qs::JsonObject", "compat/file_view.h", False),
    "Quickshell.Io/IpcHandler": Runtime("qs::IpcHandler", "compat/ipc.h", False),
    "Quickshell.Hyprland/GlobalShortcut": Runtime("qs::GlobalShortcut", "compat/hyprland.h", False),
    "Quickshell.Services.Pipewire/PwObjectTracker": Runtime("qs::PwObjectTracker", "compat/pipewire.h", False),
    "Quickshell/ObjectModel": Runtime("qs::UntypedObjectModel", "compat/object_model.h", False),
    "Quickshell.Services.Notifications/Notification": Runtime("qs::Notification", "compat/notifications.h", False),
    "Quickshell.Services.Notifications/NotificationAction": Runtime("qs::NotificationAction", "compat/notifications.h", False),
    "Quickshell.Services.Notifications/NotificationServer": Runtime(
        "qs::NotificationServer", "compat/notifications.h", False
    ),
}


def runtime_for(ref: BuiltinRef) -> Runtime | None:
    # Quickshell exports from internal submodules (Quickshell.Hyprland._Ipc) the public one re-exports.
    module = ref.module.split("._")[0]
    return RUNTIME.get(f"{module}/{ref.name}") or RUNTIME.get(f"QtQuick/{ref.name}")


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
    """ii::<namespace>::File; an inline component is File_Inline at namespace scope (not a nested
    class), so other headers can forward-declare it."""
    name = f"{ref.path.stem}_{ref.inline}" if ref.inline else ref.path.stem
    return "::".join(["ii", *namespace_parts(ref), name])


def component_for_class(qualified: str) -> ComponentRef | None:
    """The component a generated class name belongs to: ii::services::Notifications_Notif ->
    services/Notifications.qml (inline Notif). Nested classes resolve to their outer class."""
    parts = qualified.strip(":").split("::")
    if parts[:1] != ["ii"]:
        return None
    for end in range(len(parts), 1, -1):
        *ns, cls = parts[1:end]
        stem, _, inline = cls.partition("_")
        for base in (SHELL_ROOT / "modules" / "ii", SHELL_ROOT / "modules", SHELL_ROOT):
            path = base.joinpath(*ns) / f"{stem}.qml"
            if path.exists() and namespace_parts(ComponentRef(path.resolve())) == ns:
                return ComponentRef(path.resolve(), inline or None)
    return None


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
        cpp = self.by_location.get((rel, name))
        return cpp.replace("nlohmann::json", JSON) if cpp else None
