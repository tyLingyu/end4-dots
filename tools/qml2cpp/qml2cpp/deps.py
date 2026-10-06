"""Which components and built-in types a set of QML files needs, transitively."""
from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

from . import dom, jsast
from .registry import BuiltinRef, ComponentRef, Registry, TypeRef


# JavaScript globals, not QML types.
JS_GLOBALS = {"Math", "JSON", "Number", "String", "Array", "Object", "Date", "Set", "Map", "Boolean", "RegExp",
              "Promise", "Error", "Infinity", "NaN", "Symbol", "Intl", "parseInt", "parseFloat", "isNaN"}


def _identifiers(ast) -> set[str]:
    """Capitalised free identifiers: candidates for types, singletons and enum owners."""
    return {name for name in jsast.free_identifiers(ast) if name[:1].isupper() and name not in JS_GLOBALS}


def _scripts(obj: dom.QmlObject):
    for b in obj.bindings:
        if b.value.script is not None:
            yield b.value.script
    for m in obj.methods:
        if m.body is not None:
            yield m.body


def _objects(obj: dom.QmlObject):
    """The object, its children and every object nested in its bindings."""
    yield obj
    for child in obj.children:
        yield from _objects(child)
    for b in obj.bindings:
        if b.value.obj is not None:
            yield from _objects(b.value.obj)
        for o in b.value.objects:
            yield from _objects(o)


@dataclass
class Closure:
    components: list[ComponentRef] = field(default_factory=list)
    builtins: dict[BuiltinRef, int] = field(default_factory=lambda: defaultdict(int))
    singletons: set[TypeRef] = field(default_factory=set)
    unresolved: dict[str, set[str]] = field(default_factory=lambda: defaultdict(set))


def closure(registry: Registry, roots: list[Path]) -> Closure:
    result = Closure()
    pending: list[ComponentRef] = [ComponentRef(p.resolve()) for p in roots]
    seen: set[ComponentRef] = set()
    while pending:
        ref = pending.pop(0)
        if ref in seen:
            continue
        seen.add(ref)
        result.components.append(ref)
        component = registry.component(ref)
        names: set[str] = set()
        for obj in _objects(component.root):
            if not obj.is_group:
                names.add(obj.type_name)
            for script in _scripts(obj):
                names |= _identifiers(script.ast)
            for b in obj.bindings:
                head = b.name.split(".")[0]
                if head[:1].isupper():
                    names.add(head)  # attached types: Layout.fillWidth, WlrLayershell.layer
        for name in sorted(names):
            target = registry.resolve_type(ref.path, name)
            if target is None:
                result.unresolved[name].add(str(ref))
            elif isinstance(target, BuiltinRef):
                result.builtins[target] += 1
                if registry.is_singleton(target):
                    result.singletons.add(target)
            else:
                if registry.is_singleton(target):
                    result.singletons.add(target)
                pending.append(target)
    return result
