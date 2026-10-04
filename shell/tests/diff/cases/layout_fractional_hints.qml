import QtQuick
import QtQuick.Layouts

Item {
    width: 300
    height: 100

    RowLayout {
        objectName: "row"
        width: 200
        height: 30
        spacing: 3
        Rectangle { objectName: "a"; implicitWidth: 10.2; implicitHeight: 7.3 }
        Rectangle { objectName: "b"; Layout.fillWidth: true; implicitWidth: 20.5; Layout.maximumWidth: 60.4; implicitHeight: 5 }
        Rectangle { objectName: "c"; Layout.fillWidth: true; implicitWidth: 0.4; implicitHeight: 5 }
    }
    RowLayout {
        objectName: "shrink"
        y: 40
        width: 30
        height: 20
        spacing: 0
        Rectangle { objectName: "s1"; Layout.fillWidth: true; implicitWidth: 25.5; Layout.minimumWidth: 10.3; implicitHeight: 5 }
        Rectangle { objectName: "s2"; Layout.fillWidth: true; implicitWidth: 25.5; Layout.minimumWidth: 5.6; implicitHeight: 5 }
    }
    ColumnLayout {
        objectName: "col"
        y: 70
        spacing: 0
        Rectangle { objectName: "k1"; implicitWidth: 12.1; implicitHeight: 3.2 }
        Rectangle { objectName: "k2"; implicitWidth: 7.9; implicitHeight: 4.5 }
    }
}
