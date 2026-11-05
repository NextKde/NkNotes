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
    title: qsTr("笔记")
    flags: Qt.Window | Qt.FramelessWindowHint
    color: AppTheme.contentBackground
    font.family: tokens.uiFontFamily
    property bool allowClose: false
    readonly property var currentNote: controller.notes.find(function(note) { return note.id === controller.selectedId && note.path === controller.path }) || ({})
    function newNote() { controller.createNote() }
    function editNote() { Qt.callLater(function() { source.editor.forceActiveFocus() }) }
    property bool sidebarRequested: controller.preference("sidebar", true)
    property bool listRequested: false
    property bool trashView: false
    function showTrash() { revealList(); organizer.clearSelection(); trashView = true; controller.inspectRecovery() }
    property string activeLibrary: controller.library
    onActiveLibraryChanged: trashView = false
    property bool compactSidebarRequested: false
    function toggleNavigation() {
        if (width >= 1000) sidebarRequested = !sidebarRequested
        else if (width >= 800) compactSidebarRequested = !compactSidebarRequested
        else listRequested = !showList
    }
    function revealList() { trashView = false; compactSidebarRequested = false; listRequested = true }
    // Presentation-only cleanup: the repository remains the source of Markdown.
    function summaryText(value) {
        return String(value || "")
            .replace(/(^|\s)#{1,6}\s+/g, "$1")
            .replace(/(^|\s)(?:[-+*]\s+)?\[[ xX]\]\s*/g, "$1")
            .replace(/(^|\s)[-+*]\s+/g, "$1")
            .replace(/(^|\s)>\s*/g, "$1")
            .replace(/!?(\[([^\]]+)\])\([^)]*\)/g, "$2")
            .replace(/\[\[([^\]|]+)\|([^\]]+)\]\]/g, "$2")
            .replace(/\[\[([^\]]+)\]\]/g, "$1")
            .replace(/(\*\*|__|~~|`)(.*?)\1/g, "$2")
            .replace(/\s+/g, " ").trim() || "空白笔记"
    }
    property real sidebarWidth: controller.preference("sidebarWidth", tokens.sidebarWidth)
    property real listWidth: controller.preference("listWidth", tokens.listWidth)
    readonly property bool hasLibrary: controller.library.length > 0
    readonly property bool showSidebar: hasLibrary && (width >= 1000 ? sidebarRequested : width >= 800 && compactSidebarRequested)
    readonly property bool showList: hasLibrary && !(width < 1000 && showSidebar) && (width >= 800 || listRequested || (trashView ? !recycleBin.currentEntry : !controller.path))
    readonly property real editorX: (showSidebar ? sidebarWidth : 0) + (showList ? (width < 800 ? width : listWidth) : 0)
    readonly property bool popupsOpen: (source.popupActive || knowledgePanel.popupActive || smartDialog.visible || missingLinkDialog.visible) || organizer.popupActive || recycleBin.popupActive || actionMenu.visible || formatMenu.visible || tableMenu.visible || linkPanel.visible || paragraphSelect.popupOpened || namePanel.visible || recoveryDialog.visible || discardDialog.visible || deleteDialog.visible || folderPanel.visible || appearancePanel.visible || discardRecoveryDialog.visible || libraryPicker.visible
    onClosing: function(close) {
        if (source.exporting) { close.accepted = false; source.notify("正在导出，请完成后再关闭窗口"); return }
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
        function onDocumentLoaded(cursor, scroll, matchLength) { root.trashView = false; root.listRequested = false; if (matchLength > 0) root.editNote() }
    }
    CommandRegistry {
        id: commands
        session: root.session; editor: source.editor; controller: root.controller
        blocked: root.popupsOpen || search.inputMethodComposing || localFind.inputMethodComposing || replacement.inputMethodComposing
        onSidebarRequested: root.toggleNavigation()
        onSearchRequested: root.openGlobalSearch()
        onNewNoteRequested: root.newNote()
        onFindRequested: { findBar.visible = true; localFind.forceActiveFocus() }
        onReplaceRequested: { findBar.visible = true; replacement.forceActiveFocus() }
    }
    // One Tab stop per list; arrows move the keyboard cursor without opening a
    // document. Activation still goes through the controller's save/drain path.
    component KeyboardList: ListView {
        id: keyboardList
        activeFocusOnTab: count > 0
        keyNavigationEnabled: false
        property string cursorKey: ""
        function keyAt(row) {
            if (!model || row < 0 || row >= count) return ""
            const entry = model === controller.noteModel ? controller.notes[row] : model[row]
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
            if (model === controller.noteModel && event.modifiers === Qt.ControlModifier && event.key === Qt.Key_A) { organizer.selectAll(); event.accepted = true; return }
            if (model === controller.noteModel && event.modifiers === Qt.ControlModifier && event.key === Qt.Key_Space && currentIndex >= 0) { organizer.toggle(controller.notes[currentIndex].path); event.accepted = true; return }
            if (model === controller.noteModel && event.key === Qt.Key_Escape && organizer.selectionMode) { organizer.clearSelection(); event.accepted = true; return }
            if (model === controller.noteModel && currentItem && (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && event.modifiers === Qt.ShiftModifier))) {
                organizer.openMenu(currentItem, controller.notes[currentIndex].path)
                event.accepted = true; return
            }
            if (model === controller.noteModel && count > 0 && (event.modifiers & Qt.ShiftModifier)
                    && (event.key === Qt.Key_Up || event.key === Qt.Key_Down || event.key === Qt.Key_Home || event.key === Qt.Key_End)) {
                const row = event.key === Qt.Key_Home ? 0 : event.key === Qt.Key_End ? count - 1
                    : Math.max(0, Math.min(count - 1, currentIndex + (event.key === Qt.Key_Down ? 1 : -1)))
                if (!organizer.selectionAnchor && currentIndex >= 0) organizer.selectionAnchor = controller.notes[currentIndex].path
                organizer.click(controller.notes[row], row, event.modifiers)
                currentIndex = row; cursorKey = keyAt(row); focusCurrent()
                event.accepted = true; return
            }
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
        onActiveFocusChanged: if (activeFocus) Qt.callLater(function() { if (root.activeFocusItem === keyboardList) focusCurrent() })
        onModelChanged: Qt.callLater(restoreCursor)
        Keys.onPressed: function(event) { handleKey(event) }
    }
    Connections { target: controller; function onNotesChanged() { Qt.callLater(noteList.restoreCursor) } }
    FolderDialog {
        id: libraryPicker
        title: "选择已有文件夹作为笔记库"
        onAccepted: root.controller.selectLibrary(selectedFolder)
    }
    component ToolbarAction: LiquidActionButton {
        property string help: ""
        iconOnly: true; implicitWidth: 32; implicitHeight: 26
        materialForm: false; backdrop: readingBackdrop
        focusPolicy: Qt.TabFocus
        Accessible.name: help
        KosToolTip { target: parent; backdrop: readingBackdrop; text: parent.help }
    }
    property bool searchOpen: false
    function openGlobalSearch() {
        revealList()
        searchOpen = true
        Qt.callLater(function() { search.forceActiveFocus(Qt.ShortcutFocusReason) })
    }
    function closeGlobalSearch() {
        if (search.inputMethodComposing) return
        controller.query = ""
        search.focus = false
        searchOpen = false
    }
    function dateGroup(note) {
        if (!note || !root.controller) return ""
        if (note.pinned) return "置顶"
        if (root.controller.sortKey === "title") return ""
        const d = new Date(root.controller.sortKey === "created" ? note.createdUtc : note.displayModifiedUtc)
        if (isNaN(d.getTime())) return "更早"
        const now = new Date(), today = new Date(now.getFullYear(), now.getMonth(), now.getDate())
        const day = new Date(d.getFullYear(), d.getMonth(), d.getDate())
        const days = Math.round((today - day) / 86400000)
        return days === 0 ? "今天" : days === 1 ? "昨天" : days < 7 && days > 0 ? "过去 7 天" : d.getFullYear() === now.getFullYear() ? (d.getMonth() + 1) + "月" : String(d.getFullYear())
    }
    component NavigationRow: LiquidNavigationButton {
        // Selection belongs to controller.filter; activation must not toggle it off.
        checkable: false
        specificationForm: true; specificationFontSize: tokens.navigationSize
        implicitHeight: tokens.navigationHeight
        specificationSelectedFillColor: tokens.navigationSelected
        specificationSelectedTextColor: AppTheme.text
        iconColor: tokens.notesYellow
    }
    header: Item {
        height: tokens.toolbarHeight
        Rectangle { visible: root.showSidebar; width: root.sidebarWidth; height: parent.height; color: tokens.sidebarBackground }
        Rectangle {
            visible: root.showList && root.editorX < root.width
            x: root.editorX - width; width: 1; height: parent.height
            color: AppTheme.border; opacity: 0.5
        }
        MouseArea {
            anchors.fill: parent
            onPressed: root.startSystemMove()
            onDoubleClicked: root.visibility === Window.Maximized ? root.showNormal() : root.showMaximized()
        }
        RowLayout {
            x: 14; width: root.showSidebar ? root.sidebarWidth - 26 : 152
            anchors.verticalCenter: parent.verticalCenter; spacing: 8
            Label {
                objectName: "notesTitle"
                text: root.title; color: AppTheme.text
                font.pixelSize: 15; font.weight: Font.DemiBold
                Layout.fillWidth: true; Layout.preferredHeight: 28
                verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
            }
            KosSidebarToggle {
                id: sidebarButton
                objectName: "sidebarToggleButton"
                implicitWidth: 28; implicitHeight: 28
                glyphColor: AppTheme.text; glyphPaneColor: glyphColor
                focusPolicy: Qt.TabFocus
                Accessible.name: "显示或隐藏导航 · Ctrl+Shift+B"
                background: LiquidActionButton {
                    enabled: false; Accessible.ignored: true
                    materialForm: false; backdrop: readingBackdrop
                    liquidIntensity: "strong"
                }
                onClicked: root.toggleNavigation()
            }
        }
        RowLayout {
            visible: root.showList
            x: root.showSidebar ? root.sidebarWidth + 18 : 178
            width: root.showSidebar ? root.listWidth - 36 : Math.max(80, root.listWidth - 190)
            anchors.verticalCenter: parent.verticalCenter
            Column {
                Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 0
                Label { width: parent.width; elide: Text.ElideRight; text: root.trashView ? "最近删除" : controller.filter === "favorite" ? "收藏" : controller.filter.startsWith("folder:") ? controller.filter.slice(7) : "全部笔记"; color: AppTheme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                Label { width: parent.width; elide: Text.ElideRight; text: root.trashView ? (recycleBin.selectionMode ? "已选 " + recycleBin.selected.length + " / " : "") + recycleBin.items.length + " 项" : organizer.selectionMode ? "已选 " + organizer.selectedPaths.length + " / " + controller.notes.length : controller.notes.length + " 篇笔记"; color: AppTheme.mutedText; font.pixelSize: 12 }
            }
            LiquidActionButton {
                visible: root.trashView ? recycleBin.selectionMode : organizer.selectionMode
                text: "取消"; Accessible.name: "取消选择"
                implicitWidth: 52; implicitHeight: 26; materialForm: true
                onClicked: root.trashView ? recycleBin.clearSelection() : organizer.clearSelection()
            }
            ToolbarAction { iconName: "more"; implicitWidth: 26; implicitHeight: 26; help: "笔记列表操作"; onClicked: root.trashView ? recycleBin.openMenu(this, null) : organizer.openMenu(this, "") }
        }
        RowLayout {
            id: headerActions
            visible: root.hasLibrary
            x: root.editorX < root.width ? Math.max(178, root.editorX + 12) : 178
            anchors.verticalCenter: parent.verticalCenter
            // Position against the actual declared circle, including its click inset.
            readonly property real controlsLeft: windowControls.x + (windowControls.buttonLayout.length ? windowControls.buttonLayout[0].x : 0)
            width: Math.max(0, controlsLeft - 10 - x); height: 28; spacing: 10
            readonly property bool wideSearch: root.width >= 1320 && width >= 330
            readonly property bool searchShown: wideSearch || root.searchOpen || controller.query.length > 0
            readonly property bool searching: !wideSearch && searchShown
            ToolbarAction { objectName: "newNoteButton"; visible: !headerActions.searching; iconName: "editRounded"; implicitWidth: 28; implicitHeight: 28; iconSize: 14; help: "新建笔记 · Ctrl+N"; enabled: !controller.busy; onClicked: root.newNote() }
            LiquidActionButtonGroup {
                id: historyButtons
                visible: parent.width > 245 && !headerActions.searching
                buttonSize: 26; materialForm: false; backdrop: readingBackdrop
                icons: ["undo", "forward"]; labels: ["撤销", "重做"]
                enabledActions: [root.canEdit && session.canUndo, root.canEdit && session.canRedo]
                onTriggered: function(index) { root.applyFormat(index === 0 ? "undo" : "redo") }
            }
            ToolbarAction { id: moreButton; visible: !headerActions.searching; iconName: "more"; help: "更多笔记操作"; onClicked: { actionMenu.anchorItem = moreButton; actionMenu.openAtAnchor() } }
            Item { visible: !headerActions.searching; Layout.fillWidth: true; Layout.minimumWidth: 0 }
            Item {
                visible: headerActions.searchShown
                Layout.preferredWidth: 160; Layout.fillWidth: headerActions.searching
                Layout.minimumWidth: 0; Layout.fillHeight: true
                clip: true
                LiquidTextField {
                    id: search
                    objectName: "searchField"
                    anchors.fill: parent
                    materialForm: false; liquidFinish: true; liquidIntensity: "light"; backdrop: readingBackdrop
                    placeholderText: "搜索笔记"; text: controller.query
                    leftPadding: 30; rightPadding: headerActions.searching ? 36 : 10
                    VectorIcon {
                        x: 8; anchors.verticalCenter: parent.verticalCenter
                        name: "search"; size: 14; color: search.textColor
                    }
                    onTextEdited: if (!inputMethodComposing) controller.query = text
                    onInputMethodComposingChanged: if (!inputMethodComposing) controller.query = text
                    onActiveFocusChanged: if (!activeFocus && !headerActions.wideSearch && text.length === 0) root.searchOpen = false
                    Keys.onEscapePressed: function(event) { if (!inputMethodComposing) { root.closeGlobalSearch(); event.accepted = true } }
                    Accessible.name: "搜索所有笔记"
                }
                ToolbarAction {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    visible: headerActions.searching
                    implicitWidth: 28; implicitHeight: 28
                    text: "×"; help: "关闭搜索"
                    enabled: !search.inputMethodComposing
                    onClicked: root.closeGlobalSearch()
                }
            }
            ToolbarAction {
                objectName: "searchToggleButton"
                visible: !headerActions.searchShown
                implicitWidth: 28; implicitHeight: 28
                iconName: "search"; iconSize: 14; help: "搜索所有笔记"
                onClicked: root.openGlobalSearch()
            }
        }
        NotesWindowControls {
            id: windowControls
            targetWindow: root; actions: ["maximize", "minimize", "close"]; maximizeMode: true
            circleSize: 14; buttonWidth: 22; buttonHeight: 22; spacing: 6
            anchors.right: parent.right; anchors.rightMargin: 10.5; anchors.verticalCenter: parent.verticalCenter
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
            sceneItem: readingScene
            allowRestoreFocus: !root.popupsOpen
            visible: root.hasLibrary && !root.trashView && !!controller.path && root.editorX < root.width
            x: root.editorX; y: formatBar.y + formatBar.height + 8 + (findBar.visible ? findBar.implicitHeight + 8 : 0)
            width: Math.max(0, parent.width - x); height: Math.max(0, parent.height - y - statusArea.height)
            bodySize: 12 + Math.round(bodySizeSlider.value * 12); readingWidth: tokens.readingWidth; inset: tokens.inset
        }


    }
    KnowledgePanel { id: knowledgePanel; anchors.fill: parent; controller: root.controller; session: root.session; sceneItem: readingScene }
    readonly property bool canEdit: !root.trashView && !!controller.path && !controller.readOnly && !session.protectedContent && !session.webLoading && !source.exporting
    readonly property bool canFormat: canEdit && !session.sourceMode
    function canApply(command) { return command === "undo" || command === "redo" ? canEdit : canFormat }
    property var pendingFormat: null
    function deliverFormat() {
        const value = pendingFormat
        if (!value) return
        if (value.epoch !== session.webDocument().epoch || !canApply(value.command)) { pendingFormat = null; return }
        if (session.composing) return
        pendingFormat = null
        session.execute(value.command)
        root.editNote()
    }
    function applyFormat(command) {
        if (!canApply(command)) return
        pendingFormat = {epoch: session.webDocument().epoch, command: command}
        if (session.composing) Qt.inputMethod.commit()
        Qt.callLater(deliverFormat)
    }
    Connections { target: session; function onStateChanged() { if (root.pendingFormat) Qt.callLater(root.deliverFormat) } }
    Row {
        id: formatBar
        visible: source.visible
        x: root.editorX + 16; y: 6; height: 26; spacing: 8
        LiquidSelect {
            id: paragraphSelect
            implicitHeight: 26; materialForm: false; backdrop: readingBackdrop
            appearanceMode: AppTheme.appearanceMode
            model: ["正文", "标题 1", "标题 2", "标题 3", "标题 4", "标题 5", "标题 6"]
            currentIndex: session.headingLevel
            enabled: root.canFormat
            Accessible.name: "段落样式"
            onActivated: function(index) { root.applyFormat(index === 0 ? "paragraph" : "heading" + index) }
        }
        LiquidActionButtonGroup {
            buttonSize: 26; materialForm: false; backdrop: readingBackdrop
            icons: ["", "", ""]; labels: ["粗体 · Ctrl+B", "斜体 · Ctrl+I", "删除线 · Ctrl+Shift+X"]
            enabledActions: [root.canFormat, root.canFormat, root.canFormat]
            onTriggered: function(index) { root.applyFormat(["bold", "italic", "strike"][index]) }
            // Shared group owns surfaces and interaction; the pinned icon catalogue
            // has no italic/strike glyphs, so supply typographic marks only.
            Row {
                anchors.fill: parent
                Repeater {
                    model: ["B", "I", "S"]
                    Text { required property string modelData; required property int index; width: 26; height: 26; text: modelData; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; color: root.canFormat ? AppTheme.text : AppTheme.disabledText; font.pixelSize: 14; font.bold: index === 0; font.italic: index === 1; font.strikeout: index === 2; Accessible.ignored: true }
                }
            }
        }
        ToolbarAction { id: insertButton; iconName: "reminder"; help: "列表与插入"; enabled: root.canFormat; onClicked: formatMenu.openAtAnchor() }
        ToolbarAction { id: tableButton; iconName: "grid"; help: "表格与行列操作"; enabled: root.canFormat; onClicked: tableMenu.openAtAnchor() }
        ToolbarAction { id: editorToolsButton; iconName: "more"; help: "正文编辑与导出"; enabled: !source.exporting; onClicked: source.openTools(editorToolsButton) }
        Label { visible: root.width - root.editorX > 640; anchors.verticalCenter: parent.verticalCenter; text: session.formatState; font.pixelSize: 10; color: AppTheme.mutedText }
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
        Rectangle { anchors.fill: parent; color: tokens.sidebarBackground }
        // No distinct content underneath the sidebar: use the specified opaque fallback.
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 12; spacing: 8
            Label { text: "我的笔记"; color: AppTheme.mutedText; font.pixelSize: 11; font.weight: Font.DemiBold; Layout.leftMargin: 12; Layout.topMargin: 12; Layout.bottomMargin: 2 }
            NavigationRow { iconName: "album"; Layout.fillWidth: true; text: "全部笔记"; specificationForm: true; checked: !root.trashView && controller.filter === "all"; onClicked: { controller.filter = "all"; root.revealList() } }
            NavigationRow { iconName: "heart"; Layout.fillWidth: true; text: "收藏"; specificationForm: true; checked: !root.trashView && controller.filter === "favorite"; onClicked: { controller.filter = "favorite"; root.revealList() } }
            NavigationRow { iconName: "trashRounded"; Layout.fillWidth: true; text: "最近删除"; specificationForm: true; checked: root.trashView; onClicked: root.showTrash() }
            Repeater { model: Object.keys(controller.smartFolders); delegate: NavigationRow { required property string modelData; Layout.fillWidth: true; text: modelData; specificationForm: true; onClicked: { root.revealList(); controller.openSmartFolder(modelData) } } }
            RowLayout {
                Layout.fillWidth: true; Layout.topMargin: 16
                Label { text: "文件夹"; color: AppTheme.mutedText; font.pixelSize: 11; Layout.fillWidth: true }
                ToolbarAction { help: "当前文件夹操作"; iconName: "more"; enabled: controller.filter.startsWith("folder:"); onClicked: organizer.openFolderMenu(controller.filter.slice(7), this) }
                ToolbarAction { id: folderButton; iconName: "plus"; help: "新建文件夹"; onClicked: root.askName("folder", folderButton) }
            }
            KeyboardList {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                model: controller.folders; spacing: 2
                delegate: NavigationRow {
                    iconName: "folder"
                    required property var modelData
                    required property int index
                    focusPolicy: Qt.StrongFocus
                    activeFocusOnTab: false
                    Keys.onPressed: function(event) { ListView.view.handleKey(event) }
                    onActiveFocusChanged: if (activeFocus) { ListView.view.currentIndex = index; ListView.view.cursorKey = ListView.view.keyAt(index) }
                    function activate() { controller.filter = "folder:" + modelData; root.revealList() }
                    width: ListView.view.width; text: modelData; specificationForm: true
                    checked: !root.trashView && controller.filter === "folder:" + modelData
                    onClicked: activate()
                    Accessible.role: Accessible.ListItem
                    Accessible.focusable: true
                    Accessible.selected: checked
                    Accessible.onPressAction: activate()
                    MouseArea { anchors.fill: parent; acceptedButtons: Qt.RightButton; onClicked: organizer.openFolderMenu(parent.modelData, parent) }
                    Accessible.name: "文件夹 " + modelData
                    KosToolTip { target: parent; backdrop: readingBackdrop; text: "打开此文件夹中的笔记" }
                }
            }
            Label { Layout.fillWidth: true; Layout.leftMargin: 12; text: controller.library.split("/").pop(); elide: Text.ElideMiddle; font.pixelSize: 11; color: AppTheme.mutedText }
            LiquidActionButton { text: "切换笔记库"; implicitHeight: 26; materialForm: false; backdrop: readingBackdrop; enabled: !controller.busy; onClicked: libraryPicker.open() }
        }
    }
    Rectangle {
        id: listColumn
        visible: root.showList
        x: root.showSidebar ? root.sidebarWidth : 0
        width: root.width < 800 ? root.width : root.listWidth; height: parent.height
        color: AppTheme.contentBackground
        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: AppTheme.border; opacity: 0.5 }
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 12; spacing: 12
            RowLayout {
                visible: !root.showSidebar; Layout.fillWidth: true
                LiquidActionButton { text: "全部"; materialForm: true; onClicked: { root.revealList(); controller.filter = "all" } }
                LiquidActionButton { text: "收藏"; materialForm: true; onClicked: { root.revealList(); controller.filter = "favorite" } }
                LiquidActionButton { text: "最近删除"; materialForm: true; onClicked: root.showTrash() }
                LiquidActionButton { id: smallFolderButton; text: "文件夹"; materialForm: true; onClicked: { folderPanel.anchorItem = smallFolderButton; folderPanel.open() } }
            }
            KeyboardList {
                id: noteList
                visible: !root.trashView
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 4
                model: controller.noteModel
                delegate: ItemDelegate {
                    id: noteRow
                    required property var modelData
                    required property int index
                    readonly property string rowStatus: [modelData.favorite ? "收藏" : "", modelData.id === controller.selectedId && modelData.path === controller.path ? controller.status : modelData.readOnlyReason ? "只读" : ""].filter(Boolean).join(" · ")
                    focusPolicy: Qt.StrongFocus
                    activeFocusOnTab: false
                    Keys.onPressed: function(event) { ListView.view.handleKey(event) }
                    onActiveFocusChanged: if (activeFocus) { ListView.view.currentIndex = index; ListView.view.cursorKey = ListView.view.keyAt(index) }
                    function activate() {
                        organizer.clearSelection()
                        organizer.selectionAnchor = modelData.path
                        controller.openNote(modelData.path, modelData.match === undefined ? -1 : modelData.match)
                    }
                    readonly property bool bulkSelected: organizer.selectedPaths.indexOf(modelData.path) >= 0
                    readonly property string groupName: root.dateGroup(modelData)
                    readonly property bool startsGroup: !!groupName && (index === 0 || root.dateGroup(controller.notes[index - 1]) !== groupName)
                    readonly property int groupHeight: startsGroup ? 42 : 0
                    width: ListView.view.width; height: tokens.noteRowHeight + groupHeight
                    Label { x: 6; y: 8; visible: noteRow.startsGroup; text: noteRow.groupName; font.pixelSize: 15; font.weight: Font.DemiBold; color: AppTheme.text }
                    Rectangle {
                        x: 6; y: noteRow.groupHeight - 1; width: parent.width - 12; height: 1
                        visible: noteRow.startsGroup && /^\d{4}$/.test(noteRow.groupName)
                        color: AppTheme.border; opacity: 0.6
                    }
                    // Show selection controls only during an explicit multi-selection.
                    leftPadding: organizer.selectionMode ? 36 : 12; topPadding: 12 + groupHeight; bottomPadding: 12
                    highlighted: organizer.selectionMode ? bulkSelected : organizer.contextPath ? modelData.path === organizer.contextPath : (modelData.id === controller.selectedId && modelData.path === controller.path)
                    background: Rectangle {
                        y: noteRow.groupHeight; height: noteRow.height - y
                        radius: 10; color: noteRow.highlighted ? tokens.notesYellow : noteRow.hovered ? AppTheme.controlBackground : "transparent"
                        border.width: noteRow.activeFocus ? 2 : 0
                        border.color: AppTheme.focusRing
                    }
                    Rectangle {
                        x: noteRow.leftPadding; y: noteRow.height - 1
                        width: Math.max(0, noteRow.width - x - 12); height: 1
                        visible: noteRow.index < noteList.count - 1
                        color: AppTheme.border; opacity: 0.5
                    }
                    readonly property string thumbnail: ""
                    Image {
                        anchors.right: parent.right; anchors.rightMargin: 12
                        y: noteRow.groupHeight + 10; width: 48; height: 48
                        visible: !!noteRow.thumbnail; source: noteRow.thumbnail
                        fillMode: Image.PreserveAspectCrop; asynchronous: true; clip: true
                        sourceSize: Qt.size(96, 96)
                    }
                    rightPadding: thumbnail ? 70 : 12
                    contentItem: Column {
                        spacing: 3
                        Row {
                            width: parent.width; spacing: 6
                            Label { width: parent.width - (favoriteMark.visible ? 20 : 0); text: (noteRow.modelData.pinned ? "置顶 · " : "") + noteRow.modelData.title; textFormat: Text.PlainText; elide: Text.ElideRight; font.pixelSize: 14; font.weight: Font.DemiBold; color: noteRow.highlighted ? "white" : AppTheme.text }
                            VectorIcon { id: favoriteMark; visible: !!noteRow.modelData.favorite; name: "heart"; size: 13; color: noteRow.highlighted ? "white" : AppTheme.mutedText }
                        }
                        Label { width: parent.width; text: new Date(noteRow.modelData.displayModifiedUtc).toLocaleDateString(Qt.locale(), "M/d") + "  " + root.summaryText(noteRow.modelData.summary); textFormat: Text.PlainText; elide: Text.ElideRight; font.pixelSize: 12; color: noteRow.highlighted ? "white" : AppTheme.mutedText }
                    }
                    MouseArea {
                        anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: function(mouse) {
                            noteRow.forceActiveFocus(Qt.MouseFocusReason)
                            if (mouse.button === Qt.RightButton) organizer.openMenu(noteRow, noteRow.modelData.path)
                            else if (!organizer.click(noteRow.modelData, noteRow.index, mouse.modifiers)) noteRow.activate()
                        }
                    }
                    LiquidCheckBox {
                        id: noteSelectionBox
                        visible: organizer.selectionMode
                        x: 6; y: noteRow.groupHeight + 12; width: 24; height: 24
                        padding: 4
                        Binding { target: noteSelectionBox.indicator; property: "implicitWidth"; value: 16 }
                        Binding { target: noteSelectionBox.indicator; property: "implicitHeight"; value: 16 }
                        Binding { target: noteSelectionBox.indicator; property: "radius"; value: 5 }
                        checked: noteRow.bulkSelected; text: ""
                        Accessible.name: "选择笔记 " + noteRow.modelData.title + "，" + noteRow.modelData.path
                        onClicked: organizer.toggle(noteRow.modelData.path)
                    }
                    onClicked: activate()
                    Accessible.role: Accessible.ListItem
                    Accessible.focusable: true
                    Accessible.selected: organizer.selectionMode ? bulkSelected : highlighted
                    Accessible.onPressAction: activate()
                    Accessible.name: modelData.title + ", " + modelData.path + (rowStatus ? ", " + rowStatus : "")
                }
                Label { anchors.centerIn: parent; visible: noteList.count === 0; text: controller.query ? "没有匹配的笔记" : "从第一篇笔记开始"; color: AppTheme.mutedText; font.pixelSize: 13 }
            }
        }
    }
    RecycleBin {
        id: recycleBin
        visible: root.trashView && root.hasLibrary
        x: listColumn.x + 12
        y: root.showSidebar ? 12 : 52
        width: Math.max(0, listColumn.width - 24)
        height: Math.max(0, parent.height - y - 12)
        listVisible: root.showList
        detailX: root.editorX - x
        detailWidth: Math.max(0, root.width - root.editorX)
        controller: root.controller; sceneItem: readingScene
        onEntryActivated: root.listRequested = false
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
        visible: root.hasLibrary && !root.trashView && root.editorX < root.width
        x: root.editorX; width: root.width - x; height: parent.height
        ColumnLayout {
            id: findBar; visible: false; x: tokens.inset; y: formatBar.y + formatBar.height + 8; width: parent.width - tokens.inset * 2; spacing: 4
            RowLayout {
                Layout.fillWidth: true; spacing: 4
                LiquidTextField { id: localFind; Layout.fillWidth: true; implicitHeight: 26; materialForm: true; placeholderText: "查找当前正文"; Accessible.name: "正文查找"; onTextEdited: if (!inputMethodComposing) root.updateSearch(); onInputMethodComposingChanged: if (!inputMethodComposing) root.updateSearch(); onAccepted: root.findNext() }
                Label { text: (source.editorState.search ? source.editorState.search.current : 0) + "/" + (source.editorState.search ? source.editorState.search.total : 0); font.pixelSize: 11; color: AppTheme.mutedText }
                LiquidActionButton { id: caseSensitive; text: "Aa"; checkable: true; materialForm: true; implicitHeight: 26; Accessible.name: "区分大小写"; onToggled: root.updateSearch() }
                LiquidActionButton { text: "上一个"; materialForm: true; implicitHeight: 26; onClicked: source.action("findNext", {direction: -1}) }
                LiquidActionButton { text: "下一个"; materialForm: true; implicitHeight: 26; onClicked: root.findNext() }
                LiquidActionButton { text: "关闭"; materialForm: true; implicitHeight: 26; onClicked: { findBar.visible = false; source.action("search", {query: ""}); source.editor.forceActiveFocus() } }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 4
                LiquidTextField { id: replacement; Layout.fillWidth: true; implicitHeight: 26; materialForm: true; placeholderText: "替换为"; Accessible.name: "正文替换文本"; onAccepted: if (!inputMethodComposing) source.action("replace", {replacement: text}) }
                LiquidActionButton { text: "替换"; materialForm: true; implicitHeight: 26; enabled: root.canFormat && !replacement.inputMethodComposing; onClicked: source.action("replace", {replacement: replacement.text}) }
                LiquidActionButton { text: "全部替换"; materialForm: true; implicitHeight: 26; enabled: root.canFormat && !replacement.inputMethodComposing; onClicked: source.action("replaceAll", {replacement: replacement.text}) }
            }
        }
        KosEmptyState { anchors.centerIn: parent; width: Math.min(360, parent.width - 48); visible: !controller.path; symbol: ""; title: "一页新的可能"; description: "选择笔记，或写下此刻的想法。"; actionText: "新建笔记"; actionEnabled: !controller.busy; onActionTriggered: root.newNote() }
    }
    ColumnLayout {
        id: statusArea
        x: root.hasLibrary && root.editorX < root.width ? root.editorX : 16; width: Math.max(0, root.width - x - 16)
        anchors.bottom: parent.bottom; spacing: 6
        visible: (!root.trashView && root.editorX < root.width) || !!controller.error
        Label { Layout.fillWidth: true; elide: Text.ElideRight; text: controller.status + (source.editorState.stats && source.editorState.stats.characters !== undefined ? " · " + source.editorState.stats.characters + " 字 · " + (source.editorState.stats.approximate ? "约 " : "") + source.editorState.stats.words + " 词 · 约 " + source.editorState.stats.minutes + " 分钟" + (source.editorState.stats.selected ? " · 选中 " + source.editorState.stats.selected + " 字" : "") : ""); color: AppTheme.mutedText; font.pixelSize: 10; leftPadding: tokens.inset; bottomPadding: controller.error ? 0 : 12 }
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
    function updateSearch() { source.action("search", {query: localFind.text, caseSensitive: caseSensitive.checked}) }
    function findNext() { if (localFind.text) { updateSearch(); source.action("findNext", {direction: 1}) } }
    function showRecovery() { controller.inspectRecovery(); recoveryDialog.open() }
    function askName(kind, anchor) {
        namePanel.kind = kind; namePanel.anchorItem = anchor
        nameField.text = kind === "copy" ? "恢复副本-" + Date.now() + ".md" : kind === "move" ? controller.path : ""
        namePanel.open(); nameField.forceActiveFocus()
    }
    LiquidPopover {
        id: formatMenu; anchorItem: insertButton; sourceItem: readingScene
        onClosed: Qt.callLater(root.restoreEditorFocus)
        LiquidPopoverItem { text: "项目列表"; onTriggered: root.applyFormat("bullet") }
        LiquidPopoverItem { text: "编号列表"; onTriggered: root.applyFormat("ordered") }
        LiquidPopoverItem { text: "任务清单"; onTriggered: root.applyFormat("task") }
        LiquidPopoverItem { text: "勾选 / 取消勾选"; onTriggered: root.applyFormat("check") }
        LiquidPopoverItem { text: "行内代码"; onTriggered: root.applyFormat("code") }
        LiquidPopoverItem { text: "插入表格"; onTriggered: root.applyFormat("table") }
        LiquidPopoverItem { text: "引用"; onTriggered: root.applyFormat("quote") }
        LiquidPopoverItem { text: "代码块"; onTriggered: root.applyFormat("codeblock") }
        LiquidPopoverItem { text: "分隔线"; onTriggered: root.applyFormat("rule") }
        LiquidPopoverItem { text: "增加列表缩进"; onTriggered: root.applyFormat("indent") }
        LiquidPopoverItem { text: "减少列表缩进"; onTriggered: root.applyFormat("outdent") }
        LiquidPopoverItem { text: "添加链接…"; onTriggered: { linkPanel.open(); linkTarget.forceActiveFocus() } }
    }
    LiquidPopover {
        id: tableMenu; anchorItem: tableButton; sourceItem: readingScene
        readonly property bool inTable: session.formatState.indexOf("表格") >= 0
        readonly property var tableInfo: source.editorState.table || ({row: -1, column: -1, rows: 0, columns: 0})
        height: Math.min(implicitHeight, root.height - 32)
        onClosed: Qt.callLater(root.restoreEditorFocus)
        LiquidPopoverItem { text: "插入三列表格"; onTriggered: root.applyFormat("table") }
        LiquidPopoverItem { enabled: tableMenu.inTable; text: "在上方插入行"; onTriggered: root.applyFormat("rowBefore") }
        LiquidPopoverItem { enabled: tableMenu.inTable; text: "在下方插入行"; onTriggered: root.applyFormat("rowAfter") }
        LiquidPopoverItem { enabled: tableMenu.inTable; text: "在左侧插入列"; onTriggered: root.applyFormat("columnBefore") }
        LiquidPopoverItem { enabled: tableMenu.inTable; text: "在右侧插入列"; onTriggered: root.applyFormat("columnAfter") }
        LiquidPopoverItem { enabled: tableMenu.inTable; text: "删除当前行"; onTriggered: root.applyFormat("deleteRow") }
        LiquidPopoverItem { enabled: tableMenu.inTable; text: "删除当前列"; onTriggered: root.applyFormat("deleteColumn") }
        LiquidPopoverItem { enabled: tableMenu.inTable; text: "删除表格"; onTriggered: root.applyFormat("deleteTable") }
        LiquidPopoverItem { enabled: tableMenu.inTable; text: "单元格左对齐"; onTriggered: root.applyFormat("alignLeft") }
        LiquidPopoverItem { enabled: tableMenu.inTable; text: "单元格居中"; onTriggered: root.applyFormat("alignCenter") }
        LiquidPopoverItem { enabled: tableMenu.inTable; text: "单元格右对齐"; onTriggered: root.applyFormat("alignRight") }
        LiquidPopoverItem { enabled: tableMenu.inTable; text: "选择整行"; onTriggered: root.applyFormat("selectRow") }
        LiquidPopoverItem { enabled: tableMenu.inTable; text: "选择整列"; onTriggered: root.applyFormat("selectColumn") }
        LiquidPopoverItem { enabled: tableMenu.inTable; text: "选择整个表格"; onTriggered: root.applyFormat("selectTable") }
        LiquidPopoverItem { enabled: tableMenu.inTable && tableMenu.tableInfo.row > 1; text: "上移当前行"; onTriggered: root.applyFormat("moveRowUp") }
        LiquidPopoverItem { enabled: tableMenu.inTable && tableMenu.tableInfo.row > 0 && tableMenu.tableInfo.row < tableMenu.tableInfo.rows - 1; text: "下移当前行"; onTriggered: root.applyFormat("moveRowDown") }
        LiquidPopoverItem { enabled: tableMenu.inTable && tableMenu.tableInfo.column > 0; text: "左移当前列"; onTriggered: root.applyFormat("moveColumnLeft") }
        LiquidPopoverItem { enabled: tableMenu.inTable && tableMenu.tableInfo.column < tableMenu.tableInfo.columns - 1; text: "右移当前列"; onTriggered: root.applyFormat("moveColumnRight") }
        LiquidPopoverItem { enabled: tableMenu.inTable; text: "退出表格"; onTriggered: root.applyFormat("exitTable") }
    }
    LiquidContentPopover {
        id: linkPanel; anchorItem: insertButton; sourceItem: readingScene; preferredSide: "bottom"; width: 300
        onClosed: Qt.callLater(root.restoreEditorFocus)
        function submit() { if (root.canFormat && !linkTarget.inputMethodComposing && session.link(linkTarget.text)) { linkTarget.clear(); close(); root.editNote() } }
        contentItem: ColumnLayout {
            Label { text: "添加链接"; color: AppTheme.text; font.weight: Font.DemiBold }
            LiquidTextField { id: linkTarget; Layout.fillWidth: true; materialForm: true; placeholderText: "https://…"; Accessible.name: "链接地址"; onAccepted: linkPanel.submit() }
            LiquidActionButton { text: "添加"; materialForm: true; implicitHeight: 24; enabled: root.canFormat && !!linkTarget.text && !linkTarget.inputMethodComposing; onClicked: linkPanel.submit() }
        }
    }
    LiquidPopover {
        id: actionMenu; anchorItem: moreButton; sourceItem: readingScene
        onClosed: Qt.callLater(root.restoreEditorFocus)
        LiquidPopoverItem { text: "保存                           Ctrl+S"; enabled: controller.dirty; onTriggered: controller.save() }
        LiquidPopoverItem { text: "整理、置顶与排序…"; enabled: !!controller.path; onTriggered: organizer.openMenu(moreButton, "") }
        LiquidPopoverItem { text: "切换收藏"; enabled: !!controller.path; onTriggered: controller.favorite() }
        LiquidPopoverItem { text: "打开今日笔记"; enabled: !!controller.library && !controller.busy; onTriggered: controller.createDailyNote() }
        LiquidPopoverItem { text: "全库重命名标签…"; enabled: !!controller.library && !controller.busy; onTriggered: organizer.openTagRename() }
        LiquidPopoverItem { text: "笔记模板…"; enabled: !!controller.library && !controller.busy; onTriggered: knowledgePanel.open("templates") }
        LiquidPopoverItem { text: "链接与反向链接…"; enabled: !!controller.path && !controller.busy; onTriggered: knowledgePanel.open("links") }
        LiquidPopoverItem { text: "标签与属性…"; enabled: !!controller.path && !controller.busy; onTriggered: knowledgePanel.open("properties") }
        LiquidPopoverItem { text: "笔记历史…"; enabled: !!controller.path && !controller.busy; onTriggered: knowledgePanel.open("history") }
        LiquidPopoverItem { text: "保存搜索为智能文件夹…"; onTriggered: smartDialog.open() }
        LiquidPopoverItem { text: "重建搜索索引"; enabled: !controller.busy; onTriggered: controller.rebuildIndex() }
        LiquidPopoverItem { text: session.sourceMode ? "切换到正文模式" : "切换到 Markdown 源码"; enabled: !!controller.path && !controller.busy && !session.composing && !session.webLoading; onTriggered: session.execute("sourceMode") }
        LiquidPopoverItem { text: "返回上一笔记"; enabled: controller.canGoBack && !controller.busy; onTriggered: controller.navigateBack() }
        LiquidPopoverItem { text: "前进到下一笔记"; enabled: controller.canGoForward && !controller.busy; onTriggered: controller.navigateForward() }
        LiquidPopoverItem { text: "恢复未完成的移动操作"; enabled: !controller.busy && !controller.dirty; onTriggered: controller.recoverMove() }
        LiquidPopoverItem { text: "移动 / 重命名文件…"; enabled: !!controller.path; onTriggered: root.askName("move", moreButton) }
        LiquidPopoverItem { text: "另存为新身份副本…"; enabled: !!controller.path; onTriggered: root.askName("copy", moreButton) }
        LiquidPopoverItem { text: "移到最近删除…"; enabled: !!controller.path; onTriggered: deleteDialog.open() }
        LiquidPopoverItem { text: "放弃本地修改，读取磁盘…"; enabled: !!controller.path; onTriggered: discardDialog.open() }
        LiquidPopoverItem { text: "查找当前正文…"; enabled: !!controller.path; onTriggered: { root.editNote(); findBar.visible = true; Qt.callLater(function() { localFind.forceActiveFocus(Qt.ShortcutFocusReason) }) } }
        LiquidPopoverItem { text: "刷新笔记库"; onTriggered: controller.refresh() }
        LiquidPopoverItem { text: "恢复中心…"; onTriggered: root.showRecovery() }
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
                delegate: NavigationRow {
                    iconName: "folder"
                    required property var modelData
                    required property int index
                    width: ListView.view.width; text: modelData; specificationForm: true
                    checked: !root.trashView && controller.filter === "folder:" + modelData
                    focusPolicy: Qt.StrongFocus
                    activeFocusOnTab: false
                    Keys.onPressed: function(event) { ListView.view.handleKey(event) }
                    onActiveFocusChanged: if (activeFocus) { ListView.view.currentIndex = index; ListView.view.cursorKey = ListView.view.keyAt(index) }
                    function activate() { root.revealList(); controller.filter = "folder:" + modelData; folderPanel.close() }
                    onClicked: activate()
                    Accessible.role: Accessible.ListItem
                    Accessible.focusable: true
                    Accessible.selected: checked
                    MouseArea { anchors.fill: parent; acceptedButtons: Qt.RightButton; onClicked: organizer.openFolderMenu(parent.modelData, parent) }
                    Accessible.name: "文件夹 " + modelData
                    Accessible.onPressAction: activate()
                }
            }
            LiquidActionButton { text: "当前文件夹操作…"; materialForm: true; enabled: controller.filter.startsWith("folder:"); onClicked: { folderPanel.close(); organizer.openFolderMenu(controller.filter.slice(7), smallFolderButton) } }
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
                    plainTrigger: true; indicatorStyle: "upDown"; indicatorBackground: true
                    popupMaterialForm: true; compactPopup: true; popupFontWeight: Font.Normal
                    popupBorderWidth: 0; popupShadowEnabled: true; neutralOptionHighlight: true
                    Layout.preferredHeight: 24
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

    NotesOrganizer { id: organizer; anchors.fill: parent; controller: root.controller; sceneItem: readingScene }
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
    Connections { target: controller; function onMissingLinkRequested() { missingLinkDialog.open() } }
    LiquidDialog {
        id: missingLinkDialog; title: "链接笔记尚不存在"; sourceItem: readingScene; anchors.centerIn: parent; width: Math.min(460, root.width - 40); standardButtons: Dialog.Cancel
        contentItem: ColumnLayout {
            Label { text: "创建并打开：\n" + (controller.missingLink.targetPath || ""); textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.Wrap; color: AppTheme.text }
            LiquidActionButton { text: "创建并打开"; materialForm: true; enabled: !!controller.missingLink.targetPath && !controller.busy; onClicked: { controller.createMissingLink(); missingLinkDialog.close() } }
        }
    }
    LiquidDialog {
        id: smartDialog; title: "保存智能文件夹"; sourceItem: readingScene; anchors.centerIn: parent; width: Math.min(440, root.width - 40)
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: ColumnLayout {
            Label { text: "保存当前查询和文件夹／收藏范围。支持 tag:标签、prop:属性=值、folder:目录、is:favorite。"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: AppTheme.text }
            LiquidTextField { id: smartName; Layout.fillWidth: true; materialForm: true; placeholderText: "智能文件夹名称" }
            LiquidActionButton { text: "删除同名智能文件夹"; materialForm: true; enabled: !!controller.smartFolders[smartName.text]; onClicked: { controller.deleteSmartFolder(smartName.text); smartDialog.close() } }
        }
        onAccepted: if (smartName.text.trim()) controller.saveSmartFolder(smartName.text)
    }
    LiquidDialog {
        id: recoveryDialog; title: "恢复中心"; sourceItem: readingScene
        onVisibleChanged: controller.setRecoveryVisible(visible)
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
                    // Hidden TextAreas still lay out their full contents. Create
                    // recovery previews only while the recovery window is open.
                    model: recoveryDialog.visible ? (controller.recovery.drafts || []) : []
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
                LiquidActionButton { text: "打开最近删除"; materialForm: true; onClicked: { recoveryDialog.close(); root.showTrash() } }
                Label { text: "保留的提交版本"; color: AppTheme.text; font.weight: Font.DemiBold }
                Label { Layout.fillWidth: true; text: "每条 intent 含目标路径、基线及新摘要，最后一行是保留文件的绝对路径。交换前它是新版本，交换后是被替换版本；需比较摘要后手动取回，不自动覆盖。"; wrapMode: Text.Wrap; color: AppTheme.mutedText; font.pixelSize: 12 }
                Repeater {
                    model: recoveryDialog.visible ? (controller.recovery.commits || []) : []
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
