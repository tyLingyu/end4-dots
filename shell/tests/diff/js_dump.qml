// JavaScript built-ins as Qt's JS engine evaluates them, as JSON (expected/js.json); runtime/js.h
// must agree on every case (tests/runtime/js_test.cpp). Numbers are compared through String().
//   QT_QPA_PLATFORM=offscreen QT_FORCE_STDERR_LOGGING=1 qml6 js_dump.qml
import QtQuick

QtObject {
    readonly property var numbers: [0, -0, 1, -1, 0.1, 0.5, 1.5, 2.5, -2.5, 123.456, 1e21, 1.5e21, 123456789012345680000,
        1e-6, 1e-7, 1.5e-7, 0.000001234, 100, 1e300, -1e-300, 5e-324, 0.49999999999999994, 1.005, 2.675, 1.45,
        8.345, 0.05, 9.995, 99.5, 0.3 - 0.1, 1 / 3, 4503599627370497, NaN, Infinity, -Infinity]
    readonly property var numberStrings: ["", "  ", " 12 ", "12px", "1e3", "-1.5", "+.5", "5.", ".", "e5", "0x1F", "0X1f",
        "-0x10", "0b101", "0o17", "0x", "Infinity", "-Infinity", "infinity", "1_000", " 12 ", "\n42\t", "1e",
        "1e+", "007", "-0", "3.14abc", "  -12.7xyz", "0.0000001", "123456789012345678901234567890"]
    readonly property var strings: ["", "hello", "  padded\t", " ﻿nbsp ", "héllo wörld", "emoji 😀 here",
        "a,b,,c", "path/to/file.mp4", "Line1\nLine2\r\nLine3", "MiXeD CaSe ÄÖÜ", "aaa", "$&$$x"]

    function s(x) { return String(x) }

    // Lone surrogates (from slicing inside a surrogate pair) have no UTF-8 form: U+FFFD, as
    // js::toUtf8 writes them.
    function utf8able(x) {
        if (Array.isArray(x))
            return x.map(utf8able)
        if (typeof x !== "string")
            return x
        let out = ""
        for (let i = 0; i < x.length; ++i) {
            const c = x.charCodeAt(i)
            if (c >= 0xD800 && c <= 0xDBFF && i + 1 < x.length && x.charCodeAt(i + 1) >= 0xDC00 && x.charCodeAt(i + 1) <= 0xDFFF) {
                out += x[i] + x[i + 1]
                ++i
            } else {
                out += c >= 0xD800 && c <= 0xDFFF ? "\ufffd" : x[i]
            }
        }
        return out
    }

    Component.onCompleted: {
        const cases = []
        const add = (op, args, result) => cases.push([op, args, result === undefined ? "undefined" : utf8able(result)])
        for (const x of numbers) {
            add("toString", [s(x)], s(x))
            add("round", [s(x)], s(Math.round(x)))
            for (const d of [0, 1, 2, 3])
                add("toFixed", [s(x), d], x.toFixed(d))
        }
        for (const [x, r] of [[255, 16], [-255, 16], [5, 2], [35, 36], [0, 16], [1e15, 36]])
            add("toStringRadix", [s(x), r], x.toString(r))
        for (const t of numberStrings) {
            add("Number", [t], s(Number(t)))
            add("parseInt", [t], s(parseInt(t)))
            add("parseFloat", [t], s(parseFloat(t)))
        }
        for (const [t, r] of [["ff", 16], ["0xff", 16], ["0xff", 10], ["777", 8], ["z", 36], ["12", 1], ["101", 2], ["9", 8]])
            add("parseIntRadix", [t, r], s(parseInt(t, r)))
        for (const t of strings) {
            add("length", [t], s(t.length))
            add("trim", [t], t.trim())
            add("toLowerCase", [t], t.toLowerCase())
            add("toUpperCase", [t], t.toUpperCase())
            for (const [a, b] of [[0, 3], [2, -1], [-3, 100], [5, 2], [-100, 2]]) {
                add("slice", [t, a, b], t.slice(a, b))
                add("substring", [t, a, b], t.substring(a, b))
                add("substr", [t, a, b], t.substr(a, b))
            }
            add("slice1", [t, 2], t.slice(2))
            for (const i of [0, 1, 7, -1, 100]) {
                add("charAt", [t, i], t.charAt(i))
                add("charCodeAt", [t, i], s(t.charCodeAt(i)))
            }
            for (const needle of ["", "l", "o w", "😀", ",", "\n", "a"]) {
                add("indexOf", [t, needle], s(t.indexOf(needle)))
                add("indexOfFrom", [t, needle, 3], s(t.indexOf(needle, 3)))
                add("lastIndexOf", [t, needle], s(t.lastIndexOf(needle)))
                add("includes", [t, needle], t.includes(needle))
                add("startsWith", [t, needle], t.startsWith(needle))
                add("endsWith", [t, needle], t.endsWith(needle))
                add("split", [t, needle], t.split(needle))
                add("splitLimit", [t, needle, 2], t.split(needle, 2))
                add("replace", [t, needle, "[$&]"], t.replace(needle, "[$&]"))
            }
            for (const [n, f] of [[10, " "], [12, "ab"], [3, "x"], [8, ""]]) {
                add("padStart", [t, n, f], t.padStart(n, f))
                add("padEnd", [t, n, f], t.padEnd(n, f))
            }
            add("repeat", [t, 3], t.repeat(3))
        }
        const regexCases = [
            ["a1b22c333", "\\d+", ""], ["a1b22c333", "\\d+", "g"], ["Hello World", "o", "g"], ["Hello World", "WORLD", "i"],
            ["key=value; k2=v2", "(\\w+)=(\\w+)", ""], ["key=value; k2=v2", "(\\w+)=(\\w+)", "g"], ["abc", "", "g"],
            ["a, b ,c", "\\s*,\\s*", ""], ["one  two\tthree", "\\s+", ""], ["x(y)?z", "(y)?z", ""], ["line1\nline2", "^line", "gm"],
            ["Display 1\nDisplay 2", "Display (\\d)", "g"], ["", "x", ""], ["", "", ""], ["2024-01-05", "(\\d+)-(\\d+)", ""],
            ["file:///home/user", "^file://", ""], ["aaa", "a*?", "g"], ["test", "^\\d+$", ""], ["12345", "^\\d+$", ""],
            // Found while translating ii: ^ with g, non-ASCII classes, JS \s and ., a literal {, UTF-16 indices.
            ["(a) (b) x", "^ *\\([^)]*\\) *", "g"], ["【歌名】标题【x】", "【[^】]*】", "g"], ["a\u00a0b\u3000c d", "\\s", "g"],
            ["a\u00a0b\u3000c d", "\\S+", "g"], ["a\u2028b", "a.b", ""], ["a\nb a-b", "a.b", "g"], ["x{1}y{22}", "{(\\d+)}", "g"],
            ["😀a😀ab", "a", "g"], ["word boundary", "\\bb", "g"], ["Ünïcödé", "[^a-z]", "g"], ["ab{", "b{", ""],
            ["曲名 - 歌手 [MV]", "\\s*\\[[^\\]]*\\]", "g"]
        ]
        for (const [t, p, f] of regexCases) {
            const re = new RegExp(p, f)
            add("reTest", [t, p, f], new RegExp(p, f).test(t))
            add("reSearch", [t, p, f], s(t.search(re)))
            const m = t.match(new RegExp(p, f))
            add("reMatch", [t, p, f], m === null ? null : Array.from(m, x => x === undefined ? "" : x))
            add("reReplace", [t, p, f, "<$1|$&>"], t.replace(new RegExp(p, f), "<$1|$&>"))
            add("reReplaceFn", [t, p, f], t.replace(new RegExp(p, f), (...a) => "{" + a[0].length + "}"))
            add("reSplit", [t, p, f], t.split(new RegExp(p, f)).map(x => x === undefined ? "" : x))
            add("reSplitLimit", [t, p, f, 2], t.split(new RegExp(p, f), 2).map(x => x === undefined ? "" : x))
            const execRe = new RegExp(p, f)
            const found = []
            let em
            for (let guard = 0; (em = execRe.exec(t)) !== null && guard < 50; ++guard) {
                found.push([String(em.index), String(execRe.lastIndex), Array.from(em, x => x === undefined ? "" : x)])
                if (!execRe.global)
                    break
                if (em[0].length === 0)
                    execRe.lastIndex++
            }
            add("reExecAll", [t, p, f], found)
        }
        const jsonCases = ['{"b":1,"a":[1,2.5,{"c":null}],"s":"q\\"u\\n\\u0001é"}', '[]', '{}', '[[],{}]', '1.0', '1e21',
            '0.1', '-0', '{"x":{"y":{"z":[true,false]}}}', '"tab\\there"', '123456789', '1.5e-7']
        for (const text of jsonCases) {
            for (const indent of [0, 2, 4])
                add("stringify", [text, indent], JSON.stringify(JSON.parse(text), null, indent))
        }
        const keys = [3, 1, 2, 1, 3, 2, 1]
        const sorted = keys.map((k, i) => [k, i]).sort((a, b) => a[0] - b[0]).map(p => p[1])
        add("stableSort", [keys.map(s)], sorted.map(s))
        add("joinNumbers", [[s(1), s(2.5), s(-0), s(1e21)]], [1, 2.5, -0, 1e21].join("|"))
        console.info("II_DUMP " + JSON.stringify({ "cases": cases }))
        Qt.quit()
    }
}
