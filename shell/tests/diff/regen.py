#!/usr/bin/env python3
"""Regenerate tests/diff/expected/*.json by running every case in cases/ through Qt (qml6).

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


def dump(case: Path) -> dict:
    proc = subprocess.run(
        ["qml6", str(HERE / "dump.qml"), "--", str(case)],
        env=ENV, capture_output=True, text=True, timeout=30,
    )
    for line in (proc.stdout + proc.stderr).splitlines():
        marker = line.find("II_DUMP ")
        if marker >= 0:
            return json.loads(line[marker + len("II_DUMP "):])
    raise RuntimeError(f"{case.name}: no dump\n{proc.stderr}")


def main() -> int:
    EXPECTED.mkdir(exist_ok=True)
    names = sys.argv[1:] or sorted(p.stem for p in CASES.glob("*.qml"))
    for name in names:
        data = dump(CASES / f"{name}.qml")
        (EXPECTED / f"{name}.json").write_text(json.dumps(data, indent=1, sort_keys=True) + "\n")
        print(f"{name}: {len(data)} items")
    return 0


if __name__ == "__main__":
    sys.exit(main())
