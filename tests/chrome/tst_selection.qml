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
            property var folderIcons: ({})
            property string error: ""
            property var folderRequest: ({})
            property bool busy: false
            property string sortKey: "modified"
            property bool sortAscending: false
            property var batchReport: ({results: [], failed: 0, succeeded: 0})
            property var tagPlan: ({count: 0})
            signal tagRenameReady()
            signal batchFinished()
            signal folderCreated(string path)
            signal folderCreationFailed(string message)
            signal folderEdited(bool success, string message)
            signal folderDeleted(bool success, string message)
            function createFolder(path, icon) { folderRequest = {path: path, icon: icon}; busy = true }
            function editFolder(path, target, icon) { folderRequest = {path: path, target: target, icon: icon}; busy = true }
        }
    }
    Component { id: organizerComponent; NotesOrganizer { width: 1000; height: 900; sceneItem: scene } }
    property var controller
    property var organizer
    function init() {
        controller = createTemporaryObject(controllerComponent, testCase)
        organizer = createTemporaryObject(organizerComponent, testCase, {controller: controller})
    }
    function test_toolbar_can_enter_note_selection_without_an_active_note() {
        controller.path = "outside-filter.md"
        organizer.openMenu(menuButton, "")
        const menu = findChild(organizer, "noteListMenu")
        tryCompare(menu, "visible", true)
        compare(organizer.targets.length, 0)
        const selectNotes = findChild(organizer, "selectNotesMenuItem")
        verify(selectNotes.enabled)
        selectNotes.triggered()
        menu.close()
        verify(organizer.noteSelectionMode)
        compare(organizer.selectedPaths.length, 0)
        verify(organizer.click(controller.notes[1], 1, Qt.NoModifier))
        compare(organizer.selectedPaths.join(), "b.md")
        organizer.toggle("b.md")
        compare(organizer.selectedPaths.length, 0)
        verify(organizer.noteSelectionMode)
        organizer.clearSelection()
        verify(!organizer.selectionMode)
    }
    function test_folder_selection_is_independent_and_prunes_removed_rows() {
        organizer.selectableFolders = ["Parent/A", "Parent/B"]
        organizer.openMenu(menuButton, "")
        const menu = findChild(organizer, "noteListMenu")
        tryCompare(menu, "visible", true)
        const selectFolders = findChild(organizer, "selectFoldersMenuItem")
        verify(selectFolders.visible)
        verify(selectFolders.enabled)
        selectFolders.triggered()
        menu.close()
        verify(organizer.folderSelectionMode)
        verify(!organizer.noteSelectionMode)
        compare(organizer.selectedFolderPaths.length, 0)
        organizer.toggleFolder("Parent/A")
        organizer.selectAll()
        compare(organizer.selectedFolderPaths.join(), "Parent/A,Parent/B")
        compare(organizer.selectedPaths.length, 0)
        organizer.selectableFolders = ["Parent/B"]
        compare(organizer.selectedFolderPaths.join(), "Parent/B")
        organizer.toggleFolder("Parent/B")
        verify(organizer.folderSelectionMode)
        organizer.toggleFolder("Parent/B")
        organizer.beginSelection("notes")
        verify(!organizer.folderSelectionMode)
        verify(organizer.noteSelectionMode)
        compare(organizer.selectedFolderPaths.length, 0)
        controller.filter = "favorite"
        verify(!organizer.selectionMode)
    }
    function test_create_folder_uses_editor_and_waits_for_result() {
        controller.folders = ["Projects"]
        organizer.newFolder("", menuButton, null)
        const dialog = findChild(organizer, "editFolderDialog")
        const name = findChild(organizer, "editFolderName")
        tryCompare(dialog, "visible", true)
        verify(dialog.creating)
        verify(dialog.destinations.includes(""))
        verify(!dialog.canSave)
        name.text = "Training"
        dialog.selectedParent = "Projects"
        dialog.selectedIcon = "cycling"
        verify(dialog.canSave)
        dialog.submit()
        compare(controller.folderRequest.path, "Projects/Training")
        compare(controller.folderRequest.icon, "cycling")
        verify(dialog.visible)
        verify(!dialog.canSave)
        controller.busy = false
        controller.folderCreationFailed("名称冲突")
        compare(dialog.errorMessage, "名称冲突")
        verify(dialog.visible)
        compare(name.text, "Training")
        compare(dialog.selectedIcon, "cycling")
        dialog.submit()
        controller.busy = false
        controller.folderCreated("Projects/Training")
        tryCompare(dialog, "visible", false)
        organizer.newFolder("Projects", menuButton, null)
        tryCompare(dialog, "visible", true)
        compare(dialog.title, "新建子文件夹")
        compare(dialog.selectedParent, "Projects")
        compare(dialog.selectedIcon, "folder")
        compare(name.text, "")
        compare(dialog.errorMessage, "")
        dialog.close()
    }
    function test_edit_folder_still_preserves_parent_restrictions() {
        controller.folders = ["Projects", "Projects/Child", "Other"]
        controller.folderIcons = {Projects: "work"}
        organizer.folderPath = "Projects"
        organizer.editFolder()
        const dialog = findChild(organizer, "editFolderDialog")
        tryCompare(dialog, "visible", true)
        verify(!dialog.creating)
        compare(dialog.selectedIcon, "work")
        verify(dialog.destinations.includes(""))
        verify(!dialog.destinations.includes("Projects"))
        verify(!dialog.destinations.includes("Projects/Child"))
        verify(!dialog.canSave)
        dialog.selectedIcon = "home"
        verify(dialog.canSave)
        dialog.submit()
        compare(controller.folderRequest.path, "Projects")
        compare(controller.folderRequest.target, "Projects")
        compare(controller.folderRequest.icon, "home")
        controller.busy = false
        controller.folderEdited(true, "")
        tryCompare(dialog, "visible", false)
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
    function test_toolbar_menu_opens_below_trigger_and_prunes_selection() {
        const menu = findChild(organizer, "noteListMenu")
        organizer.openMenu(menuButton, "")
        tryCompare(menu, "visible", true)
        const below = menuButton.mapToItem(menu.parent, menuButton.width / 2, menuButton.height + 4)
        tryVerify(() => Math.abs(menu.x + menu.width / 2 - below.x) < 1)
        compare(menu.y, below.y)
        verify(menu.arrowEnabled)
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
