// QColor's HSV/HSL accessors and Qt.hsva/hsla for a few colours, as JSON (expected/color.json).
//   QT_QPA_PLATFORM=offscreen QT_FORCE_STDERR_LOGGING=1 qml6 color_dump.qml
import QtQuick

QtObject {
    readonly property var samples: ["#ff0000", "#33aa77", "#808080", "#1d1b20", "#cfbcff", "#e6e0e9", "#000000", "#ffffff", "#ffa500", "#4a4458"]

    function r6(x) { return Math.round(x * 1e6) / 1e6 }

    Component.onCompleted: {
        const out = { "accessors": {}, "hsva": [], "hsla": [] }
        for (const s of samples) {
            const c = Qt.color(s)
            out.accessors[s] = [r6(c.hsvHue), r6(c.hsvSaturation), r6(c.hsvValue), r6(c.hslHue), r6(c.hslSaturation), r6(c.hslLightness)]
        }
        for (const [h, sat, v, a] of [[0.1, 0.5, 0.7, 1], [0.95, 0.2, 0.3, 0.5], [-1, 0, 0.4, 1], [0.5, 1, 1, 1], [0.66, 0.8, 0.2, 1]]) {
            const c1 = Qt.hsva(h, sat, v, a)
            out.hsva.push([h, sat, v, a, r6(c1.r), r6(c1.g), r6(c1.b), r6(c1.a)])
            const c2 = Qt.hsla(h, sat, v, a)
            out.hsla.push([h, sat, v, a, r6(c2.r), r6(c2.g), r6(c2.b), r6(c2.a)])
        }
        console.info("II_DUMP " + JSON.stringify(out))
        Qt.quit()
    }
}
