import QtQuick
import QtTest
import NextNotes.ChromeTests

TestCase {
    id: testCase
    name: "NoteListSelection"
    when: windowShown
    visible: true
    width: 1000; height: 900
    Item { id: scene; anchors.fill: parent }
    Item { id: menuButton; x: 420; y: 20; width: 26; height: 26 }
    Item { id: row; x: 360; y: 90; width: 250; height: 100 }
    Component {
        id: controllerComponent
        QtObject {
            property string library: "/synthetic"
            property string filter: "all"
            property string query: ""
            property string path: "a.md"
            property var notes: [{path: "a.md"}, {path: "b.md"}, {path: "c.md"}]
            property var folders: []
            property bool busy: false
            property string sortKey: "modified"
            property bool sortAscending: false
            property var batchReport: ({results: [], failed: 0, succeeded: 0})
            property var tagPlan: ({count: 0})
            signal tagRenameReady()
            signal batchFinished()
        }
    }
    Component { id: organizerComponent; NotesOrganizer { width: 1000; height: 900; sceneItem: scene } }
    property var controller
    property var organizer
    function init() {
        controller = createTemporaryObject(controllerComponent, testCase)
        organizer = createTemporaryObject(organizerComponent, testCase, {controller: controller})
    }
    function test_plain_click_leaves_selection_and_last_uncheck_exits() {
        verify(organizer.click(controller.notes[1], 1, Qt.ControlModifier))
        verify(organizer.selectionMode)
        compare(organizer.selectedPaths.join(), "b.md")
        verify(!organizer.click(controller.notes[2], 2, Qt.NoModifier))
        compare(organizer.selectedPaths.length, 0)
        verify(!organizer.selectionMode)
        compare(organizer.selectionAnchor, "c.md")
        organizer.toggle("a.md")
        organizer.toggle("a.md")
        verify(!organizer.selectionMode)
        compare(organizer.selectionAnchor, "")
    }
    function test_shift_anchor_union_and_limit() {
        organizer.click(controller.notes[0], 0, Qt.NoModifier)
        verify(organizer.click(controller.notes[2], 2, Qt.ShiftModifier))
        compare(organizer.selectedPaths.join(), "a.md,b.md,c.md")
        organizer.click(controller.notes[1], 1, Qt.ShiftModifier)
        compare(organizer.selectedPaths.join(), "a.md,b.md")
        organizer.click(controller.notes[2], 2, Qt.ControlModifier | Qt.ShiftModifier)
        compare(organizer.selectedPaths.join(), "a.md,b.md,c.md")
        organizer.clearSelection()
        controller.path = "missing.md"
        organizer.click(controller.notes[1], 1, Qt.ShiftModifier)
        compare(organizer.selectedPaths.join(), "b.md")
        controller.notes = Array.from({length: 201}, (_, i) => ({path: i + ".md"}))
        organizer.clearSelection(); organizer.selectAll()
        verify(!organizer.selectionMode)
        verify(organizer.notice.length > 0)
    }
    function test_context_target_does_not_start_selection() {
        const menu = findChild(organizer, "noteListMenu")
        organizer.openMenu(row, "b.md", Qt.point(30, 12))
        tryCompare(menu, "visible", true)
        verify(!menu.arrowEnabled)
        verify(!organizer.selectionMode)
        compare(organizer.targets.join(), "b.md")
        const click = row.mapToItem(menu.parent, 30, 12)
        tryCompare(menu, "y", click.y)
        compare(menu.x, click.x)
        menu.close()
        verify(!organizer.selectionMode)
        organizer.toggle("a.md"); organizer.toggle("c.md")
        organizer.openMenu(row, "c.md", Qt.point(30, 12))
        compare(organizer.targets.join(), "a.md,c.md")
        menu.close()
        organizer.openMenu(row, "b.md", Qt.point(30, 12))
        verify(!organizer.selectionMode)
        compare(organizer.targets.join(), "b.md")
        menu.close()
    }
    function test_toolbar_menu_opens_to_lower_left_and_prunes_selection() {
        const menu = findChild(organizer, "noteListMenu")
        organizer.openMenu(menuButton, "")
        tryCompare(menu, "visible", true)
        const below = menuButton.mapToItem(menu.parent, menuButton.width, menuButton.height + 4)
        tryVerify(() => Math.abs(menu.x + menu.width - below.x) < 1)
        compare(menu.y, below.y)
        verify(!menu.arrowEnabled)
        menu.close()
        controller.path = "outside-filter.md"
        organizer.openMenu(menuButton, "")
        compare(organizer.targets.length, 0) // Never act on an invisible active note.
        menu.close()
        organizer.selectAll()
        controller.notes = [{path: "c.md"}]
        compare(organizer.selectedPaths.join(), "c.md")
        controller.notes = []
        verify(!organizer.selectionMode)
        organizer.selectAll()
        verify(!organizer.selectionMode)
    }
}
