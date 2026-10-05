"""Built-in QML types from the .qmltypes files Qt and Quickshell install.

A .qmltypes file is a small QML-like document: `Module { Component { name: "..."; Property {...} } }`.
From it we get, per C++ class: the QML names it is exported as (per module), its prototype
(base class), and its properties, signals, methods, enums and attached type.
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

QML_ROOT = Path("/usr/lib/qt6/qml")

_TOKEN = re.compile(r'\s*(?://[^\n]*\n\s*)*(?:("(?:[^"\\]|\\.)*")|([\[\]{}:;,])|([A-Za-z_][\w.]*)|(-?[\d.]+))')


def _tokens(text: str):
    pos = 0
    while pos < len(text):
        m = _TOKEN.match(text, pos)
        if not m or m.end() == pos:
            if text[pos:].strip() == "":
                return
            raise ValueError(f"qmltypes: unexpected {text[pos:pos + 30]!r}")
        pos = m.end()
        string, punct, ident, number = m.groups()
        if string is not None:
            yield ("str", bytes(string[1:-1], "utf-8").decode("unicode_escape"))
        elif punct is not None:
            yield ("punct", punct)
        elif ident is not None:
            yield ("ident", ident)
        elif number is not None:
            yield ("num", float(number) if "." in number else int(number))


@dataclass
class Node:
    kind: str
    fields: dict = field(default_factory=dict)
    children: list["Node"] = field(default_factory=list)


def _parse(tokens: list, i: int) -> tuple[Node, int]:
    kind = tokens[i][1]
    assert tokens[i + 1] == ("punct", "{"), tokens[i : i + 3]
    node = Node(kind)
    i += 2
    while tokens[i] != ("punct", "}"):
        name = tokens[i][1]
        if tokens[i + 1] == ("punct", "{"):
            child, i = _parse(tokens, i)
            node.children.append(child)
            continue
        assert tokens[i + 1] == ("punct", ":"), tokens[i : i + 3]
        i += 2
        if tokens[i] == ("punct", "["):
            values = []
            i += 1
            while tokens[i] != ("punct", "]"):
                if tokens[i][0] != "punct":
                    values.append(tokens[i][1])
                i += 1
            value = values
            i += 1
        else:
            value = tokens[i][1]
            i += 1
        node.fields[name] = value
        if tokens[i] == ("punct", ";"):
            i += 1
    return node, i + 1


@dataclass
class TypeProperty:
    name: str
    cpp_type: str  # "double", "QString", "QQuickItem", "QQmlListProperty<QObject>", ...
    is_pointer: bool
    is_list: bool
    is_readonly: bool


@dataclass
class TypeInfo:
    cpp_name: str
    prototype: str | None
    exports: dict[str, str]  # module -> exported QML name
    properties: dict[str, TypeProperty]
    signals: dict[str, list[tuple[str, str]]]
    methods: dict[str, list[tuple[str, str]]]
    enums: dict[str, list[str]]
    attached_type: str | None
    is_singleton: bool
    default_property: str | None
    # Every overload of each method: (parameters, return type); "void" when none is given.
    overloads: dict[str, list[tuple[list[tuple[str, str]], str]]] = field(default_factory=dict)
    qml_file: Path | None = None  # a type implemented in QML (qmldir `Name ver File.qml`)


class BuiltinTypes:
    """Every built-in type, by C++ name and by (module, QML name)."""

    def __init__(self, root: Path = QML_ROOT):
        self.root = root
        self.by_cpp: dict[str, TypeInfo] = {}
        self.by_module: dict[str, dict[str, str]] = {}
        files = sorted(root.rglob("*.qmltypes"))
        for path in files:
            self._load(path)
        for qmldir in sorted(root.rglob("qmldir")):
            self._load_composites(qmldir)

    def _load_composites(self, qmldir: Path) -> None:
        """Types a module implements in QML (`FileView 0.0 FileView.qml`): no C++ class, no qmltypes."""
        module = None
        for line in qmldir.read_text().splitlines():
            words = line.split()
            if words[:1] == ["module"] and len(words) > 1:
                module = words[1]
            elif module and words and words[-1].endswith(".qml") and words[0] not in ("internal",):
                name = words[1] if words[0] == "singleton" else words[0]
                if name in self.by_module.get(module, {}):
                    continue
                cpp = f"{module}/{name}.qml"
                # Qt Quick Controls styles implement each control in QML on top of the C++ template type.
                template = self.by_module.get("QtQuick.Templates", {}).get(name) if module.startswith("QtQuick.Controls") else None
                self.by_cpp[cpp] = TypeInfo(
                    cpp_name=cpp, prototype=template, exports={module: name}, properties={}, signals={}, methods={},
                    enums={}, attached_type=None, is_singleton=words[0] == "singleton", default_property=None,
                    qml_file=None if template else qmldir.parent / words[-1],
                )
                self.by_module.setdefault(module, {})[name] = cpp

    def _load(self, path: Path) -> None:
        tokens = list(_tokens(path.read_text()))
        # Skip the leading `import QtQuick.tooling 1.2`.
        start = next(i for i, t in enumerate(tokens) if t == ("ident", "Module"))
        module, _ = _parse(tokens, start)
        for c in module.children:
            if c.kind != "Component":
                continue
            exports: dict[str, str] = {}
            for e in c.fields.get("exports", []):
                qml, _, _version = e.partition(" ")
                mod, _, name = qml.rpartition("/")
                exports[mod] = name
            info = TypeInfo(
                cpp_name=c.fields["name"],
                prototype=c.fields.get("prototype"),
                exports=exports,
                properties={},
                signals={},
                methods={},
                enums={},
                attached_type=c.fields.get("attachedType"),
                is_singleton=bool(c.fields.get("isSingleton", False)),
                default_property=c.fields.get("defaultProperty"),
            )
            for member in c.children:
                f = member.fields
                if member.kind == "Property":
                    info.properties[f["name"]] = TypeProperty(
                        name=f["name"],
                        cpp_type=f.get("type", ""),
                        is_pointer=bool(f.get("isPointer", False)),
                        is_list=bool(f.get("isList", False)),
                        is_readonly=bool(f.get("isReadonly", False)),
                    )
                elif member.kind in ("Signal", "Method"):
                    params = [(p.fields.get("name", ""), p.fields.get("type", "")) for p in member.children]
                    (info.signals if member.kind == "Signal" else info.methods)[f["name"]] = params
                    if member.kind == "Method":
                        info.overloads.setdefault(f["name"], []).append((params, f.get("type", "void")))
                elif member.kind == "Enum":
                    info.enums[f["name"]] = list(f.get("values", []))
            self.by_cpp[info.cpp_name] = info
            for mod, name in exports.items():
                self.by_module.setdefault(mod, {})[name] = info.cpp_name

    def reexports(self, module: str) -> list[str]:
        """Modules whose types `import module` also brings in (qmldir `import` lines), transitively."""
        out: list[str] = []
        pending = [module]
        while pending:
            current = pending.pop()
            if current in out:
                continue
            out.append(current)
            qmldir = self.root / current.replace(".", "/") / "qmldir"
            if qmldir.exists():
                for line in qmldir.read_text().splitlines():
                    words = line.split()
                    if "import" in words[:3]:
                        pending.append(words[words.index("import") + 1])
        return out

    def lookup(self, module: str, name: str) -> TypeInfo | None:
        for mod in self.reexports(module):
            cpp = self.by_module.get(mod, {}).get(name)
            if cpp:
                return self.by_cpp.get(cpp)
        return None

    def chain(self, info: TypeInfo):
        """The type and its prototypes, most derived first."""
        seen = set()
        while info is not None and info.cpp_name not in seen:
            seen.add(info.cpp_name)
            self._read_composite(info)
            yield info
            info = self.by_cpp.get(info.prototype) if info.prototype else None

    # QML property types -> the C++ names .qmltypes uses, for types implemented in QML.
    _QML_TO_QT = {"bool": "bool", "int": "int", "real": "double", "double": "double", "string": "QString",
                  "url": "QUrl", "color": "QColor", "var": "QVariant", "": "QVariant"}

    def _read_composite(self, info: TypeInfo) -> None:
        """A type implemented in QML (Quickshell's FileView over the C++ FileViewInternal): its
        prototype, properties, signals and typed functions, read from the file itself."""
        if info.qml_file is None:
            return
        from . import dom

        path, info.qml_file = info.qml_file, None
        qml = dom.load(path)
        root = qml.component.root
        module = next(iter(info.exports))
        for uri in [module, *(i.uri for i in qml.imports if not i.is_directory)]:
            base = self.lookup(uri, root.type_name)
            if base is not None:
                info.prototype = base.cpp_name
                break
        for p in root.properties:
            qt = self._QML_TO_QT.get(p.type_name)
            if qt is not None:
                info.properties[p.name] = TypeProperty(p.name, qt, False, p.is_list, p.is_readonly)
        for m in root.methods:
            params = [(n, self._QML_TO_QT.get(t, "")) for n, t in m.parameters]
            if m.kind == "signal":
                info.signals[m.name] = params
            elif m.returns is not None and all(t for _, t in params):
                info.overloads.setdefault(m.name, []).append((params, self._QML_TO_QT.get(m.returns, "")))

    def property(self, info: TypeInfo, name: str) -> TypeProperty | None:
        for t in self.chain(info):
            if name in t.properties:
                return t.properties[name]
        return None
