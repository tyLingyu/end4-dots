import QtQuick
import QtQuick.Layouts

Item {
    width: 300
    height: 100

    RowLayout {
        objectName: "row"
        spacing: 0
        Rectangle { objectName: "explicitW"; width: 30; height: 12 }
        Rectangle { objectName: "both"; implicitWidth: 10; width: 40; implicitHeight: 8 }
        Rectangle { objectName: "plain"; implicitWidth: 15; implicitHeight: 9 }
    }
}
