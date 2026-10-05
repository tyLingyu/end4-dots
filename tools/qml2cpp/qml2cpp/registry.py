"""Name resolution across ii's QML: which file or built-in type a name used in a file refers to."""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from . import dom
from .qmltypes import BuiltinTypes, TypeInfo

REPO = Path(__file__).resolve().parents[3]
SHELL_ROOT = REPO / "dots/.config/quickshell/ii"


@dataclass(frozen=True)
class ComponentRef:
    """A QML component defined in ii: a file, or an inline component inside one."""

    path: Path
    inline: str | None = None

    @property
    def name(self) -> str:
        return self.inline or self.path.stem

    def __str__(self) -> str:
        rel = self.path.relative_to(SHELL_ROOT)
        return f"{rel}::{self.inline}" if self.inline else str(rel)


@dataclass(frozen=True)
class BuiltinRef:
    module: str
    name: str
    cpp_name: str

    def __str__(self) -> str:
        return f"{self.module}/{self.name}"


TypeRef = ComponentRef | BuiltinRef


class Registry:
    def __init__(self, root: Path = SHELL_ROOT):
        self.root = root
        self.builtins = BuiltinTypes()
        self.files: dict[Path, dom.QmlFile] = {}

    def file(self, path: Path) -> dom.QmlFile:
        path = path.resolve()
        if path not in self.files:
            self.files[path] = dom.load(path)
        return self.files[path]

    def module_dir(self, uri: str) -> Path | None:
        """`qs.modules.common` -> <shell>/modules/common (quickshell maps `qs` to the shell root)."""
        if uri == "qs":
            return self.root
        if uri.startswith("qs."):
            return self.root / uri[3:].replace(".", "/")
        return None

    def resolve_type(self, path: Path, name: str) -> TypeRef | None:
        """What a type name written in `path` refers to, following QML's lookup order."""
        qml = self.file(path)
        if "." in name:
            # `Foo.Bar`: an inline component of a component in scope.
            outer, _, inner = name.partition(".")
            ref = self.resolve_type(path, outer)
            if isinstance(ref, ComponentRef) and inner in self.file(ref.path).inline_components:
                return ComponentRef(ref.path, inner)
            return None
        if name in qml.inline_components:
            return ComponentRef(path.resolve(), name)
        for imp in qml.imports:
            directory = Path(imp.uri) if imp.is_directory else self.module_dir(imp.uri)
            if directory is not None:
                candidate = directory / f"{name}.qml"
                if candidate.exists():
                    return ComponentRef(candidate.resolve())
                continue
            info = self.builtins.lookup(imp.uri, name)
            if info is not None:
                return BuiltinRef(imp.uri, name, info.cpp_name)
        return None

    def builtin_info(self, ref: BuiltinRef) -> TypeInfo:
        return self.builtins.by_cpp[ref.cpp_name]

    def component(self, ref: ComponentRef) -> dom.Component:
        qml = self.file(ref.path)
        return qml.inline_components[ref.inline] if ref.inline else qml.component

    def is_singleton(self, ref: TypeRef) -> bool:
        if isinstance(ref, ComponentRef):
            return ref.inline is None and self.file(ref.path).is_singleton
        return self.builtin_info(ref).is_singleton
