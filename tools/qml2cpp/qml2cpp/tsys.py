"""The type system: properties (with C++ types and access paths) of every QML type in use.

A "type" is a generated component (a .qml file, an inline component, or an anonymous object
nested in a binding such as `sizes: QtObject { ... }`), or a built-in type from .qmltypes.
"""
from __future__ import annotations

from dataclasses import dataclass, field

from . import dom
from .qmltypes import TypeInfo
from .registry import BuiltinRef, ComponentRef, Registry, TypeRef
from .typemap import QML_BASIC, QT_CPP, VarTypes, qualified_class, runtime_for


@dataclass(frozen=True)
class AnonRef:
    """An anonymous object type: the value of `name: Type { ... }` inside a component."""

    owner: ComponentRef
    path: tuple[str, ...]  # property names from the component root: ("animation", "elementMove")
    base: str  # the type name written in QML ("QtObject")

    @property
    def name(self) -> str:
        return "".join(p.lstrip("#")[:1].upper() + p.lstrip("#")[1:] for p in self.path)

    def __str__(self) -> str:
        return f"{self.owner}::{'.'.join(self.path)}"


AnyType = ComponentRef | BuiltinRef | AnonRef


@dataclass
class Prop:
    name: str
    cpp_type: str | None  # value type; None when it cannot be expressed yet
    access: str  # member expression from an object pointer: "x", "border.width", "anchors().fill"
    kind: str = "property"  # "property" | "alias" | "group" | "attached" | "plain"
    object_type: AnyType | None = None  # the QML type when the value is an object (pointer)
    is_list: bool = False
    readonly: bool = False
    owner: AnyType | None = None
    alias_of: tuple[str, str | None] | None = None  # (id, property) an alias points to
    component_of: AnyType | None = None  # for Component<T> properties: T


@dataclass
class TypeView:
    ref: AnyType
    cpp_class: str
    base: AnyType | None
    props: dict[str, Prop] = field(default_factory=dict)
    is_visual: bool = False
    is_singleton: bool = False


# Grouped properties of built-in types, as the runtime exposes them.
GROUPS: dict[str, dict[str, tuple[str, str]]] = {
    "anchors": {
        "fill": ("anchors().fill", "Item*"),
        "centerIn": ("anchors().centerIn", "Item*"),
        "left": ("anchors().left", "AnchorLine"),
        "right": ("anchors().right", "AnchorLine"),
        "horizontalCenter": ("anchors().horizontalCenter", "AnchorLine"),
        "top": ("anchors().top", "AnchorLine"),
        "bottom": ("anchors().bottom", "AnchorLine"),
        "verticalCenter": ("anchors().verticalCenter", "AnchorLine"),
        "margins": ("anchors().margins", "double"),
        "leftMargin": ("anchors().leftMargin", "double"),
        "rightMargin": ("anchors().rightMargin", "double"),
        "topMargin": ("anchors().topMargin", "double"),
        "bottomMargin": ("anchors().bottomMargin", "double"),
        "horizontalCenterOffset": ("anchors().horizontalCenterOffset", "double"),
        "verticalCenterOffset": ("anchors().verticalCenterOffset", "double"),
        "alignWhenCentered": ("anchors().alignWhenCentered", "bool"),
    },
    "border": {"width": ("border.width", "double"), "color": ("border.color", "Color")},
    "font": {
        "family": ("font.family", "std::string"),
        "pixelSize": ("font.pixelSize", "double"),
        "pointSize": ("font.pointSize", "double"),
        "weight": ("font.weight", "int"),
        "bold": ("font.bold", "bool"),
        "italic": ("font.italic", "bool"),
        "variableAxes": ("font.variableAxes", "std::map<std::string, double>"),
    },
}

# `Layout.*` attached properties.
LAYOUT_ATTACHED = {
    "fillWidth": "std::optional<bool>",
    "fillHeight": "std::optional<bool>",
    "minimumWidth": "double",
    "minimumHeight": "double",
    "preferredWidth": "double",
    "preferredHeight": "double",
    "maximumWidth": "double",
    "maximumHeight": "double",
    "alignment": "int",
    "margins": "double",
    "leftMargin": "double",
    "rightMargin": "double",
    "topMargin": "double",
    "bottomMargin": "double",
}


class TypeSystem:
    def __init__(self, registry: Registry, store=None):
        self.registry = registry
        self.store = store  # jsstore.JsStore: where translated functions keep their C++ signatures
        self.var_types = VarTypes()
        self._views: dict[AnyType, TypeView] = {}
        # Child objects that declare their own properties define anonymous types, by object identity.
        self._anon_by_obj: dict[int, AnonRef] = {}
        self._labeled: dict[tuple[ComponentRef, str], dom.QmlObject] = {}

    # ── Lookup ───────────────────────────────────────────────────────────────

    def view(self, ref: AnyType) -> TypeView:
        if ref not in self._views:
            self._views[ref] = TypeView(ref, "", None)  # placeholder breaks cycles
            if isinstance(ref, BuiltinRef):
                self._views[ref] = self._builtin_view(ref)
            elif isinstance(ref, ComponentRef):
                self._views[ref] = self._component_view(ref)
            else:
                self._views[ref] = self._anon_view(ref)
        return self._views[ref]

    def prop(self, ref: AnyType, name: str) -> Prop | None:
        return self.view(ref).props.get(name)

    def function(self, ref: AnyType, name: str):
        """The C++ signature of a JS function on a component (or its bases), once translated."""
        from .signature import parse

        while isinstance(ref, ComponentRef) and self.store is not None:
            root = self.registry.component(ref).root
            if any(m.name == name and m.kind == "function" for m in root.methods):
                entry = self.store.find(ref.path, f"{root.id or 'root'}.{name}")
                if entry and entry.get("signature"):
                    return parse(entry["signature"])
                return None
            ref = self.view(ref).base
        return None

    def resolve(self, file: ComponentRef, type_name: str) -> AnyType | None:
        return self.registry.resolve_type(file.path, type_name)

    def object_type(self, owner: ComponentRef, obj: dom.QmlObject) -> AnyType | None:
        """The type of a (non-root) object: its own anonymous type if it declares properties."""
        if obj.properties and not obj.is_group and obj is not self.registry.component(owner).root:
            if id(obj) not in self._anon_by_obj:
                label = "#" + (obj.id or f"{obj.type_name}{len(self._anon_by_obj)}")
                self._anon_by_obj[id(obj)] = AnonRef(owner, (label,), obj.type_name)
                self._labeled[(owner, label)] = obj
            return self._anon_by_obj[id(obj)]
        return self.resolve(owner, obj.type_name)

    # ── Built-in types ───────────────────────────────────────────────────────

    def _qt_type(self, cpp: str, is_pointer: bool) -> tuple[str | None, AnyType | None]:
        if is_pointer:
            for info in self.registry.builtins.by_cpp.values():
                if info.cpp_name == cpp and info.exports:
                    module, name = next(iter(info.exports.items()))
                    ref = BuiltinRef(module, name, cpp)
                    runtime = runtime_for(ref)
                    return (f"{runtime.cpp}*" if runtime else None), ref
            return None, None
        return QT_CPP.get(cpp), None

    def _builtin_view(self, ref: BuiltinRef) -> TypeView:
        info: TypeInfo = self.registry.builtin_info(ref)
        runtime = runtime_for(ref)
        view = TypeView(ref, runtime.cpp if runtime else f"/*{ref}*/", None, is_visual=bool(runtime and runtime.visual))
        view.is_singleton = info.is_singleton
        for t in reversed(list(self.registry.builtins.chain(info))):
            for p in t.properties.values():
                cpp, obj = self._qt_type(p.cpp_type, p.is_pointer)
                view.props[p.name] = Prop(p.name, cpp, p.name, object_type=obj, is_list=p.is_list,
                                          readonly=p.is_readonly, owner=ref)
        for group, members in GROUPS.items():
            if group in view.props:
                view.props[group] = Prop(group, None, group, kind="group", owner=ref)
        return view

    # ── Components ───────────────────────────────────────────────────────────

    def _declared_type(self, owner: ComponentRef, p: dom.PropertyDef) -> tuple[str | None, AnyType | None]:
        name = p.type_name
        if name in QML_BASIC:
            return QML_BASIC[name], None
        if name == "var":
            return self.var_types.lookup(owner, p.name) or "nlohmann::json", None
        if name.startswith("list<") and name.endswith(">"):
            inner, ref = self._declared_type(owner, dom.PropertyDef(p.name, name[5:-1]))
            return (f"std::vector<{inner}>" if inner else None), ref
        ref = self.resolve(owner, name)
        if ref is None:
            return None, None
        return f"{self.view(ref).cpp_class}*", ref

    def _add_declared(self, view: TypeView, owner: ComponentRef, obj: dom.QmlObject, path: tuple[str, ...]):
        inline_objects = {b.name: b.value.obj for b in obj.bindings if b.value.obj is not None and not b.is_on}
        for p in obj.properties:
            if p.is_alias:
                view.props[p.name] = Prop(p.name, None, p.name, kind="alias", owner=view.ref)
                continue
            cpp, ref = self._declared_type(owner, p)
            nested = inline_objects.get(p.name)
            # `property Component x: Component { T { ... } }`: a factory of T.
            if nested is not None and nested.type_name == "Component" and nested.children:
                inner = self.resolve(owner, nested.children[0].type_name)
                inner_cls = self.view(inner).cpp_class if inner is not None else None
                view.props[p.name] = Prop(p.name, f"Component<{inner_cls}>" if inner_cls else None, p.name,
                                          object_type=None, owner=view.ref, component_of=inner)
                continue
            # `property QtObject sizes` bound to `sizes: QtObject { ... }`: use the concrete object.
            if nested is not None and (cpp is None or p.type_name in ("QtObject", "var") or ref is not None):
                ref = AnonRef(owner, path + (p.name,), nested.type_name)
                cpp = f"{self.view(ref).cpp_class}*"
            view.props[p.name] = Prop(p.name, cpp, p.name, object_type=ref, is_list=p.is_list,
                                      readonly=p.is_readonly, owner=view.ref)

    def _component_view(self, ref: ComponentRef) -> TypeView:
        component = self.registry.component(ref)
        base = self.resolve(ref, component.root.type_name)
        view = TypeView(ref, qualified_class(ref), base)
        if base is not None:
            base_view = self.view(base)
            view.props.update(base_view.props)
            view.is_visual = base_view.is_visual
        view.is_singleton = self.registry.is_singleton(ref)
        self._add_declared(view, ref, component.root, ())
        self._resolve_aliases(view, ref, component.root)
        return view

    def ids(self, owner: ComponentRef, root: dom.QmlObject) -> dict[str, AnyType]:
        """Every id in a component (QML ids are visible throughout the component)."""
        from .deps import _objects

        out: dict[str, AnyType] = {}
        for obj in _objects(root):
            if obj.id and not obj.is_group:
                t = owner if obj is root else self.object_type(owner, obj)
                if t is not None:
                    out[obj.id] = t
        return out

    def _resolve_aliases(self, view: TypeView, owner: ComponentRef, root: dom.QmlObject) -> None:
        aliases = [p for p in view.props.values() if p.kind == "alias" and p.owner == view.ref]
        if not aliases:
            return
        ids = self.ids(owner, root)
        targets = {b.name: b.value.script for b in root.bindings if b.value.script is not None}
        for prop in aliases:
            script = targets.get(prop.name)
            ast = script.ast if script else None
            if ast is not None and ast.kind == "FieldMemberExpression" and ast.children[0].kind == "IdentifierExpression":
                target_id, member = ast.children[0].attrs["name"], ast.attrs["name"]
                target = self.prop(ids[target_id], member) if target_id in ids else None
                if target is not None and target.kind == "property":
                    prop.cpp_type, prop.object_type, prop.alias_of = target.cpp_type, target.object_type, (target_id, member)
            elif ast is not None and ast.kind == "IdentifierExpression" and ast.attrs["name"] in ids:
                target_type = ids[ast.attrs["name"]]
                prop.cpp_type, prop.object_type = f"{self.view(target_type).cpp_class}*", target_type
                prop.alias_of = (ast.attrs["name"], None)

    def _anon_view(self, ref: AnonRef) -> TypeView:
        obj = self.anon_object(ref)
        base = self.resolve(ref.owner, obj.type_name)
        view = TypeView(ref, f"{qualified_class(ref.owner)}::{ref.name}", base)
        if base is not None:
            base_view = self.view(base)
            view.props.update(base_view.props)
            view.is_visual = base_view.is_visual
        self._add_declared(view, ref.owner, obj, ref.path)
        return view

    def anon_object(self, ref: AnonRef) -> dom.QmlObject:
        obj = self.registry.component(ref.owner).root
        for name in ref.path:
            if name.startswith("#"):
                obj = self._labeled[(ref.owner, name)]
            else:
                obj = next(b.value.obj for b in obj.bindings if b.name == name and b.value.obj is not None)
        return obj
