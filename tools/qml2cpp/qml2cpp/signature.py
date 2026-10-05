"""C++ function signatures as written in data/js function entries: "Color mix(Color a, double p = 0.5)"."""
from __future__ import annotations

import re
from dataclasses import dataclass


@dataclass
class Signature:
    returns: str
    name: str
    params: list[tuple[str, str, str | None]]  # (type, name, default)
    text: str

    @property
    def required(self) -> int:
        return sum(1 for _, _, default in self.params if default is None)

    def definition(self, qualified: str) -> str:
        """The out-of-class definition head: defaults dropped, name qualified."""
        params = ", ".join(f"{t} {n}" for t, n, _ in self.params)
        return f"{self.returns} {qualified}({params})"


def _split_top(text: str) -> list[str]:
    parts, depth, current = [], 0, ""
    for ch in text:
        if ch in "<([{":
            depth += 1
        elif ch in ">)]}":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(current.strip())
            current = ""
        else:
            current += ch
    if current.strip():
        parts.append(current.strip())
    return parts


def parse(text: str) -> Signature:
    m = re.fullmatch(r"\s*(.+?)\s+(\w+)\s*\((.*)\)\s*(?:const)?\s*", text, re.S)
    if not m:
        raise ValueError(f"bad signature: {text!r}")
    returns, name, params_text = m.groups()
    params = []
    for part in _split_top(params_text):
        decl, _, default = part.partition("=")
        decl = decl.strip()
        pm = re.fullmatch(r"(.+?)\s*([A-Za-z_]\w*)", decl)
        if not pm:
            raise ValueError(f"bad parameter {part!r} in {text!r}")
        params.append((pm.group(1).strip(), pm.group(2), default.strip() or None if default else None))
    return Signature(returns.strip(), name, params, text.strip())
