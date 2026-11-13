import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kos.Ui

Item {
    id: root
    required property var controller
    required property Item sceneItem
    readonly property bool popupActive: menu.visible || filePicker.visible || folderPicker.visible || externalPicker.visible || review.visible || result.visible || history.visible || exportPicker.visible || backupPicker.visible || restorePicker.visible || restoreConfirm.visible || transfer.visible || backupInfo.visible || operationErrorDialog.visible || exportDone.visible
    property var report: controller.importReport
    property var sources: []
    property string destination: ""
    property bool preserveStructure: true
    property bool keepDuplicates: false
    property var exportPaths: []
    property string exportFolder: ""
    property url exportDirectory
    property int exportCount: 0
    property var exportWarnings: []
    property url restoreSource
    property bool backupWorking: false
    property string operationError: ""
    readonly property var progress: controller.operationProgress
    readonly property string phaseName: ({scanning: "正在查找笔记…", reviewing: "正在读取导入内容…", preparing: "正在检查笔记…", importing: "正在导入…", finished: "正在整理导入结果…"})[progress.phase] || "正在处理…"
    signal browseRequested()
    function openImportMenu(anchor) { menu.openFromButton(anchor) }
    function showImportHistory() { controller.inspectImports(); history.open() }
    function createBackup() { backupPicker.selectedFile = String(backupPicker.currentFolder).replace(/\/$/, "") + "/" + encodeURIComponent("笔记备份-" + Qt.formatDateTime(new Date(), "yyyyMMdd-HHmmss") + ".nkbackup"); backupPicker.open() }
    function restoreBackup() { restorePicker.open() }
    function importLegacy() { destination = ""; preview(["file://" + controller.legacyLibrary.split("/").map(encodeURIComponent).join("/")]) }
    function openExternalFile() { externalPicker.open() }
    function importFiles() { destination = controller.filter.startsWith("folder:") ? controller.filter.slice(7) : ""; filePicker.open() }
    function importFolder() { destination = controller.filter.startsWith("folder:") ? controller.filter.slice(7) : ""; folderPicker.open() }
    function preview(values) { keepDuplicates = false; sources = values; controller.previewImport(values, destination, preserveStructure) }
    function exportSelection(paths, folder) { exportPaths = paths || []; exportFolder = folder || ""; exportPicker.open() }
    Connections {
        target: root.controller
        function onChanged() {
            if (!root.controller.busy && !root.controller.importPlan.count && !root.controller.importPlan.folderCount && review.visible) review.close()
            if (root.backupWorking && !root.controller.busy) root.backupWorking = false
            if ((root.progress.phase || root.backupWorking) && !review.visible) { if (!transfer.visible) transfer.open() }
            else transfer.close()
        }
        function onLibraryOperationFailed(message) { root.backupWorking = false; transfer.close(); review.close(); root.operationError = message; operationErrorDialog.open() }
        function onNotesExported(directory, count, warnings) { root.exportDirectory = directory; root.exportCount = count; root.exportWarnings = warnings || []; exportDone.open() }
        function onBackupFinished() { root.backupWorking = false; transfer.close(); backupInfo.open(); root.browseRequested() }
        function onImportPreviewReady() {
            root.sources = Array.from(root.controller.importPlan.sources || [])
            root.destination = root.controller.importPlan.folder || ""
            root.preserveStructure = root.controller.importPlan.keepStructure
            review.open()
        }
        function onImportFinished() { root.report = root.controller.importReport; review.close(); if (root.report.cancelled || (root.report.failed || 0) > 0 || (root.report.skipped || 0) > 0 || (root.report.warnings || []).length) result.open(); root.browseRequested() }
    }
    NotesFilePicker {
        systemFileDialogs: root.controller.systemFileDialogs
        onFailed: function(message) { root.operationError = message; operationErrorDialog.open() }
        sourceItem: root.sceneItem; acceptText: "选择"; rejectText: "取消"
        locationPlaceholder: "输入文件夹路径"; fileNamePlaceholder: "文件名"; emptyText: "此文件夹中没有可选文件"
        overwriteTitle: "替换这个文件？"; overwriteText: "目标位置已有同名文件，替换后将覆盖原内容。"; overwriteAcceptText: "替换"; unavailableText: "文件夹不存在或无法访问"; id: filePicker; title: "选择要导入的笔记"; mode: "openFiles"; nameFilters: ["*.md", "*.MD"]; onAccepted: root.preview(selectedFiles) }
    NotesFilePicker {
        systemFileDialogs: root.controller.systemFileDialogs
        onFailed: function(message) { root.operationError = message; operationErrorDialog.open() }
        sourceItem: root.sceneItem; acceptText: "选择"; rejectText: "取消"
        locationPlaceholder: "输入文件夹路径"; fileNamePlaceholder: "文件名"; emptyText: "此文件夹中没有可选文件"
        overwriteTitle: "替换这个文件？"; overwriteText: "目标位置已有同名文件，替换后将覆盖原内容。"; overwriteAcceptText: "替换"; unavailableText: "文件夹不存在或无法访问"; mode: "folder"; id: folderPicker; title: "选择要收集的文件夹"; onAccepted: root.preview([selectedFolder]) }
    NotesFilePicker {
        systemFileDialogs: root.controller.systemFileDialogs
        onFailed: function(message) { root.operationError = message; operationErrorDialog.open() }
        sourceItem: root.sceneItem; acceptText: "选择"; rejectText: "取消"
        locationPlaceholder: "输入文件夹路径"; fileNamePlaceholder: "文件名"; emptyText: "此文件夹中没有可选文件"
        overwriteTitle: "替换这个文件？"; overwriteText: "目标位置已有同名文件，替换后将覆盖原内容。"; overwriteAcceptText: "替换"; unavailableText: "文件夹不存在或无法访问"; id: externalPicker; title: "打开外部 Markdown"; nameFilters: ["*.md", "*.MD"]; onAccepted: root.controller.openExternal(selectedFile) }
    NotesFilePicker {
        systemFileDialogs: root.controller.systemFileDialogs
        onFailed: function(message) { root.operationError = message; operationErrorDialog.open() }
        sourceItem: root.sceneItem; acceptText: "选择"; rejectText: "取消"
        locationPlaceholder: "输入文件夹路径"; fileNamePlaceholder: "文件名"; emptyText: "此文件夹中没有可选文件"
        overwriteTitle: "替换这个文件？"; overwriteText: "目标位置已有同名文件，替换后将覆盖原内容。"; overwriteAcceptText: "替换"; unavailableText: "文件夹不存在或无法访问"; mode: "folder"; id: exportPicker; title: "选择导出位置"; onAccepted: root.controller.exportNotes(selectedFolder, root.exportPaths, root.exportFolder) }
    NotesFilePicker {
        systemFileDialogs: root.controller.systemFileDialogs
        onFailed: function(message) { root.operationError = message; operationErrorDialog.open() }
        id: backupPicker; sourceItem: root.sceneItem; title: "创建完整备份"; mode: "saveFile"
        acceptText: "保存备份"; rejectText: "取消"; defaultSuffix: "nkbackup"; nameFilters: ["*.nkbackup"]
        locationPlaceholder: "输入文件夹路径"; fileNamePlaceholder: "备份名称"; emptyText: "此文件夹中没有备份"
        overwriteTitle: "替换这个备份？"; overwriteText: "同名备份将被替换。"; overwriteAcceptText: "替换"; unavailableText: "文件夹不存在或无法访问"
        onAccepted: { root.backupWorking = true; root.controller.createBackup(selectedFile) }
    }
    NotesFilePicker {
        systemFileDialogs: root.controller.systemFileDialogs
        onFailed: function(message) { root.operationError = message; operationErrorDialog.open() }
        id: restorePicker; sourceItem: root.sceneItem; title: "选择笔记备份"; mode: "openFile"
        acceptText: "继续"; rejectText: "取消"; nameFilters: ["*.nkbackup"]
        locationPlaceholder: "输入文件夹路径"; emptyText: "此文件夹中没有备份"; unavailableText: "文件夹不存在或无法访问"
        onAccepted: { root.restoreSource = selectedFile; restoreConfirm.open() }
    }
    LiquidDialog {
        id: restoreConfirm; sourceItem: root.sceneItem; title: "恢复这个备份？"
        height: Math.min(360, parent.height - 40)
        standardButtons: Dialog.Ok | Dialog.Cancel; acceptText: "恢复备份"; rejectText: "取消"
        acceptEnabled: !root.controller.busy
        contentItem: ScrollView {
            id: restoreReviewScroll; clip: true; contentWidth: availableWidth
            Label { width: restoreReviewScroll.availableWidth; text: (root.controller.recoveryAvailable ? "当前笔记空间无法打开。将先保留原空间目录，再从备份恢复笔记、附件、历史和最近删除。原空间不会被覆盖，外部 Markdown 文件不受影响。" : "将用备份替换当前笔记空间，包括附件、历史和最近删除。恢复前会自动保存当前空间的完整副本。外部 Markdown 文件不受影响。") + "\n\n" + decodeURIComponent(String(root.restoreSource).split("/").pop()); textFormat: Text.PlainText; wrapMode: Text.Wrap; color: AppTheme.text }
        }
        onAccepted: { root.backupWorking = true; root.controller.restoreBackup(root.restoreSource) }
    }
    LiquidDialog {
        id: transfer; sourceItem: root.sceneItem; title: root.backupWorking ? "完整备份与恢复" : "导入笔记"
        standardButtons: Dialog.NoButton; closePolicy: Popup.NoAutoClose
        width: Math.min(440,parent.width-40)
        contentItem: ColumnLayout {
            spacing: 14
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: root.backupWorking ? root.controller.status : root.progress.cancelling ? "正在停止，请稍候…" : root.phaseName; color: AppTheme.text }
            Label { visible: !root.backupWorking; text: root.progress.total ? (root.progress.completed || 0) + " / " + root.progress.total + " 篇" : "正在扫描文件夹"; color: AppTheme.mutedText }
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; visible: !root.backupWorking; text: "停止后保留已经完成的笔记。再次导入时可跳过重复内容。"; color: AppTheme.mutedText; font.pixelSize: 12 }
        }
        footer: DialogButtonBox {
            buttonLayout: DialogButtonBox.MacLayout; padding: 24; topPadding: 12; alignment: Qt.AlignRight; background: Item {}
            visible: !root.backupWorking
            LiquidDialogButton { text: "停止"; enabled: !root.progress.cancelling; onClicked: root.controller.cancelImportOperation() }
        }
    }
    LiquidDialog {
        id: exportDone; sourceItem: root.sceneItem; title: "导出完成"
        standardButtons: Dialog.Close; closeText: "完成"
        width: Math.min(480,parent.width-40); height: Math.min(320,parent.height-40)
        contentItem: ScrollView {
            id: exportSummary; clip: true; contentWidth: availableWidth
            ColumnLayout {
                width: exportSummary.availableWidth; spacing: 12
                Label { Layout.fillWidth: true; text: "已导出 " + root.exportCount + " 篇笔记。"; wrapMode: Text.Wrap; color: AppTheme.text }
                Label { Layout.fillWidth: true; text: "保存位置"; color: AppTheme.mutedText; font.pixelSize: 12 }
                Label { Layout.fillWidth: true; text: decodeURIComponent(String(root.exportDirectory).replace(/^file:\/\//, "")); textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; color: AppTheme.mutedText; font.pixelSize: 12 }
                Label { Layout.fillWidth: true; visible: root.exportWarnings.length > 0; text: "部分链接未能完整导出，原文已保留："; wrapMode: Text.Wrap; color: AppTheme.text }
                Repeater {
                    model: root.exportWarnings
                    Label { required property var modelData; Layout.fillWidth: true; text: (modelData.path ? modelData.path + "：" : "") + modelData.message; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; color: AppTheme.mutedText; font.pixelSize: 12 }
                }
                LiquidActionButton { text: "查看导出内容"; materialForm: true; onClicked: { Qt.openUrlExternally(root.exportDirectory); exportDone.close() } }
            }
        }
    }
    LiquidDialog {
        id: operationErrorDialog; sourceItem: root.sceneItem; title: "操作未完成"
        standardButtons: Dialog.Close; closeText: "关闭"
        contentItem: Label { text: root.operationError; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: AppTheme.text }
    }
    LiquidDialog {
        id: backupInfo; sourceItem: root.sceneItem
        readonly property bool restored: !!(root.controller.backupResult.safetyCopy || root.controller.backupResult.preservedDirectory)
        title: restored ? "备份已恢复" : "备份已保存"
        standardButtons: Dialog.Close; closeText: "完成"
        contentItem: ColumnLayout {
            spacing: 12
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: root.controller.backupResult.preservedDirectory ? "已恢复笔记、附件、历史和最近删除。原笔记空间的完整目录保留在：" : root.controller.backupResult.safetyCopy ? "已恢复笔记、附件、历史和最近删除。恢复前的内容保存在：" : "完整备份包含笔记、文件夹、标签、附件、历史、导入记录和最近删除。保存位置："; color: AppTheme.text }
            Label { Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; text: root.controller.backupResult.preservedDirectory || root.controller.backupResult.safetyCopy || root.controller.backupResult.file || ""; color: AppTheme.mutedText }
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; visible: (root.controller.backupResult.cacheFailures || 0) > 0; text: "部分附件缓存未能刷新。附件原件仍在备份和数据库中，请检查磁盘空间后重新打开应用。"; color: AppTheme.mutedText }
        }
    }
    NotesPopover {
        id: menu; sourceItem: root.sceneItem
        LiquidPopoverItem { text: "导入笔记…"; enabled: !root.controller.busy; onTriggered: root.importFiles() }
        LiquidPopoverItem { text: "从文件夹收集…"; enabled: !root.controller.busy; onTriggered: root.importFolder() }
    }
    LiquidDialog {
        acceptText: "确认"; rejectText: "取消"; closeText: "关闭"
        id: review; onRejected: root.controller.cancelImport(); title: "导入“" + (root.controller.importPlan.name || "笔记") + "”"; sourceItem: root.sceneItem
        parent: Overlay.overlay; anchors.centerIn: parent
        width: Math.min(480, parent.width - 40); height: Math.min(360 + ((root.controller.importPlan.duplicates || 0) > 0 ? 36 : 0), parent.height - 40)
        standardButtons: Dialog.NoButton
        closePolicy: root.controller.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
        footer: DialogButtonBox {
        buttonLayout: DialogButtonBox.MacLayout
            padding: 24; topPadding: 12; spacing: 8; alignment: Qt.AlignRight
            background: Item {}
            LiquidDialogButton { text: root.controller.busy ? "停止读取" : "取消"; enabled: !root.progress.cancelling; onClicked: { if(root.controller.busy)root.controller.cancelImportOperation();else review.reject() } }
            LiquidDialogButton { primaryAction: true; text: root.controller.busy ? "正在导入…" : "导入"; enabled: !root.controller.busy && ((root.controller.importPlan.count || 0) > 0 || (root.controller.importPlan.folderCount || 0) > 0); onClicked: { review.close(); root.controller.confirmImport(root.keepDuplicates) } }
        }
        contentItem: ScrollView {
            id: reviewScroll; clip: true; contentWidth: availableWidth
            ColumnLayout {
                width: reviewScroll.availableWidth; spacing: 14
                Label { Layout.fillWidth: true; text: "找到 " + (root.controller.importPlan.count || 0) + " 篇笔记、" + (root.controller.importPlan.folderCount || 0) + " 个文件夹。"; wrapMode: Text.Wrap; color: AppTheme.text }
                Label { text: "导入到"; color: AppTheme.mutedText; font.pixelSize: 12 }
                LiquidSelect {
                    appearanceMode: AppTheme.appearanceMode;
                    Layout.fillWidth: true; materialForm: true
                    readonly property var locations: [""].concat(Array.from(root.controller.folders))
                    model: locations.map(p => p ? p.split("/").join(" › ") : "未分类")
                    currentIndex: Math.max(0, locations.indexOf(root.destination))
                    enabled: !root.controller.busy; Accessible.name: "导入目标文件夹"
                    onActivated: function(index) { root.destination = locations[index]; root.controller.previewImport(root.sources, root.destination, root.preserveStructure) }
                }
                LiquidCheckBox { visible: !!root.controller.importPlan.hasFolders; Layout.fillWidth: true; textWrapMode: Text.WordWrap; text: "保留文件夹结构"; checked: root.preserveStructure; enabled: !root.controller.busy; onClicked: { root.preserveStructure = checked; root.controller.previewImport(root.sources, root.destination, checked) } }
                LiquidCheckBox { Layout.fillWidth: true; textWrapMode: Text.WordWrap; visible: (root.controller.importPlan.duplicates || 0) > 0; text: "保留重复副本（发现 " + (root.controller.importPlan.duplicates || 0) + " 篇已导入笔记）"; checked: root.keepDuplicates; enabled: !root.controller.busy; onClicked: root.keepDuplicates = checked }
                Label { Layout.fillWidth: true; text: "会收集可访问的关联附件。需要一起导入相对路径的附件时，请使用“从文件夹收集”。创建独立副本，不修改原文件，也不自动同步后续修改。"; wrapMode: Text.Wrap; color: AppTheme.mutedText; font.pixelSize: 12 }
                Label { Layout.fillWidth: true; visible: !!(root.controller.importPlan.problems || []).length; text: (root.controller.importPlan.problems || []).length + " 项无法读取或格式不支持，完成后可查看详情。"; wrapMode: Text.Wrap; color: AppTheme.mutedText; font.pixelSize: 12 }
            }
        }
    }

    LiquidDialog {
        acceptText: "确认"; rejectText: "取消"; closeText: "关闭"
        id: result; title: (root.report.cancelled || root.report.interrupted) ? "导入未完成" : "导入完成"; sourceItem: root.sceneItem; parent: Overlay.overlay; anchors.centerIn: parent
        width: Math.min(560, parent.width - 40); height: Math.min(490, parent.height - 40); standardButtons: Dialog.Close
        contentItem: ColumnLayout {
            Label { Layout.fillWidth: true; text: ((root.report.cancelled || root.report.interrupted) ? "已保留完成的内容，未处理 " + (root.report.remaining || 0) + " 篇。\n" : "") + "已导入 " + (root.report.succeeded || 0) + " 篇笔记、" + (root.report.folderCount || 0) + " 个文件夹，跳过 " + (root.report.skipped || 0) + " 篇，失败 " + (root.report.failed || 0) + " 篇。"; wrapMode: Text.Wrap; color: AppTheme.text }
            Label { Layout.fillWidth: true; visible: (root.report.warnings || []).length > 0; text: "有需要留意的项目，请查看下面的说明。"; wrapMode: Text.Wrap; color: AppTheme.mutedText }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 10
                model: (root.report.results || []).filter(i => i.status !== "imported").concat(root.report.warnings || [])
                delegate: Column {
                    required property var modelData
                    width: ListView.view.width; spacing: 4
                    Label { width: parent.width; text: modelData.source || ""; textFormat: Text.PlainText; elide: Text.ElideMiddle; color: AppTheme.text; font.pixelSize: 12 }
                    Label { width: parent.width; text: modelData.message || ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: AppTheme.mutedText; font.pixelSize: 12 }
                }
                Label { anchors.centerIn: parent; visible: parent.count === 0; text: "所有内容已导入。原文件保持不变。"; color: AppTheme.mutedText }
            }
        }
    }
    LiquidDialog {
        acceptText: "确认"; rejectText: "取消"; closeText: "关闭"
        id: history; title: "导入记录"; sourceItem: root.sceneItem; parent: Overlay.overlay; anchors.centerIn: parent
        width: Math.min(540, parent.width - 40); height: Math.min(480, parent.height - 40); standardButtons: Dialog.Close
        contentItem: ListView {
            clip: true; spacing: 4; model: root.controller.importHistory
            delegate: ItemDelegate {
                required property var modelData
                width: ListView.view.width; height: 64
                contentItem: Column {
                    spacing: 4
                    Label { width: Math.max(0, parent.width - 60); text: modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight; color: AppTheme.text }
                    Label { width: Math.max(0, parent.width - 60); elide: Text.ElideRight; text: Qt.formatDateTime(new Date(modelData.time), "yyyy/M/d HH:mm") + " · " + (modelData.report.succeeded || 0) + " 篇" + (modelData.report.interrupted || modelData.report.cancelled ? " · 未完成" : ""); color: AppTheme.mutedText; font.pixelSize: 12 }
                }
                LiquidActionButton { materialForm: true; implicitHeight: 26; implicitWidth: 52; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; text: "详情"; onClicked: { root.report = modelData.report; history.close(); result.open() } }
                onClicked: { root.controller.filter = "import:" + modelData.id; root.controller.query = ""; history.close(); root.browseRequested() }
            }
            Label { anchors.centerIn: parent; visible: parent.count === 0; text: "还没有导入记录"; color: AppTheme.mutedText }
        }
    }
}
