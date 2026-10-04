import QtQuick
import QtQuick.Layouts

Item {
    width: 150
    height: 50

    RowLayout {
        objectName: "row"
        anchors.fill: parent
        spacing: 5
        Rectangle { objectName: "s1"; implicitWidth: 100; Layout.minimumWidth: 40; implicitHeight: 10 }
        Rectangle { objectName: "s2"; implicitWidth: 100; Layout.minimumWidth: 10; implicitHeight: 10 }
    }
}
