import QtQuick

Item {
    width: 401
    height: 301

    Rectangle { objectName: "c1"; width: 50; height: 30; anchors.centerIn: parent }
    Rectangle { objectName: "c2"; width: 50.5; height: 30.4; anchors.centerIn: parent }
    Item {
        objectName: "inner"
        x: 10.5
        y: 3
        width: 99
        height: 77
        Rectangle { objectName: "c3"; width: 10; height: 9; anchors.centerIn: parent }
        Rectangle {
            objectName: "c4"
            width: 10
            height: 9
            anchors.centerIn: parent
            anchors.alignWhenCentered: false
        }
    }
}
