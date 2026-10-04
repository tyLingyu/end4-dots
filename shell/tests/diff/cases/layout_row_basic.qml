import QtQuick
import QtQuick.Layouts

Item {
    width: 400
    height: 100

    RowLayout {
        objectName: "row"
        anchors.fill: parent
        spacing: 10
        Rectangle { objectName: "a"; implicitWidth: 50; implicitHeight: 20 }
        Rectangle { objectName: "b"; Layout.fillWidth: true; implicitHeight: 30 }
        Rectangle { objectName: "c"; implicitWidth: 30; Layout.fillHeight: true }
    }
}
