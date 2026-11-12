import QtQuick
import QtQuick.Controls
import QtTest
import NextNotes.ChromeTests

TestCase {
    id: testCase
    name: "NoteBrowserViews"
    when: windowShown
    visible: true
    width: 900; height: 700
    QtObject {
        id: controller
        property var notes: Array.from({length: 12}, (_, i) => ({id: String(i), path: i + ".md"}))
        readonly property var noteModel: notes
    }
    QtObject {
        id: organization
        property string selectionAnchor: ""
        property var selectedPaths: []
        readonly property bool selectionMode: selectedPaths.length > 0
        function selectAll() { selectedPaths = controller.notes.map(note => note.path) }
        function toggle(path) { selectedPaths = selectedPaths.includes(path) ? selectedPaths.filter(p => p !== path) : selectedPaths.concat([path]) }
        function clearSelection() { selectedPaths = [] }
        function click(note, index, modifiers) {
            const start = controller.notes.findIndex(n => n.path === selectionAnchor)
            selectedPaths = controller.notes.slice(Math.min(start, index), Math.max(start, index) + 1).map(n => n.path)
        }
    }
    NotesBrowser {
        id: browser
        width: 720; height: 500
        controller: controller; organization: organization
        noteDelegate: Component {
            ItemDelegate {
                required property var modelData
                required property int index
                width: browser.gallery ? browser.activeView.cellWidth - 12 : browser.width
                height: browser.gallery ? 104 : 60
                function activate() {}
            }
        }
    }
    function press(key, modifiers) {
        const event = {key: key, modifiers: modifiers || Qt.NoModifier, accepted: false, isAutoRepeat: false}
        browser.handleKey(event)
        verify(event.accepted)
    }
    function init() {
        browser.viewMode = "list"
        organization.selectionAnchor = ""
        organization.selectedPaths = []
        wait(20)
        browser.setCursor(0)
    }
    function test_gallery_moves_by_row_and_preserves_selection_on_switch() {
        organization.selectedPaths = ["1.md", "2.md"]
        organization.selectionAnchor = "1.md"
        browser.setCursor(2)
        browser.viewMode = "gallery"
        tryCompare(browser.activeView, "currentIndex", 2)
        compare(browser.columns, 3)
        compare(organization.selectedPaths.join(), "1.md,2.md")
        compare(organization.selectionAnchor, "1.md")
        press(Qt.Key_Down)
        compare(browser.activeView.currentIndex, 5)
        press(Qt.Key_Left)
        compare(browser.activeView.currentIndex, 4)
        press(Qt.Key_Down, Qt.ShiftModifier)
        compare(organization.selectedPaths.join(), "1.md,2.md,3.md,4.md,5.md,6.md,7.md")
        browser.viewMode = "list"
        tryCompare(browser.activeView, "currentIndex", 7)
        compare(organization.selectionAnchor, "1.md")
    }
    function test_gallery_keeps_two_columns_at_minimum_width() {
        browser.viewMode = "gallery"
        browser.width = 320
        compare(browser.columns, 2)
        browser.width = 720
        compare(browser.columns, 3)
    }
    function test_cursor_follows_identity_after_model_reorder() {
        browser.setCursor(3)
        const original = controller.notes.slice()
        controller.notes = original.slice().reverse()
        tryCompare(browser.activeView, "currentIndex", 8)
        compare(browser.cursorKey, "3\n3.md")
        controller.notes = original
        tryCompare(browser.activeView, "currentIndex", 3)
    }
}
