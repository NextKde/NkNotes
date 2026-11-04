import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs
import Kos.Ui

ApplicationWindow {
    id: root
    required property var session
    required property var controller
    width: controller.preference("width", tokens.windowWidth)
    height: controller.preference("height", tokens.windowHeight)
    minimumWidth: 540; minimumHeight: 480
    visible: true
    title: controller.path ? controller.path + " — NextNotes" : "NextNotes"
    flags: Qt.Window | Qt.FramelessWindowHint
    color: AppTheme.contentBackground
    property bool allowClose: false
    property bool sidebarRequested: controller.preference("sidebar", true)
    property bool listRequested: false
    property real sidebarWidth: controller.preference("sidebarWidth", tokens.sidebarWidth)
    property real listWidth: controller.preference("listWidth", tokens.listWidth)
    readonly property bool hasLibrary: controller.library.length > 0
    readonly property bool showSidebar: hasLibrary && sidebarRequested && width >= 1000
    readonly property bool showList: hasLibrary && (width >= 800 || listRequested || !controller.path)
    readonly property real editorX: (showSidebar ? sidebarWidth : 0) + (showList ? (width < 800 ? width : listWidth) : 0)
    readonly property bool popupsOpen: actionMenu.visible || formatMenu.visible || namePanel.visible || recoveryDialog.visible || discardDialog.visible || deleteDialog.visible || folderPanel.visible || appearancePanel.visible || discardRecoveryDialog.visible || libraryPicker.visible
    onClosing: function(close) {
        controller.setPreference("width", width); controller.setPreference("height", height)
        if (!allowClose) { close.accepted = false; controller.requestClose() }
    }
    onSidebarRequestedChanged: controller.setPreference("sidebar", sidebarRequested)
    onSidebarWidthChanged: controller.setPreference("sidebarWidth", sidebarWidth)
    onListWidthChanged: controller.setPreference("listWidth", listWidth)
    Binding { target: AppTheme; property: "compactDesign"; value: true }
    Component.onCompleted: {
        AppTheme.reduceTransparency = controller.preference("reduceTransparency", false)
        AppTheme.reduceMotion = controller.preference("reduceMotion", false)
        AppTheme.appearanceMode = controller.preference("appearance", "system")
    }
    NotesTokens { id: tokens }
    Connections {
        target: controller
        function onCloseReady() { root.allowClose = true; root.close() }
        function onDocumentLoaded() { root.listRequested = false }
    }
    CommandRegistry {
        id: commands
        session: root.session; editor: source.editor; controller: root.controller
        blocked: root.popupsOpen || search.inputMethodComposing || localFind.inputMethodComposing
        onSidebarRequested: root.sidebarRequested = !root.sidebarRequested
        onSearchRequested: { root.listRequested = true; Qt.callLater(function() { search.forceActiveFocus(Qt.ShortcutFocusReason) }) }
        onFindRequested: { findBar.visible = true; localFind.forceActiveFocus() }
    }
    // One Tab stop per list; arrows move the keyboard cursor without opening a
    // document. Activation still goes through the controller's save/drain path.
    component KeyboardList: ListView {
        activeFocusOnTab: count > 0
        keyNavigationEnabled: false
        property string cursorKey: ""
        function keyAt(row) {
            if (!model || row < 0 || row >= count) return ""
            const entry = model[row]
            return typeof entry === "string" ? entry : entry.id + "\n" + entry.path
        }
        function focusCurrent() {
            if (!count) return
            if (currentIndex < 0) currentIndex = 0
            positionViewAtIndex(currentIndex, ListView.Contain)
            if (currentItem) currentItem.forceActiveFocus(Qt.TabFocusReason)
        }
        function restoreCursor() {
            let row = -1
            for (let i = 0; i < count; ++i) {
                if (keyAt(i) === cursorKey) { row = i; break }
            }
            currentIndex = row >= 0 ? row : count ? 0 : -1
            cursorKey = keyAt(currentIndex)
            if (activeFocus) focusCurrent()
        }
        function handleKey(event) {
            if (event.modifiers !== Qt.NoModifier) return
            let row = currentIndex
            if (event.key === Qt.Key_Down) row = Math.min(count - 1, row + 1)
            else if (event.key === Qt.Key_Up) row = Math.max(0, row - 1)
            else if (event.key === Qt.Key_Home) row = 0
            else if (event.key === Qt.Key_End) row = count - 1
            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                if (currentItem && !event.isAutoRepeat) currentItem.activate()
                event.accepted = true
                return
            } else return
            currentIndex = row
            cursorKey = keyAt(row)
            focusCurrent()
            event.accepted = true
        }
        onActiveFocusChanged: if (activeFocus) focusCurrent()
        onModelChanged: Qt.callLater(restoreCursor)
        Keys.onPressed: function(event) { handleKey(event) }
    }
    FolderDialog {
        id: libraryPicker
        title: "选择已有文件夹作为笔记库"
        onAccepted: root.controller.selectLibrary(selectedFolder)
    }
    header: Item {
        height: 38
        MouseArea {
            anchors.fill: parent; anchors.rightMargin: windowControls.width + 28
            onPressed: root.startSystemMove()
            onDoubleClicked: root.visibility === Window.Maximized ? root.showNormal() : root.showMaximized()
        }
        Label {
            anchors.left: parent.left; anchors.leftMargin: 20; anchors.verticalCenter: parent.verticalCenter
            text: "NextNotes"; color: AppTheme.text; font.pixelSize: 12; font.weight: Font.DemiBold
        }
        KosWindowControls {
            id: windowControls
            targetWindow: root; actions: ["maximize", "minimize", "close"]; maximizeMode: true
            circleSize: 14; buttonWidth: 22; buttonHeight: 22; spacing: 6
            anchors.right: parent.right; anchors.rightMargin: 14; anchors.verticalCenter: parent.verticalCenter
        }
    }
    // Only ordinary content belongs to the sampled scene. Controls, navigation,
    // popup consumers and the capture itself are independent siblings.
    Item {
        id: readingScene
        anchors.fill: parent
        Rectangle { anchors.fill: parent; color: AppTheme.contentBackground }
        SourceEditor {
            id: source
            session: root.session; controller: root.controller
            visible: root.hasLibrary && !!controller.path && root.editorX < root.width
            x: root.editorX; y: tokens.toolbarHeight * 2 + (findBar.visible ? 36 : 0)
            width: Math.max(0, parent.width - x); height: Math.max(0, parent.height - y - statusArea.height)
            bodySize: 12 + Math.round(bodySizeSlider.value * 12); readingWidth: tokens.readingWidth; inset: tokens.inset
        }
    }
    LiquidBackdrop { id: readingBackdrop; sourceItem: readingScene; registerAsDefault: false }
    KosEmptyState {
        anchors.centerIn: parent; width: Math.min(420, root.width - 64)
        visible: !root.hasLibrary
        symbol: ""; title: "让想法有处可归"
        description: "选择一个本地文件夹，开始记录。\n笔记以 Markdown 文件保存，由你掌握。"
        actionText: controller.busy ? "正在打开…" : "选择笔记库"; actionEnabled: !controller.busy
        onActionTriggered: libraryPicker.open()
    }
    KosSidebar {
        id: sidebar
        visible: root.showSidebar
        x: 0; y: 0; height: parent.height; width: root.sidebarWidth
        expanded: root.showSidebar; expandedWidth: root.sidebarWidth
        floating: false; surfaceColor: "transparent"
        Rectangle { anchors.fill: parent; color: AppTheme.contentSecondaryBackground }
        // No distinct content underneath the sidebar: use the specified opaque fallback.
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 14; spacing: 4
            Label { text: "资料库"; color: AppTheme.text; font.pixelSize: 20; font.weight: Font.DemiBold; Layout.topMargin: 14; Layout.bottomMargin: 20 }
            LiquidNavigationButton { Layout.fillWidth: true; text: "全部笔记"; specificationForm: true; checked: controller.filter === "all"; onClicked: { controller.filter = "all"; root.listRequested = true } }
            LiquidNavigationButton { Layout.fillWidth: true; text: "收藏"; specificationForm: true; checked: controller.filter === "favorite"; onClicked: { controller.filter = "favorite"; root.listRequested = true } }
            LiquidNavigationButton { Layout.fillWidth: true; text: "最近删除"; specificationForm: true; checkable: false; onClicked: root.showRecovery() }
            RowLayout {
                Layout.fillWidth: true; Layout.topMargin: 22
                Label { text: "文件夹"; color: AppTheme.mutedText; font.pixelSize: 11; Layout.fillWidth: true }
                LiquidActionButton { id: folderButton; text: "新建"; implicitHeight: 26; materialForm: false; liquidIntensity: "light"; backdrop: readingBackdrop; onClicked: root.askName("folder", folderButton) }
            }
            KeyboardList {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                model: controller.folders; spacing: 2
                delegate: LiquidNavigationButton {
                    required property var modelData
                    required property int index
                    focusPolicy: Qt.StrongFocus
                    activeFocusOnTab: false
                    Keys.onPressed: function(event) { ListView.view.handleKey(event) }
                    onActiveFocusChanged: if (activeFocus) { ListView.view.currentIndex = index; ListView.view.cursorKey = ListView.view.keyAt(index) }
                    function activate() { controller.filter = "folder:" + modelData; root.listRequested = true }
                    width: ListView.view.width; text: modelData; specificationForm: true
                    checked: controller.filter === "folder:" + modelData
                    onClicked: activate()
                    Accessible.role: Accessible.ListItem
                    Accessible.focusable: true
                    Accessible.selected: checked
                    Accessible.onPressAction: activate()
                    Accessible.name: "文件夹 " + modelData
                    KosToolTip { target: parent; backdrop: readingBackdrop; text: "打开此文件夹中的笔记" }
                }
            }
            Label { Layout.fillWidth: true; text: controller.library; elide: Text.ElideMiddle; font.pixelSize: 10; color: AppTheme.mutedText }
            LiquidActionButton { text: "切换笔记库"; implicitHeight: 26; materialForm: false; liquidIntensity: "light"; backdrop: readingBackdrop; enabled: !controller.busy; onClicked: libraryPicker.open() }
        }
    }
    Rectangle {
        id: listColumn
        visible: root.showList
        x: root.showSidebar ? root.sidebarWidth : 0
        width: root.width < 800 ? root.width : root.listWidth; height: parent.height
        color: AppTheme.contentSecondaryBackground
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 16; spacing: 12
            RowLayout {
                Layout.fillWidth: true
                Label { Layout.fillWidth: true; text: controller.filter === "favorite" ? "收藏" : controller.filter.startsWith("folder:") ? controller.filter.slice(7) : "全部笔记"; elide: Text.ElideRight; color: AppTheme.text; font.pixelSize: 20; font.weight: Font.DemiBold }
                LiquidActionButton { id: newButton; text: "新建"; implicitHeight: 26; materialForm: false; liquidIntensity: "light"; backdrop: readingBackdrop; enabled: !controller.busy; onClicked: controller.createNote(); KosToolTip { target: newButton; backdrop: readingBackdrop; text: "新建笔记 · Ctrl+N" } }
                LiquidActionButton { visible: root.width < 800 && !!controller.path; text: "返回"; implicitHeight: 26; materialForm: true; onClicked: root.listRequested = false }
            }
            LiquidTextField {
                id: search
                Layout.fillWidth: true; implicitHeight: 30
                materialForm: false; liquidFinish: true; liquidIntensity: "light"; backdrop: readingBackdrop
                placeholderText: "搜索标题与正文"; text: controller.query
                onTextEdited: if (!inputMethodComposing) controller.query = text
                onInputMethodComposingChanged: if (!inputMethodComposing) controller.query = text
                Accessible.name: "搜索所有笔记"
            }
            RowLayout {
                visible: !root.showSidebar; Layout.fillWidth: true
                LiquidActionButton { text: "全部"; materialForm: true; onClicked: controller.filter = "all" }
                LiquidActionButton { text: "收藏"; materialForm: true; onClicked: controller.filter = "favorite" }
                LiquidActionButton { text: "资料库"; materialForm: true; onClicked: root.showRecovery() }
                LiquidActionButton { id: smallFolderButton; text: "文件夹"; materialForm: true; onClicked: { folderPanel.anchorItem = smallFolderButton; folderPanel.open() } }
            }
            Label { text: controller.notes.length + " 篇笔记 · 最近修改"; color: AppTheme.mutedText; font.pixelSize: 11 }
            KeyboardList {
                id: noteList
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 4
                model: controller.notes
                delegate: ItemDelegate {
                    id: noteRow
                    required property var modelData
                    required property int index
                    readonly property string rowStatus: [modelData.favorite ? "收藏" : "", modelData.id === controller.selectedId && modelData.path === controller.path ? controller.status : modelData.readOnlyReason ? "只读" : ""].filter(Boolean).join(" · ")
                    focusPolicy: Qt.StrongFocus
                    activeFocusOnTab: false
                    Keys.onPressed: function(event) { ListView.view.handleKey(event) }
                    onActiveFocusChanged: if (activeFocus) { ListView.view.currentIndex = index; ListView.view.cursorKey = ListView.view.keyAt(index) }
                    function activate() { controller.openNote(modelData.path, modelData.match === undefined ? -1 : modelData.match) }
                    width: ListView.view.width; height: 104
                    highlighted: modelData.id === controller.selectedId && modelData.path === controller.path
                    background: Rectangle {
                        radius: 9; color: noteRow.highlighted || noteRow.hovered ? AppTheme.controlBackground : "transparent"
                        border.width: noteRow.activeFocus ? 2 : 0
                        border.color: AppTheme.focusRing
                    }
                    contentItem: Column {
                        spacing: 6
                        Label { width: parent.width; text: (noteRow.rowStatus ? noteRow.rowStatus + " · " : "") + noteRow.modelData.title; elide: Text.ElideRight; font.pixelSize: 14; font.weight: Font.DemiBold; color: AppTheme.text }
                        Label { width: parent.width; text: noteRow.modelData.summary || "空白笔记"; maximumLineCount: 2; wrapMode: Text.Wrap; elide: Text.ElideRight; font.pixelSize: 12; color: AppTheme.mutedText }
                        Label { width: parent.width; text: noteRow.modelData.path + " · " + noteRow.modelData.timeSource + " " + new Date(noteRow.modelData.displayModifiedUtc).toLocaleDateString(Qt.locale(), "MM/dd"); elide: Text.ElideMiddle; font.pixelSize: 10; color: AppTheme.mutedText }
                    }
                    onClicked: activate()
                    Accessible.role: Accessible.ListItem
                    Accessible.focusable: true
                    Accessible.selected: highlighted
                    Accessible.onPressAction: activate()
                    Accessible.name: modelData.title + ", " + modelData.path + (rowStatus ? ", " + rowStatus : "")
                }
                Label { anchors.centerIn: parent; visible: noteList.count === 0; text: controller.query ? "没有匹配的笔记" : "从第一篇笔记开始"; color: AppTheme.mutedText; font.pixelSize: 13 }
            }
        }
    }
    MouseArea {
        visible: root.showSidebar; x: root.sidebarWidth - 3; width: 6; height: parent.height; cursorShape: Qt.SplitHCursor
        property real startWidth: 0; property real startX: 0
        onPressed: function(mouse) { startWidth = root.sidebarWidth; startX = mapToItem(root.contentItem, mouse.x, mouse.y).x }
        onPositionChanged: function(mouse) { if (pressed) root.sidebarWidth = Math.max(180, Math.min(260, startWidth + mapToItem(root.contentItem, mouse.x, mouse.y).x - startX)) }
    }
    MouseArea {
        visible: root.showList && root.width >= 800; x: root.editorX - 3; width: 6; height: parent.height; cursorShape: Qt.SplitHCursor
        property real startWidth: 0; property real startX: 0
        onPressed: function(mouse) { startWidth = root.listWidth; startX = mapToItem(root.contentItem, mouse.x, mouse.y).x }
        onPositionChanged: function(mouse) { if (pressed) root.listWidth = Math.max(240, Math.min(340, startWidth + mapToItem(root.contentItem, mouse.x, mouse.y).x - startX)) }
    }
    Item {
        id: editorChrome
        visible: root.hasLibrary && root.editorX < root.width
        x: root.editorX; width: root.width - x; height: parent.height
        RowLayout {
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 14; spacing: 6
            LiquidActionButton {
                id: sidebarButton
                text: root.width < 800 ? "笔记" : "侧栏"; implicitHeight: 26
                materialForm: false; liquidIntensity: "light"; backdrop: readingBackdrop
                onClicked: root.width < 800 ? root.listRequested = true : root.sidebarRequested = !root.sidebarRequested
                KosToolTip { target: sidebarButton; backdrop: readingBackdrop; text: "展开导航 · Ctrl+Shift+B" }
            }
            LiquidActionButton { text: "新建"; implicitHeight: 26; materialForm: false; liquidIntensity: "light"; backdrop: readingBackdrop; enabled: !controller.busy; onClicked: controller.createNote() }
            Item { Layout.fillWidth: true }
            LiquidActionButton { text: "撤销"; visible: editorChrome.width > 470; implicitHeight: 26; materialForm: true; focusPolicy: Qt.TabFocus; enabled: session.canUndo && !controller.readOnly && !session.composing; onClicked: { session.undo(); source.editor.forceActiveFocus() } }
            LiquidActionButton { id: formatButton; text: "格式"; implicitHeight: 26; materialForm: false; liquidIntensity: "light"; backdrop: readingBackdrop; focusPolicy: Qt.TabFocus; enabled: !controller.readOnly && !session.composing; onClicked: formatMenu.openAtAnchor() }
            LiquidActionButton { id: moreButton; focusPolicy: Qt.StrongFocus; text: "更多"; implicitHeight: 26; materialForm: false; liquidIntensity: "light"; backdrop: readingBackdrop; onClicked: actionMenu.openAtAnchor() }
        }
        RowLayout {
            anchors.top: parent.top; anchors.topMargin: tokens.toolbarHeight + 16; anchors.left: parent.left; anchors.right: parent.right; anchors.leftMargin: tokens.inset; anchors.rightMargin: tokens.inset
            Label { Layout.fillWidth: true; text: controller.path || "选择一篇笔记"; elide: Text.ElideMiddle; font.pixelSize: 11; color: AppTheme.mutedText }
            Label { text: "MARKDOWN"; color: AppTheme.mutedText; font.pixelSize: 10; font.letterSpacing: 1.2 }
        }
        RowLayout {
            id: findBar; visible: false; x: tokens.inset; y: tokens.toolbarHeight * 2 - 4; width: parent.width - tokens.inset * 2
            LiquidTextField { id: localFind; Layout.fillWidth: true; materialForm: true; placeholderText: "查找当前正文"; onAccepted: root.findNext() }
            LiquidActionButton { text: "下一个"; materialForm: true; onClicked: root.findNext() }
            LiquidActionButton { text: "关闭"; materialForm: true; onClicked: { findBar.visible = false; source.editor.forceActiveFocus() } }
        }
        KosEmptyState { anchors.centerIn: parent; width: Math.min(360, parent.width - 48); visible: !controller.path; symbol: ""; title: "一页新的可能"; description: "选择笔记，或写下此刻的想法。"; actionText: "新建笔记"; actionEnabled: !controller.busy; onActionTriggered: controller.createNote() }
    }
    ColumnLayout {
        id: statusArea
        x: root.hasLibrary && root.editorX < root.width ? root.editorX : 16; width: Math.max(0, root.width - x - 16)
        anchors.bottom: parent.bottom; spacing: 6
        visible: root.editorX < root.width || !!controller.error
        Label { Layout.fillWidth: true; text: controller.status; color: AppTheme.mutedText; font.pixelSize: 11; leftPadding: 14; bottomPadding: controller.error ? 0 : 12 }
        Rectangle {
            visible: !!controller.error
            Layout.fillWidth: true; implicitHeight: errorColumn.implicitHeight + 24; radius: 8; color: AppTheme.controlBackground
            ColumnLayout {
                id: errorColumn; anchors.fill: parent; anchors.margins: 12
                Label { Layout.fillWidth: true; text: controller.error; wrapMode: Text.Wrap; color: AppTheme.text; font.pixelSize: 12; maximumLineCount: 5; elide: Text.ElideRight }
                RowLayout {
                    LiquidActionButton { text: "重试保存"; materialForm: true; enabled: !!controller.path && !controller.busy; onClicked: controller.save() }
                    LiquidActionButton { id: errorCopy; text: "另存副本"; materialForm: true; enabled: !!controller.path && !controller.busy; onClicked: root.askName("copy", errorCopy) }
                    LiquidActionButton { text: "恢复中心"; materialForm: true; onClicked: root.showRecovery() }
                }
            }
        }
    }
    function revealRecoveryControl(item) {
        if (!item.activeFocus) return
        const flick = recoveryScroll.contentItem
        const top = item.mapToItem(flick.contentItem, 0, 0).y
        if (top < flick.contentY) flick.contentY = top
        else if (top + item.height > flick.contentY + flick.height)
            flick.contentY = top + item.height - flick.height
    }
    function restoreEditorFocus() {
        // A menu action may open another panel or the local search field.
        if (!root.popupsOpen && !localFind.activeFocus && source.visible)
            source.editor.forceActiveFocus(Qt.OtherFocusReason)
    }
    function findNext() {
        if (!localFind.text) return
        let p = session.source.toLowerCase().indexOf(localFind.text.toLowerCase(), source.editor.selectionEnd)
        if (p < 0) p = session.source.toLowerCase().indexOf(localFind.text.toLowerCase())
        if (p >= 0) { source.editor.select(p, p + localFind.text.length); source.contentItem.contentY = Math.max(0, source.editor.positionToRectangle(p).y - 80) }
    }
    function showRecovery() { controller.inspectRecovery(); recoveryDialog.open() }
    function askName(kind, anchor) {
        namePanel.kind = kind; namePanel.anchorItem = anchor
        nameField.text = kind === "copy" ? "恢复副本-" + Date.now() + ".md" : kind === "move" ? controller.path : ""
        namePanel.open(); nameField.forceActiveFocus()
    }
    LiquidPopover {
        id: formatMenu; anchorItem: formatButton; sourceItem: readingScene
        onClosed: Qt.callLater(root.restoreEditorFocus)
        function apply(command) { commands.run(command) }
        LiquidPopoverItem { text: "加粗                         Ctrl+B"; onTriggered: formatMenu.apply("bold") }
        LiquidPopoverItem { text: "斜体                         Ctrl+I"; onTriggered: formatMenu.apply("italic") }
        LiquidPopoverItem { text: "删除线                Ctrl+Shift+X"; onTriggered: formatMenu.apply("strike") }
        LiquidPopoverItem { text: "行内代码                    Ctrl+E"; onTriggered: formatMenu.apply("code") }
        LiquidPopoverItem { text: "撤销"; enabled: session.canUndo && !controller.readOnly && !session.composing; onTriggered: { session.undo(); source.editor.forceActiveFocus() } }
        LiquidPopoverItem { text: "重做"; enabled: session.canRedo && !controller.readOnly && !session.composing; onTriggered: { session.redo(); source.editor.forceActiveFocus() } }
    }
    LiquidPopover {
        id: actionMenu; anchorItem: moreButton; sourceItem: readingScene
        onClosed: Qt.callLater(root.restoreEditorFocus)
        LiquidPopoverItem { text: "保存                           Ctrl+S"; enabled: controller.dirty; onTriggered: controller.save() }
        LiquidPopoverItem { text: "切换收藏"; enabled: !!controller.path; onTriggered: controller.favorite() }
        LiquidPopoverItem { text: "移动 / 重命名文件…"; enabled: !!controller.path; onTriggered: root.askName("move", moreButton) }
        LiquidPopoverItem { text: "另存为新身份副本…"; enabled: !!controller.path; onTriggered: root.askName("copy", moreButton) }
        LiquidPopoverItem { text: "移到最近删除…"; enabled: !!controller.path; onTriggered: deleteDialog.open() }
        LiquidPopoverItem { text: "放弃本地修改，读取磁盘…"; enabled: !!controller.path; onTriggered: discardDialog.open() }
        LiquidPopoverItem { text: "查找当前正文…"; enabled: !!controller.path; onTriggered: { findBar.visible = true; Qt.callLater(function() { localFind.forceActiveFocus(Qt.ShortcutFocusReason) }) } }
        LiquidPopoverItem { text: "刷新笔记库"; onTriggered: controller.refresh() }
        LiquidPopoverItem { text: "恢复中心 / 最近删除…"; onTriggered: root.showRecovery() }
        LiquidPopoverItem { text: "切换笔记库…"; onTriggered: libraryPicker.open() }
        LiquidPopoverItem { text: "阅读与外观…"; onTriggered: { appearancePanel.open() } }
    }
    LiquidContentPopover {
        id: namePanel; sourceItem: readingScene; preferredSide: "bottom"; width: 330
        property string kind: "folder"
        contentItem: ColumnLayout {
            Label { text: namePanel.kind === "folder" ? "新建文件夹" : namePanel.kind === "move" ? "移动 / 重命名" : "保存独立副本"; font.weight: Font.DemiBold; color: AppTheme.text }
            Label { Layout.fillWidth: true; text: namePanel.kind === "folder" ? "输入库内相对路径，父目录须已存在。" : namePanel.kind === "move" ? "只修改文件路径，不改标题或链接。" : "只更新副本的稳定 ID；原始文件和恢复记录保留。"; wrapMode: Text.Wrap; color: AppTheme.mutedText; font.pixelSize: 12 }
            LiquidTextField { id: nameField; Layout.fillWidth: true; materialForm: true; placeholderText: namePanel.kind === "folder" ? "文件夹名称" : "文件夹/笔记.md"; onAccepted: namePanel.submit() }
            LiquidActionButton { text: "确认"; materialForm: true; enabled: !!nameField.text && !nameField.inputMethodComposing && !controller.busy; onClicked: namePanel.submit() }
        }
        function submit() {
            if (!nameField.text || nameField.inputMethodComposing) return
            if (kind === "folder") controller.createFolder(nameField.text)
            else if (kind === "move") controller.move(nameField.text)
            else controller.saveCopy(nameField.text)
            close()
        }
    }
    LiquidContentPopover {
        id: folderPanel; sourceItem: readingScene; preferredSide: "bottom"; width: 300
        contentItem: ColumnLayout {
            Label { text: "文件夹"; color: AppTheme.text; font.weight: Font.DemiBold }
            KeyboardList {
                Layout.fillWidth: true; Layout.preferredHeight: Math.min(260, contentHeight); clip: true
                model: controller.folders
                delegate: LiquidNavigationButton {
                    required property var modelData
                    required property int index
                    width: ListView.view.width; text: modelData; specificationForm: true
                    checked: controller.filter === "folder:" + modelData
                    focusPolicy: Qt.StrongFocus
                    activeFocusOnTab: false
                    Keys.onPressed: function(event) { ListView.view.handleKey(event) }
                    onActiveFocusChanged: if (activeFocus) { ListView.view.currentIndex = index; ListView.view.cursorKey = ListView.view.keyAt(index) }
                    function activate() { controller.filter = "folder:" + modelData; folderPanel.close() }
                    onClicked: activate()
                    Accessible.role: Accessible.ListItem
                    Accessible.focusable: true
                    Accessible.selected: checked
                    Accessible.name: "文件夹 " + modelData
                    Accessible.onPressAction: activate()
                }
            }
            LiquidActionButton { text: "新建文件夹…"; materialForm: true; onClicked: { folderPanel.close(); root.askName("folder", smallFolderButton) } }
        }
    }
    LiquidContentPopover {
        id: appearancePanel; anchorItem: moreButton; sourceItem: readingScene; preferredSide: "bottom"; width: 300
        contentItem: SettingsGroup {
            title: "阅读与外观"
            SettingsItem {
                title: "主题"; minimumLabelWidth: 50
                LiquidSelect {
                    model: ["系统", "浅色", "深色"]; materialForm: true
                    Accessible.name: "主题"
                    currentIndex: Math.max(0, ["system", "light", "dark"].indexOf(AppTheme.appearanceMode))
                    onActivated: function(index) { AppTheme.appearanceMode = ["system", "light", "dark"][index]; controller.setPreference("appearance", AppTheme.appearanceMode) }
                }
            }
            SettingsItem {
                title: "正文字号"; minimumLabelWidth: 70
                LiquidSlider { id: bodySizeSlider; Accessible.name: "正文字号"; Layout.preferredWidth: 120; Layout.preferredHeight: 24; materialForm: true; thumbWidth: 26; thumbHeight: 14; trackHeight: 4; value: (controller.preference("bodySize", tokens.bodySize) - 12) / 12; onPreviewChanged: function(v) { value = v }; onCommitRequested: function(v) { controller.setPreference("bodySize", 12 + Math.round(v * 12)) }; onCanceled: value = (controller.preference("bodySize", tokens.bodySize) - 12) / 12 }
            }
            SettingsItem {
                title: "减少透明效果"; minimumLabelWidth: 140
                SettingsSwitch { accessibleName: "减少透明效果"; checked: AppTheme.reduceTransparency; onToggled: { AppTheme.reduceTransparency = checked; controller.setPreference("reduceTransparency", checked) } }
            }
            SettingsItem {
                title: "减少动画"; minimumLabelWidth: 140
                SettingsSwitch { accessibleName: "减少动画"; checked: AppTheme.reduceMotion; onToggled: { AppTheme.reduceMotion = checked; controller.setPreference("reduceMotion", checked) } }
            }
        }
    }

    LiquidDialog {
        id: deleteDialog; title: "移到最近删除？"; sourceItem: readingScene
        width: Math.min(420, root.width - 40); anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label { text: "先保存当前输入，再移动文件。你可以在恢复中心还原它。"; wrapMode: Text.Wrap; color: AppTheme.text }
        onAccepted: controller.trash()
    }
    LiquidDialog {
        id: discardDialog; title: "放弃当前本地输入？"; sourceItem: readingScene
        width: Math.min(440, root.width - 40); anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label { text: "成功读取磁盘后才替换编辑内容，并删除此笔记的恢复草稿。需要保留本地版本时，请先另存副本。"; wrapMode: Text.Wrap; color: AppTheme.text }
        onAccepted: controller.discardAndReload()
    }
    LiquidDialog {
        id: recoveryDialog; title: "恢复中心"; sourceItem: readingScene
        width: Math.min(700, root.width - 40); height: Math.min(590, root.height - 60); anchors.centerIn: parent
        standardButtons: Dialog.Close
        contentItem: ScrollView {
            id: recoveryScroll
            clip: true; contentWidth: availableWidth
            ColumnLayout {
                width: recoveryScroll.availableWidth; spacing: 12
                Label { Layout.fillWidth: true; text: "恢复不会自动覆盖正文。草稿可载入编辑区；基线已变时请另存。提交记录保留的旧版本不会自动清理。"; wrapMode: Text.Wrap; color: AppTheme.mutedText; font.pixelSize: 12 }
                Label { text: "恢复草稿"; color: AppTheme.text; font.weight: Font.DemiBold }
                Repeater {
                    model: controller.recovery.drafts || []
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Label { Layout.fillWidth: true; text: modelData.path + (modelData.durable === false ? " · 仅内存，勿关闭" : "") + (modelData.matchesBase ? " · 基线一致" : " · 磁盘已改变"); wrapMode: Text.Wrap; color: AppTheme.text }
                        RowLayout {
                            LiquidActionButton { focusPolicy: Qt.StrongFocus; onActiveFocusChanged: root.revealRecoveryControl(this); text: "载入草稿"; Accessible.name: text + " " + modelData.path; materialForm: true; onClicked: { controller.recoverDraft(modelData.path, modelData.text, modelData.baseHash); recoveryDialog.close() } }
                            LiquidActionButton { focusPolicy: Qt.StrongFocus; onActiveFocusChanged: root.revealRecoveryControl(this); text: "复制草稿到新文件"; Accessible.name: text + " " + modelData.path; materialForm: true; onClicked: controller.copyRecovery(modelData.text) }
                            LiquidActionButton { focusPolicy: Qt.StrongFocus; onActiveFocusChanged: root.revealRecoveryControl(this); text: "丢弃…"; Accessible.name: "丢弃恢复草稿 " + modelData.path; materialForm: true; onClicked: { discardRecoveryDialog.draftPath = modelData.path; discardRecoveryDialog.open() } }
                        }
                        TextArea { focusPolicy: Qt.StrongFocus; onActiveFocusChanged: root.revealRecoveryControl(this); Accessible.name: "恢复草稿正文 " + modelData.path; Layout.fillWidth: true; text: modelData.text; readOnly: true; selectByMouse: true; wrapMode: Text.Wrap; implicitHeight: 80; color: AppTheme.text; background: null }
                    }
                }
                Label { text: "最近删除"; color: AppTheme.text; font.weight: Font.DemiBold }
                Repeater {
                    model: Object.keys(controller.recovery.trash || {})
                    delegate: RowLayout {
                        required property string modelData
                        Layout.fillWidth: true
                        Label { Layout.fillWidth: true; text: controller.recovery.trash[modelData].source; elide: Text.ElideMiddle; color: AppTheme.text }
                        LiquidActionButton { focusPolicy: Qt.StrongFocus; onActiveFocusChanged: root.revealRecoveryControl(this); text: "恢复"; Accessible.name: text + " " + controller.recovery.trash[modelData].source; materialForm: true; onClicked: controller.restore(modelData) }
                        LiquidActionButton { focusPolicy: Qt.StrongFocus; onActiveFocusChanged: root.revealRecoveryControl(this); text: "恢复到根目录副本"; Accessible.name: text + " " + controller.recovery.trash[modelData].source; materialForm: true; onClicked: controller.restore(modelData, "还原-" + Date.now() + ".md") }
                    }
                }
                Label { text: "保留的提交版本"; color: AppTheme.text; font.weight: Font.DemiBold }
                Label { Layout.fillWidth: true; text: "每条 intent 含目标路径、基线及新摘要，最后一行是保留文件的绝对路径。交换前它是新版本，交换后是被替换版本；需比较摘要后手动取回，不自动覆盖。"; wrapMode: Text.Wrap; color: AppTheme.mutedText; font.pixelSize: 12 }
                Repeater {
                    model: controller.recovery.commits || []
                    delegate: ColumnLayout {
                        id: commitRow
                        required property var modelData
                        Layout.fillWidth: true
                        Label { text: commitRow.modelData.name + " · " + commitRow.modelData.time; color: AppTheme.text }
                        Repeater {
                            model: commitRow.modelData.versions
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Label { text: (modelData.version === "before" ? "保存前" : "本次待保存") + (modelData.available ? "" : " · 无法确认"); color: AppTheme.text }
                                TextArea { Layout.fillWidth: true; text: modelData.text || "版本缺失或校验未通过"; readOnly: true; selectByMouse: true; wrapMode: Text.Wrap; implicitHeight: 80; background: null; color: AppTheme.text; Accessible.name: "提交版本预览" }
                                LiquidActionButton { text: "另存为恢复副本"; materialForm: true; enabled: modelData.available; onClicked: controller.recoverCommit(commitRow.modelData.token, modelData.version) }
                            }
                        }
                    }
                }
                LiquidActionButton { focusPolicy: Qt.StrongFocus; onActiveFocusChanged: root.revealRecoveryControl(this); text: "在文件管理器定位恢复目录"; materialForm: true; onClicked: controller.openRecoveryDirectory() }
            }
        }
    }
    LiquidDialog {
        id: discardRecoveryDialog; property string draftPath: ""
        title: "丢弃恢复草稿？"; sourceItem: readingScene; width: Math.min(420, root.width - 40); anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label { text: "只删除恢复记录，不修改磁盘正文。若尚未另存副本，草稿内容将无法恢复。\n" + discardRecoveryDialog.draftPath; wrapMode: Text.Wrap; color: AppTheme.text }
        onAccepted: controller.discardRecovery(draftPath)
    }
    KosWindowResizeHandles { parent: root.contentItem.parent; targetWindow: root }
}
