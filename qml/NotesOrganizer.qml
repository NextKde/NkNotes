import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kos.Ui
import "FolderIcons.js" as FolderIcons

Item {
    id: root
    required property var controller
    required property Item sceneItem
    NotesTokens { id: tokens }
    readonly property var folderIconChoices: FolderIcons.choices
    signal exportRequested(var paths, string folder)
    signal smartFolderRequested()
    property var selectedPaths: []
    property var selectedFolderPaths: []
    property var selectableFolders: []
    property string selectionKind: ""
    readonly property bool noteSelectionMode: selectionKind === "notes" || selectedPaths.length > 0
    readonly property bool folderSelectionMode: selectionKind === "folders"
    readonly property bool selectionMode: noteSelectionMode || folderSelectionMode
    onSelectableFoldersChanged: selectedFolderPaths = selectedFolderPaths.filter(path => selectableFolders.includes(path))
    property string dragKind: ""
    property var dragPaths: []
    property string dragFolder: ""
    property string dragTitle: ""
    property string dropHint: ""
    property var dropOwner: null
    function showDropHint(owner, text) { dropOwner = owner; dropHint = text }
    function clearDropHint(owner) { if (dropOwner === owner) { dropOwner = null; dropHint = "" } }
    readonly property bool dragging: dragKind !== ""
    function startNoteDrag(path, title) {
        dragKind = "notes"; dragPaths = selectedPaths.indexOf(path) >= 0 ? selectedPaths.slice() : [path]
        dragTitle = dragPaths.length > 1 ? dragPaths.length + " 篇笔记" : title
    }
    function startFolderDrag(path) { dragKind = "folder"; dragFolder = path; dragTitle = path.split("/").pop() }
    function endDrag() { dragKind = ""; dragPaths = []; dragFolder = ""; dropOwner = null; dropHint = "" }
    function canPlaceFolder(parent, sibling, after) {
        if (controller.busy || dragKind !== "folder" || !Array.from(controller.folders).includes(dragFolder)) return false
        if (parent === dragFolder || parent.startsWith(dragFolder + "/")) return false
        if (parent && !Array.from(controller.folders).includes(parent)) return false
        const target = (parent ? parent + "/" : "") + dragFolder.split("/").pop()
        if (target !== dragFolder && Array.from(controller.folders).includes(target)) return false
        const siblings = Array.from(controller.folders).filter(p => parentFolder(p) === parent)
        if (sibling && (sibling === dragFolder || !siblings.includes(sibling))) return false
        const next = siblings.filter(p => p !== dragFolder)
        const at = sibling ? next.indexOf(sibling) + (after ? 1 : 0) : next.length
        next.splice(at, 0, target)
        return JSON.stringify(next) !== JSON.stringify(siblings)
    }
    function placeFolder(parent, sibling, after) {
        if (!canPlaceFolder(parent, sibling, after)) return false
        controller.moveFolder(dragFolder, parent, sibling || "", !!after)
        clearSelection(); return true
    }
    function canDrop(folder, notesAllowed) {
        if (controller.busy || !dragging) return false
        if (dragKind === "notes") return notesAllowed && dragPaths.some(p => parentFolder(p) !== folder)
        const target = (folder ? folder + "/" : "") + dragFolder.split("/").pop()
        return folder !== dragFolder && !folder.startsWith(dragFolder + "/") && target !== dragFolder && !Array.from(controller.folders).includes(target)
    }
    function dropInto(folder, notesAllowed) {
        if (!canDrop(folder, notesAllowed)) return false
        if (dragKind === "notes") controller.organize("move", dragPaths.filter(p => parentFolder(p) !== folder), folder)
        else controller.moveFolder(dragFolder, folder, "", false)
        clearSelection(); return true
    }
    property string selectionAnchor: ""
    property var targets: []
    property string folderPath: ""
    property string notice: ""
    readonly property string contextPath: menu.visible && targets.length === 1 ? targets[0] : ""
    readonly property bool popupActive: menu.visible || tagActions.visible || sortActions.visible || movePanel.visible || deletePanel.visible || resultPanel.visible || folderMenu.visible || trashFolderDialog.visible || editFolderDialog.visible || tagsPanel.visible || tagRenamePanel.visible || tagRenameConfirm.visible
    readonly property var targetNotes: controller.notes.filter(n => targets.indexOf(n.path) >= 0)
    property string scope: controller.library + "\n" + controller.filter + "\n" + controller.query
    onScopeChanged: clearSelection()
    function clearSelection() { selectedPaths = []; selectedFolderPaths = []; selectionAnchor = ""; selectionKind = "" }
    function beginSelection(kind, initialPaths) {
        clearSelection()
        selectionKind = kind
        if (kind === "notes" && initialPaths) {
            selectedPaths = initialPaths.slice()
            selectionAnchor = selectedPaths[0] || ""
        }
    }
    function toggleFolder(path) {
        if (!folderSelectionMode || !selectableFolders.includes(path)) return
        let paths = selectedFolderPaths.slice()
        const at = paths.indexOf(path)
        if (at >= 0) paths.splice(at, 1)
        else if (paths.length < 200) paths.push(path)
        else { notice = "一次最多选择 200 个文件夹"; return }
        selectedFolderPaths = paths
    }
    function toggle(path) {
        if (folderSelectionMode) clearSelection()
        let paths = selectedPaths.slice()
        const at = paths.indexOf(path)
        if (at >= 0) paths.splice(at, 1)
        else if (paths.length < 200) paths.push(path)
        else { notice = "一次最多选择 200 篇笔记"; return }
        selectedPaths = paths; selectionAnchor = paths.length ? path : ""
    }
    function click(note, index, modifiers) {
        if (folderSelectionMode) clearSelection()
        if (modifiers & Qt.ShiftModifier) {
            let anchor = controller.notes.findIndex(n => n.path === (selectionAnchor || controller.path))
            if (anchor < 0) anchor = index
            const range = controller.notes.slice(Math.min(anchor, index), Math.max(anchor, index) + 1).map(n => n.path)
            if (range.length > 200) { notice = "一次最多选择 200 篇笔记"; return true }
            const combined = (modifiers & (Qt.ControlModifier | Qt.MetaModifier)) ? Array.from(new Set(selectedPaths.concat(range))) : range
            if (combined.length > 200) { notice = "一次最多选择 200 篇笔记"; return true }
            selectedPaths = combined
            if (!selectionAnchor) selectionAnchor = controller.notes[anchor].path
            return true
        }
        if (modifiers & (Qt.ControlModifier | Qt.MetaModifier)) { toggle(note.path); return true }
        if (selectionKind === "notes") { toggle(note.path); return true }
        clearSelection()
        selectionAnchor = note.path
        return false
    }
    function selectAll() {
        if (folderSelectionMode) {
            if (selectableFolders.length > 200) { notice = "一次最多选择 200 个文件夹"; return }
            selectedFolderPaths = selectableFolders.slice()
            return
        }
        if (controller.notes.length > 200) { notice = "结果超过 200 篇，请先缩小筛选范围"; return }
        selectedPaths = controller.notes.map(n => n.path)
        if (!selectionAnchor && selectedPaths.length) selectionAnchor = selectedPaths[0]
    }
    function openMenu(anchor, path, position) {
        // Right-clicking an unselected row targets only that note; opening or
        // dismissing the context menu must not enter a sticky selection mode.
        if (path && selectedPaths.indexOf(path) < 0) clearSelection()
        targets = path ? (selectedPaths.indexOf(path) >= 0 ? selectedPaths.slice() : [path])
                       : selectedPaths.length ? selectedPaths.slice()
                       : controller.notes.some(n => n.path === controller.path) ? [controller.path] : []
        menu.rowMenu = !!path
        if (path) menu.openAtPoint(anchor, position)
        else menu.openFromButton(anchor)
    }
    function parentFolder(path) { const at = path.lastIndexOf("/"); return at < 0 ? "" : path.slice(0, at) }
    function folderLabel(path) { return path ? path.split("/").join(" › ") : "未分类" }
    function moveNotes(paths, anchor, originMenu) {
        targets = paths.slice()
        const current = targets.length ? parentFolder(targets[0]) : ""
        folderChoice.currentIndex = Math.max(0, movePanel.folders.indexOf(current))
        if (originMenu) movePanel.openFromMenu(originMenu); else movePanel.openFromButton(anchor)
    }
    function openTagRename() { tagRenamePanel.open() }
    function perform(action) { if (targets.length && !controller.busy) controller.organize(action, targets) }
    function deleteNotes(paths) {
        if (!paths.length || root.controller.busy) return
        deletePanel.reviewedPaths = paths.slice()
        deletePanel.errorMessage = ""
        deletePanel.pendingAction = ""
        deletePanel.open()
    }
    function newFolder(parentPath, anchor, originMenu) {
        folderPath = parentPath || ""
        editFolderDialog.creating = true
        editFolderDialog.originalPath = ""
        editFolderName.text = ""
        editFolderDialog.selectedIcon = "folder"
        editFolderDialog.selectedParent = folderPath
        editFolderDialog.errorMessage = ""
        if (originMenu) originMenu.close()
        editFolderDialog.open()
    }
    function editFolder() {
        editFolderDialog.creating = false
        editFolderDialog.originalPath = folderPath
        editFolderName.text = folderPath.split("/").pop()
        editFolderDialog.selectedIcon = controller.folderIcons[folderPath] || "folder"
        editFolderDialog.selectedParent = parentFolder(folderPath)
        editFolderDialog.errorMessage = ""
        folderMenu.close()
        editFolderDialog.open()
    }
    function openFolderMenu(path, anchor, position) {
        folderPath = path
        if (position) folderMenu.openAtPoint(anchor, position)
        else folderMenu.openFromButton(anchor)
    }
    Connections {
        target: root.controller
        function onNotesChanged() {
            const visible = new Set(root.controller.notes.map(n => n.path))
            const paths = root.selectedPaths.filter(p => visible.has(p))
            if (JSON.stringify(paths) !== JSON.stringify(root.selectedPaths)) root.selectedPaths = paths
            if (root.selectionAnchor && !visible.has(root.selectionAnchor)) root.selectionAnchor = ""
        }
        function onTagRenameReady() { tagRenamePanel.close(); tagRenameConfirm.open() }
        function onFolderCreated(path) {
            if (editFolderDialog.visible && editFolderDialog.creating) editFolderDialog.close()
        }
        function onFolderCreationFailed(message) {
            if (editFolderDialog.visible && editFolderDialog.creating) editFolderDialog.errorMessage = message
        }
        function onFolderEdited(success, message) {
            if (!editFolderDialog.visible || editFolderDialog.creating) return
            if (success) editFolderDialog.close()
            else editFolderDialog.errorMessage = message
        }
        function onFolderDeleted(success, message) {
            if (!trashFolderDialog.visible) return
            if (success) trashFolderDialog.close()
            else trashFolderDialog.errorMessage = message
        }
        function onBatchFinished() {
            const report = root.controller.batchReport
            const failed = (report.results || []).filter(r => !r.ok).map(r => r.path)
            if (report.action === "move" || report.action === "trash" || report.action === "purgeNotes") root.selectedPaths = root.selectedPaths.filter(p => failed.indexOf(p) >= 0)
            if (deletePanel.visible && deletePanel.pendingAction === report.action) {
                deletePanel.pendingAction = ""
                if (!report.failed) deletePanel.close()
                else {
                    deletePanel.reviewedPaths = failed
                    deletePanel.errorMessage = (report.succeeded ? "已完成 " + report.succeeded + " 篇，其余未删除。\n" : "")
                        + (report.results || []).filter(r => !r.ok).map(r => r.message).join("\n")
                }
                return
            }
            if (report.failed) resultPanel.open()
        }
    }
    NotesPopover {
        id: menu; sourceItem: root.sceneItem
        objectName: "noteListMenu"
        property bool rowMenu: false
        readonly property bool allPinned: root.targetNotes.length > 0 && root.targetNotes.every(n => n.pinned)
        readonly property bool allFavorite: root.targetNotes.length > 0 && root.targetNotes.every(n => n.favorite)
        LiquidPopoverItem {
            objectName: "selectNotesMenuItem"
            text: root.noteSelectionMode ? "取消选择笔记" : "选择笔记"
            enabled: !root.controller.busy && (root.noteSelectionMode || root.controller.notes.length > 0)
            onTriggered: { if (root.noteSelectionMode) root.clearSelection(); else root.beginSelection("notes", menu.rowMenu ? root.targets : []) }
        }
        LiquidPopoverItem {
            objectName: "selectFoldersMenuItem"
            visible: !menu.rowMenu; height: visible ? implicitHeight : 0
            text: root.folderSelectionMode ? "取消选择文件夹" : "选择文件夹"
            enabled: !root.controller.busy && (root.folderSelectionMode || root.selectableFolders.length > 0)
            onTriggered: { if (root.folderSelectionMode) root.clearSelection(); else root.beginSelection("folders") }
        }
        LiquidPopoverItem {
            visible: !menu.rowMenu; height: visible ? implicitHeight : 0
            text: root.folderSelectionMode ? "全选子文件夹" : "全选当前结果"
            enabled: root.folderSelectionMode ? root.selectableFolders.length > 0 : root.controller.notes.length > 0
            onTriggered: root.selectAll()
        }
        LiquidPopoverItem { visible: menu.rowMenu; height: visible ? implicitHeight : 0; text: menu.allPinned ? "取消置顶" : "置顶"; enabled: root.targets.length > 0 && !root.controller.busy; onTriggered: root.perform(menu.allPinned ? "unpin" : "pin") }
        LiquidPopoverItem { visible: menu.rowMenu; height: visible ? implicitHeight : 0; text: menu.allFavorite ? "取消收藏" : "收藏"; enabled: root.targets.length > 0 && !root.controller.busy; onTriggered: root.perform(menu.allFavorite ? "unfavorite" : "favorite") }
        LiquidPopoverItem { visible: menu.rowMenu; height: visible ? implicitHeight : 0; text: "标签…"; enabled: root.targets.length > 0 && !root.controller.busy; onTriggered: tagActions.openFromMenu(menu) }
        LiquidPopoverItem { visible: menu.rowMenu; height: visible ? implicitHeight : 0; text: "创建副本"; enabled: root.targets.length > 0 && !root.controller.busy; onTriggered: root.perform("duplicate") }
        LiquidPopoverItem { visible: menu.rowMenu; height: visible ? implicitHeight : 0; text: "移动到文件夹…"; enabled: root.targets.length > 0 && !root.controller.busy; onTriggered: root.moveNotes(root.targets, null, menu) }
        LiquidPopoverItem { visible: menu.rowMenu; height: visible ? implicitHeight : 0; text: "删除笔记…"; enabled: root.targets.length > 0 && !root.controller.busy; onTriggered: root.deleteNotes(root.targets) }
        LiquidPopoverItem { visible: !menu.rowMenu; height: visible ? implicitHeight : 0; text: "排序…"; onTriggered: sortActions.openFromMenu(menu) }
    }
    NotesPopover {
        id: tagActions; sourceItem: root.sceneItem
        LiquidPopoverItem { text: "添加标签…"; onTriggered: { tagsPanel.operation = "add"; tagsPanel.open() } }
        LiquidPopoverItem { text: "移除标签…"; onTriggered: { tagsPanel.operation = "remove"; tagsPanel.open() } }
    }
    NotesPopover {
        id: sortActions; sourceItem: root.sceneItem
        LiquidPopoverItem { text: "按修改时间排序"; checkable: true; autoExclusive: true; checked: root.controller.sortKey === "modified"; onTriggered: root.controller.sortKey = "modified" }
        LiquidPopoverItem { text: "按创建时间排序"; checkable: true; autoExclusive: true; checked: root.controller.sortKey === "created"; onTriggered: root.controller.sortKey = "created" }
        LiquidPopoverItem { text: "按标题排序"; checkable: true; autoExclusive: true; checked: root.controller.sortKey === "title"; onTriggered: root.controller.sortKey = "title" }
        LiquidPopoverItem { text: root.controller.sortKey === "title" ? "标题升序" : "最早在前"; checkable: true; checked: root.controller.sortAscending; onTriggered: root.controller.sortAscending = true }
        LiquidPopoverItem { text: root.controller.sortKey === "title" ? "标题降序" : "最新在前"; checkable: true; checked: !root.controller.sortAscending; onTriggered: root.controller.sortAscending = false }
    }

    LiquidDialog {
        acceptText: "应用"; rejectText: "取消"; closeText: "关闭"
        acceptEnabled: !!batchTags.text.trim() && !batchTags.inputMethodComposing && !root.controller.busy
        onAccepted: root.controller.editTags(tagsPanel.operation, root.targets, batchTags.text.split(",").map(t => t.trim()).filter(t => t))
        id: tagsPanel; property string operation: "add"
        title: operation === "add" ? "批量添加标签" : "批量移除标签"
        sourceItem: root.sceneItem; anchors.centerIn: parent; width: Math.min(440, root.width - 40); standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: ColumnLayout {
            Label { text: "处理所选 " + root.targets.length + " 篇笔记；其他标签和正文保持不变。"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: AppTheme.text }
            LiquidTextField { id: batchTags; Layout.fillWidth: true; materialForm: true; placeholderText: "标签，多个用逗号分隔"; Accessible.name: "批量标签" }
        }
    }
    LiquidDialog {
        acceptText: "预览"; rejectText: "取消"; closeText: "关闭"
        acceptEnabled: !!oldTag.text.trim() && !!newTag.text.trim() && !oldTag.inputMethodComposing && !newTag.inputMethodComposing && !root.controller.busy
        onAccepted: root.controller.previewTagRename(oldTag.text, newTag.text)
        id: tagRenamePanel; title: "全库重命名标签"; sourceItem: root.sceneItem; anchors.centerIn: parent; width: Math.min(440, root.width - 40); standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: ColumnLayout {
            Label { text: "查找全库使用这个标签的笔记，先预览影响范围。"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: AppTheme.text }
            LiquidTextField { id: oldTag; Layout.fillWidth: true; materialForm: true; placeholderText: "原标签"; Accessible.name: "原标签" }
            LiquidTextField { id: newTag; Layout.fillWidth: true; materialForm: true; placeholderText: "新标签"; Accessible.name: "新标签" }
        }
    }
    LiquidDialog {
        acceptText: "重命名"; rejectText: "取消"; closeText: "关闭"
        acceptEnabled: root.controller.tagPlan.count > 0 && !root.controller.busy
        onAccepted: root.controller.confirmTagRename()
        id: tagRenameConfirm; title: "确认标签重命名"; sourceItem: root.sceneItem; anchors.centerIn: parent; width: Math.min(440, root.width - 40); standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: ColumnLayout {
            Label { text: (root.controller.tagPlan.tags || []).join("") + " → " + (root.controller.tagPlan.replacement || "") + "\n影响 " + (root.controller.tagPlan.count || 0) + " 篇笔记。已有新标签会合并去重；预览后发生修改的笔记将跳过并报告。"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: AppTheme.text }
        }
    }
    NotesContentPopover {
        id: movePanel; sourceItem: root.sceneItem; preferredWidth: 350
        readonly property var folders: [""].concat(Array.from(root.controller.folders))
        readonly property string destination: folders[folderChoice.currentIndex] || ""
        readonly property bool changesFolder: root.targets.some(p => root.parentFolder(p) !== destination)
        contentItem: ColumnLayout {
            Label { text: root.targets.length === 1 ? "移动笔记" : "移动 " + root.targets.length + " 篇笔记"; font.bold: true; color: AppTheme.text }
            Label { Layout.fillWidth: true; text: "当前位置：" + (Array.from(new Set(root.targets.map(p => root.folderLabel(root.parentFolder(p))))).join("、")); wrapMode: Text.Wrap; color: AppTheme.mutedText; font.pixelSize: 12 }
            LiquidSelect {
                    appearanceMode: AppTheme.appearanceMode; id: folderChoice; Layout.fillWidth: true; materialForm: true; model: movePanel.folders.map(p => p ? p.split("/").join(" › ") : "未分类"); Accessible.name: "移动目标文件夹"; onActivated: function(index) { folderChoice.currentIndex = index } }
            Label { text: "移动后仍可在“全部笔记”找到。同名笔记不会被覆盖。"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: AppTheme.mutedText; font.pixelSize: 12 }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                LiquidActionButton { text: "取消"; materialForm: true; onClicked: movePanel.close() }
                LiquidActionButton { text: "移动"; materialForm: true; enabled: !root.controller.busy && folderChoice.currentIndex >= 0 && movePanel.changesFolder; onClicked: { root.controller.organize("move", root.targets.filter(p => root.parentFolder(p) !== movePanel.destination), movePanel.destination); movePanel.close() } }
            }
        }
    }
    LiquidDialog {
        id: deletePanel; title: "删除笔记？"; sourceItem: root.sceneItem; anchors.centerIn: parent; width: Math.min(420, root.width - 40)
        standardButtons: Dialog.NoButton
        closePolicy: root.controller.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
        property var reviewedPaths: []
        property string pendingAction: ""
        property string errorMessage: ""
        onOpened: cancelNoteDelete.forceActiveFocus()
        function submit(action) {
            if (root.controller.busy || !reviewedPaths.length) return
            errorMessage = ""; pendingAction = action
            root.controller.organize(action, reviewedPaths)
        }
        contentItem: ColumnLayout {
            spacing: 10
            Label { text: "将删除所选 " + deletePanel.reviewedPaths.length + " 篇笔记。移到最近删除后可以恢复。"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: AppTheme.text }
            Label { text: "直接删除不会进入最近删除，无法撤销；笔记的历史版本也会删除。"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: AppTheme.destructive }
            Label { text: deletePanel.errorMessage || root.controller.error; visible: !!text; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText; color: AppTheme.destructive }
        }
        footer: Item {
            implicitHeight: 62
            RowLayout {
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                anchors.leftMargin: 16; anchors.rightMargin: 16; anchors.bottomMargin: 16; spacing: 8
                LiquidDialogButton {
                    text: "直接删除"; destructive: true; enabled: !root.controller.busy
                    surfaceColor: enabled ? AppTheme.destructive : AppTheme.controlBackground
                    textColor: enabled ? "white" : AppTheme.disabledText
                    onClicked: deletePanel.submit("purgeNotes")
                }
                Item { Layout.fillWidth: true }
                LiquidDialogButton { id: cancelNoteDelete; text: "取消"; enabled: !root.controller.busy; onClicked: deletePanel.close() }
                LiquidDialogButton { text: "移到最近删除"; primaryAction: true; enabled: !root.controller.busy; onClicked: deletePanel.submit("trash") }
            }
        }
    }
    LiquidDialog {
        id: resultPanel; sourceItem: root.sceneItem; width: Math.min(430, parent.width - 32)
        parent: Overlay.overlay; anchors.centerIn: parent
        title: "整理结果"; standardButtons: Dialog.Close; closeText: "关闭"
        contentItem: ColumnLayout {
            Label { text: "完成 " + root.controller.batchReport.succeeded + " 项，失败 " + root.controller.batchReport.failed + " 项"; font.bold: true; color: AppTheme.text }
            ListView {
                Layout.fillWidth: true; Layout.preferredHeight: Math.min(320, root.height - 180); clip: true
                model: (root.controller.batchReport.results || []).filter(r => !r.ok)
                delegate: Column {
                    required property var modelData
                    width: ListView.view.width; spacing: 4; bottomPadding: 12
                    Label { width: parent.width; text: modelData.path; textFormat: Text.PlainText; elide: Text.ElideMiddle; font.bold: true; color: AppTheme.text }
                    Label { width: parent.width; text: modelData.message; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: AppTheme.mutedText }
                }
            }
        }
    }
    component FolderMenuItem: LiquidPopoverItem {
        id: folderItem
        property string leadingIconName: ""
        // A 14px icon and 6px gap, reserved on every folder-menu row.
        leftPadding: 28
        VectorIcon {
            visible: folderItem.leadingIconName !== ""
            name: folderItem.leadingIconName; size: 14
            strokeWidth: folderItem.iconStrokeWidth
            anchors.left: parent.left; anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            color: folderItem.enabled ? AppTheme.text : AppTheme.disabledText
        }
    }
    NotesPopover {
        id: folderMenu; sourceItem: root.sceneItem
        FolderMenuItem { text: "编辑…"; leadingIconName: "editRounded"; enabled: !root.controller.busy; onTriggered: root.editFolder() }
        FolderMenuItem { text: "新建子文件夹…"; onTriggered: root.newFolder(root.folderPath, null, folderMenu) }
        FolderMenuItem { text: "导出其中的笔记…"; enabled: !root.controller.busy; onTriggered: root.exportRequested([], root.folderPath) }
        FolderMenuItem { text: "删除文件夹…"; enabled: !root.controller.busy; onTriggered: trashFolderDialog.open() }
    }
    LiquidDialog {
        id: editFolderDialog
        objectName: "editFolderDialog"
        title: creating ? (selectedParent ? "新建子文件夹" : "新建文件夹") : "编辑文件夹"; sourceItem: root.sceneItem
        width: Math.min(340, root.width - 32)
        padding: 16; spacing: 0
        topPadding: 4; bottomPadding: 4
        header: Label {
            text: editFolderDialog.title
            leftPadding: 16; rightPadding: 16; topPadding: 16; bottomPadding: 12
            color: AppTheme.text; font.pixelSize: 17; font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        standardButtons: Dialog.NoButton
        closePolicy: root.controller.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
        property bool creating: false
        property string originalPath: ""
        property string selectedIcon: "folder"
        property string hoveredIconLabel: ""
        readonly property string selectedIconLabel: (root.folderIconChoices.find(choice => choice.name === selectedIcon) || { label: "文件夹" }).label
        property string selectedParent: ""
        property string errorMessage: ""
        readonly property color formGroupColor: AppTheme.dark ? "#363638" : "#EDEDEF"
        onSelectedIconChanged: errorMessage = ""
        onSelectedParentChanged: errorMessage = ""
        readonly property var destinations: [""].concat(Array.from(root.controller.folders)).filter(p => creating || (p !== originalPath && !p.startsWith(originalPath + "/")))
        readonly property string enteredName: editFolderName.text.trim()
        readonly property string proposedPath: (selectedParent ? selectedParent + "/" : "") + enteredName
        readonly property bool nameAlreadyUsed: proposedPath !== originalPath && Array.from(root.controller.folders).includes(proposedPath)
        readonly property bool validName: !!enteredName && enteredName !== "." && enteredName !== ".." && !/[\\/\u0000]/.test(enteredName) && ![".git", ".nextnotes"].includes(enteredName)
        readonly property bool hasChanges: creating || proposedPath !== originalPath || selectedIcon !== (root.controller.folderIcons[originalPath] || "folder")
        readonly property bool canSave: validName && !nameAlreadyUsed && hasChanges && destinations.includes(selectedParent) && (creating || Array.from(root.controller.folders).includes(originalPath)) && !editFolderName.inputMethodComposing && !root.controller.busy
        onOpened: { hoveredIconLabel = ""; editFolderName.forceActiveFocus(); editFolderName.selectAll() }
        function submit() {
            if (!canSave) return
            errorMessage = ""
            if (creating) root.controller.createFolder(proposedPath, selectedIcon)
            else root.controller.editFolder(originalPath, proposedPath, selectedIcon)
        }
        contentItem: ColumnLayout {
            spacing: 6
            enabled: !root.controller.busy
            // Use CalendarTaskEditorDialog's block input and grouped settings rows.
            SettingsGroup {
                Layout.fillWidth: true
                cornerRadius: 8; backgroundColor: editFolderDialog.formGroupColor
                SettingsTextField {
                    id: editFolderName; objectName: "editFolderName"
                    implicitHeight: 28; filled: false; blockInput: true
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    placeholderText: "文件夹名称"
                    Accessible.name: "文件夹名称"; onAccepted: editFolderDialog.submit()
                    onTextEdited: editFolderDialog.errorMessage = ""
                }
            }
            SettingsGroup {
                Layout.fillWidth: true
                title: "图标 · " + (editFolderDialog.hoveredIconLabel || editFolderDialog.selectedIconLabel); titleColor: AppTheme.mutedText
                titleFont.pixelSize: 12; titleFont.weight: Font.Medium
                cornerRadius: 8; backgroundColor: editFolderDialog.formGroupColor
                Item {
                    objectName: "editFolderIcons"
                    implicitHeight: iconGrid.implicitHeight + 16
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    GridLayout {
                        id: iconGrid
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.leftMargin: 12; anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        columns: 8; rowSpacing: 8
                        columnSpacing: Math.max(0, (width - columns * 24) / (columns - 1))
                        Repeater {
                            model: root.folderIconChoices
                            delegate: Button {
                                id: iconChoice
                                required property var modelData
                                implicitWidth: 24; implicitHeight: 24; padding: 0; hoverEnabled: true
                                focusPolicy: Qt.StrongFocus
                                checked: editFolderDialog.selectedIcon === modelData.name
                                onHoveredChanged: {
                                    if (hovered) editFolderDialog.hoveredIconLabel = modelData.label
                                    else if (editFolderDialog.hoveredIconLabel === modelData.label) editFolderDialog.hoveredIconLabel = ""
                                }
                                Accessible.name: modelData.label + (checked ? "，已选择" : "")
                                Accessible.checkable: true; Accessible.checked: checked
                                onClicked: editFolderDialog.selectedIcon = modelData.name
                                background: Rectangle {
                                    radius: 12
                                    color: iconChoice.checked ? tokens.primaryYellow
                                        : iconChoice.down ? AppTheme.withAlpha(AppTheme.text, 0.10)
                                        : iconChoice.hovered || iconChoice.visualFocus ? AppTheme.withAlpha(AppTheme.text, 0.07) : "transparent"
                                }
                                contentItem: Item {
                                    VectorIcon {
                                        anchors.centerIn: parent; name: iconChoice.modelData.name; size: 16
                                        color: iconChoice.checked ? "#242424" : AppTheme.text
                                    }
                                }
                                KosToolTip { target: iconChoice; text: iconChoice.modelData.label; backdrop: editFolderDialog.contentBackdrop }
                            }
                        }
                    }
                }
            }
            SettingsGroup {
                Layout.fillWidth: true
                title: "位置"; titleColor: AppTheme.mutedText
                titleFont.pixelSize: 12; titleFont.weight: Font.Medium
                cornerRadius: 8; backgroundColor: editFolderDialog.formGroupColor
                SettingsItem {
                    title: "父级文件夹"; allowStacking: false
                    minimumLabelWidth: 72; horizontalPadding: 12
                    LiquidSelect {
                        id: folderParent; objectName: "editFolderParent"
                        implicitHeight: 24
                        Layout.fillWidth: true; Layout.minimumWidth: 0
                        materialForm: true; appearanceMode: AppTheme.appearanceMode
                        plainTrigger: true; indicatorStyle: "upDown"; indicatorBackground: true
                        popupMaterialForm: true; compactPopup: true; neutralOptionHighlight: true
                        popupBorderWidth: 0; popupShadowEnabled: true; popupFontWeight: Font.Normal
                        model: editFolderDialog.destinations.map(p => p ? p.split("/").join(" › ") : "无（侧栏文件夹）")
                        currentIndex: editFolderDialog.destinations.indexOf(editFolderDialog.selectedParent)
                        Accessible.name: "父级文件夹"
                        onActivated: function(index) { editFolderDialog.selectedParent = editFolderDialog.destinations[index] }
                    }
                }
            }
            Label {
                text: editFolderDialog.nameAlreadyUsed ? "这个位置已有同名文件夹，请修改名称或父级文件夹。" : editFolderDialog.errorMessage || root.controller.error
                visible: !!text; Layout.fillWidth: true
                textFormat: Text.PlainText; wrapMode: Text.Wrap; color: AppTheme.destructive; font.pixelSize: 11
            }
        }
        footer: Item {
            // 16px above and below the 30px actions; do not compress with the form.
            implicitHeight: 62
            RowLayout {
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                anchors.leftMargin: 16; anchors.rightMargin: 16; anchors.bottomMargin: 16
                spacing: 8
                Item { Layout.fillWidth: true }
                LiquidDialogButton { text: "取消"; enabled: !root.controller.busy; onClicked: editFolderDialog.close() }
                LiquidDialogButton {
                    text: editFolderDialog.creating ? (root.controller.busy ? "创建中…" : "创建") : (root.controller.busy ? "保存中…" : "保存")
                    primaryAction: true; enabled: editFolderDialog.canSave
                    surfaceColor: enabled ? tokens.primaryYellow : AppTheme.controlBackground
                    accentColor: tokens.primaryYellow
                    textColor: enabled ? "#242424" : AppTheme.disabledText
                    onClicked: editFolderDialog.submit()
                }
            }
        }
    }
    LiquidDialog {
        id: trashFolderDialog; title: "删除整个文件夹？"; sourceItem: root.sceneItem; anchors.centerIn: parent; width: Math.min(420, root.width - 40)
        standardButtons: Dialog.NoButton
        closePolicy: root.controller.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
        property string targetPath: ""
        property string errorMessage: ""
        onOpened: { targetPath = root.folderPath; errorMessage = ""; cancelFolderDelete.forceActiveFocus() }
        contentItem: ColumnLayout {
            spacing: 10
            Label { text: trashFolderDialog.targetPath.split("/").join(" › "); textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.Wrap; font.bold: true; color: AppTheme.text }
            Label { text: "将删除这个文件夹、子文件夹及其中的全部笔记。移到最近删除后可以恢复。"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: AppTheme.text }
            Label { text: "直接删除不会进入最近删除，无法撤销；其中笔记的历史版本也会删除。"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: AppTheme.destructive }
            Label { text: trashFolderDialog.errorMessage || root.controller.error; visible: !!text; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText; color: AppTheme.destructive }
        }
        footer: Item {
            implicitHeight: 62
            RowLayout {
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                anchors.leftMargin: 16; anchors.rightMargin: 16; anchors.bottomMargin: 16; spacing: 8
                LiquidDialogButton {
                    text: "直接删除"; destructive: true; enabled: !root.controller.busy
                    surfaceColor: enabled ? AppTheme.destructive : AppTheme.controlBackground
                    textColor: enabled ? "white" : AppTheme.disabledText
                    onClicked: { trashFolderDialog.errorMessage = ""; root.controller.manageFolder("purgeFolder", trashFolderDialog.targetPath) }
                }
                Item { Layout.fillWidth: true }
                LiquidDialogButton { id: cancelFolderDelete; text: "取消"; enabled: !root.controller.busy; onClicked: trashFolderDialog.close() }
                LiquidDialogButton {
                    text: "移到最近删除"; primaryAction: true; enabled: !root.controller.busy
                    onClicked: { trashFolderDialog.errorMessage = ""; root.controller.manageFolder("trashFolder", trashFolderDialog.targetPath) }
                }
            }
        }
    }
    Label { visible: !!root.notice; text: root.notice; color: AppTheme.mutedText; anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.margins: 20; Timer { running: !!root.notice; interval: 4500; onTriggered: root.notice = "" } }
}
