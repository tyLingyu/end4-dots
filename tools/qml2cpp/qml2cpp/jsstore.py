"""Translations of JavaScript the translator cannot do mechanically, kept outside the generated code.

data/js/<qml path>.json maps a stable key to {code, context, kind, form, expected, cpp}. The generator uses
`cpp` when present and otherwise emits a stub (a compile error) and records the entry with
`cpp: null` for AI (or a person) to fill. Regenerating never loses filled translations.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

from .registry import SHELL_ROOT

STORE = Path(__file__).resolve().parent.parent / "data" / "js"


class JsStore:
    def __init__(self, root: Path = STORE):
        self.root = root
        self._files: dict[str, dict] = {}
        self._used: dict[str, set[str]] = {}

    def _rel(self, qml: Path) -> str:
        return str(qml.relative_to(SHELL_ROOT))

    def _data(self, rel: str) -> dict:
        if rel not in self._files:
            path = self.root / f"{rel}.json"
            self._files[rel] = json.loads(path.read_text()) if path.exists() else {}
        return self._files[rel]

    @staticmethod
    def key(context: str, code: str) -> str:
        return hashlib.sha1(f"{context}\0{code}".encode()).hexdigest()[:12]

    def get(self, qml: Path, context: str, code: str, expected: str, kind: str, form: str = "statements") -> tuple[str, str | None]:
        """(key, translation or None). Records the entry so it shows up in the to-do list."""
        rel = self._rel(qml)
        key = self.key(context, code)
        entry = self._data(rel).setdefault(key, {"cpp": None})
        entry.update({"kind": kind, "form": form, "context": context, "expected": expected, "code": code})
        self._used.setdefault(rel, set()).add(key)
        return key, entry.get("cpp")

    def find(self, qml: Path, context: str) -> dict | None:
        """The entry recorded for a context (e.g. a function `root.mix`), if any."""
        for entry in self._data(self._rel(qml)).values():
            if entry.get("context") == context:
                return entry
        return None

    def fill(self, qml: Path, translations: dict[str, dict]) -> list[str]:
        """Store translations given by context ({context: {"cpp": ..., "signature": ...}}).
        Returns the contexts that matched no recorded entry."""
        data = self._data(self._rel(qml))
        unknown = []
        for context, fields in translations.items():
            entry = next((e for e in data.values() if e.get("context") == context), None)
            if entry is None:
                unknown.append(context)
                continue
            entry.update({k: v for k, v in fields.items() if k in ("cpp", "signature")})
            self._used.setdefault(self._rel(qml), set())
        return unknown

    def save(self) -> None:
        for rel, data in self._files.items():
            if rel not in self._used:
                continue  # only touched, never regenerated in this run: leave the file as it is
            used = self._used.get(rel, set())
            # Drop entries no longer produced, unless they hold a translation (kept for reference).
            kept = {k: v for k, v in sorted(data.items()) if k in used or v.get("cpp")}
            path = self.root / f"{rel}.json"
            if not kept:
                path.unlink(missing_ok=True)
                continue
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(kept, indent=1, ensure_ascii=False) + "\n")

    def pending(self) -> dict[str, list[dict]]:
        out: dict[str, list[dict]] = {}
        for rel, keys in self._used.items():
            entries = [dict(self._files[rel][k], key=k) for k in sorted(keys) if not self._files[rel][k].get("cpp")]
            if entries:
                out[rel] = entries
        return out
