"""Typed translation of a deliberately small JavaScript subset into C++.

Anything outside the subset raises Untranslatable: the caller then emits a stub for AI to fill
(see jsstore.py). The subset is chosen so a successful translation is certainly right:
literals, property chains through known types, arithmetic, comparisons, boolean logic,
conditionals and a few Math functions, with JS semantics kept where they differ from C++.
"""
from __future__ import annotations

import json
from dataclasses import dataclass, field

from .jsast import Node
from .registry import BuiltinRef, ComponentRef
from .tsys import AnyType, Prop, TypeSystem
from .typemap import runtime_for

NUMERIC = {"double", "int"}


class Untranslatable(Exception):
    pass


@dataclass
class Value:
    cpp: str
    type: str | None  # C++ value type
    obj: AnyType | None = None  # QML type, when the value is an object
    is_ref: bool = False  # cpp is an object reference (singleton) rather than a pointer
    group: str | None = None  # `x.font`: cpp/obj are x, members are looked up in the group

    def member(self, access: str) -> str:
        return f"{self.cpp}{'.' if self.is_ref else '->'}{access}"


@dataclass
class Scope:
    file: ComponentRef  # where names are resolved (imports)
    scope_obj: Value  # the object the binding is on
    root_obj: Value  # the component's root object
    ids: dict[str, Value] = field(default_factory=dict)
    locals: dict[str, Value] = field(default_factory=dict)


def cpp_string(text: str) -> str:
    return f"std::string({json.dumps(text, ensure_ascii=False)})"


class Translator:
    def __init__(self, ts: TypeSystem):
        self.ts = ts
        # Components referenced by translated code (singletons), so the caller can include them.
        self.used: set[ComponentRef] = set()

    # ── Entry points ─────────────────────────────────────────────────────────

    def expression(self, node: Node | None, scope: Scope) -> Value:
        if node is None:
            raise Untranslatable("empty expression")
        method = getattr(self, f"_{node.kind}", None)
        if method is None:
            raise Untranslatable(node.kind)
        return method(node, scope)

    def coerce(self, value: Value, target: str) -> str:
        """`value` as a C++ expression of type `target` (QML's assignment conversions)."""
        if value.type == target:
            return value.cpp
        if target == "double" and value.type == "int":
            return value.cpp
        if target == "int" and value.type == "double":
            return f"static_cast<int>({value.cpp})"  # QML int properties truncate (ToInt32)
        if target == "std::optional<bool>" and value.type == "bool":
            return value.cpp
        if target.startswith("std::vector<") and value.type == "std::vector<>":
            return f"{target}{{}}"
        if target == "Color" and value.type == "std::string" and value.cpp.startswith("std::string("):
            return f"qmlColor({value.cpp[len('std::string('):-1]})"
        if target == "std::string" and value.type in NUMERIC:
            return f"jsString({value.cpp})"
        if target == "std::vector<js::Json>" and value.type and value.cpp.startswith(f"{value.type}{{"):
            return target + value.cpp[len(value.type):]  # an array literal for a list<var>
        if target.endswith("*") and value.type == "std::nullptr_t":
            return "nullptr"
        if target.endswith("*") and value.type and value.type.endswith("*"):
            return f"static_cast<{target}>({value.cpp})" if value.type != target else value.cpp
        raise Untranslatable(f"cannot assign {value.type} to {target}")

    # ── Literals ─────────────────────────────────────────────────────────────

    def _NumericLiteral(self, node: Node, scope: Scope) -> Value:
        text = node.attrs["value"]
        number = float(text)
        if number.is_integer() and abs(number) < 2**31:
            return Value(str(int(number)), "int")
        return Value(repr(number), "double")

    def _StringLiteral(self, node: Node, scope: Scope) -> Value:
        return Value(cpp_string(node.attrs["value"]), "std::string")

    def _TrueLiteral(self, node: Node, scope: Scope) -> Value:
        return Value("true", "bool")

    def _FalseLiteral(self, node: Node, scope: Scope) -> Value:
        return Value("false", "bool")

    def _NullExpression(self, node: Node, scope: Scope) -> Value:
        return Value("nullptr", "std::nullptr_t")

    def _ArrayPattern(self, node: Node, scope: Scope) -> Value:
        elements = [
            self.expression(element.children[0], scope)
            for item in node.children
            if item.kind == "PatternElementList"
            for element in item.children
            if element.kind == "PatternElement" and element.children
        ]
        if not elements:
            return Value("{}", "std::vector<>")  # an empty list fits any list type
        if all(e.type in NUMERIC for e in elements):
            items = [e.cpp if e.type == "double" or e.cpp.lstrip("-").isdigit() else f"static_cast<double>({e.cpp})" for e in elements]
            return Value(f"std::vector<double>{{{', '.join(items)}}}", "std::vector<double>")
        if all(e.type == "std::string" for e in elements):
            return Value(f"std::vector<std::string>{{{', '.join(e.cpp for e in elements)}}}", "std::vector<std::string>")
        raise Untranslatable("array of mixed types")

    def _NestedExpression(self, node: Node, scope: Scope) -> Value:
        inner = self.expression(node.children[0], scope)
        return Value(f"({inner.cpp})", inner.type, inner.obj, inner.is_ref)

    # ── Names ────────────────────────────────────────────────────────────────

    def _property_value(self, base: Value, prop: Prop) -> Value:
        if prop.kind == "group":
            return Value(base.cpp, None, base.obj, base.is_ref, group=prop.name)
        if prop.kind == "value":  # computed by a method: `parent.left` -> parent->leftLine()
            return Value(base.member(prop.access), prop.cpp_type)
        if prop.kind == "alias" and prop.cpp_type is not None:
            if prop.alias_of and prop.alias_of[1] is None:  # alias to an object
                return Value(base.member(prop.access), prop.cpp_type, prop.object_type)
            return Value(f"{base.member(prop.access)}->get()", prop.cpp_type, prop.object_type)
        if prop.kind != "property" or prop.cpp_type is None:
            raise Untranslatable(f"property {prop.name} ({prop.kind}) not readable yet")
        return Value(f"{base.member(prop.access)}.get()", prop.cpp_type, prop.object_type)

    def _IdentifierExpression(self, node: Node, scope: Scope) -> Value:
        name = node.attrs["name"]
        if name in scope.locals:
            return scope.locals[name]
        if name in scope.ids:
            return scope.ids[name]
        for obj in (scope.scope_obj, scope.root_obj):
            if obj.obj is not None and (prop := self.ts.prop(obj.obj, name)) is not None:
                return self._property_value(obj, prop)
        ref = self.ts.resolve(scope.file, name)
        if ref is not None and self.ts.view(ref).is_singleton:
            if isinstance(ref, ComponentRef):
                self.used.add(ref)
                return Value(f"::{self.ts.view(ref).cpp_class}::instance()", None, ref, is_ref=True)
            if isinstance(ref, BuiltinRef) and (runtime := runtime_for(ref)) is not None:
                self.used.add(ref)  # Quickshell, Hyprland, Pipewire: compat singletons
                return Value(f"{runtime.cpp}::instance()", None, ref, is_ref=True)
        raise Untranslatable(f"name {name}")

    def _FieldMemberExpression(self, node: Node, scope: Scope) -> Value:
        if node.attrs.get("dotToken") == "?.":
            raise Untranslatable("optional chaining")
        name = node.attrs["name"]
        base_node = node.children[0]
        enum = self._enum(base_node, name, scope)
        if enum is not None:
            return enum
        base = self.expression(base_node, scope)
        if base.group is not None:
            member = self.ts.view(base.obj).groups.get(base.group, {}).get(name)
            if member is None:
                raise Untranslatable(f"member {base.group}.{name}")
            return Value(f"{base.member(member[0])}.get()", member[1])
        if base.obj is None:
            raise Untranslatable(f"member {name} of {base.type}")
        prop = self.ts.prop(base.obj, name)
        if prop is None:
            raise Untranslatable(f"unknown member {name}")
        return self._property_value(base, prop)

    # QML enums the runtime knows.
    ENUMS = {
        ("Text", "AlignLeft"): ("Align::Left", "int"),
        ("Text", "AlignRight"): ("Align::Right", "int"),
        ("Text", "AlignHCenter"): ("Align::HCenter", "int"),
        ("Text", "AlignTop"): ("Align::Top", "int"),
        ("Text", "AlignBottom"): ("Align::Bottom", "int"),
        ("Text", "AlignVCenter"): ("Align::VCenter", "int"),
        ("Qt", "AlignLeft"): ("Align::Left", "int"),
        ("Qt", "AlignRight"): ("Align::Right", "int"),
        ("Qt", "AlignHCenter"): ("Align::HCenter", "int"),
        ("Qt", "AlignTop"): ("Align::Top", "int"),
        ("Qt", "AlignBottom"): ("Align::Bottom", "int"),
        ("Qt", "AlignVCenter"): ("Align::VCenter", "int"),
        ("Qt", "AlignCenter"): ("Align::Center", "int"),
        ("Text", "NoWrap"): ("WrapMode::NoWrap", "WrapMode"),
        ("Text", "WordWrap"): ("WrapMode::WordWrap", "WrapMode"),
        ("Text", "WrapAnywhere"): ("WrapMode::WrapAnywhere", "WrapMode"),
        ("Text", "Wrap"): ("WrapMode::Wrap", "WrapMode"),
        ("Text", "ElideNone"): ("Elide::None", "Elide"),
        ("Text", "ElideLeft"): ("Elide::Left", "Elide"),
        ("Text", "ElideMiddle"): ("Elide::Middle", "Elide"),
        ("Text", "ElideRight"): ("Elide::Right", "Elide"),
        # QFont::Weight
        ("Font", "Thin"): ("100", "int"),
        ("Font", "ExtraLight"): ("200", "int"),
        ("Font", "Light"): ("300", "int"),
        ("Font", "Normal"): ("400", "int"),
        ("Font", "Medium"): ("500", "int"),
        ("Font", "DemiBold"): ("600", "int"),
        ("Font", "Bold"): ("700", "int"),
        ("Font", "ExtraBold"): ("800", "int"),
        ("Font", "Black"): ("900", "int"),
    }

    def _enum(self, base: Node, name: str, scope: Scope) -> Value | None:
        if base.kind != "IdentifierExpression":
            return None
        hit = self.ENUMS.get((base.attrs["name"], name))
        if hit:
            return Value(hit[0], hit[1])
        # Quickshell's enums are types holding `enum Enum`: WlrLayer.Overlay -> qs::WlrLayer::Overlay.
        ref = self.ts.resolve(scope.file, base.attrs["name"]) if base.attrs["name"] not in scope.ids else None
        if isinstance(ref, BuiltinRef):
            info = self.ts.registry.builtin_info(ref)
            if name in info.enums.get("Enum", []):
                cls = "qs::" + info.cpp_name.rsplit("::", 1)[-1]
                return Value(f"{cls}::{name}", cls)
        if base.attrs["name"] == "Easing":
            # QEasingCurve::Type is sequential, so its position in .qmltypes is its value.
            easing = self.ts.registry.builtins.by_cpp.get("QQmlEasing")
            values = easing.enums.get("Type", []) if easing else []
            if name in values:
                return Value(str(values.index(name)), "int")
        return None

    # ── Operators ────────────────────────────────────────────────────────────

    @staticmethod
    def truthy(value: Value) -> str:
        if value.type == "bool":
            return value.cpp
        if value.type in NUMERIC:
            return f"({value.cpp} != 0)"
        if value.type == "std::string":
            return f"!{value.cpp}.empty()"
        if value.type and value.type.endswith("*"):
            return f"({value.cpp} != nullptr)"
        raise Untranslatable(f"truthiness of {value.type}")

    def _NotExpression(self, node: Node, scope: Scope) -> Value:
        operand = self.expression(node.children[0], scope)
        return Value(f"!{self.truthy(operand)}", "bool")

    def _UnaryMinusExpression(self, node: Node, scope: Scope) -> Value:
        operand = self.expression(node.children[0], scope)
        if operand.type not in NUMERIC:
            raise Untranslatable("unary minus")
        return Value(f"(-{operand.cpp})", operand.type)

    def _BinaryExpression(self, node: Node, scope: Scope) -> Value:
        op = node.attrs.get("operatorToken", "")
        left = self.expression(node.children[0], scope)
        right = self.expression(node.children[1], scope)
        lt, rt = left.type, right.type
        if op in ("+", "-", "*", "/", "%"):
            if op == "+" and lt == "std::string" and rt == "std::string":
                return Value(f"({left.cpp} + {right.cpp})", "std::string")
            if lt in NUMERIC and rt in NUMERIC:
                if op == "/" or (op == "%" and "double" in (lt, rt)):
                    if op == "%":
                        return Value(f"std::fmod({left.cpp}, {right.cpp})", "double")
                    return Value(f"(static_cast<double>({left.cpp}) / {right.cpp})", "double")
                result = "int" if lt == rt == "int" else "double"
                return Value(f"({left.cpp} {op} {right.cpp})", result)
            raise Untranslatable(f"{lt} {op} {rt}")
        if op in ("===", "==", "!==", "!=", "<", ">", "<=", ">="):
            comparable = (lt in NUMERIC and rt in NUMERIC) or lt == rt or (
                lt and rt and lt.endswith("*") and rt in ("std::nullptr_t",) or (lt == "std::nullptr_t" and rt and rt.endswith("*"))
            )
            if not comparable or (op in ("<", ">", "<=", ">=") and lt not in NUMERIC and lt != "std::string"):
                raise Untranslatable(f"compare {lt} {op} {rt}")
            cpp_op = {"===": "==", "!==": "!="}.get(op, op)
            return Value(f"({left.cpp} {cpp_op} {right.cpp})", "bool")
        if op in ("&&", "||"):
            # JS returns an operand, not a bool; only both-bool is a plain C++ && / ||.
            if lt == rt == "bool":
                return Value(f"({left.cpp} {op} {right.cpp})", "bool")
            raise Untranslatable(f"{lt} {op} {rt}")
        raise Untranslatable(f"operator {op}")

    def _ConditionalExpression(self, node: Node, scope: Scope) -> Value:
        cond = self.expression(node.children[0], scope)
        yes = self.expression(node.children[1], scope)
        no = self.expression(node.children[2], scope)
        if yes.type in NUMERIC and no.type in NUMERIC:
            result = "int" if yes.type == no.type == "int" else "double"
            return Value(f"({self.truthy(cond)} ? {yes.cpp} : {no.cpp})", result)
        if yes.type != no.type or yes.type is None:
            raise Untranslatable(f"conditional {yes.type} : {no.type}")
        return Value(f"({self.truthy(cond)} ? {yes.cpp} : {no.cpp})", yes.type, yes.obj if yes.obj == no.obj else None)

    # ── Calls ────────────────────────────────────────────────────────────────

    @staticmethod
    def arguments(call: Node) -> list[Node]:
        """Argument expressions of a call (QQmlJS nests ArgumentList as a linked list)."""
        out: list[Node] = []

        def flatten(node: Node):
            for child in node.children:
                if child.kind == "ArgumentList":
                    flatten(child)
                else:
                    out.append(child)

        for child in call.children[1:]:
            if child.kind == "ArgumentList":
                flatten(child)
        return out

    # Methods of built-in types the runtime provides with the same name and no arguments.
    BUILTIN_METHODS = {"start", "stop", "restart", "complete"}

    MATH = {
        "round": "jsRound",  # JS rounds .5 towards +Infinity; std::round rounds away from zero
        "floor": "std::floor",
        "ceil": "std::ceil",
        "abs": "std::fabs",
        "sqrt": "std::sqrt",
        "pow": "std::pow",
        "min": "std::min",
        "max": "std::max",
        "sin": "std::sin",
        "cos": "std::cos",
    }

    # Expressions without side effects: extra arguments made of these can be dropped, as JS ignores them.
    PURE = {"IdentifierExpression", "FieldMemberExpression", "NumericLiteral", "StringLiteral", "TrueLiteral",
            "FalseLiteral", "NullExpression", "NestedExpression", "UnaryMinusExpression", "NotExpression",
            "ConditionalExpression"}

    ASSIGNMENTS = {"=", "+=", "-=", "*=", "/=", "%=", "**=", "&=", "|=", "^=", "<<=", ">>=", ">>>=", "&&=", "||=", "??="}

    def _pure(self, node: Node) -> bool:
        return all(n.kind in self.PURE or n.kind == "BinaryExpression" and n.attrs.get("operatorToken") not in self.ASSIGNMENTS
                   for n in node.walk())

    def _CallExpression(self, node: Node, scope: Scope) -> Value:
        callee = node.children[0]
        arg_nodes = self.arguments(node)
        args = [self.expression(a, scope) for a in arg_nodes]
        if (
            callee.kind == "FieldMemberExpression"
            and callee.children[0].kind == "IdentifierExpression"
            and callee.children[0].attrs["name"] == "Math"
            and callee.attrs["name"] in self.MATH
        ):
            if any(a.type not in NUMERIC for a in args):
                raise Untranslatable("Math on non-numbers")
            fn = self.MATH[callee.attrs["name"]]
            cast = [a.cpp if a.type == "double" else f"static_cast<double>({a.cpp})" for a in args]
            return Value(f"{fn}({', '.join(cast)})", "double")
        # A component's own JS function, once translated (its signature is in data/js).
        if callee.kind == "FieldMemberExpression":
            name = callee.attrs["name"]
            base = self.expression(callee.children[0], scope)
            target = base.member(name)
        elif callee.kind == "IdentifierExpression":
            name = callee.attrs["name"]
            base = scope.root_obj
            target = f"this->{name}" if base.cpp == "this" else base.member(name)
        else:
            raise Untranslatable("call")
        if base.obj is not None and name in self.BUILTIN_METHODS and not args and not isinstance(base.obj, ComponentRef):
            return Value(f"{target}()", "void")
        signal = self.ts.signal(base.obj, name) if base.obj is not None else None
        if signal is not None and self.ts.function(base.obj, name) is None:
            if len(args) != len(signal) or any(t is None for _, t in signal):
                raise Untranslatable(f"emit {name}")
            coerced = [self.coerce(a, t) for a, (_, t) in zip(args, signal)]
            return Value(f"{target}.emit({', '.join(coerced)})", "void")
        signature = self.ts.function(base.obj, name) if base.obj is not None else None
        if signature is None and base.obj is not None:
            signature = self.ts.builtin_method(base.obj, name, len(args))
        if signature is None:
            raise Untranslatable(f"call {name}")
        if len(args) > len(signature.params) and all(self._pure(a) for a in arg_nodes[len(signature.params):]):
            args = args[: len(signature.params)]  # JS ignores extra arguments
        if not signature.required <= len(args) <= len(signature.params):
            raise Untranslatable(f"call {name} with {len(args)} arguments")
        coerced = [self.coerce(a, signature.params[i][0].removeprefix("const ").removesuffix("&").strip()) for i, a in enumerate(args)]
        return Value(f"{target}({', '.join(coerced)})", signature.returns)

    # ── Statements ───────────────────────────────────────────────────────────

    def statements(self, node: Node | None, scope: Scope, returns: str | None = None) -> list[str]:
        """C++ statements for a JS statement list, a block, or a bare expression (a handler body)."""
        if node is None:
            return []
        kind = node.kind
        if kind in ("StatementList", "Block", "SourceElements"):
            out: list[str] = []
            for child in node.children:
                out += self.statements(child, scope, returns)
            return out
        if kind == "EmptyStatement":
            return []
        if kind == "LabelledStatement":
            # `onPressed: f()` inside a function is a label, not a handler; unused unless jumped to.
            if any(n.kind in ("BreakStatement", "ContinueStatement") and n.attrs.get("label") for n in node.walk()):
                raise Untranslatable("labelled jump")
            return self.statements(node.children[-1], scope, returns)
        if kind == "ExpressionStatement":
            return [self.expression_statement(node.children[0], scope)]
        if kind in ("BinaryExpression", "CallExpression"):
            return [self.expression_statement(node, scope)]
        if kind == "IfStatement":
            cond = self.truthy(self.expression(node.children[0], scope))
            out = [f"if ({cond}) {{", *(f"  {line}" for line in self.statements(node.children[1], scope, returns)), "}"]
            if len(node.children) > 2:
                out[-1] = "} else {"
                out += [*(f"  {line}" for line in self.statements(node.children[2], scope, returns)), "}"]
            return out
        if kind == "ReturnStatement":
            if not node.children:
                return ["return;"]
            value = self.expression(node.children[0], scope)
            return [f"return {self.coerce(value, returns) if returns else value.cpp};"]
        if kind == "VariableStatement":
            out = []
            for decl in node.children[0].children:
                if decl.kind != "PatternElement" or not decl.children:
                    raise Untranslatable("variable declaration")
                value = self.expression(decl.children[0], scope)
                if value.type in (None, "void", "std::nullptr_t", "std::vector<>"):
                    raise Untranslatable(f"variable of type {value.type}")
                name = decl.attrs["bindingIdentifier"]
                out.append(f"auto {name} = {value.cpp};")
                scope.locals[name] = Value(name, value.type, value.obj, value.is_ref)
            return out
        raise Untranslatable(kind)

    def expression_statement(self, node: Node, scope: Scope) -> str:
        if node.kind == "BinaryExpression" and node.attrs.get("operatorToken") == "=":
            setter, cpp_type = self.lvalue(node.children[0], scope)
            value = self.expression(node.children[1], scope)
            return f"{setter}.set({self.coerce(value, cpp_type)});"
        if node.kind == "CallExpression":
            return f"{self.expression(node, scope).cpp};"
        raise Untranslatable(f"statement {node.kind}")

    def lvalue(self, node: Node, scope: Scope) -> tuple[str, str]:
        """(property expression, C++ type) of an assignment target."""
        if node.kind == "FieldMemberExpression":
            base = self.expression(node.children[0], scope)
            prop = self.ts.prop(base.obj, node.attrs["name"]) if base.obj is not None else None
        elif node.kind == "IdentifierExpression":
            name = node.attrs["name"]
            base = prop = None
            for obj in (scope.scope_obj, scope.root_obj):
                if obj.obj is not None and (prop := self.ts.prop(obj.obj, name)) is not None:
                    base = obj
                    break
        else:
            raise Untranslatable("assignment target")
        if prop is None or prop.kind != "property" or prop.cpp_type is None or base is None:
            raise Untranslatable("assignment to unknown property")
        return (prop.access if base.cpp == "this" else base.member(prop.access)), prop.cpp_type

