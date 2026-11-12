import QtQuick
import QtQuick.Controls

GridView {
    id: root
    required property var organization
    property string viewMode: "list"
    readonly property bool gallery: viewMode === "gallery"
    readonly property int columns: gallery ? Math.max(2, Math.floor(width / 196)) : 1
    property string cursorKey: ""
    objectName: "childFolderBrowser"
    cellWidth: width / columns
    cellHeight: gallery ? 104 : 62
    clip: true
    activeFocusOnTab: visible && count > 0
    keyNavigationEnabled: false
    Accessible.name: gallery ? "子文件夹画廊" : "子文件夹列表"

    function keyAt(row) { return row >= 0 && row < count ? String(model[row]) : "" }
    function focusCurrent() {
        if (!count) return
        if (currentIndex < 0) currentIndex = 0
        cursorKey = keyAt(currentIndex)
        positionViewAtIndex(currentIndex, GridView.Contain)
        if (currentItem) currentItem.forceActiveFocus(Qt.TabFocusReason)
    }
    function restoreCursor() {
        let row = -1
        for (let i = 0; i < count; ++i) if (keyAt(i) === cursorKey) { row = i; break }
        currentIndex = row >= 0 ? row : count ? 0 : -1
        cursorKey = keyAt(currentIndex)
        if (activeFocus) focusCurrent()
    }
    function handleKey(event) {
        if (event.modifiers === Qt.ControlModifier && event.key === Qt.Key_A) {
            if (!organization.folderSelectionMode) organization.beginSelection("folders")
            organization.selectAll(); event.accepted = true; return
        }
        if (event.modifiers === Qt.ControlModifier && event.key === Qt.Key_Space && currentIndex >= 0) {
            if (!organization.folderSelectionMode) organization.beginSelection("folders")
            organization.toggleFolder(keyAt(currentIndex)); event.accepted = true; return
        }
        if (event.key === Qt.Key_Escape && organization.selectionMode) { organization.clearSelection(); event.accepted = true; return }
        if (currentItem && (event.key === Qt.Key_Menu || event.key === Qt.Key_F10 && event.modifiers === Qt.ShiftModifier)) {
            currentItem.openContext(); event.accepted = true; return
        }
        if (event.modifiers !== Qt.NoModifier || !count) return
        let row = currentIndex
        if (event.key === Qt.Key_Down) row += columns
        else if (event.key === Qt.Key_Up) row -= columns
        else if (gallery && event.key === Qt.Key_Right) row += 1
        else if (gallery && event.key === Qt.Key_Left) row -= 1
        else if (event.key === Qt.Key_Home) row = 0
        else if (event.key === Qt.Key_End) row = count - 1
        else if ([Qt.Key_Return, Qt.Key_Enter, Qt.Key_Space].includes(event.key)) {
            if (currentItem && !event.isAutoRepeat) currentItem.activate()
            event.accepted = true; return
        } else return
        currentIndex = Math.max(0, Math.min(count - 1, row))
        focusCurrent(); event.accepted = true
    }
    onViewModeChanged: {
        const hadFocus = activeFocus || !!currentItem && currentItem.activeFocus
        Qt.callLater(function() {
            forceLayout()
            restoreCursor()
            if (hadFocus) focusCurrent()
        })
    }
    onModelChanged: Qt.callLater(restoreCursor)
    onActiveFocusChanged: if (activeFocus) Qt.callLater(function() { if (root.activeFocus) root.focusCurrent() })
    Keys.onPressed: event => handleKey(event)
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
}
