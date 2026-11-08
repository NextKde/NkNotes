import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kos.Ui

Item {
    id: root
    required property var controller
    required property Item sceneItem
    property bool listVisible: true
    property real detailX: width
    property real detailWidth: 0
    property var selected: []
    property string currentKey: ""
    property string selectionAnchor: ""
    property var reviewed: []
    property var targets: []
    readonly property var items: (controller.recovery.trashItems || []).slice().sort((a, b) => String(b.deletedUtc).localeCompare(String(a.deletedUtc)))
    readonly property var chosen: items.filter(i => selected.indexOf(key(i)) >= 0)
    readonly property var currentEntry: items.find(i => key(i) === currentKey) || null
    readonly property bool selectionMode: selected.length > 0
    readonly property bool popupActive: menu.visible || purgeDialog.visible
    signal entryActivated()
    function key(item) { return item.kind + ":" + item.token }
    function clearSelection() { selected = []; selectionAnchor = "" }
    function confirm(entries) { reviewed = JSON.parse(JSON.stringify(entries)); purgeDialog.open() }
    function toggle(item) {
        const k = key(item), next = selected.slice(), at = next.indexOf(k)
        if (at >= 0) next.splice(at, 1); else next.push(k)
        selected = next; selectionAnchor = next.length ? k : ""
    }
    function selectAll() { selected = items.map(i => key(i)); if (selected.length) selectionAnchor = selected[0] }
    function activate(item) { clearSelection(); currentKey = key(item); selectionAnchor = currentKey; entryActivated() }
    function click(item, index, modifiers) {
        if (modifiers & Qt.ShiftModifier) {
            let anchor = items.findIndex(i => key(i) === (selectionAnchor || currentKey))
            if (anchor < 0) anchor = index
            const range = items.slice(Math.min(anchor, index), Math.max(anchor, index) + 1).map(i => key(i))
            selected = modifiers & (Qt.ControlModifier | Qt.MetaModifier) ? Array.from(new Set(selected.concat(range))) : range
            if (!selectionAnchor) selectionAnchor = key(items[anchor])
        } else if (modifiers & (Qt.ControlModifier | Qt.MetaModifier)) toggle(item)
        else activate(item)
    }
    function openMenu(anchor, item) {
        if (item && selected.indexOf(key(item)) < 0) clearSelection()
        targets = item ? (selected.indexOf(key(item)) >= 0 ? chosen.slice() : [item]) : chosen.length ? chosen.slice() : currentEntry ? [currentEntry] : []
        menu.preferredSide = item ? "right" : "bottom"
        menu.anchorAlignment = "end"
        menu.anchorItem = anchor
        menu.openAtAnchor()
    }
    property string library: controller.library
    onLibraryChanged: { clearSelection(); currentKey = "" }
    onVisibleChanged: if (!visible) { clearSelection(); menu.close() }
    Connections {
        target: root.controller
        function onChanged() {
            root.selected = root.selected.filter(k => root.items.some(i => root.key(i) === k))
            if (!root.currentEntry) root.currentKey = ""
        }
    }
    ListView {
        id: list
        anchors.fill: parent; visible: root.listVisible; clip: true; spacing: 4
        model: root.items
        activeFocusOnTab: count > 0
        keyNavigationEnabled: false
        function focusCurrent() { positionViewAtIndex(currentIndex, ListView.Contain); if (currentItem) currentItem.forceActiveFocus() }
        function handleKey(event) {
            const command = event.modifiers & (Qt.ControlModifier | Qt.MetaModifier)
            if (command && event.key === Qt.Key_A) root.selectAll()
            else if (event.key === Qt.Key_Escape && root.selectionMode) root.clearSelection()
            else if (command && event.key === Qt.Key_Space && currentIndex >= 0) root.toggle(root.items[currentIndex])
            else if (currentItem && (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && event.modifiers === Qt.ShiftModifier))) root.openMenu(currentItem, root.items[currentIndex])
            else if (count && [Qt.Key_Up, Qt.Key_Down, Qt.Key_Home, Qt.Key_End].indexOf(event.key) >= 0) {
                const row = event.key === Qt.Key_Home ? 0 : event.key === Qt.Key_End ? count - 1 : Math.max(0, Math.min(count - 1, currentIndex + (event.key === Qt.Key_Down ? 1 : -1)))
                if (event.modifiers & Qt.ShiftModifier) {
                    if (!root.selectionAnchor && currentIndex >= 0) root.selectionAnchor = root.key(root.items[currentIndex])
                    root.click(root.items[row], row, event.modifiers)
                }
                currentIndex = row; focusCurrent()
            } else if (currentItem && [Qt.Key_Return, Qt.Key_Enter, Qt.Key_Space].indexOf(event.key) >= 0) root.activate(root.items[currentIndex])
            else return
            event.accepted = true
        }
        Keys.onPressed: event => handleKey(event)
        onActiveFocusChanged: if (activeFocus) focusCurrent()
        delegate: ItemDelegate {
            id: row
            required property var modelData
            required property int index
            readonly property bool bulkSelected: root.selected.indexOf(root.key(modelData)) >= 0
            width: ListView.view.width; height: 68
            leftPadding: root.selectionMode ? 36 : 12; rightPadding: 12
            focusPolicy: Qt.StrongFocus; activeFocusOnTab: false
            highlighted: root.selectionMode ? bulkSelected : menu.visible && root.targets.length === 1 ? root.key(root.targets[0]) === root.key(modelData) : root.currentKey === root.key(modelData)
            onActiveFocusChanged: if (activeFocus) list.currentIndex = index
            Keys.onPressed: event => list.handleKey(event)
            background: Rectangle {
                radius: 10; color: row.highlighted ? AppTheme.dark ? "#bc8b16" : "#ffb921" : row.hovered ? AppTheme.controlBackground : "transparent"
                border.width: row.activeFocus ? 2 : 0; border.color: AppTheme.focusRing
            }
            Rectangle {
                x: row.leftPadding; y: row.height - 1
                width: Math.max(0, row.width - x - 12); height: 1
                visible: row.index < list.count - 1
                color: AppTheme.border; opacity: 0.5
            }
            contentItem: Column {
                spacing: 3
                Label { width: parent.width; text: row.modelData.title || row.modelData.source.split("/").pop().replace(/\.md$/i, ""); textFormat: Text.PlainText; elide: Text.ElideRight; font.pixelSize: 14; font.weight: Font.DemiBold; color: row.highlighted ? "white" : AppTheme.text }
                Label { width: parent.width; text: (row.modelData.kind === "folder" ? "文件夹 · " : "") + (row.modelData.purging ? "删除待完成" : "删除于 " + new Date(row.modelData.deletedUtc).toLocaleDateString(Qt.locale(), "M/d")); elide: Text.ElideRight; font.pixelSize: 12; color: row.highlighted ? "white" : AppTheme.mutedText }
            }
            MouseArea {
                anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function(mouse) {
                    row.forceActiveFocus(Qt.MouseFocusReason)
                    if (mouse.button === Qt.RightButton) root.openMenu(row, row.modelData)
                    else root.click(row.modelData, row.index, mouse.modifiers)
                }
            }
            LiquidCheckBox {
                id: trashSelectionBox
                x: 6; y: 12; width: 24; height: 24; text: ""
                padding: 4
                visible: root.selectionMode
                Binding { target: trashSelectionBox.indicator; property: "implicitWidth"; value: 16 }
                Binding { target: trashSelectionBox.indicator; property: "implicitHeight"; value: 16 }
                Binding { target: trashSelectionBox.indicator; property: "radius"; value: 5 }
                checked: row.bulkSelected
                Accessible.name: "选择回收项 " + row.modelData.source
                onClicked: root.toggle(row.modelData)
            }
            onClicked: root.activate(modelData)
            Accessible.role: Accessible.ListItem
            Accessible.name: (modelData.kind === "folder" ? "文件夹 " : "") + (modelData.title || modelData.source.split("/").pop().replace(/\.md$/i, ""))
            Accessible.selected: highlighted
            Accessible.onPressAction: root.activate(modelData)
        }
        Label { anchors.centerIn: parent; visible: !list.count; text: "最近删除为空"; color: AppTheme.mutedText; font.pixelSize: 13 }
    }
    Item {
        x: root.detailX; width: root.detailWidth; height: root.height
        visible: width > 0
        ColumnLayout {
            anchors.centerIn: parent; width: Math.min(440, parent.width - 52); spacing: 16
            Label { Layout.fillWidth: true; text: root.currentEntry ? (root.currentEntry.title || root.currentEntry.source.split("/").pop().replace(/\.md$/i, "")) : "最近删除"; textFormat: Text.PlainText; wrapMode: Text.Wrap; font.pixelSize: 24; font.weight: Font.DemiBold; color: AppTheme.text }
            Label { Layout.fillWidth: true; text: root.currentEntry ? (root.currentEntry.kind === "folder" ? "文件夹" : (root.currentEntry.source.includes("/") ? root.currentEntry.source.slice(0, root.currentEntry.source.lastIndexOf("/")).split("/").join(" › ") : "随手记")) + "\n删除于 " + Qt.formatDateTime(new Date(root.currentEntry.deletedUtc), "yyyy/M/d HH:mm") + (root.currentEntry.purging ? "\n删除待完成" : "") : "选择一个项目以恢复或永久删除。"; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: AppTheme.mutedText }
            Flow {
                Layout.fillWidth: true; spacing: 8; visible: !!root.currentEntry
                LiquidActionButton { text: "恢复"; materialForm: true; enabled: !!root.currentEntry && !root.currentEntry.purging && !root.controller.busy; onClicked: root.controller.recycle("restore", [root.currentEntry]) }
                LiquidActionButton { text: "恢复副本"; materialForm: true; visible: !!root.currentEntry && root.currentEntry.kind === "note"; enabled: !!root.currentEntry && !root.currentEntry.purging && !root.controller.busy; onClicked: root.controller.restore(root.currentEntry.token, "还原-" + Date.now() + ".md") }
                LiquidActionButton { text: "永久删除…"; materialForm: true; enabled: !!root.currentEntry && !root.controller.busy; onClicked: root.confirm([root.currentEntry]) }
            }
        }
    }
    LiquidPopover {
        id: menu; sourceItem: root.sceneItem; arrowEnabled: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnReleaseOutside
        LiquidPopoverItem { text: root.selectionMode ? "取消所有选择" : "选择当前项目"; enabled: root.selectionMode || !!root.targets.length; onTriggered: { if (root.selectionMode) root.clearSelection(); else root.selected = root.targets.map(i => root.key(i)) } }
        LiquidPopoverItem { visible: menu.preferredSide !== "right"; height: visible ? implicitHeight : 0; text: "全选当前结果"; enabled: !!root.items.length; onTriggered: root.selectAll() }
        LiquidPopoverItem { text: "恢复所选"; enabled: !!root.targets.length && !root.targets.some(i => i.purging) && !root.controller.busy; onTriggered: root.controller.recycle("restore", root.targets) }
        LiquidPopoverItem { text: "恢复副本"; enabled: root.targets.length === 1 && root.targets[0].kind === "note" && !root.targets[0].purging && !root.controller.busy; onTriggered: root.controller.restore(root.targets[0].token, "还原-" + Date.now() + ".md") }
        LiquidPopoverItem { text: "永久删除所选…"; enabled: !!root.targets.length && !root.controller.busy; onTriggered: root.confirm(root.targets) }
        LiquidPopoverItem { visible: menu.preferredSide !== "right"; height: visible ? implicitHeight : 0; text: "清空最近删除…"; enabled: !!root.items.length && !root.controller.busy; onTriggered: root.confirm(root.items) }
    }
    LiquidDialog {
        acceptText: "永久删除"; rejectText: "取消"; closeText: "关闭"
        id: purgeDialog; title: "永久删除 " + root.reviewed.length + " 项？"; sourceItem: root.sceneItem
        parent: Overlay.overlay; anchors.centerIn: parent; width: Math.min(520, parent.width - 40); height: Math.min(380, parent.height - 40)
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: ColumnLayout {
            Label { Layout.fillWidth: true; text: "这些笔记和文件夹将无法从最近删除恢复。文件夹内的全部内容也会删除。"; wrapMode: Text.Wrap; color: AppTheme.text }
            ScrollView { Layout.fillWidth: true; Layout.fillHeight: true; KosTextArea { readOnly: true; text: root.reviewed.map(i => i.title || i.source.split("/").pop().replace(/\.md$/i, "")).join("\n"); wrapMode: Text.Wrap; color: AppTheme.text; background: null } }
        }
        onAccepted: root.controller.recycle("purge", root.reviewed)
    }
}
