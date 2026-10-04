// Stage-1 visual check: an ii OSD-like panel built only from what the runtime supports so far
// (Rectangle, Text, RowLayout, anchors). ii-shell --demo builds the same scene in C++.
import QtQuick
import QtQuick.Layouts

Item {
    width: 300
    height: 72

    Rectangle {
        objectName: "panel"
        anchors.fill: parent
        anchors.margins: 6
        radius: 30
        color: "#211f26"

        RowLayout {
            objectName: "row"
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 20
            spacing: 12

            Text {
                objectName: "icon"
                text: "volume_up"
                font.family: "Material Symbols Rounded"
                font.pixelSize: 26
                color: "#cfbcff"
            }
            Rectangle {
                objectName: "track"
                Layout.fillWidth: true
                implicitHeight: 12
                radius: 6
                color: "#4a4458"
                Rectangle {
                    objectName: "fill"
                    width: parent.width * 0.42
                    height: parent.height
                    radius: 6
                    color: "#cfbcff"
                }
            }
            Text {
                objectName: "value"
                text: "42"
                font.family: "Google Sans Flex"
                font.pixelSize: 16
                font.variableAxes: { "wght": 450, "wdth": 100 }
                color: "#e6e0e9"
            }
        }
    }
}
