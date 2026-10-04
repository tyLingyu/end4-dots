import QtQuick
import QtQuick.Layouts

Item {
    width: 400
    height: 300

    RowLayout {
        objectName: "fixedOnly"
        width: 300
        height: 20
        Rectangle { objectName: "x1"; implicitWidth: 50; implicitHeight: 10 }
        Rectangle { objectName: "x2"; implicitWidth: 50; implicitHeight: 10 }
    }
    RowLayout {
        objectName: "shrinkFill"
        y: 30
        width: 100
        height: 20
        spacing: 0
        Rectangle { objectName: "sf1"; Layout.fillWidth: true; implicitWidth: 80; Layout.minimumWidth: 20; implicitHeight: 10 }
        Rectangle { objectName: "sf2"; Layout.fillWidth: true; implicitWidth: 60; Layout.minimumWidth: 40; implicitHeight: 10 }
    }
    RowLayout {
        objectName: "belowMin"
        y: 60
        width: 30
        height: 20
        spacing: 0
        Rectangle { objectName: "bm1"; Layout.fillWidth: true; implicitWidth: 80; Layout.minimumWidth: 20; implicitHeight: 10 }
        Rectangle { objectName: "bm2"; Layout.fillWidth: true; implicitWidth: 60; Layout.minimumWidth: 40; implicitHeight: 10 }
    }
    RowLayout {
        objectName: "maxHit"
        y: 90
        width: 300
        height: 20
        spacing: 0
        Rectangle { objectName: "mh1"; Layout.fillWidth: true; implicitWidth: 10; Layout.maximumWidth: 30; implicitHeight: 10 }
        Rectangle { objectName: "mh2"; Layout.fillWidth: true; implicitWidth: 10; implicitHeight: 10 }
        Rectangle { objectName: "mh3"; Layout.fillWidth: true; implicitWidth: 20; implicitHeight: 10 }
    }
    RowLayout {
        objectName: "allMax"
        y: 120
        width: 300
        height: 20
        spacing: 0
        Rectangle { objectName: "am1"; Layout.fillWidth: true; implicitWidth: 10; Layout.maximumWidth: 40; implicitHeight: 10 }
        Rectangle { objectName: "am2"; implicitWidth: 25; implicitHeight: 10 }
    }
    RowLayout {
        objectName: "zeros"
        y: 150
        width: 301
        height: 20
        spacing: 0
        Rectangle { objectName: "z1"; Layout.fillWidth: true; implicitHeight: 10 }
        Rectangle { objectName: "z2"; Layout.fillWidth: true; implicitHeight: 10 }
        Rectangle { objectName: "z3"; Layout.fillWidth: true; Layout.preferredWidth: 1; implicitHeight: 10 }
    }
    ColumnLayout {
        objectName: "crossFill"
        y: 180
        width: 100
        height: 60
        Rectangle { objectName: "cf1"; Layout.fillWidth: true; Layout.maximumWidth: 70; implicitHeight: 10 }
        Rectangle { objectName: "cf2"; implicitWidth: 31; implicitHeight: 10; Layout.alignment: Qt.AlignLeft }
        Rectangle { objectName: "cf3"; implicitWidth: 31; implicitHeight: 10 }
    }
}
