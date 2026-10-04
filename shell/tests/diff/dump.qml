// Loads one diff-test case in a real (offscreen) window and prints the geometry of every
// item that has an objectName, after the first frame (so polish/layout has run).
//
//   QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software qml6 dump.qml -- cases/foo.qml
import QtQuick
import QtQuick.Window

Window {
    id: win
    width: 800
    height: 600
    visible: true

    property string caseFile: Qt.application.arguments[Qt.application.arguments.length - 1]
    property bool dumped: false

    Loader {
        id: loader
        source: Qt.resolvedUrl(win.caseFile)
    }

    function round(v) {
        return Math.round(v * 1000) / 1000
    }

    function collect(item, root, out) {
        if (item.objectName !== "") {
            const p = item.mapToItem(root, 0, 0)
            const entry = {
                x: round(p.x),
                y: round(p.y),
                width: round(item.width),
                height: round(item.height),
                implicitWidth: round(item.implicitWidth),
                implicitHeight: round(item.implicitHeight),
                visible: item.visible,
                opacity: round(item.opacity)
            }
            out[item.objectName] = entry
        }
        for (let i = 0; i < item.children.length; ++i)
            collect(item.children[i], root, out)
    }

    onFrameSwapped: {
        if (dumped || loader.status !== Loader.Ready)
            return
        dumped = true
        const out = {}
        collect(loader.item, loader.item, out)
        console.info("II_DUMP " + JSON.stringify(out))
        Qt.quit()
    }

    Timer {
        interval: 5000
        running: true
        onTriggered: {
            console.warn("II_DUMP_ERROR timeout, loader status " + loader.status)
            Qt.exit(1)
        }
    }
}
