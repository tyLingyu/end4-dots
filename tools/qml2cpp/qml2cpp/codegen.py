"""C++ for one QML file: a class per component, built in two phases like QML itself.

Phase 1 creates the whole object tree (so every id exists), phase 2 sets properties, bindings,
handlers and behaviors. JavaScript the translator can't do becomes `II_TODO_JS("key")`, a
compile error, with the entry recorded in data/js for AI to fill (jsstore.py).
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field

from . import dom
from .jsexpr import NUMERIC, Scope, Translator, Untranslatable, Value
from .jsstore import JsStore
from .registry import SHELL_ROOT, BuiltinRef, ComponentRef
from .tsys import ATTACHED, AnonRef, AnyType, TypeSystem
from .structs import HEADER as STRUCTS_HEADER
from .typemap import JSON, RUNTIME, component_for_class, generated_header, namespace_parts, qualified_class, runtime_for

# Value types of the runtime and their headers (generated member types may use them).
RUNTIME_VALUE_HEADERS = {"Color": "runtime/color.h", "Point": "runtime/geometry.h", "Size": "runtime/geometry.h",
                         "Rect": "runtime/geometry.h", "DateTime": "runtime/datetime.h", "Easing": "runtime/easing.h",
                         "AnchorLine": "runtime/anchors.h"}

# Properties ii sets that have no effect in ii-shell's renderer.
IGNORED = {"renderType", "antialiasing", "smooth", "font.hintingPreference", "layer.smooth", "linkColor"}


def _flatten_groups(obj: dom.QmlObject) -> None:
    """`anchors { fill: parent }` -> binding `anchors.fill` on the object itself."""
    kept = []
    for child in obj.children:
        if child.is_group:
            _flatten_groups(child)
            for b in child.bindings:
                obj.bindings.append(dom.Binding(f"{child.type_name}.{b.name}", b.value, b.is_signal_handler, b.is_on))
        else:
            _flatten_groups(child)
            kept.append(child)
    obj.children = kept
    for b in obj.bindings:
        if b.value.obj is not None:
            _flatten_groups(b.value.obj)


def has_toplevel_return(code: str) -> bool:
    """Whether C++ code is a function body: a `return` outside any nested braces (a lambda's own
    `return` inside an expression doesn't count). String and char literals are skipped."""
    depth = 0
    i = 0
    while i < len(code):
        c = code[i]
        if c in "\"'":
            i += 1
            while i < len(code) and code[i] != c:
                i += 2 if code[i] == "\\" else 1
        elif code.startswith("//", i):
            i = code.find("\n", i)
            if i < 0:
                break
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        elif depth == 0 and re.match(r"\breturn\b", code[i:]) and (i == 0 or not (code[i - 1].isalnum() or code[i - 1] == "_")):
            return True
        i += 1
    return False


@dataclass
class Emitted:
    var: Value
    obj: dom.QmlObject
    type: AnyType | None
    path: str  # stable context path for jsstore keys: "valueIndicator/RowLayout.0"


@dataclass
class ComponentGen:
    ts: TypeSystem
    tr: Translator
    store: JsStore
    ref: ComponentRef
    body: list[str] = field(default_factory=list)
    members: list[str] = field(default_factory=list)
    nested: list[str] = field(default_factory=list)
    includes: set[str] = field(default_factory=set)
    header_includes: set[str] = field(default_factory=set)
    forwards: set[str] = field(default_factory=set)
    stubs: int = 0
    translated: int = 0
    methods: list[str] = field(default_factory=list)
    definitions: list[str] = field(default_factory=list)
    objects: list[Emitted] = field(default_factory=list)
    std_includes: set[str] = field(default_factory=set)
    anon_emitted: list[Emitted] = field(default_factory=list)  # objects with their own class, factories included
    pending_components: list[tuple[dom.QmlObject, Value, str]] = field(default_factory=list)

    def __post_init__(self):
        self.component = self.ts.registry.component(self.ref)
        _flatten_groups(self.component.root)
        self.view = self.ts.view(self.ref)
        self.cls = self.view.cpp_class.split("::")[-1]
        self.counter = 0
        self.factory_depth = 0

    # ── Types ────────────────────────────────────────────────────────────────

    def class_of(self, t: AnyType | None) -> str | None:
        if t is None:
            return None
        if isinstance(t, BuiltinRef):
            runtime = runtime_for(t)
            if runtime is None:
                return None
            self.header_includes.add(runtime.header)
            return runtime.cpp
        if isinstance(t, ComponentRef):
            self.includes.add(generated_header(t))
            if t.path != self.ref.path:  # classes of this file are declared at its top
                ns, _, name = qualified_class(t).rpartition("::")
                self.forwards.add(f"namespace {ns} {{ class {name}; }}")
            return "::" + self.ts.view(t).cpp_class
        return "::" + self.ts.view(t).cpp_class

    def uses_type(self, cpp: str | None) -> None:
        """Declarations a member type needs: var-types structs, Quickshell classes inside containers."""
        for header, needle in (("<map>", "std::map<"), ("<optional>", "std::optional<"), ("<functional>", "std::function<"),
                               ("runtime/component.h", "Component<")):
            if needle in (cpp or ""):
                (self.std_includes if header.startswith("<") else self.header_includes).add(header)
        for word in re.findall(r"[A-Za-z_][\w:]*", cpp or ""):
            if word.removeprefix("ii::") in RUNTIME_VALUE_HEADERS:
                self.header_includes.add(RUNTIME_VALUE_HEADERS[word.removeprefix("ii::")])
            elif word in self.ts.var_types.structs:
                self.header_includes.add(STRUCTS_HEADER)
            elif word == JSON:
                self.header_includes.add("runtime/js.h")
            elif word.startswith("::ii::") and (ref := component_for_class(word)) is not None and ref.path != self.ref.path:
                ns, _, name = qualified_class(ref).rpartition("::")
                self.forwards.add(f"namespace {ns} {{ class {name}; }}")
                self.includes.add(generated_header(ref))
            elif word.startswith("qs::"):
                runtime = next((r for r in RUNTIME.values() if r.cpp == word), None)
                if runtime is not None:
                    self.header_includes.add(runtime.header)

    def scan_includes(self, code: str) -> None:
        """Headers for the classes the final code names, filled translations included."""
        for m in re.finditer(r"::ii(?:::\w+)+", code):
            ref = component_for_class(m.group(0))
            if ref is not None and ref.path != self.ref.path:
                self.includes.add(generated_header(ref))
        for m in re.finditer(r"\bqs::(\w+)", code):
            runtime = next((r for r in RUNTIME.values() if r.cpp == f"qs::{m.group(1)}"), None)
            if runtime is not None:
                self.includes.add(runtime.header)
        if re.search(r"\bqt::", code):
            self.includes.add("runtime/qt.h")
        for namespace, header in (("Align", "runtime/layout.h"), ("WrapMode", "runtime/text.h"), ("Elide", "runtime/text.h"),
                                  ("MouseButton", "runtime/mouse_area.h"), ("Easing", "runtime/easing.h")):
            if re.search(rf"\b{namespace}::|\beasingTypeFromQt\b" if namespace == "Easing" else rf"\b{namespace}::", code):
                self.includes.add(header)
        for m in re.finditer(r"\bii::(\w+)\b(?!::)", code):
            runtime = next((r for r in RUNTIME.values() if r.cpp == m.group(1)), None)
            if runtime is not None:
                self.includes.add(runtime.header)  # ii::Rectangle in a dynamic_cast, say

    def is_visual(self, t: AnyType | None) -> bool:
        return t is not None and self.ts.view(t).is_visual

    # ── Phase 1: objects ─────────────────────────────────────────────────────

    def create_objects(self) -> None:
        root = self.component.root
        root_var = Value("this", f"{self.cls}*", self.ref)
        self.objects.append(Emitted(root_var, root, self.ref, root.id or "root"))
        self.ids: dict[str, Value] = {}
        if self.ref.inline:
            # Ids resolve through the creation context: ii creates inline components in their own
            # file, so the file's root id names its main object; for a singleton, the instance.
            outer = ComponentRef(self.ref.path)
            outer_root = self.ts.registry.component(outer).root
            if outer_root.id and self.ts.view(outer).is_singleton:
                self.ids[outer_root.id] = Value(f"::{self.ts.view(outer).cpp_class}::instance()", None, outer, is_ref=True)
        if root.id:
            self.ids[root.id] = root_var
        self._children(root, root_var, self.ref, root.id or "root")
        self._object_values(root, root_var, (), root.id or "root")

    def emitted(self, e: Emitted) -> None:
        self.objects.append(e)
        if isinstance(e.type, AnonRef) and all(x.type != e.type for x in self.anon_emitted):
            self.anon_emitted.append(e)

    def _children(self, obj: dom.QmlObject, parent: Value, parent_type: AnyType | None, path: str) -> None:
        counts: dict[str, int] = {}
        for child in obj.children:
            child_type = self.ts.object_type(self.ref, child)
            index = counts.get(child.type_name, 0)
            counts[child.type_name] = index + 1
            child_path = f"{path}/{child.id or f'{child.type_name}.{index}'}"
            if child.type_name == "Component":
                self.component_child(child, child_path)
                continue
            default = self.ts.view(parent_type).default_property if parent_type is not None else None
            default_prop = self.ts.prop(parent_type, default) if default else None
            if default_prop is not None and default_prop.component_of is not None:
                # Variants / Repeater / Instantiator: the child is the delegate, instantiated per model
                # item with `required property modelData` set. Needs the stage 3 delegate runtime.
                self.stub_line(child_path, f"{obj.type_name} {{ {child.type_name} {{ ... }} }}", "delegate (default component property)", "")
                continue
            cls = self.class_of(child_type)
            if cls is None:
                self.stub_line(child_path, f"object {child.type_name}", "object", "")
                continue
            content = self.ts.view(parent_type).content if parent_type is not None else None
            if content and self.is_visual(child_type):
                creator = f"{parent.member(content)}->add<{cls}>()"  # a window's visual children
            else:
                make = "add" if self.is_visual(parent_type) and self.is_visual(child_type) else "create"
                creator = f"{parent.cpp}->{make}<{cls}>()" if parent.cpp != "this" else f"{make}<{cls}>()"
            if child.id and not self.factory_depth:
                member = self.id_member(child.id)
                self.members.append(f"{cls}* {member} = nullptr;")
                self.body.append(f"{member} = {creator};")
                var = Value(member, f"{cls}*", child_type)
                self.ids[child.id] = var
            else:
                self.counter += 1
                name = child.id or f"o{self.counter}"
                self.body.append(f"auto* {name} = {creator};")
                var = Value(name, f"{cls}*", child_type)
                if child.id:
                    self.ids[child.id] = var
            self.emitted(Emitted(var, child, child_type, child_path))
            if isinstance(child_type, BuiltinRef) and child_type.name == "Loader":
                # `source:` URLs are relative to the QML file that declares the loader.
                self.body.append(f'{var.cpp}->setSourceBase("{self.ref.path.relative_to(SHELL_ROOT)}");')
            self._children(child, var, child_type, child_path)
            self._object_values(child, var, child_type.path if isinstance(child_type, AnonRef) else (), child_path)

    def _object_values(self, obj: dom.QmlObject, var: Value, prefix: tuple[str, ...], path: str) -> None:
        """Bindings whose value is an object (`sizes: QtObject { ... }`, `mask: Region { ... }`):
        create the object, owned by `var`, and point the property at it."""
        for b in obj.bindings:
            if b.value.obj is None or b.is_on or b.is_signal_handler or b.name in IGNORED:
                continue
            child_path = f"{path}/{b.name}"
            prop = self.ts.prop(var.obj, b.name) if var.obj is not None and "." not in b.name else None
            if prop is not None and prop.component_of is not None:
                continue  # a component: its factory is made in phase 2
            anon = AnonRef(self.ref, prefix + (b.name,), b.value.obj.type_name)
            if prop is not None and prop.object_type == anon:
                value_type = anon  # a property declared here with its value: the object's own class
            elif prop is not None and prop.kind == "property" and prop.object_type is not None and prop.cpp_type:
                value_type = self.ts.object_type(self.ref, b.value.obj)
            else:
                self.stub_line(child_path, f"{b.name}: {b.value.obj.type_name} {{ ... }}", "object value (no object property)", "")
                continue
            cls = self.class_of(value_type)
            if cls is None:
                self.stub_line(child_path, f"{b.name}: {b.value.obj.type_name} {{ ... }}", "object value (unknown type)", "")
                continue
            creator = f"{'create' if var.cpp == 'this' else var.cpp + '->create'}<{cls}>()"
            vid = b.value.obj.id
            if vid and not self.factory_depth:
                name = self.id_member(vid)
                self.members.append(f"{cls}* {name} = nullptr;")
                self.body.append(f"{name} = {creator};")
            else:
                self.counter += 1
                name = vid or f"o{self.counter}"
                self.body.append(f"auto* {name} = {creator};")
            setter = prop.access if var.cpp == "this" else var.member(prop.access)
            self.body.append(f"{setter}.set({name});")
            sub = Value(name, f"{cls}*", value_type)
            if vid:
                self.ids[vid] = sub
            self.emitted(Emitted(sub, b.value.obj, value_type, child_path))
            self._children(b.value.obj, sub, value_type, child_path)
            self._object_values(b.value.obj, sub, value_type.path if isinstance(value_type, AnonRef) else (), child_path)

    def component_child(self, child: dom.QmlObject, path: str) -> None:
        """`Component { id: x; T { ... } }` among an object's children: a factory member `x`."""
        inner = child.children[0] if len(child.children) == 1 else None
        inner_type = self.ts.object_type(self.ref, inner) if inner is not None else None
        cls = self.class_of(inner_type)
        if not child.id or cls is None or self.factory_depth:
            self.stub_line(path, f"Component {{ {inner.type_name if inner else ''} }}", "component (no id, unknown type or nested)", "")
            return
        member = self.id_member(child.id)
        self.members.append(f"Component<{cls}> {member};")
        self.header_includes.add("runtime/component.h")
        self.ids[child.id] = Value(member, f"Component<{cls}>")
        self.pending_components.append((child, Value(member, f"Component<{cls}>"), path))

    def id_member(self, id_name: str) -> str:
        return f"{id_name}Id" if id_name in self.view.props else id_name

    # ── Phase 2: properties ──────────────────────────────────────────────────

    def target(self, t: AnyType, name: str) -> tuple[str, str | None, str]:
        """(access, C++ type, kind) of a binding target, which may be dotted."""
        head, _, rest = name.partition(".")
        if rest:
            members = ATTACHED.get(head) or self.ts.view(t).groups.get(head)
            if members is None or rest not in members:
                raise Untranslatable(f"no property {name}")
            access, cpp = members[rest]
            return access, cpp, "property"
        prop = self.ts.prop(t, name)
        if prop is None:
            raise Untranslatable(f"no property {name}")
        return prop.access, prop.cpp_type, prop.kind

    def scope_for(self, e: Emitted) -> Scope:
        root = self.objects[0].var
        return Scope(self.ref, e.var if e.var.cpp != "this" else root, root, ids=dict(self.ids))

    def stub_line(self, context: str, code: str, kind: str, expected: str, form: str = "statements") -> str:
        """JavaScript the translator can't do: its translation from data/js, else a stub.
        form "statements": C++ statements put where the stub is (in the constructor or factory);
        "expression": a C++ expression of type `expected`, returned to the caller to wrap;
        "body": statements of a handler body (`expected` says its parameters), returned."""
        key, cpp = self.store.get(self.ref.path, context, code, expected, kind, form)
        if cpp is not None:
            self.translated += 1
            if form == "statements":
                self.body += cpp.splitlines()
            return cpp
        self.stubs += 1
        self.body.append(f'II_TODO_JS("{key}");  // {kind} {context}: {" ".join(code.split())[:100]}')
        return ""

    def bind_properties(self) -> None:
        for e in list(self.objects):
            self.bind_object(e)

    def bind_object(self, e: Emitted) -> None:
        if e.type is None or (isinstance(e.type, BuiltinRef) and e.type.name == "Connections"):
            return
        aliases = {p.name for p in self.component.root.properties if p.is_alias} if e is self.objects[0] else set()
        easing: dict[str, dom.Binding] = {}
        for b in e.obj.bindings:
            if b.name in IGNORED or b.is_signal_handler or b.is_on or b.name in aliases:
                continue  # aliases are wired in alias_wiring(); handlers and `on` objects come later
            if b.value.obj is not None:
                prop = self.ts.prop(e.type, b.name) if "." not in b.name else None
                if prop is not None and prop.component_of is not None:
                    self.component_factory(e, b, prop.access, prop.cpp_type)
                continue  # other object values were created (or stubbed) in phase 1
            if not b.value.script:
                if b.value.objects:
                    self.stub_line(f"{e.path}.{b.name}", f"{b.name}: [{', '.join(o.type_name for o in b.value.objects)}]",
                                   "object list", "")
                continue
            if b.name.startswith("easing."):
                easing[b.name.removeprefix("easing.")] = b
                continue
            self.binding(e, b)
        if easing:
            self.easing_binding(e, easing)

    def member_of(self, var: Value, access: str) -> str:
        return var.member(access) if var.cpp != "this" else access

    def component_factory(self, e: Emitted, b: dom.Binding, access: str, cpp_type: str) -> None:
        """`x: Component { T { ... } }`, or `sourceComponent: T { ... }` (an implicit component)."""
        explicit = b.value.obj.type_name == "Component" and b.value.obj.children
        inner = b.value.obj.children[0] if explicit else b.value.obj
        lines = self.factory(inner, f"{e.path}.{b.name}")
        if lines is not None:
            self.body.append(f"{self.member_of(e.var, access)}.set({cpp_type}([=, this](Object& owner) {{")
            self.body += [f"  {line}" for line in lines]
            self.body.append("}));")

    def component_members(self) -> None:
        for child, member, path in self.pending_components:
            lines = self.factory(child.children[0], path)
            if lines is not None:
                self.body.append(f"{member.cpp} = {member.type}([=, this](Object& owner) {{")
                self.body += [f"  {line}" for line in lines]
                self.body.append("});")

    def factory(self, inner: dom.QmlObject, path: str) -> list[str] | None:
        """The body of a factory lambda building `inner`'s object tree, bindings and handlers.
        Ids inside are local to it, as a Component's ids are to its own context."""
        inner_type = self.ts.object_type(self.ref, inner)
        cls = self.class_of(inner_type)
        if cls is None:
            self.stub_line(path, f"component {inner.type_name}", "component (unknown type)", "")
            return None
        saved_body, saved_ids, start = self.body, dict(self.ids), len(self.objects)
        self.body = []
        self.factory_depth += 1
        self.counter += 1
        var = Value(f"c{self.counter}", f"{cls}*", inner_type)
        self.body.append(f"auto* {var.cpp} = owner.create<{cls}>();")
        if inner.id:
            self.ids[inner.id] = var
        self.emitted(Emitted(var, inner, inner_type, path))
        self._children(inner, var, inner_type, path)
        self._object_values(inner, var, inner_type.path if isinstance(inner_type, AnonRef) else (), path)
        created = self.objects[start:]
        for sub in created:
            self.bind_object(sub)
        self.handlers(created)
        self.body.append(f"return {var.cpp};")  # Component::createObject() completes it
        lines, self.body, self.ids = self.body, saved_body, saved_ids
        del self.objects[start:]
        self.factory_depth -= 1
        return lines

    def easing_binding(self, e: Emitted, parts: dict[str, dom.Binding]) -> None:
        """`easing.type` / `easing.bezierCurve` / `easing.overshoot` -> one Easing value."""
        fields = []
        try:
            for key, target, wrap in (("type", "int", "easingTypeFromQt({})"), ("bezierCurve", "std::vector<double>", "{}"),
                                      ("overshoot", "double", "{}")):
                if key in parts:
                    value = self.tr.expression(parts[key].value.script.ast, self.scope_for(e))
                    fields.append(f".{'type' if key == 'type' else key} = {wrap.format(self.tr.coerce(value, target))}")
        except Untranslatable as err:
            code = "; ".join(f"easing.{k}: {b.value.script.code}" for k, b in parts.items())
            self.stub_line(f"{e.path}.easing", code, f"easing ({err})", "Easing")
            return
        self.translated += len(parts)
        expr = f"Easing{{{', '.join(fields)}}}"
        self.emit_assignment(self.member_of(e.var, "easing") + ".", expr, f"{self.cls}/{e.path.rsplit('/', 1)[-1]}.easing")

    def emit_assignment(self, setter: str, expr: str, label: str) -> None:
        if ".get()" in expr or "->get()" in expr or "instance()" in expr:
            self.body.append(f'{setter}bind([=, this] {{ return {expr}; }}, "{label}");')
        else:
            self.body.append(f"{setter}set({expr});")

    # ── `Behavior on x`, `NumberAnimation on x` ──────────────────────────────

    def on_objects(self, emitted: list[Emitted]) -> None:
        for e in emitted:
            if e.type is None:
                continue
            for b in e.obj.bindings:
                if b.is_on and b.value.obj is not None:
                    self.on_object(e, b)

    def on_object(self, e: Emitted, b: dom.Binding) -> None:
        context = f"{e.path}.{b.name}"
        obj = b.value.obj
        try:
            access, cpp_type, kind = self.target(e.type, b.name)
            if kind != "property" or cpp_type is None:
                raise Untranslatable(f"{kind} target")
        except Untranslatable as err:
            self.stub_line(context, f"{obj.type_name} on {b.name}", f"on ({err})", "")
            return
        member = self.member_of(e.var, access)
        create = "create" if e.var.cpp == "this" else f"{e.var.cpp}->create"
        obj_type = self.ts.object_type(self.ref, obj)
        self.counter += 1
        if obj.type_name == "Behavior" and (obj.properties or obj.id or any(c.children for c in obj.children)):
            # A group (SequentialAnimation { ... PropertyAction {} }) needs a Behavior that drives
            # animation groups; a Behavior with an id or its own properties needs a class of its own.
            self.stub_line(context, f"Behavior on {b.name} {{ {' '.join(c.type_name for c in obj.children)} {{ ... }} }}",
                           "behavior (animation group, id or own properties)", "")
            return
        if obj.type_name == "Behavior":
            self.class_of(obj_type)
            var = Value(f"b{self.counter}", f"Behavior<{cpp_type}>*", obj_type)
            self.body.append(f"auto* {var.cpp} = {create}<Behavior<{cpp_type}>>({member});")
            be = Emitted(var, obj, obj_type, f"{context}.Behavior")
            for bb in obj.bindings:
                if bb.name == "animation" and bb.value.script:
                    self.behavior_animation(be, bb)
                elif bb.name == "enabled" and bb.value.script:
                    self.binding(be, bb)
            for child in obj.children:
                child_type = self.ts.object_type(self.ref, child)
                cls = self.class_of(child_type)
                if cls is None:
                    self.stub_line(f"{context}.Behavior", child.type_name, "behavior animation (unknown type)", "")
                    continue
                self.counter += 1
                anim = Value(f"a{self.counter}", f"{cls}*", child_type)
                self.body.append(f"auto* {anim.cpp} = {var.cpp}->setAnimation<{cls}>();")
                self.bind_object(Emitted(anim, child, child_type, f"{context}.Behavior/{child.type_name}"))
            return
        # A property value source: the animation targets the property and runs by default.
        cls = self.class_of(obj_type)
        if cls is None or obj.children:
            self.stub_line(context, f"{obj.type_name} on {b.name}", "on (animation group)", "")
            return
        anim = Value(f"a{self.counter}", f"{cls}*", obj_type)
        self.body.append(f"auto* {anim.cpp} = {create}<{cls}>();")
        self.body.append(f"{anim.cpp}->target = &{member};")
        self.bind_object(Emitted(anim, obj, obj_type, f"{context}.{obj.type_name}"))
        if not any(x.name == "running" for x in obj.bindings):
            self.body.append(f"{anim.cpp}->running.set(true);")

    def behavior_animation(self, be: Emitted, b: dom.Binding) -> None:
        """`animation: Appearance.animation.x.numberAnimation.createObject(this)`."""
        ast = b.value.script.ast
        try:
            callee = ast.children[0] if ast is not None and ast.kind == "CallExpression" else None
            if callee is None or callee.kind != "FieldMemberExpression" or callee.attrs.get("name") != "createObject":
                raise Untranslatable("animation is not Component.createObject()")
            component = self.tr.expression(callee.children[0], self.scope_for(be))
            if not (component.type or "").startswith("Component<"):
                raise Untranslatable(f"createObject on {component.type}")
        except Untranslatable as err:
            filled = self.stub_line(f"{be.path}.animation", b.value.script.code, f"behavior animation ({err})",
                                    "Animation* (created by this code, owned by the Behavior)", "expression")
            if filled:
                self.body.append(f"{be.var.cpp}->adoptAnimation({filled});")
            return
        self.translated += 1
        self.body.append(f"{be.var.cpp}->adoptAnimation({component.cpp}.createObject(*{be.var.cpp}));")

    def binding(self, e: Emitted, b: dom.Binding) -> None:
        context = f"{e.path}.{b.name}"
        label = f"{self.cls}/{e.path.rsplit('/', 1)[-1]}.{b.name}"
        try:
            access, cpp_type, kind = self.target(e.type, b.name)
        except Untranslatable as err:
            self.stub_line(context, b.value.script.code, f"binding ({err})", "")
            return
        member = self.member_of(e.var, access)
        setter = f"{member}->" if kind == "alias" else f"{member}."
        try:
            value = self.tr.expression(b.value.script.ast, self.scope_for(e))
            if cpp_type is None:
                raise Untranslatable("target type unknown")
            expr = self.tr.coerce(value, cpp_type)
        except Untranslatable as err:
            filled = self.stub_line(context, b.value.script.code, f"binding ({err})", cpp_type or "", "expression")
            if filled and has_toplevel_return(filled):  # a lambda body (a JS block binding)
                self.body.append(f"{setter}bind([=, this]() -> {cpp_type} {{")
                self.body += [f"  {line}" for line in filled.splitlines()]
                self.body.append(f'}}, "{label}");')
            elif filled and ("//" in filled or "\n" in filled):  # keep a trailing comment off the `;`
                self.body.append(f"{setter}bind([=, this] {{")
                self.body += [f"  return {line}" if i == 0 else f"    {line}" for i, line in enumerate(filled.splitlines())]
                self.body += ["  ;", f'}}, "{label}");']
            elif filled:
                self.body.append(f'{setter}bind([=, this] {{ return {filled}; }}, "{label}");')
            return
        self.translated += 1
        self.emit_assignment(setter, expr, label)

    def handlers(self, objects: list[Emitted]) -> None:
        """`on` objects, signal handlers, Connections and functions of the given objects."""
        self.on_objects(objects)
        for e in objects:
            if isinstance(e.type, BuiltinRef) and e.type.name == "Connections":
                self.connections(e)
                continue
            if isinstance(e.type, BuiltinRef) and e.type.name == "IpcHandler":
                self.ipc_functions(e)
            for b in e.obj.bindings:
                if b.is_signal_handler and b.value.script:
                    self.handler(e, b)
            for m in e.obj.methods:
                if m.kind == "function" and m.body is not None:
                    self.function(e, m)

    def handler_body(self, context: str, script: dom.Script, scope: Scope, params: str = "") -> list[str] | None:
        """Statements of a handler body: mechanical, else from data/js, else a stub."""
        try:
            lines = self.tr.statements(script.ast, scope)
            self.translated += 1
            return lines
        except Untranslatable as err:
            filled = self.stub_line(context, script.code, f"statements ({err})", f"handler body, parameters ({params})", "body")
            return filled.splitlines() if filled else None

    def signal_target(self, e: Emitted, handler_name: str) -> tuple[str, list[tuple[str, str | None]]] | None:
        """For `onFoo` on an object: (C++ signal expression, parameters)."""
        if handler_name.startswith("Component."):
            return self.member_of(e.var, "completed"), []
        name = handler_name[2:3].lower() + handler_name[3:]
        # A property's change first: .qmltypes also lists NOTIFY signals (valueChanged) as signals,
        # but the runtime notifies through the property itself.
        if name.endswith("Changed") and (prop := self.ts.prop(e.type, name[: -len("Changed")])) is not None:
            if prop.kind == "property":
                return self.member_of(e.var, self.ts.change_signal(e.type, prop)), []
        params = self.ts.signal(e.type, name)
        if params is not None:
            return self.member_of(e.var, self.ts.signal_member(e.type, name)), params
        return None

    def connect_lines(self, signal: str, params: list[tuple[str, str | None]], body: list[str], connect: str) -> list[str]:
        decls = ", ".join(f"[[maybe_unused]] {t} const& {n}" for n, t in params)
        return [f"{signal}.{connect}([=, this]({decls}) {{", *(f"  {line}" for line in body), "});"]

    def handler(self, e: Emitted, b: dom.Binding) -> None:
        context = f"{e.path}.{b.name}"
        target = self.signal_target(e, b.name)
        if target is None or any(t is None for _, t in target[1]):
            self.stub_line(context, b.value.script.code, "handler (unknown signal)", "void")
            return
        signal, params = target
        scope = self.scope_for(e)
        scope.locals.update({n: Value(n, t) for n, t in params})
        body = self.handler_body(context, b.value.script, scope, ", ".join(f"{t} const& {n}" for n, t in params))
        if body is not None:
            self.body += self.connect_lines(signal, params, body, "connectForever")

    def connections(self, e: Emitted) -> None:
        """`Connections { target: X; function onFoo() {...} }`: reconnect whenever the target changes."""
        context = f"{e.path}.Connections"
        target_binding = next((b for b in e.obj.bindings if b.name == "target" and b.value.script), None)
        try:
            if target_binding is None:
                raise Untranslatable("no target")
            target = self.tr.expression(target_binding.value.script.ast, self.scope_for(e))
            if target.obj is None:
                raise Untranslatable(f"target of type {target.type}")
        except Untranslatable as err:
            code = " ".join(e.obj.methods[0].body.code.split())[:60] if e.obj.methods else ""
            self.stub_line(context, (target_binding.value.script.code if target_binding else "") + " | " + code,
                           f"connections ({err})", "")
            return
        cls = self.class_of(target.obj)
        pointer = f"&{target.cpp}" if target.is_ref else target.cpp
        self.body.append(f'{e.var.cpp}->target.bind([=, this] {{ return static_cast<Object*>({pointer}); }}, "{self.cls}/Connections.target");')
        lines = [f"{e.var.cpp}->setConnector([=, this](Object* object, std::vector<Connection>& out) {{",
                 f"  auto* target = static_cast<{cls}*>(object);"]
        typed = Emitted(Value("target", f"{cls}*", target.obj), e.obj, target.obj, e.path)
        for m in e.obj.methods:
            if m.kind != "function" or m.body is None:
                continue
            found = self.signal_target(typed, m.name)
            if found is None:
                self.stub_line(f"{context}.{m.name}", m.body.code, "connections handler (unknown signal)", "void")
                continue
            signal, params = found
            scope = self.scope_for(e)
            scope.locals.update({n: Value(n, t) for (n, t) in params})
            scope.locals["target"] = Value("target", f"{cls}*", target.obj)
            body = self.handler_body(f"{context}.{m.name}", m.body, scope,
                                     ", ".join(f"{t} const& {n}" for n, t in params) + f"; `target` is the {cls}*")
            if body is not None:
                lines += [f"  {line}" for line in self.connect_lines(f"out.push_back({signal}", params, body, "connect")]
                lines[-1] = "  }));"
        lines.append("});")
        self.body += lines

    def ipc_functions(self, e: Emitted) -> None:
        """IpcHandler functions are commands of `qs ipc call <target> <function>`."""
        for m in e.obj.methods:
            if m.kind != "function" or m.body is None:
                continue
            context = f"{e.path}.{m.name}"
            if m.parameters:
                self.stub_line(context, f"function {m.name}({', '.join(n for n, _ in m.parameters)}) {{ {m.body.code} }}",
                               "ipc function (parameters)", "")
                continue
            body = self.handler_body(context, m.body, self.scope_for(e))
            if body is not None:
                self.body += [f'{e.var.cpp}->addFunction("{m.name}", [=, this](const std::vector<std::string>&) -> std::string {{',
                              *(f"  {line}" for line in body), "  return {};", "});"]

    def function(self, e: Emitted, m: dom.Method) -> None:
        """A JS function: a member function once data/js holds its signature and body."""
        from .signature import parse

        params = ", ".join(n for n, _ in m.parameters)
        context = f"{e.path}.{m.name}"
        code = f"function {m.name}({params}) {{ {m.body.code} }}"
        expected = "signature (C++ declaration) + cpp (function body statements)"
        if e is not self.objects[0]:
            if not (isinstance(e.type, BuiltinRef) and e.type.name in ("IpcHandler", "Connections")):
                self.stub_line(context, code, "function of a child object", "")
            return
        signature = self.ts.function(self.ref, m.name) if self.ts.is_procedure(m) else None
        if signature is not None and not (self.store.find(self.ref.path, context) or {}).get("signature"):
            lines = self.handler_body(context, m.body, self.scope_for(e))
            if lines is None:
                return
            body = "\n".join(lines)
        else:
            key, body = self.store.get(self.ref.path, context, code, expected, "function", "function")
            entry = self.store.find(self.ref.path, context) or {}
            if body is None or not entry.get("signature"):
                self.stubs += 1
                self.body.append(f'II_TODO_JS("{key}");  // function {context}')
                return
            signature = parse(entry["signature"])
        self.translated += 1
        self.uses_type(signature.text)
        self.methods.append(f"{signature.text};")
        self.definitions.append(f"  {signature.definition(f'{self.cls}::{m.name}')} {{")
        self.definitions += [f"    {line}" for line in body.splitlines()]
        self.definitions.append("  }")
        self.definitions.append("")

    # ── Output ───────────────────────────────────────────────────────────────

    def declared_members(self) -> list[str]:
        out = []
        for m in self.component.root.methods:
            if m.kind == "signal":
                params = self.ts.signal(self.ref, m.name) or []
                out.append(f"Signal<{', '.join(t or JSON for _, t in params)}> {self.ts.signal_member(self.ref, m.name)};")
                self.uses_type(JSON if any(t is None for _, t in params) else None)
        for p in self.component.root.properties:
            prop = self.view.props[p.name]
            self.class_of(prop.object_type)
            self.uses_type(prop.cpp_type)
            if prop.kind == "alias":
                if prop.alias_of and prop.alias_of[1] is None:
                    out.append(f"{prop.cpp_type or 'void*'} {p.name} = nullptr;  // alias {prop.alias_of[0]}")
                else:
                    target = ".".join(x for x in (prop.alias_of or ("?", "?")) if x)
                    out.append(f"Property<{prop.cpp_type or 'void'}>* {p.name} = nullptr;  // alias {target}")
            else:
                cpp = prop.cpp_type or f"{JSON} /* TODO type */"
                self.uses_type(cpp)
                out.append(f"Property<{cpp}> {prop.access};")
        return out

    def alias_wiring(self) -> None:
        for p in self.component.root.properties:
            prop = self.view.props[p.name]
            if prop.kind == "alias" and prop.alias_of:
                target_id, member = prop.alias_of
                target = self.ids.get(target_id)
                if target is not None:
                    self.body.append(f"{p.name} = {f'&{target.member(member)}' if member else target.cpp};")

    def anon_classes(self) -> list[str]:
        """Nested classes for anonymous objects: property declarations only (bindings are set by
        the component constructor, which sees every id)."""
        out: list[str] = [f"class {e.type.name};" for e in self.anon_emitted]
        if out:
            out.append("")
        for e in self.anon_emitted:
            view = self.ts.view(e.type)
            base = self.class_of(view.base) or "Object"
            out.append(f"class {e.type.name} : public {base} {{")
            out.append("public:")
            for p in e.obj.properties:
                prop = view.props[p.name]
                self.class_of(prop.object_type)
                self.uses_type(prop.cpp_type)
                out.append(f"  Property<{prop.cpp_type or JSON + ' /* TODO type */'}> {p.name};")
                self.uses_type(prop.cpp_type or JSON)
            out.append("};")
            out.append("")
        return out

    def generate(self) -> None:
        """Fill decl (the class definition) and defn (its out-of-class definitions)."""
        self.tr.used.clear()
        self.create_objects()
        self.alias_wiring()
        self.bind_properties()
        self.component_members()
        self.handlers(self.objects)
        for r in self.tr.used:
            if isinstance(r, ComponentRef):
                self.includes.add(generated_header(r))
            elif (runtime := runtime_for(r)) is not None:
                self.includes.add(runtime.header)
        self.scan_includes("\n".join(self.body + self.definitions))

        base = self.class_of(self.view.base) or "Object"
        if isinstance(self.view.base, ComponentRef) and self.view.base.path != self.ref.path:
            self.header_includes.add(generated_header(self.view.base))  # a base class must be complete
        singleton = self.view.is_singleton
        nested_decls = self.anon_classes()
        declared = self.declared_members()  # before the includes are read: registers the types used
        self.decl = [
            f"class {self.cls} : public {base} {{",
            "public:",
            *([f"  static {self.cls}& instance();", ""] if singleton else []),
            *(f"  {line}" for line in nested_decls),
            f"  {self.cls}();",
            "",
            *(f"  {m}" for m in declared),
            "",
            *(f"  {m}" for m in self.methods),
            *([""] if self.methods else []),
            *(f"  {m}" for m in self.members),
            "};",
        ]
        self.defn = [
            *([f"{self.cls}& {self.cls}::instance() {{",
               "  // Published before it is constructed: bindings reached while constructing (an inline",
               "  // component reading its singleton, singletons reading each other) get the object under",
               "  // construction and re-evaluate once its properties are set, as in QML. A function-local",
               "  // static object would throw recursive_init_error instead.",
               f"  static {self.cls}* self = nullptr;",
               "  if (self == nullptr) {",
               f"    self = static_cast<{self.cls}*>(::operator new(sizeof({self.cls})));",
               f"    new (self) {self.cls}();",
               "    self->complete();",
               "  }",
               "  return *self;",
               "}", ""] if singleton else []),
            f"{self.cls}::{self.cls}() {{",
            *(f"  {line}" for line in self.body),
            "}",
            "",
            *(line[2:] if line.startswith("  ") else line for line in self.definitions),
        ]


def generate_file(ts: TypeSystem, tr: Translator, store: JsStore, path) -> tuple[str, str, list[ComponentGen]]:
    """Header and source for one QML file: its inline components (bases first), then its component."""
    qml = ts.registry.file(path)
    refs = [ComponentRef(path, name) for name in qml.inline_components]
    ordered: list[ComponentRef] = []

    def visit(ref: ComponentRef) -> None:
        if ref in ordered:
            return
        base = ts.view(ref).base
        if isinstance(base, ComponentRef) and base in refs:
            visit(base)
        ordered.append(ref)

    for ref in refs:
        visit(ref)
    gens = [ComponentGen(ts, tr, store, ref) for ref in [*ordered, ComponentRef(path)]]
    for gen in gens:
        gen.generate()

    main = gens[-1]
    rel = path.relative_to(SHELL_ROOT)
    ns = "::".join(["ii", *namespace_parts(main.ref)])
    own = {f"namespace {ns} {{ class {g.cls}; }}" for g in gens}
    header_includes = set().union(*(g.header_includes for g in gens)) | {"runtime/property.h"}
    header_includes -= {generated_header(main.ref)}
    std_includes = set().union(*(g.std_includes for g in gens)) | {"<string>", "<vector>"}
    includes = set().union(*(g.includes for g in gens)) - {generated_header(main.ref)}
    forwards = set().union(*(g.forwards for g in gens)) - own
    indent = lambda lines: [f"  {line}" if line else "" for line in lines]
    header = [
        "#pragma once",
        f"// Generated by qml2cpp from {rel}.",
        "",
        *sorted(f'#include "{h}"' for h in header_includes),
        "",
        *sorted(f"#include {h}" for h in std_includes),
        "",
        *sorted(forwards),
        "",
        f"namespace {ns} {{",
        "",
        *indent([f"class {g.cls};" for g in gens]),
        "",
    ]
    source = [
        f"// Generated by qml2cpp from {rel}.",
        f'#include "{generated_header(main.ref)}"',
        "",
        *sorted(f'#include "{h}"' for h in includes),
        '#include "runtime/js.h"',
        "",
        f"namespace {ns} {{",
        "",
    ]
    for g in gens:
        header += [*indent(g.decl), ""]
        source += indent(g.defn)
    header += [f"}} // namespace {ns}", ""]
    source += [f"}} // namespace {ns}", ""]
    strip = lambda lines: "\n".join(line.rstrip() for line in lines)
    return strip(header), strip(source), gens
