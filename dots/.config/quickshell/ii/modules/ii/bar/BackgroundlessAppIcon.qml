import QtQuick
import Qt5Compat.GraphicalEffects
import Quickshell.Widgets

/**
 * Full-color app icon without the solid "tile" many icons are drawn on
 * (a colored rounded square with a white glyph): the glyph takes the tile's color instead.
 * Other icons, or ones whose glyph would get lost on the bar, are shown as is.
 */
Item {
    id: root
    required property string source
    property real implicitSize: 26
    property color backgroundColor: "white" // What the icon sits on
    property color tileColor: "transparent"
    property bool tileRemoved: false

    implicitWidth: implicitSize
    implicitHeight: implicitSize

    onBackgroundColorChanged: analyzer.requestPaint()

    IconImage {
        id: icon
        anchors.fill: parent
        source: root.source
        visible: !root.tileRemoved
    }

    // Maps the tile to the background and white to the tile color, so antialiased edges stay smooth.
    // Alpha goes 0 -> 1 on both ends to leave transparency untouched.
    LevelAdjust {
        anchors.fill: icon
        source: icon
        visible: root.tileRemoved
        minimumInput: Qt.rgba(root.tileColor.r, root.tileColor.g, root.tileColor.b, 0)
        maximumInput: Qt.rgba(1, 1, 1, 1)
        minimumOutput: Qt.rgba(root.backgroundColor.r, root.backgroundColor.g, root.backgroundColor.b, 0)
        maximumOutput: Qt.rgba(root.tileColor.r, root.tileColor.g, root.tileColor.b, 1)
    }

    // Only used to read the icon's pixels
    Canvas {
        id: analyzer
        anchors.fill: parent
        opacity: 0
        property string loadedSource: ""

        function reload() {
            if (loadedSource.length > 0)
                unloadImage(loadedSource);
            root.tileRemoved = false;
            loadedSource = root.source;
            if (loadedSource.length === 0)
                return;
            if (isImageLoaded(loadedSource))
                requestPaint();
            else
                loadImage(loadedSource);
        }

        Component.onCompleted: reload()
        Connections {
            target: root
            function onSourceChanged() {
                analyzer.reload();
            }
        }
        onImageLoaded: requestPaint()

        onPaint: {
            if (loadedSource.length === 0 || !isImageLoaded(loadedSource)) {
                root.tileRemoved = false;
                return;
            }
            const tile = root.findWhiteGlyphTile(getContext("2d").createImageData(loadedSource));
            if (tile)
                root.tileColor = Qt.rgba(tile[0] / 255, tile[1] / 255, tile[2] / 255, 1);
            root.tileRemoved = tile !== null;
        }
    }

    function luminance(c) {
        return (0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]) / 255;
    }

    function distance(a, b) {
        return Math.sqrt((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2);
    }

    // Returns the tile color as [r, g, b] if the icon is a white glyph on a solid tile, otherwise null
    function findWhiteGlyphTile(image) {
        const d = image.data;
        const w = image.width;
        const h = image.height;
        if (w < 8 || h < 8)
            return null;
        const at = (x, y) => (Math.round(y) * w + Math.round(x)) * 4;

        // Walk inward from the edge midpoints and corners; just past the first solid pixel should be the tile
        const starts = [[0, h / 2, 1, 0], [w - 1, h / 2, -1, 0], [w / 2, 0, 0, 1], [w / 2, h - 1, 0, -1],
            [0, 0, 1, 1], [w - 1, 0, -1, 1], [0, h - 1, 1, -1], [w - 1, h - 1, -1, -1]];
        const step = Math.max(1, Math.round(w / 50));
        const probes = [];
        for (const [x0, y0, dx, dy] of starts) {
            for (let s = 0; s < w * 0.35; s++) {
                if (d[at(x0 + dx * s, y0 + dy * s) + 3] < 240)
                    continue;
                const j = at(x0 + dx * (s + step), y0 + dy * (s + step));
                if (d[j + 3] >= 240)
                    probes.push([d[j], d[j + 1], d[j + 2]]);
                break;
            }
        }
        // Most probes must agree on one solid color
        let agreeing = [];
        for (const p of probes) {
            const group = probes.filter(q => distance(p, q) < 40);
            if (group.length > agreeing.length)
                agreeing = group;
        }
        if (agreeing.length < 6)
            return null;
        const tile = [0, 1, 2].map(k => Math.round(agreeing.reduce((sum, p) => sum + p[k], 0) / agreeing.length));

        // The tile must cover most of the icon, with a white glyph on top of it
        let opaque = 0, tileLike = 0, glyphCount = 0, glyphR = 0, glyphG = 0, glyphB = 0;
        for (let i = 0; i < d.length; i += 4) {
            if (d[i + 3] < 128)
                continue;
            opaque++;
            const dr = d[i] - tile[0], dg = d[i + 1] - tile[1], db = d[i + 2] - tile[2];
            const distSq = dr * dr + dg * dg + db * db;
            if (distSq < 40 * 40) {
                tileLike++;
            } else if (distSq > 120 * 120 && d[i + 3] > 200) {
                glyphCount++;
                glyphR += d[i];
                glyphG += d[i + 1];
                glyphB += d[i + 2];
            }
        }
        if (opaque < w * h * 0.5 || tileLike < opaque * 0.4 || glyphCount < opaque * 0.03)
            return null;

        const glyph = [glyphR / glyphCount, glyphG / glyphCount, glyphB / glyphCount];
        const glyphIsWhite = Math.min(...glyph) > 190 && Math.max(...glyph) - Math.min(...glyph) < 50;
        if (!glyphIsWhite || distance(tile, [255, 255, 255]) < 100)
            return null;
        // The recolored glyph has to stand out from the bar
        const bg = [root.backgroundColor.r * 255, root.backgroundColor.g * 255, root.backgroundColor.b * 255];
        if (Math.abs(luminance(tile) - luminance(bg)) < 0.25)
            return null;
        return tile;
    }
}
