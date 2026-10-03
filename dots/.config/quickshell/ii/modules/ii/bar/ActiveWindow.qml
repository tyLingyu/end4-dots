import qs.services
import qs.modules.common
import qs.modules.common.widgets
import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Wayland
import Quickshell.Hyprland

Item {
    id: root
    readonly property HyprlandMonitor monitor: Hyprland.monitorFor(root.QsWindow.window?.screen)
    readonly property Toplevel activeWindow: ToplevelManager.activeToplevel

    property string activeWindowAddress: `0x${activeWindow?.HyprlandToplevel?.address}`
    property bool focusingThisMonitor: HyprlandData.activeWorkspace?.monitor == monitor?.name
    property var biggestWindow: HyprlandData.biggestWindowForWorkspace(HyprlandData.monitors[root.monitor?.id]?.activeWorkspace.id)

    readonly property bool showingActiveWindow: !!(root.focusingThisMonitor && root.activeWindow?.activated && root.biggestWindow)
    readonly property string appId: (root.showingActiveWindow ? root.activeWindow?.appId : root.biggestWindow?.class) ?? ""

    // Desktop entries load asynchronously, so look the entry up again once they're in
    property int desktopEntriesRevision: 0
    Connections {
        target: DesktopEntries
        function onApplicationsChanged() {
            root.desktopEntriesRevision++;
        }
    }
    readonly property var desktopEntry: {
        root.desktopEntriesRevision;
        return root.appId.length > 0 ? DesktopEntries.heuristicLookup(root.appId) : null;
    }
    // Friendly name from the desktop entry instead of a raw id like "com.example.App"
    readonly property string appName: root.appId.length > 0 ? (root.desktopEntry?.name || root.appId) : Translation.tr("Desktop")
    readonly property string appIconSource: Quickshell.iconPath(root.desktopEntry?.icon || AppSearch.guessIcon(root.appId), "application-x-executable")

    implicitWidth: rowLayout.implicitWidth

    RowLayout {
        id: rowLayout
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 10

        BackgroundlessAppIcon {
            visible: root.appId.length > 0
            Layout.alignment: Qt.AlignVCenter
            implicitSize: 26
            source: root.appIconSource
            backgroundColor: Appearance.colors.colLayer0
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: -4

            StyledText {
                Layout.fillWidth: true
                font.pixelSize: Appearance.font.pixelSize.smaller
                color: Appearance.colors.colSubtext
                elide: Text.ElideRight
                text: root.appName
            }

            StyledText {
                Layout.fillWidth: true
                font.pixelSize: Appearance.font.pixelSize.small
                color: Appearance.colors.colOnLayer0
                elide: Text.ElideRight
                text: root.showingActiveWindow ?
                    root.activeWindow?.title :
                    (root.biggestWindow?.title) ?? `${Translation.tr("Workspace")} ${monitor?.activeWorkspace?.id ?? 1}`
            }
        }
    }
}
