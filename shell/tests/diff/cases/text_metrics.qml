import QtQuick

Item {
    width: 400
    height: 400

    Text { objectName: "main15"; text: "Hello, ii-shell 你好"; font.family: "Google Sans Flex"; font.pixelSize: 15 }
    Text { objectName: "main16axes"; y: 30; text: "Volume 42%"; font.family: "Google Sans Flex"; font.pixelSize: 16; font.variableAxes: { "wght": 450, "wdth": 100 } }
    Text { objectName: "rubik13"; y: 60; text: "Rubik 13px"; font.family: "Rubik"; font.pixelSize: 13 }
    Text { objectName: "icon"; y: 90; text: "volume_up"; font.family: "Material Symbols Rounded"; font.pixelSize: 24 }
    Text { objectName: "mono"; y: 130; text: "0123456789"; font.family: "JetBrainsMono NF"; font.pixelSize: 14 }
    Text { objectName: "native"; y: 160; text: "Native rendering"; renderType: Text.NativeRendering; font.family: "Google Sans Flex"; font.pixelSize: 15 }
    Text {
        objectName: "wrapped"
        y: 190
        width: 120
        wrapMode: Text.WordWrap
        text: "A longer sentence that has to wrap onto several lines"
        font.family: "Google Sans Flex"
        font.pixelSize: 15
    }
    Text {
        objectName: "elided"
        y: 290
        width: 80
        elide: Text.ElideRight
        text: "A longer sentence that gets elided"
        font.family: "Google Sans Flex"
        font.pixelSize: 15
    }
    Text { objectName: "empty"; y: 320; text: ""; font.family: "Google Sans Flex"; font.pixelSize: 15 }
    Text { objectName: "vcenter"; y: 340; height: 40; verticalAlignment: Text.AlignVCenter; text: "Mid"; font.family: "Google Sans Flex"; font.pixelSize: 15 }
}
