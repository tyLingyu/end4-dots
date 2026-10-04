// Samples Qt's easing curves (QEasingCurve, as used by QML animations) at 41 progress points
// through AnimationController, and prints them as JSON for tests/diff/expected/easing.json.
//   QT_QPA_PLATFORM=offscreen QT_FORCE_STDERR_LOGGING=1 qml6 easing_dump.qml
import QtQuick

Item {
    id: root
    property real v: 0

    // ii's curves (modules/common/Appearance.qml animationCurves) plus the named types it uses.
    readonly property var curves: ({
        "expressiveFastSpatial": [0.42, 1.67, 0.21, 0.90, 1, 1],
        "expressiveDefaultSpatial": [0.38, 1.21, 0.22, 1.00, 1, 1],
        "expressiveSlowSpatial": [0.39, 1.29, 0.35, 0.98, 1, 1],
        "expressiveEffects": [0.34, 0.80, 0.34, 1.00, 1, 1],
        "emphasized": [0.05, 0, 2 / 15, 0.06, 1 / 6, 0.4, 5 / 24, 0.82, 0.25, 1, 1, 1],
        "emphasizedFirstHalf": [0.05, 0, 2 / 15, 0.06, 1 / 6, 0.4, 5 / 24, 0.82],
        "emphasizedLastHalf": [5 / 24, 0.82, 0.25, 1, 1, 1],
        "emphasizedAccel": [0.3, 0, 0.8, 0.15, 1, 1],
        "emphasizedDecel": [0.05, 0.7, 0.1, 1, 1, 1],
        "standard": [0.2, 0, 0, 1, 1, 1],
        "standardAccel": [0.3, 0, 1, 1, 1, 1],
        "standardDecel": [0, 0, 0, 1, 1, 1]
    })
    readonly property var types: ({
        "Linear": Easing.Linear,
        "InQuad": Easing.InQuad, "OutQuad": Easing.OutQuad, "InOutQuad": Easing.InOutQuad,
        "InCubic": Easing.InCubic, "OutCubic": Easing.OutCubic, "InOutCubic": Easing.InOutCubic,
        "InSine": Easing.InSine, "OutSine": Easing.OutSine, "InOutSine": Easing.InOutSine,
        "InExpo": Easing.InExpo, "OutExpo": Easing.OutExpo, "InOutExpo": Easing.InOutExpo,
        "OutBack": Easing.OutBack
    })

    NumberAnimation { id: anim; target: root; property: "v"; from: 0; to: 1; duration: 1000 }
    AnimationController { id: controller; animation: anim }

    // A fresh animation given ii's malformed 8-value curve (not a multiple of 6), as settings.qml does.
    NumberAnimation {
        id: fresh
        target: root; property: "v"; from: 0; to: 1; duration: 1000
        easing.type: Easing.BezierSpline
        easing.bezierCurve: root.curves.emphasizedFirstHalf
    }
    AnimationController { id: freshController; animation: fresh }

    function sample() {
        controller.reload()
        const values = []
        for (let i = 0; i <= 40; ++i) {
            controller.progress = i / 40
            values.push(Math.round(root.v * 1e9) / 1e9)
        }
        controller.progress = 0
        return values
    }

    Component.onCompleted: {
        const out = {}
        for (const name in types) {
            anim.easing.type = types[name]
            out[name] = sample()
        }
        anim.easing.type = Easing.BezierSpline
        for (const name in curves) {
            anim.easing.bezierCurve = curves[name]
            out["bezier:" + name] = sample()
        }
        const freshValues = []
        for (let i = 0; i <= 40; ++i) {
            freshController.progress = i / 40
            freshValues.push(Math.round(root.v * 1e9) / 1e9)
        }
        out["fresh:emphasizedFirstHalf"] = freshValues
        console.info("II_DUMP " + JSON.stringify(out))
        Qt.quit()
    }
}
