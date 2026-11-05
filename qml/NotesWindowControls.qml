import QtQuick
import Kos.Ui

// Like NkMail, load the host protocol optionally and retain client controls until
// takeover is acknowledged. The pinned Kos.Ui exposes the actual button layout.
KosWindowControls {
    id: root
    property Component platformComponent: Qt.createComponent("qrc:/nextnotes/platform/NativeWindowControls.qml")
    property var platformSlot: null
    readonly property bool compositorActive: platformSlot !== null && platformSlot.active
    externalDrawing: compositorActive
    hoverGlyphs: platformSlot && platformSlot.supported ? platformSlot.hoverGlyphs : true

    function connectPlatform() {
        if (platformSlot) {
            platformSlot.destroy()
            platformSlot = null
        }
        if (platformComponent && platformComponent.status === Component.Ready)
            platformSlot = platformComponent.createObject(root, {
                target: root,
                buttons: Qt.binding(() => root.buttonLayout),
                enabled: Qt.binding(() => root.maximizeMode || root.actions.indexOf("maximize") < 0)
            })
    }
    onPlatformComponentChanged: connectPlatform()
    Component.onCompleted: connectPlatform()
    Connections {
        target: root.platformComponent
        function onStatusChanged() { root.connectPlatform() }
    }
}
