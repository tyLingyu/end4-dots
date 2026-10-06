#!/usr/bin/env python3
"""Regenerate tests/diff/expected/*.json by running every case in cases/ through Qt (qml6),
and every *_dump.qml script (easing, color, js: <name>_dump.qml -> expected/<name>.json).

The C++ diff test compares ii-shell's runtime against these files, so Qt itself is only
needed when a case is added or changed, not to run the test suite.
"""
import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
CASES = HERE / "cases"
EXPECTED = HERE / "expected"
ENV = {
    **os.environ,
    "QT_QPA_PLATFORM": "offscreen",
    "QT_QUICK_BACKEND": "software",
    # Qt logs to journald instead of stderr when stderr is not a terminal.
    "QT_FORCE_STDERR_LOGGING": "1",
}


def run(args: list[str], name: str) -> dict:
    proc = subprocess.run(["qml6", *args], env=ENV, capture_output=True, text=True, timeout=30)
    for line in (proc.stdout + proc.stderr).split("\n"):  # not splitlines(): U+2028 occurs in the data
        marker = line.find("II_DUMP ")
        if marker >= 0:
            return json.loads(line[marker + len("II_DUMP "):])
    raise RuntimeError(f"{name}: no dump\n{proc.stderr}")


def dump(case: Path) -> dict:
    return run([str(HERE / "dump.qml"), "--", str(case)], case.name)


def main() -> int:
    EXPECTED.mkdir(exist_ok=True)
    scripts = {p.stem.removesuffix("_dump"): p for p in HERE.glob("*_dump.qml")}
    names = sys.argv[1:] or [*sorted(p.stem for p in CASES.glob("*.qml")), *sorted(scripts)]
    for name in names:
        if name in scripts:
            data = run([str(scripts[name])], name)
            text = json.dumps(data, indent=0, ensure_ascii=False, sort_keys=True)
        else:
            data = dump(CASES / f"{name}.qml")
            text = json.dumps(data, indent=1, sort_keys=True) + "\n"
        (EXPECTED / f"{name}.json").write_text(text)
        print(f"{name}: {len(data)} items")
    return 0


if __name__ == "__main__":
    sys.exit(main())
