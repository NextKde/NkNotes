import QtQuick
import QtQuick.Window
import QtTest
import NextNotes.ChromeTests

TestCase {
    id: testCase
    name: "NotesWindowControls"
    when: windowShown
    visible: true
    width: 500; height: 200
    QtObject {
        id: target
        property int visibility: Window.Windowed
        property int closeRequests: 0
        function showMaximized() { visibility = Window.Maximized }
        function showNormal() { visibility = Window.Windowed }
        function showMinimized() { visibility = Window.Minimized }
        function close() { closeRequests++ }
    }
    Component {
        id: platformMock
        QtObject {
            property var target
            property var buttons: []
            property bool enabled: true
            property bool active: false
            property bool supported: true
            property bool hoverGlyphs: false
        }
    }
    Component { id: controlComponent; NotesWindowControls { targetWindow: target; maximizeMode: true; circleSize: 14; buttonWidth: 22; buttonHeight: 22; spacing: 6 } }
    function test_takeover_layout_and_service_loss() {
        const controls = createTemporaryObject(controlComponent, testCase, {platformComponent: platformMock})
        tryCompare(controls, "width", 78)
        compare(controls.buttonLayout.length, 3)
        for (let i = 0; i < 3; ++i) {
            const rect = controls.buttonLayout[i]
            compare(rect.action, ["maximize", "minimize", "close"][i])
            compare(rect.x, 4 + i * 28); compare(rect.y, 4)
            compare(rect.width, 14); compare(rect.height, 14)
        }
        const closeButton = findChild(controls, "windowcloseButton")
        compare(closeButton.opacity, 1)
        controls.platformSlot.active = true
        compare(closeButton.opacity, 0); compare(closeButton.enabled, false)
        compare(controls.opacity, 1); compare(controls.width, 78)
        controls.platformSlot.active = false
        compare(closeButton.opacity, 1); compare(closeButton.enabled, true)
        controls.actions = ["close", "minimize"]
        tryCompare(controls, "width", 50)
        compare(controls.platformSlot.buttons.length, 2)
        compare(controls.platformSlot.buttons[0].action, "close")
        controls.actions = ["close"]
        tryCompare(controls, "width", 22)
        compare(controls.platformSlot.buttons.length, 1)
        controls.actions = []
        tryCompare(controls, "width", 0)
        compare(controls.platformSlot.buttons.length, 0)
        controls.actions = ["maximize", "minimize", "close"]
        tryCompare(controls, "width", 78)
        controls.platformSlot.active = true
        controls.platformComponent = null
        compare(controls.platformSlot, null)
        compare(controls.externalDrawing, false)
        const maximize = findChild(controls, "windowfullscreenButton")
        mouseClick(maximize); compare(target.visibility, Window.Maximized)
        mouseClick(maximize); compare(target.visibility, Window.Windowed)
        mouseClick(findChild(controls, "windowminimizeButton")); compare(target.visibility, Window.Minimized)
        mouseClick(findChild(controls, "windowcloseButton")); compare(target.closeRequests, 1)
    }
    function test_optional_native_adapter_can_be_absent() {
        const missing = Qt.createComponent("MissingPlatformAdapter.qml")
        const controls = createTemporaryObject(controlComponent, testCase, {platformComponent: missing})
        tryCompare(controls, "width", 78)
        compare(controls.platformSlot, null)
        compare(controls.externalDrawing, false)
        verify(findChild(controls, "windowcloseButton").enabled)
    }
    function test_installed_native_adapter() {
        const controls = createTemporaryObject(controlComponent, testCase)
        tryVerify(() => controls.platformComponent.status !== Component.Loading)
        if (controls.platformComponent.status === Component.Error) {
            compare(controls.externalDrawing, false)
            console.log("Native window adapter unavailable; client fallback retained")
            return
        }
        tryVerify(() => controls.platformSlot !== null)
        tryCompare(controls, "width", 78)
        compare(controls.platformSlot.buttons.length, 3)
        compare(controls.platformSlot.target, controls)
        console.log("Native window adapter loaded; supported=" + controls.platformSlot.supported
                    + ", active=" + controls.platformSlot.active)
    }
}
