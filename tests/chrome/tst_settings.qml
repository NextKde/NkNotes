import QtQuick
import QtQuick.Controls
import QtTest
import NextNotes.ChromeTests

TestCase {
    id: testCase
    name: "SettingsHandoff"
    when: windowShown
    visible: true
    width: 800; height: 700
    Item { id: scene; anchors.fill: parent }
    QtObject {
        id: preferences
        property bool busy: false
        property string legacyLibrary: ""
        function preference(key, fallback) { return fallback }
        function setPreference(key, value) {}
    }
    NotesSettings {
        id: settings
        hostWindow: testCase.Window.window; controller: preferences; sourceItem: scene
    }
    SignalSpy { id: actions; target: settings; signalName: "actionRequested" }
    function cleanup() { settings.close(); tryCompare(settings, "visible", false); actions.clear() }
    function test_action_waits_until_dialog_is_closed() {
        settings.show(0)
        tryCompare(settings, "opened", true)
        const initialHeight = settings.height
        settings.section = 1
        compare(settings.height, initialHeight)
        settings.section = 2
        compare(settings.height, initialHeight)
        settings.request("templates")
        tryCompare(actions, "count", 1)
        verify(!settings.visible)
        compare(actions.signalArguments[0][0], "templates")
        compare(settings.pendingAction, "")
        settings.show(0)
        tryCompare(settings, "opened", true)
        settings.close()
        tryCompare(settings, "visible", false)
        compare(actions.count, 1)
    }
}
