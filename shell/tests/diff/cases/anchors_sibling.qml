import QtQuick

Item {
    width: 300
    height: 200

    Rectangle { id: a; objectName: "a"; x: 20; y: 30; width: 50; height: 40 }
    Rectangle {
        objectName: "rightOfA"
        anchors.left: a.right
        anchors.leftMargin: 5
        anchors.top: a.top
        width: 30
        height: 30
    }
    Rectangle {
        objectName: "belowA"
        anchors.top: a.bottom
        anchors.horizontalCenter: a.horizontalCenter
        width: 21
        height: 10
    }
    Rectangle {
        objectName: "stretch"
        anchors.left: a.right
        anchors.right: parent.right
        anchors.rightMargin: 7
        y: 100
        height: 5
    }
    Rectangle {
        objectName: "bottomRight"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 4
        anchors.bottomMargin: 9
        width: 10
        height: 10
    }
    Rectangle {
        objectName: "offsetCenter"
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: 12.5
        anchors.verticalCenterOffset: -3
        width: 20
        height: 20
    }
    Rectangle {
        objectName: "vcenterOnly"
        x: 5
        anchors.verticalCenter: parent.verticalCenter
        width: 8
        height: 13
    }
}
