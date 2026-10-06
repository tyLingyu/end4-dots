"""Load QML files through Qt's own parser (qmldom) and simplify its JSON into a small model.

qmldom (shipped with Qt 6) dumps the full code model, including a JavaScript AST for every
expression. Parsing with Qt itself means every construct ii uses is understood exactly as
quickshell understands it. Dumps are cached by file content hash.
"""
from __future__ import annotations

import hashlib
import re
import json
import subprocess
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from . import jsast

QMLDOM = "/usr/lib/qt6/bin/qmldom"
CACHE_DIR = Path(__file__).resolve().parent.parent / ".cache"


# ── Model ─────────────────────────────────────────────────────────────────────


@dataclass
class Script:
    """A JavaScript expression or statement block, with Qt's AST for it."""

    code: str
    ast: jsast.Node | None  # QQmlJS AST, from qmldom's astRelocatableDump
    line: int


@dataclass
class PropertyDef:
    name: str
    type_name: str  # "real", "string", "var", "alias", "list<string>", a component name, ...
    is_list: bool = False
    is_readonly: bool = False
    is_required: bool = False
    is_alias: bool = False
    is_default: bool = False


@dataclass
class Method:
    name: str
    kind: str  # "function" or "signal"
    parameters: list[tuple[str, str]]  # (name, type name; "" when untyped)
    body: Script | None
    returns: str | None = None  # `function f(): string`: the annotated return type
    rest: bool = False  # the last parameter is `...name` (qmldom does not say; read from the source)


@dataclass
class BindingValue:
    """The right-hand side of `name: ...`: an expression, an object, or a list of objects."""

    script: Script | None = None
    obj: QmlObject | None = None
    objects: list[QmlObject] = field(default_factory=list)


@dataclass
class Binding:
    name: str  # may be dotted: "anchors.fill", "Layout.fillWidth"
    value: BindingValue
    is_signal_handler: bool = False
    # `Behavior on x` / `NumberAnimation on x`: an object installed *on* the property.
    is_on: bool = False


@dataclass
class QmlObject:
    type_name: str  # "Rectangle", "Item", or a lowercase grouped-property name ("anchors")
    id: str | None
    properties: list[PropertyDef]
    bindings: list[Binding]
    methods: list[Method]
    children: list[QmlObject]  # objects in the default property

    @property
    def is_group(self) -> bool:
        """`anchors { ... }`, `font { ... }`: a grouped property block, not a new object."""
        return self.type_name[:1].islower()


@dataclass
class Enum:
    name: str
    values: list[tuple[str, int]]


@dataclass
class Component:
    name: str
    root: QmlObject
    enums: list[Enum]
    is_inline: bool = False


@dataclass
class Import:
    uri: str  # "QtQuick", "qs.services", or a directory path for implicit/relative imports
    alias: str | None
    is_directory: bool


@dataclass
class QmlFile:
    path: Path
    imports: list[Import]
    is_singleton: bool
    component: Component
    inline_components: dict[str, Component]


# ── qmldom ────────────────────────────────────────────────────────────────────


def _dump(path: Path) -> dict:
    data = path.read_bytes()
    key = hashlib.sha256(data + QMLDOM.encode()).hexdigest()[:24]
    cached = CACHE_DIR / f"{path.stem}-{key}.json"
    if cached.exists():
        return json.loads(cached.read_text())
    out = subprocess.run([QMLDOM, "-d", "-D", "none", str(path)], capture_output=True, text=True, check=True).stdout
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    cached.write_text(out)
    return json.loads(out)


def _script(node: dict | None) -> Script | None:
    if not node or node.get("~type~") != "ScriptExpression":
        return None
    return Script(
        code=node.get("code", ""),
        ast=jsast.parse(node.get("astRelocatableDump", "")),
        line=node.get("localOffset", {}).get("startLine", 0),
    )


def _object(node: dict) -> QmlObject:
    properties = [
        PropertyDef(
            name=p["name"],
            type_name=p.get("typeName", ""),
            is_list=p.get("isList", False),
            is_readonly=p.get("isReadonly", False),
            is_required=p.get("isRequired", False),
            is_alias=p.get("isAlias", False),
            is_default=p.get("isDefaultMember", False),
        )
        for defs in node.get("propertyDefs", {}).values()
        for p in defs
    ]
    bindings = []
    for entries in node.get("bindings", {}).values():
        for b in entries:
            value = b.get("value")
            bv = BindingValue()
            if isinstance(value, dict) and value.get("~type~") == "QmlObject":
                bv.obj = _object(value)
            elif isinstance(value, list):
                bv.objects = [_object(v) for v in value if isinstance(v, dict) and v.get("~type~") == "QmlObject"]
            else:
                bv.script = _script(value)
            bindings.append(
                Binding(
                    name=b["name"],
                    value=bv,
                    is_signal_handler=b.get("isSignalHandler", False),
                    is_on=bv.obj is not None and b.get("bindingType", 0) == 1,
                )
            )
    methods = [
        Method(
            name=m["name"],
            kind="signal" if m.get("methodType") == 0 else "function",
            parameters=[(p.get("name", ""), p.get("typeName", "")) for p in m.get("parameters", [])],
            body=_script(m.get("body")),
            returns=(m.get("typeName") or None) if m.get("returnType") else None,
        )
        for defs in node.get("methods", {}).values()
        for m in defs
    ]
    return QmlObject(
        type_name=node.get("name", ""),
        id=node.get("idStr") or None,
        properties=properties,
        bindings=bindings,
        methods=methods,
        children=[_object(c) for c in node.get("children", [])],
    )


def _component(node: dict, inline: bool) -> Component:
    enums = []
    for defs in node.get("enumerations", {}).values():
        for e in defs:
            enums.append(Enum(e["name"], [(v["name"], int(v.get("value", i))) for i, v in enumerate(e.get("values", []))]))
    return Component(name=node["name"], root=_object(node["objects"][0]), enums=enums, is_inline=inline)


def _all_objects(obj: QmlObject):
    yield obj
    for child in obj.children:
        yield from _all_objects(child)
    for b in obj.bindings:
        if b.value.obj is not None:
            yield from _all_objects(b.value.obj)
        for o in b.value.objects:
            yield from _all_objects(o)


def load(path: Path) -> QmlFile:
    item = _dump(path)["currentItem"]
    components = item["components"]
    main = _component(components[""][0], inline=False)
    inline = {}
    for key, defs in components.items():
        if key == "":
            continue
        for c in defs:
            comp = _component(c, inline=True)
            inline[comp.name.split(".")[-1]] = comp
    imports = []
    for imp in item.get("imports", []):
        uri = imp.get("uri", "")
        is_dir = uri.startswith('"')
        if is_dir:
            # Directory imports are absolute (the implicit same-directory one) or relative to the file.
            uri = str((path.parent / uri.strip('"')).resolve())
        imports.append(Import(uri=uri, alias=imp.get("importId") or None, is_directory=is_dir))
    is_singleton = any(p.get("name") == "Singleton" for p in item.get("pragmas", []))
    text = path.read_text()
    for comp in [main, *inline.values()]:
        for obj in _all_objects(comp.root):
            for m in obj.methods:
                if m.kind == "function" and re.search(rf"function\s+{re.escape(m.name)}\s*\([^)]*\.\.\.", text):
                    m.rest = True
    return QmlFile(path=path, imports=imports, is_singleton=is_singleton, component=main, inline_components=inline)
