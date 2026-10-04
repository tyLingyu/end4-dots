import QtQuick

Item {
    width: 400
    height: 300

    Rectangle {
        objectName: "fill"
        anchors.fill: parent
        anchors.margins: 10
    }
    Rectangle {
        objectName: "center"
        width: 51
        height: 31
        anchors.centerIn: parent
    }
}
