// Renders one visual case with Qt and saves it as a PNG (the reference image).
//   QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1.25 qml6 render.qml -- case.qml out.png
import QtQuick
import QtQuick.Window

Window {
    id: win
    property var args: Qt.application.arguments
    width: loader.item ? loader.item.width : 100
    height: loader.item ? loader.item.height : 100
    color: "transparent"
    visible: true

    Loader {
        id: loader
        source: Qt.resolvedUrl(win.args[win.args.length - 2])
    }

    property bool saved: false
    onFrameSwapped: {
        if (saved || !loader.item)
            return
        saved = true
        loader.item.grabToImage(result => {
            result.saveToFile(win.args[win.args.length - 1])
            Qt.quit()
        })
    }
}
