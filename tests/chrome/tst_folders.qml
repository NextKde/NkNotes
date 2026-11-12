import QtQuick
import QtQuick.Controls
import QtTest
import NextNotes.ChromeTests

TestCase {
    id: testCase
    name: "FolderBrowserViews"
    when: windowShown
    visible: true
    width: 900; height: 700
    QtObject {
        id: organization
        property bool folderSelectionMode: false
        readonly property bool selectionMode: folderSelectionMode
        property var selectedFolderPaths: []
        function beginSelection(kind) { folderSelectionMode = kind === "folders" }
        function selectAll() { selectedFolderPaths = browser.model.slice() }
        function toggleFolder(path) { selectedFolderPaths = selectedFolderPaths.includes(path) ? selectedFolderPaths.filter(p => p !== path) : selectedFolderPaths.concat([path]) }
        function clearSelection() { folderSelectionMode = false; selectedFolderPaths = [] }
    }
    FolderBrowser {
        id: browser
        width: 440; height: 400
        organization: organization
        model: Array.from({length: 8}, (_, i) => "Parent/" + i)
        delegate: ItemDelegate {
            required property string modelData
            required property int index
            width: browser.cellWidth; height: browser.cellHeight
            function activate() { testCase.activatedPath = modelData }
            function openContext() { testCase.contextPath = modelData }
            onActiveFocusChanged: if (activeFocus) { browser.currentIndex = index; browser.cursorKey = modelData }
        }
    }
    property string activatedPath: ""
    property string contextPath: ""
    function press(key, modifiers) {
        const event = {key: key, modifiers: modifiers || Qt.NoModifier, accepted: false, isAutoRepeat: false}
        browser.handleKey(event)
        verify(event.accepted)
    }
    function init() {
        organization.clearSelection()
        browser.viewMode = "list"
        browser.currentIndex = 0
        browser.cursorKey = browser.keyAt(0)
        activatedPath = ""; contextPath = ""
        wait(20)
    }
    function test_switch_preserves_cursor_and_selection() {
        browser.currentIndex = 2
        browser.cursorKey = browser.keyAt(2)
        organization.beginSelection("folders")
        organization.toggleFolder("Parent/1")
        browser.viewMode = "gallery"
        wait(20)
        compare(browser.currentIndex, 2)
        compare(browser.columns, 2)
        press(Qt.Key_Down)
        compare(browser.currentIndex, 4)
        press(Qt.Key_Right)
        compare(browser.currentIndex, 5)
        compare(organization.selectedFolderPaths.join(), "Parent/1")
        browser.viewMode = "list"
        wait(20)
        compare(browser.currentIndex, 5)
        press(Qt.Key_Up)
        compare(browser.currentIndex, 4)
        press(Qt.Key_Return)
        compare(activatedPath, "Parent/4")
        press(Qt.Key_Menu)
        compare(contextPath, "Parent/4")
    }
    function test_keyboard_selection_in_gallery() {
        browser.viewMode = "gallery"
        wait(20)
        press(Qt.Key_Space, Qt.ControlModifier)
        verify(organization.folderSelectionMode)
        compare(organization.selectedFolderPaths.join(), "Parent/0")
        press(Qt.Key_A, Qt.ControlModifier)
        compare(organization.selectedFolderPaths.length, 8)
        press(Qt.Key_Escape)
        verify(!organization.selectionMode)
    }
}
