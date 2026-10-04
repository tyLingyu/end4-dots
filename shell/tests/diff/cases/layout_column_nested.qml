import QtQuick
import QtQuick.Layouts

Item {
    width: 300
    height: 300

    ColumnLayout {
        objectName: "col"
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4
        RowLayout {
            objectName: "header"
            Layout.fillWidth: true
            Rectangle { objectName: "icon"; implicitWidth: 24; implicitHeight: 24 }
            Rectangle { objectName: "title"; Layout.fillWidth: true; implicitHeight: 16 }
        }
        Rectangle { objectName: "body"; Layout.fillWidth: true; Layout.fillHeight: true }
        Rectangle { objectName: "footer"; implicitWidth: 50; implicitHeight: 20; Layout.alignment: Qt.AlignRight }
        Rectangle { objectName: "centered"; implicitWidth: 51; implicitHeight: 20; Layout.alignment: Qt.AlignHCenter }
    }
}
