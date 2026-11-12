import QtQuick
import QtQuick.Controls

Item {
    id: root
    required property var controller
    required property var organization
    required property Component noteDelegate
    property string viewMode: "list"
    readonly property bool gallery: viewMode === "gallery"
    readonly property var activeView: gallery ? grid : list
    readonly property int count: controller.notes.length
    readonly property int columns: Math.max(2, Math.floor(width / 196))
    property string cursorKey: ""
    property string previousMode: "list"
    property var positions: ({})

    function keyAt(row) {
        const note = controller.notes[row]
        return note ? note.id + "\n" + note.path : ""
    }
    function indexOf(key) {
        for (let i = 0; i < count; ++i) if (keyAt(i) === key) return i
        return -1
    }
    function setCursor(row) {
        activeView.currentIndex = row
        cursorKey = keyAt(row)
    }
    function focusCurrent() {
        if (!count) return
        if (activeView.currentIndex < 0) setCursor(0)
        activeView.positionViewAtIndex(activeView.currentIndex, ListView.Contain)
        if (activeView.currentItem) activeView.currentItem.forceActiveFocus(Qt.TabFocusReason)
    }
    function restoreCursor() {
        const row = indexOf(cursorKey)
        setCursor(row >= 0 ? row : count ? 0 : -1)
        if (activeView.activeFocus) focusCurrent()
    }
    function positionViewAtBeginning() {
        positions = ({})
        activeView.positionViewAtBeginning()
    }
    function switchView() {
        const old = previousMode === "gallery" ? grid : list
        const hadFocus = old.activeFocus || !!old.currentItem && old.currentItem.activeFocus
        const first = old.indexAt(Math.max(1, old.width / 2), old.contentY + 10)
        const saved = positions
        saved[previousMode] = {key: keyAt(first >= 0 ? first : old.currentIndex), y: old.contentY}
        positions = saved
        previousMode = viewMode
        Qt.callLater(function() {
            restoreCursor()
            const position = positions[viewMode]
            const row = indexOf(position ? position.key : saved[old === grid ? "gallery" : "list"]?.key)
            if (row >= 0) activeView.positionViewAtIndex(row, ListView.Beginning)
            if (hadFocus) focusCurrent()
        })
    }
    function handleKey(event) {
        const view = activeView, current = view.currentIndex
        if (event.modifiers === Qt.ControlModifier && event.key === Qt.Key_A) { organization.selectAll(); event.accepted = true; return }
        if (event.modifiers === Qt.ControlModifier && event.key === Qt.Key_Space && current >= 0) { organization.toggle(controller.notes[current].path); event.accepted = true; return }
        if (event.key === Qt.Key_Escape && organization.selectionMode) { organization.clearSelection(); event.accepted = true; return }
        if (view.currentItem && (event.key === Qt.Key_Menu || event.key === Qt.Key_F10 && event.modifiers === Qt.ShiftModifier)) {
            organization.openMenu(view.currentItem, controller.notes[current].path)
            event.accepted = true; return
        }
        if (event.modifiers & ~(Qt.ShiftModifier | Qt.ControlModifier)) return
        let row = current
        const step = gallery ? columns : 1
        if (event.key === Qt.Key_Down) row += step
        else if (event.key === Qt.Key_Up) row -= step
        else if (gallery && event.key === Qt.Key_Right) row += 1
        else if (gallery && event.key === Qt.Key_Left) row -= 1
        else if (event.key === Qt.Key_Home) row = 0
        else if (event.key === Qt.Key_End) row = count - 1
        else if (event.modifiers === Qt.NoModifier && [Qt.Key_Return, Qt.Key_Enter, Qt.Key_Space].indexOf(event.key) >= 0) {
            if (view.currentItem && !event.isAutoRepeat) view.currentItem.activate()
            event.accepted = true; return
        } else return
        if (!count) return
        row = Math.max(0, Math.min(count - 1, row))
        if (event.modifiers & Qt.ShiftModifier) {
            if (!organization.selectionAnchor && current >= 0) organization.selectionAnchor = controller.notes[current].path
            organization.click(controller.notes[row], row, event.modifiers)
        }
        setCursor(row); focusCurrent(); event.accepted = true
    }
    onViewModeChanged: switchView()
    Connections { target: root.controller; function onNotesChanged() { Qt.callLater(root.restoreCursor) } }
    ListView {
        id: list
        objectName: "notesListView"
        anchors.fill: parent; visible: !root.gallery; clip: true; spacing: 4
        model: root.controller.noteModel
        delegate: root.noteDelegate
        activeFocusOnTab: visible && count > 0; keyNavigationEnabled: false
        Accessible.name: "笔记列表"
        Keys.onPressed: event => root.handleKey(event)
        onActiveFocusChanged: if (activeFocus) Qt.callLater(root.focusCurrent)
        ScrollBar.vertical: ScrollBar {}
    }
    GridView {
        id: grid
        objectName: "notesGalleryView"
        anchors.fill: parent; visible: root.gallery; clip: true
        cellWidth: width / root.columns; cellHeight: 116
        model: root.controller.noteModel
        delegate: root.noteDelegate
        activeFocusOnTab: visible && count > 0; keyNavigationEnabled: false
        Accessible.name: "笔记画廊"
        Keys.onPressed: event => root.handleKey(event)
        onActiveFocusChanged: if (activeFocus) Qt.callLater(root.focusCurrent)
        ScrollBar.vertical: ScrollBar {}
    }
}
