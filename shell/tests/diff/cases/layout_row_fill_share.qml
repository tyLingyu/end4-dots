import QtQuick
import QtQuick.Layouts

Item {
    width: 500
    height: 50

    RowLayout {
        objectName: "row"
        anchors.fill: parent
        spacing: 0
        Rectangle { objectName: "f1"; Layout.fillWidth: true; implicitWidth: 100; implicitHeight: 10 }
        Rectangle { objectName: "f2"; Layout.fillWidth: true; implicitWidth: 20; implicitHeight: 10 }
        Rectangle { objectName: "f3"; Layout.fillWidth: true; Layout.maximumWidth: 60; implicitHeight: 10 }
        Rectangle { objectName: "fixed"; implicitWidth: 40; implicitHeight: 10 }
    }
}
