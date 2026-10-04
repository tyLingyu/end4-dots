import QtQuick
import QtQuick.Layouts

Item {
    width: 300
    height: 200

    RowLayout {
        objectName: "row"
        x: 3
        y: 4
        spacing: 7
        Rectangle { objectName: "a"; implicitWidth: 20; implicitHeight: 40 }
        Rectangle { objectName: "top"; implicitWidth: 10; implicitHeight: 10; Layout.alignment: Qt.AlignTop }
        Rectangle { objectName: "mid"; implicitWidth: 10; implicitHeight: 11 }
        Rectangle { objectName: "bottom"; implicitWidth: 10; implicitHeight: 10; Layout.alignment: Qt.AlignBottom }
        Rectangle { objectName: "hiddenOne"; implicitWidth: 99; implicitHeight: 99; visible: false }
        Rectangle { objectName: "pref"; implicitWidth: 10; Layout.preferredWidth: 33; implicitHeight: 5 }
        Rectangle { objectName: "margined"; implicitWidth: 10; implicitHeight: 10; Layout.leftMargin: 4; Layout.topMargin: 6 }
    }
}
