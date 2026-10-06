"""List every `property var` and `property list<var>` in the ii QML tree as JSON (input for var
type inference). For a list<var> the inferred cpp_type is the whole list type (std::vector<T>)."""
import json, re, subprocess, sys, os
root = subprocess.check_output(["git", "rev-parse", "--show-toplevel"], cwd=os.path.dirname(os.path.abspath(__file__)), text=True).strip()
base = "dots/.config/quickshell/ii/"
files = subprocess.check_output(["git", "ls-files", base + "*.qml"], cwd=root, text=True).split()
pat = re.compile(r'^(\s*)((?:readonly\s+|required\s+|default\s+)*)property\s+(var|list<var>)\s+(\w+)\s*(?::\s*(.*)|;?\s*(?://.*)?)$')
out = []
for f in files:
    lines = open(os.path.join(root, f), encoding="utf-8").read().split("\n")
    for i, l in enumerate(lines):
        m = pat.match(l)
        if not m: continue
        init = (m.group(5) or "").strip()
        # pull in continuation lines for multi-line initializers (brace/bracket balance)
        depth = init.count("{") + init.count("[") + init.count("(") - init.count("}") - init.count("]") - init.count(")")
        j = i
        while depth > 0 and j + 1 < len(lines) and j - i < 12:
            j += 1
            seg = lines[j].strip()
            init += " " + seg
            depth += seg.count("{") + seg.count("[") + seg.count("(") - seg.count("}") - seg.count("]") - seg.count(")")
        out.append({"id": len(out), "file": f[len(base):], "line": i + 1, "name": m.group(4), "declared": m.group(3),
                    "modifiers": m.group(2).split(), "init": init[:400]})
json.dump(out, open(sys.argv[1], "w"), ensure_ascii=False, indent=1)
print(len(out), "entries in", len({e["file"] for e in out}), "files")
