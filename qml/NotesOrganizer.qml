import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kos.Ui

Item {
    id: root
    required property var controller
    required property Item sceneItem
    property var selectedPaths: []
    readonly property bool selectionMode: selectedPaths.length > 0
    property string selectionAnchor: ""
    property var targets: []
    property string folderPath: ""
    property string notice: ""
    property Item menuOrigin: null
    readonly property string contextPath: menu.visible && targets.length === 1 ? targets[0] : ""
    readonly property bool popupActive: menu.visible || tagActions.visible || sortActions.visible || movePanel.visible || deletePanel.visible || resultPanel.visible || folderMenu.visible || renamePanel.visible || trashFolderDialog.visible || tagsPanel.visible || tagRenamePanel.visible || tagRenameConfirm.visible
    readonly property var targetNotes: controller.notes.filter(n => targets.indexOf(n.path) >= 0)
    property string scope: controller.library + "\n" + controller.filter + "\n" + controller.query
    onScopeChanged: clearSelection()
    function clearSelection() { selectedPaths = []; selectionAnchor = "" }
    function toggle(path) {
        let paths = selectedPaths.slice()
        const at = paths.indexOf(path)
        if (at >= 0) paths.splice(at, 1)
        else if (paths.length < 200) paths.push(path)
        else { notice = "一次最多选择 200 篇笔记"; return }
        selectedPaths = paths; selectionAnchor = paths.length ? path : ""
    }
    function click(note, index, modifiers) {
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
        clearSelection()
        selectionAnchor = note.path
        return false
    }
    function selectAll() {
        if (controller.notes.length > 200) { notice = "结果超过 200 篇，请先缩小筛选范围"; return }
        selectedPaths = controller.notes.map(n => n.path)
        if (!selectionAnchor && selectedPaths.length) selectionAnchor = selectedPaths[0]
    }
    function openMenu(anchor, path) {
        // Right-clicking an unselected row targets only that note; opening or
        // dismissing the context menu must not enter a sticky selection mode.
        if (path && selectedPaths.indexOf(path) < 0) clearSelection()
        targets = path ? (selectedPaths.indexOf(path) >= 0 ? selectedPaths.slice() : [path])
                       : selectedPaths.length ? selectedPaths.slice()
                       : controller.notes.some(n => n.path === controller.path) ? [controller.path] : []
        menuOrigin = anchor
        menu.preferredSide = path ? "right" : "bottom"
        menu.anchorAlignment = "end"
        menu.anchorItem = anchor
        menu.openAtAnchor()
    }
    function openTagRename() { tagRenamePanel.open() }
    function perform(action) { if (targets.length && !controller.busy) controller.organize(action, targets) }
    function openFolderMenu(path, anchor) { folderPath = path; folderMenu.anchorItem = anchor; folderMenu.openAtAnchor() }
    Connections {
        target: root.controller
        function onNotesChanged() {
            const visible = new Set(root.controller.notes.map(n => n.path))
            const paths = root.selectedPaths.filter(p => visible.has(p))
            if (JSON.stringify(paths) !== JSON.stringify(root.selectedPaths)) root.selectedPaths = paths
            if (root.selectionAnchor && !visible.has(root.selectionAnchor)) root.selectionAnchor = ""
        }
        function onTagRenameReady() { tagRenamePanel.close(); tagRenameConfirm.open() }
        function onBatchFinished() {
            const report = root.controller.batchReport
            const failed = (report.results || []).filter(r => !r.ok).map(r => r.path)
            if (report.action === "move" || report.action === "trash") root.selectedPaths = root.selectedPaths.filter(p => failed.indexOf(p) >= 0)
            if (report.failed) { resultPanel.anchorItem = root.sceneItem; resultPanel.open() }
        }
    }
    LiquidPopover {
        id: menu; sourceItem: root.sceneItem
        objectName: "noteListMenu"
        arrowEnabled: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnReleaseOutside
        height: Math.min(implicitHeight, Math.max(150, root.height - 32))
        readonly property bool rowMenu: preferredSide === "right"
        readonly property bool allPinned: root.targetNotes.length > 0 && root.targetNotes.every(n => n.pinned)
        readonly property bool allFavorite: root.targetNotes.length > 0 && root.targetNotes.every(n => n.favorite)
        LiquidPopoverItem { text: root.selectionMode ? "取消选择" : "选择笔记"; enabled: root.selectionMode || root.targets.length > 0; onTriggered: { if (root.selectionMode) root.clearSelection(); else { root.selectedPaths = root.targets.slice(); root.selectionAnchor = root.selectedPaths[0] || "" } } }
        LiquidPopoverItem { visible: !menu.rowMenu; height: visible ? implicitHeight : 0; text: "全选当前结果"; enabled: !!root.controller.notes.length; onTriggered: root.selectAll() }
        LiquidPopoverItem { visible: menu.rowMenu; height: visible ? implicitHeight : 0; text: menu.allPinned ? "取消置顶" : "置顶"; enabled: root.targets.length > 0 && !root.controller.busy; onTriggered: root.perform(menu.allPinned ? "unpin" : "pin") }
        LiquidPopoverItem { visible: menu.rowMenu; height: visible ? implicitHeight : 0; text: menu.allFavorite ? "取消收藏" : "收藏"; enabled: root.targets.length > 0 && !root.controller.busy; onTriggered: root.perform(menu.allFavorite ? "unfavorite" : "favorite") }
        LiquidPopoverItem { visible: menu.rowMenu; height: visible ? implicitHeight : 0; text: "标签…"; enabled: root.targets.length > 0 && !root.controller.busy; onTriggered: Qt.callLater(function() { tagActions.preferredSide = "right"; tagActions.anchorItem = root.menuOrigin; tagActions.openAtAnchor() }) }
        LiquidPopoverItem { visible: menu.rowMenu; height: visible ? implicitHeight : 0; text: "复制笔记"; enabled: root.targets.length > 0 && !root.controller.busy; onTriggered: root.perform("duplicate") }
        LiquidPopoverItem { visible: menu.rowMenu; height: visible ? implicitHeight : 0; text: "移动到文件夹…"; enabled: root.targets.length > 0 && !root.controller.busy; onTriggered: { movePanel.anchorItem = root.menuOrigin; movePanel.open() } }
        LiquidPopoverItem { visible: menu.rowMenu; height: visible ? implicitHeight : 0; text: "移到最近删除…"; enabled: root.targets.length > 0 && !root.controller.busy; onTriggered: deletePanel.open() }
        LiquidPopoverItem { visible: !menu.rowMenu; height: visible ? implicitHeight : 0; text: "排序…"; onTriggered: Qt.callLater(function() { sortActions.anchorItem = root.menuOrigin; sortActions.openAtAnchor() }) }
        LiquidPopoverItem { visible: !menu.rowMenu; height: visible ? implicitHeight : 0; text: "全库重命名标签…"; enabled: !root.controller.busy; onTriggered: root.openTagRename() }
    }
    LiquidPopover {
        id: tagActions; sourceItem: root.sceneItem
        LiquidPopoverItem { text: "添加标签…"; onTriggered: { tagsPanel.operation = "add"; tagsPanel.open() } }
        LiquidPopoverItem { text: "移除标签…"; onTriggered: { tagsPanel.operation = "remove"; tagsPanel.open() } }
    }
    LiquidPopover {
        id: sortActions; sourceItem: root.sceneItem
        LiquidPopoverItem { text: "按修改时间排序"; checkable: true; autoExclusive: true; checked: root.controller.sortKey === "modified"; onTriggered: root.controller.sortKey = "modified" }
        LiquidPopoverItem { text: "按创建时间排序"; checkable: true; autoExclusive: true; checked: root.controller.sortKey === "created"; onTriggered: root.controller.sortKey = "created" }
        LiquidPopoverItem { text: "按标题排序"; checkable: true; autoExclusive: true; checked: root.controller.sortKey === "title"; onTriggered: root.controller.sortKey = "title" }
        LiquidPopoverItem { text: root.controller.sortAscending ? "改为降序" : "改为升序"; onTriggered: root.controller.sortAscending = !root.controller.sortAscending }
    }

    LiquidDialog {
        id: tagsPanel; property string operation: "add"
        title: operation === "add" ? "批量添加标签" : "批量移除标签"
        sourceItem: root.sceneItem; anchors.centerIn: parent; width: Math.min(440, root.width - 40); standardButtons: Dialog.Cancel
        contentItem: ColumnLayout {
            Label { text: "处理所选 " + root.targets.length + " 篇笔记；其他标签和正文保持不变。"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: AppTheme.text }
            LiquidTextField { id: batchTags; Layout.fillWidth: true; materialForm: true; placeholderText: "标签，多个用逗号分隔"; Accessible.name: "批量标签" }
            LiquidActionButton { text: "应用标签"; materialForm: true; enabled: !!batchTags.text.trim() && !batchTags.inputMethodComposing && !root.controller.busy; onClicked: { root.controller.editTags(tagsPanel.operation, root.targets, batchTags.text.split(",").map(t => t.trim()).filter(t => t)); tagsPanel.close() } }
        }
    }
    LiquidDialog {
        id: tagRenamePanel; title: "全库重命名标签"; sourceItem: root.sceneItem; anchors.centerIn: parent; width: Math.min(440, root.width - 40); standardButtons: Dialog.Cancel
        contentItem: ColumnLayout {
            Label { text: "查找全库使用这个标签的笔记，先预览影响范围。"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: AppTheme.text }
            LiquidTextField { id: oldTag; Layout.fillWidth: true; materialForm: true; placeholderText: "原标签"; Accessible.name: "原标签" }
            LiquidTextField { id: newTag; Layout.fillWidth: true; materialForm: true; placeholderText: "新标签"; Accessible.name: "新标签" }
            LiquidActionButton { text: "预览标签重命名"; materialForm: true; enabled: !!oldTag.text.trim() && !!newTag.text.trim() && !oldTag.inputMethodComposing && !newTag.inputMethodComposing && !root.controller.busy; onClicked: root.controller.previewTagRename(oldTag.text, newTag.text) }
        }
    }
    LiquidDialog {
        id: tagRenameConfirm; title: "确认标签重命名"; sourceItem: root.sceneItem; anchors.centerIn: parent; width: Math.min(440, root.width - 40); standardButtons: Dialog.Cancel
        contentItem: ColumnLayout {
            Label { text: (root.controller.tagPlan.tags || []).join("") + " → " + (root.controller.tagPlan.replacement || "") + "\n影响 " + (root.controller.tagPlan.count || 0) + " 篇笔记。已有新标签会合并去重；预览后发生修改的笔记将跳过并报告。"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: AppTheme.text }
            LiquidActionButton { text: "确认重命名标签"; materialForm: true; enabled: root.controller.tagPlan.count > 0 && !root.controller.busy; onClicked: { root.controller.confirmTagRename(); tagRenameConfirm.close() } }
        }
    }
    LiquidContentPopover {
        id: movePanel; sourceItem: root.sceneItem; preferredSide: "bottom"; width: 350
        readonly property var folders: [""].concat(Array.from(root.controller.folders))
        contentItem: ColumnLayout {
            Label { text: "移动 " + root.targets.length + " 篇笔记"; font.bold: true; color: AppTheme.text }
            LiquidSelect { id: folderChoice; Layout.fillWidth: true; materialForm: true; model: movePanel.folders.map(p => p || "笔记库根目录"); Accessible.name: "移动目标文件夹"; onActivated: function(index) { folderChoice.currentIndex = index } }
            Label { text: "同名文件不会覆盖；无法安全移动的笔记保留原位，并逐项说明原因。"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: AppTheme.mutedText; font.pixelSize: 12 }
            LiquidActionButton { text: "确认移动"; materialForm: true; enabled: !root.controller.busy && folderChoice.currentIndex >= 0; onClicked: { root.controller.organize("move", root.targets, movePanel.folders[folderChoice.currentIndex]); movePanel.close() } }
        }
    }
    LiquidDialog {
        id: deletePanel; implicitHeight: 220; height: Math.min(220, root.height - 40); title: "移到最近删除？"; sourceItem: root.sceneItem; anchors.centerIn: parent; width: Math.min(420, root.width - 40)
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label { text: "先保存当前输入，再将所选 " + root.targets.length + " 篇笔记移到回收区；可以在恢复中心还原。"; wrapMode: Text.Wrap; color: AppTheme.text }
        onAccepted: root.perform("trash")
    }
    LiquidContentPopover {
        id: resultPanel; sourceItem: root.sceneItem; width: 430; preferredSide: "bottom"
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
            LiquidActionButton { text: "关闭"; materialForm: true; onClicked: resultPanel.close() }
        }
    }
    LiquidPopover {
        id: folderMenu; sourceItem: root.sceneItem
        LiquidPopoverItem { text: "新建子文件夹…"; onTriggered: { renamePanel.operation = "createFolder"; renameField.text = root.folderPath + "/"; renamePanel.anchorItem = folderMenu.anchorItem; renamePanel.open() } }
        LiquidPopoverItem { text: "移动 / 重命名文件夹…"; enabled: !root.controller.busy; onTriggered: { renamePanel.operation = "renameFolder"; renameField.text = root.folderPath; renamePanel.anchorItem = folderMenu.anchorItem; renamePanel.open() } }
        LiquidPopoverItem { text: "文件夹移到最近删除…"; enabled: !root.controller.busy; onTriggered: trashFolderDialog.open() }
        LiquidPopoverItem { text: "删除空文件夹"; enabled: !root.controller.busy; onTriggered: root.controller.manageFolder("removeFolder", root.folderPath) }
    }
    LiquidContentPopover {
        id: renamePanel; sourceItem: root.sceneItem; width: 350; preferredSide: "bottom"
        property string operation: "renameFolder"
        contentItem: ColumnLayout {
            Label { text: renamePanel.operation === "createFolder" ? "新建子文件夹" : "移动 / 重命名文件夹"; font.bold: true; color: AppTheme.text }
            LiquidTextField { id: renameField; Layout.fillWidth: true; materialForm: true; Accessible.name: "文件夹相对路径" }
            LiquidActionButton { text: "确认"; materialForm: true; enabled: !!renameField.text && !renameField.inputMethodComposing && !root.controller.busy; onClicked: { if (renamePanel.operation === "createFolder") root.controller.createFolder(renameField.text); else root.controller.manageFolder("renameFolder", root.folderPath, renameField.text); renamePanel.close() } }
        }
    }
    LiquidDialog {
        id: trashFolderDialog; title: "删除整个文件夹？"; sourceItem: root.sceneItem; anchors.centerIn: parent; width: Math.min(420, root.width - 40)
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label { text: "文件夹及全部内容将移到最近删除，可在恢复中心整体还原。\n" + root.folderPath; wrapMode: Text.Wrap; color: AppTheme.text }
        onAccepted: root.controller.manageFolder("trashFolder", root.folderPath)
    }
    Label { visible: !!root.notice; text: root.notice; color: AppTheme.mutedText; anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.margins: 20; Timer { running: !!root.notice; interval: 4500; onTriggered: root.notice = "" } }
}
