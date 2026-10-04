import QtQuick

Item {
    width: 200
    height: 200

    Item { objectName: "implicitOnly"; implicitWidth: 40; implicitHeight: 25 }
    Item { objectName: "explicitWins"; implicitWidth: 40; implicitHeight: 25; width: 10 }
    Item {
        objectName: "hidden"
        visible: false
        width: 5
        height: 5
        Item { objectName: "hiddenChild"; x: 1; y: 2; width: 3; height: 3 }
    }
    Item {
        objectName: "faded"
        x: 50
        y: 60
        opacity: 0.5
        Item { objectName: "fadedChild"; x: 7; y: 8; width: 1; height: 1 }
    }
}
