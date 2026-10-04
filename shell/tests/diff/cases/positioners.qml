import QtQuick

Item {
    width: 300
    height: 300

    Row {
        objectName: "row"
        x: 5
        spacing: 3
        padding: 2
        Rectangle { objectName: "r1"; width: 10; height: 20 }
        Rectangle { objectName: "r2"; width: 15; height: 5; visible: false }
        Rectangle { objectName: "r3"; width: 20; height: 8 }
    }
    Column {
        objectName: "col"
        y: 50
        spacing: 4
        Rectangle { objectName: "k1"; width: 30; height: 10 }
        Rectangle { objectName: "k2"; width: 12; height: 11 }
    }
}
