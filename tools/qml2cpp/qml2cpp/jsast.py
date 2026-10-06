"""Qt's own JavaScript AST for an expression, from qmldom's `astRelocatableDump`.

qmldom also exports a structured `scriptElement` tree, but it drops operators (every binary
operator is "operation 127", `?.` looks like `.`), so the translator reads the relocatable
dump instead: QQmlJS::AST printed as `<Node attr="value">children</Node>`, with every token.
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field

_OPEN = re.compile(r'<(\w+)((?:\s+\w+="(?:[^"\\]|\\.)*")*)\s*>')
_ATTR = re.compile(r'(\w+)="((?:[^"\\]|\\.)*)"')
_CLOSE = re.compile(r"</(\w+)>")


@dataclass
class Node:
    kind: str  # QQmlJS::AST class name without namespace: "BinaryExpression", "FieldMemberExpression", ...
    attrs: dict[str, str] = field(default_factory=dict)
    children: list["Node"] = field(default_factory=list)

    def walk(self):
        yield self
        for child in self.children:
            yield from child.walk()

    def __repr__(self) -> str:
        inner = ", ".join(repr(c) for c in self.children)
        attrs = {k: v for k, v in self.attrs.items() if not k.endswith("Token")}
        return f"{self.kind}{attrs if attrs else ''}({inner})"


def _unescape(value: str) -> str:
    return re.sub(r"\\(.)", lambda m: {"n": "\n", "t": "\t"}.get(m.group(1), m.group(1)), value)


def parse(dump: str) -> Node | None:
    """Parse one relocatable dump; None for an empty dump."""
    stack: list[Node] = [Node("Root")]
    pos = 0
    while pos < len(dump):
        if dump[pos].isspace():
            pos += 1
            continue
        if m := _OPEN.match(dump, pos):
            node = Node(m.group(1), {k: _unescape(v) for k, v in _ATTR.findall(m.group(2))})
            stack[-1].children.append(node)
            stack.append(node)
            pos = m.end()
        elif m := _CLOSE.match(dump, pos):
            assert stack[-1].kind == m.group(1), (stack[-1].kind, m.group(1))
            stack.pop()
            pos = m.end()
        else:
            raise ValueError(f"jsast: unexpected {dump[pos:pos + 40]!r}")
    root = stack[0]
    _fold_templates(root)
    return root.children[0] if root.children else None


def _fold_templates(node: Node) -> None:
    """qmldom prints a template literal's chain (`a${x}b${y}c`) as sibling TemplateLiteral
    elements, each with its text and the expression after it. Fold each run into one
    TemplateString node whose children alternate TemplateChunk(value) and expressions."""
    for child in node.children:
        _fold_templates(child)
    folded: list[Node] = []
    i = 0
    while i < len(node.children):
        child = node.children[i]
        if child.kind != "TemplateLiteral" or not child.attrs.get("literalToken", "").startswith("`"):
            folded.append(child)
            i += 1
            continue
        template = Node("TemplateString")
        while True:
            part = node.children[i]
            template.children.append(Node("TemplateChunk", {"value": part.attrs.get("value", "")}))
            template.children += part.children
            i += 1
            token = part.attrs.get("literalToken", "")
            if token.endswith("`") and len(token) > 1 or i >= len(node.children):
                break
            if node.children[i].kind != "TemplateLiteral" or not node.children[i].attrs.get("literalToken", "").startswith("}"):
                break
        folded.append(template)
    node.children = folded


def free_identifiers(node: Node | None) -> set[str]:
    """Identifiers used as values (not member names): `Audio` and `x` in `Audio.sink + x`."""
    if node is None:
        return set()
    return {n.attrs.get("name", "") for n in node.walk() if n.kind == "IdentifierExpression"}
