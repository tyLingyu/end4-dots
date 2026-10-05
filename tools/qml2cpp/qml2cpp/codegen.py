"""C++ for one QML file: a class per component, built in two phases like QML itself.

Phase 1 creates the whole object tree (so every id exists), phase 2 sets properties, bindings,
handlers and behaviors. JavaScript the translator can't do becomes `II_TODO_JS("key")`, a
compile error, with the entry recorded in data/js for AI to fill (jsstore.py).
"""
from __future__ import annotations

from dataclasses import dataclass, field

from . import dom
from .jsexpr import NUMERIC, Scope, Translator, Untranslatable, Value
from .jsstore import JsStore
from .registry import SHELL_ROOT, BuiltinRef, ComponentRef
from .tsys import GROUPS, LAYOUT_ATTACHED, AnonRef, AnyType, TypeSystem
from .typemap import generated_header, namespace_parts, runtime_for

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
            if t.path != self.ref.path:
                ns = "::".join(["ii", *namespace_parts(t)])
                self.forwards.add(f"namespace {ns} {{ class {t.path.stem}; }}")
            return "::" + self.ts.view(t).cpp_class
        return "::" + self.ts.view(t).cpp_class

    def is_visual(self, t: AnyType | None) -> bool:
        return t is not None and self.ts.view(t).is_visual

    # ── Phase 1: objects ─────────────────────────────────────────────────────

    def create_objects(self) -> None:
        root = self.component.root
        root_var = Value("this", f"{self.cls}*", self.ref)
        self.objects.append(Emitted(root_var, root, self.ref, root.id or "root"))
        self.ids: dict[str, Value] = {}
        if root.id:
            self.ids[root.id] = root_var
        self._children(root, root_var, self.ref, root.id or "root")
        self._anon_objects(root, root_var, (), root.id or "root")

    def _children(self, obj: dom.QmlObject, parent: Value, parent_type: AnyType | None, path: str) -> None:
        counts: dict[str, int] = {}
        for child in obj.children:
            child_type = self.ts.object_type(self.ref, child)
            index = counts.get(child.type_name, 0)
            counts[child.type_name] = index + 1
            child_path = f"{path}/{child.id or f'{child.type_name}.{index}'}"
            cls = self.class_of(child_type)
            if cls is None:
                self.stub_line(child_path, f"object {child.type_name}", "object", "")
                continue
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
            self.objects.append(Emitted(var, child, child_type, child_path))
            self._children(child, var, child_type, child_path)
            self._anon_objects(child, var, child_type.path if isinstance(child_type, AnonRef) else (), child_path)

    def _anon_objects(self, obj: dom.QmlObject, var: Value, prefix: tuple[str, ...], path: str) -> None:
        """`sizes: QtObject { ... }`: create the nested object and point the property at it."""
        for b in obj.bindings:
            if b.value.obj is None or b.is_on or var.cpp != "this" and not prefix:
                continue
            anon = AnonRef(self.ref, prefix + (b.name,), b.value.obj.type_name)
            prop = self.ts.prop(var.obj, b.name)
            if prop is None or prop.object_type != anon:
                continue
            cls = self.class_of(anon)
            self.counter += 1
            name = f"o{self.counter}"
            self.body.append(f"auto* {name} = {'create' if var.cpp == 'this' else var.cpp + '->create'}<{cls}>();")
            self.body.append(f"{var.member(b.name)}.set({name});")
            sub = Value(name, f"{cls}*", anon)
            child_path = f"{path}/{b.name}"
            self.objects.append(Emitted(sub, b.value.obj, anon, child_path))
            self._children(b.value.obj, sub, anon, child_path)
            self._anon_objects(b.value.obj, sub, prefix + (b.name,), child_path)

    def id_member(self, id_name: str) -> str:
        return f"{id_name}Id" if id_name in self.view.props else id_name

    # ── Phase 2: properties ──────────────────────────────────────────────────

    def target(self, t: AnyType, name: str) -> tuple[str, str | None, str]:
        """(access, C++ type, kind) of a binding target, which may be dotted."""
        head, _, rest = name.partition(".")
        if head == "Layout" and rest in LAYOUT_ATTACHED:
            return f"layout().{rest}", LAYOUT_ATTACHED[rest], "property"
        if rest and head in GROUPS and rest in GROUPS[head]:
            access, cpp = GROUPS[head][rest]
            return access, cpp, "property"
        prop = self.ts.prop(t, name) if not rest else None
        if prop is None:
            raise Untranslatable(f"no property {name}")
        return prop.access, prop.cpp_type, prop.kind

    def scope_for(self, e: Emitted) -> Scope:
        root = self.objects[0].var
        return Scope(self.ref, e.var if e.var.cpp != "this" else root, root, ids=dict(self.ids))

    def stub_line(self, context: str, code: str, kind: str, expected: str) -> str:
        key, cpp = self.store.get(self.ref.path, context, code, expected, kind)
        if cpp is not None:
            self.translated += 1
            return cpp
        self.stubs += 1
        self.body.append(f'II_TODO_JS("{key}");  // {kind} {context}: {" ".join(code.split())[:100]}')
        return ""

    def bind_properties(self) -> None:
        for e in list(self.objects):
            self.bind_object(e)

    def bind_object(self, e: Emitted) -> None:
        if e.type is None:
            return
        aliases = {p.name for p in self.component.root.properties if p.is_alias} if e is self.objects[0] else set()
        easing: dict[str, dom.Binding] = {}
        for b in e.obj.bindings:
            if b.name in IGNORED or b.is_signal_handler or b.is_on or b.name in aliases:
                continue  # aliases are wired in alias_wiring(); handlers and `on` objects come later
            if b.value.obj is not None:
                prop = self.ts.prop(e.type, b.name)
                if prop is not None and prop.component_of is not None:
                    self.component_factory(e, b, prop.access, prop.cpp_type)
                continue
            if not b.value.script:
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
        """`x: Component { T { ... } }`: a factory lambda building the object tree and its bindings."""
        inner = b.value.obj.children[0]
        inner_type = self.ts.object_type(self.ref, inner)
        cls = self.class_of(inner_type)
        saved_body, start = self.body, len(self.objects)
        self.body = []
        self.factory_depth += 1
        self.counter += 1
        var = Value(f"c{self.counter}", f"{cls}*", inner_type)
        self.body.append(f"auto* {var.cpp} = owner.create<{cls}>();")
        if inner.id:
            self.ids[inner.id] = var
        self.objects.append(Emitted(var, inner, inner_type, f"{e.path}.{b.name}"))
        self._children(inner, var, inner_type, f"{e.path}.{b.name}")
        created = self.objects[start:]
        for sub in created:
            self.bind_object(sub)
        self.on_objects(created)
        self.body += [f"{var.cpp}->complete();", f"return {var.cpp};"]
        lines, self.body = self.body, saved_body
        del self.objects[start:]
        self.factory_depth -= 1
        self.body.append(f"{self.member_of(e.var, access)}.set({cpp_type}([=, this](Object& owner) {{")
        self.body += [f"  {line}" for line in lines]
        self.body.append("}));")

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
            filled = self.stub_line(f"{be.path}.animation", b.value.script.code, f"behavior animation ({err})", "")
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
            filled = self.stub_line(context, b.value.script.code, f"binding ({err})", cpp_type or "")
            if filled:
                self.body.append(f'{setter}bind([=, this] {{ return {filled}; }}, "{label}");')
            return
        self.translated += 1
        self.emit_assignment(setter, expr, label)

    def handlers(self) -> None:
        self.on_objects(self.objects)
        for e in self.objects:
            for b in e.obj.bindings:
                if b.is_signal_handler and b.value.script:
                    self.stub_line(f"{e.path}.{b.name}", b.value.script.code, "handler", "void")
            for m in e.obj.methods:
                if m.kind == "function" and m.body is not None:
                    self.function(e, m)

    def function(self, e: Emitted, m: dom.Method) -> None:
        """A JS function: a member function once data/js holds its signature and body."""
        from .signature import parse

        params = ", ".join(n for n, _ in m.parameters)
        context = f"{e.path}.{m.name}"
        code = f"function {m.name}({params}) {{ {m.body.code} }}"
        expected = "signature (C++ declaration) + cpp (function body statements)"
        key, body = self.store.get(self.ref.path, context, code, expected, "function")
        entry = self.store.find(self.ref.path, context) or {}
        if body is None or not entry.get("signature") or e is not self.objects[0]:
            if body is None or not entry.get("signature"):
                self.stubs += 1
                self.body.append(f'II_TODO_JS("{key}");  // function {context}')
            return
        signature = parse(entry["signature"])
        self.translated += 1
        self.methods.append(f"{signature.text};")
        self.definitions.append(f"  {signature.definition(f'{self.cls}::{m.name}')} {{")
        self.definitions += [f"    {line}" for line in body.splitlines()]
        self.definitions.append("  }")
        self.definitions.append("")

    # ── Output ───────────────────────────────────────────────────────────────

    def declared_members(self) -> list[str]:
        out = []
        for p in self.component.root.properties:
            prop = self.view.props[p.name]
            if prop.kind == "alias":
                if prop.alias_of and prop.alias_of[1] is None:
                    out.append(f"{prop.cpp_type or 'void*'} {p.name} = nullptr;  // alias {prop.alias_of[0]}")
                else:
                    target = ".".join(x for x in (prop.alias_of or ("?", "?")) if x)
                    out.append(f"Property<{prop.cpp_type or 'void'}>* {p.name} = nullptr;  // alias {target}")
            else:
                cpp = prop.cpp_type or "nlohmann::json /* TODO type */"
                out.append(f"Property<{cpp}> {p.name};")
        return out

    def alias_wiring(self) -> None:
        for p in self.component.root.properties:
            prop = self.view.props[p.name]
            if prop.kind == "alias" and prop.alias_of:
                target_id, member = prop.alias_of
                target = self.ids.get(target_id)
                if target is not None:
                    self.body.append(f"{p.name} = {f'&{target.member(member)}' if member else target.cpp};")

    def generate(self) -> tuple[str, str]:
        self.tr.used.clear()
        self.create_objects()
        self.alias_wiring()
        self.bind_properties()
        self.handlers()
        self.includes |= {generated_header(r) for r in self.tr.used}

        base = self.class_of(self.view.base) or "Object"
        rel = self.ref.path.relative_to(SHELL_ROOT)
        ns = "::".join(["ii", *namespace_parts(self.ref)])
        singleton = self.view.is_singleton
        nested_decls = self.anon_classes()

        header = [
            "#pragma once",
            f"// Generated by qml2cpp from {rel}.",
            "",
            *sorted(f'#include "{h}"' for h in self.header_includes | {"runtime/property.h"}),
            "",
            "#include <nlohmann/json.hpp>",
            "",
            "#include <string>",
            "#include <vector>",
            "",
            *sorted(f for f in self.forwards),
            "",
            f"namespace {ns} {{",
            "",
            f"  class {self.cls} : public {base} {{",
            "  public:",
            *([f"    static {self.cls}& instance();", ""] if singleton else []),
            *(f"    {line}" for line in nested_decls),
            f"    {self.cls}();",
            "",
            *(f"    {m}" for m in self.declared_members()),
            "",
            *(f"    {m}" for m in self.methods),
            *([""] if self.methods else []),
            *(f"    {m}" for m in self.members),
            "  };",
            "",
            f"}} // namespace {ns}",
            "",
        ]
        source = [
            f"// Generated by qml2cpp from {rel}.",
            f'#include "{generated_header(self.ref)}"',
            "",
            *sorted(f'#include "{h}"' for h in self.includes - {generated_header(self.ref)}),
            '#include "runtime/js.h"',
            "",
            f"namespace {ns} {{",
            "",
            *([f"  {self.cls}& {self.cls}::instance() {{",
               f"    static {self.cls} object;",
               "    object.complete();",
               "    return object;",
               "  }", ""] if singleton else []),
            f"  {self.cls}::{self.cls}() {{",
            *(f"    {line}" for line in self.body),
            "  }",
            "",
            *self.definitions,
            f"}} // namespace {ns}",
            "",
        ]
        return "\n".join(header), "\n".join(source)

    def anon_classes(self) -> list[str]:
        """Nested classes for anonymous objects: property declarations only (bindings are set by
        the component constructor, which sees every id)."""
        out: list[str] = [f"class {e.type.name};" for e in self.objects[1:] if isinstance(e.type, AnonRef)]
        if out:
            out.append("")
        for e in self.objects[1:]:
            if not isinstance(e.type, AnonRef):
                continue
            view = self.ts.view(e.type)
            base = self.class_of(view.base) or "Object"
            out.append(f"class {e.type.name} : public {base} {{")
            out.append("public:")
            for p in e.obj.properties:
                prop = view.props[p.name]
                out.append(f"  Property<{prop.cpp_type or 'nlohmann::json /* TODO type */'}> {p.name};")
            out.append("};")
            out.append("")
        return out
