import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Kos.Ui
import "FolderIcons.js" as FolderIcons

ApplicationWindow {
    id: root
    required property var session
    required property var controller
    width: controller.preference("width", tokens.windowWidth)
    height: controller.preference("height", tokens.windowHeight)
    readonly property int editorMinimumWidth: 720
    readonly property real browserMinimumWidth: 240
    minimumWidth: editorMinimumWidth
    minimumHeight: 480
    // Wayland compositor scripts may bypass the advertised minimum; keep the
    // editor no narrower than the full toolbar in every case.
    onWidthChanged: {
        if (width < editorMinimumWidth) width = editorMinimumWidth
        if (width >= editorMinimumWidth + browserMinimumWidth + sidebarWidth
                || width < editorMinimumWidth + browserMinimumWidth) listHidden = false
    }
    visible: true
    title: qsTr("笔记")
    flags: Qt.Window | Qt.FramelessWindowHint
    color: AppTheme.contentBackground
    font.family: tokens.uiFontFamily
    property bool allowClose: false
    readonly property var currentNote: controller.notes.find(function(note) { return note.id === controller.selectedId && note.path === controller.path }) || ({})
    function newNote() { searchOpen = false; searchPreviousFilter = ""; searchPreviousTrash = false; controller.createNote() }
    function editNote() { Qt.callLater(function() { source.editor.forceActiveFocus() }) }
    property bool sidebarRequested: controller.preference("sidebar", true)
    property bool listRequested: false
    property bool listHidden: false
    property bool compactSidebarRequested: false
    property bool trashView: false
    readonly property string listTitle: controller.query && !root.trashView ? "搜索结果" : root.trashView ? "最近删除" : controller.filter === "favorite" ? "收藏" : controller.filter === "inbox" ? "未分类" : controller.filter.startsWith("import:") ? "本次导入" : controller.filter.startsWith("folder:") ? controller.filter.slice(7).split("/").pop() : "全部笔记"
    function showTrash() { browseCategory(controller.filter); organizer.clearSelection(); trashView = true; controller.inspectRecovery() }
    property string activeLibrary: controller.library
    onActiveLibraryChanged: trashView = false
    function toggleNavigation() {
        if (width >= editorMinimumWidth + browserMinimumWidth + sidebarWidth) sidebarRequested = !sidebarRequested
        else if (width >= editorMinimumWidth + sidebarWidth) compactSidebarRequested = !compactSidebarRequested
        else listRequested = !showList
    }
    readonly property var topLevelFolders: Array.from(controller.folders).filter(p => !p.includes("/"))
    readonly property string currentFolder: !trashView && !controller.query && controller.filter.startsWith("folder:") ? controller.filter.slice(7) : ""
    readonly property var childFolders: currentFolder ? Array.from(controller.folders).filter(p => organizer.parentFolder(p) === currentFolder) : []
    property bool childFoldersExpanded: true
    onCurrentFolderChanged: childFoldersExpanded = true
    function browseParentFolder() {
        const parent = organizer.parentFolder(currentFolder)
        browseCategory(parent ? "folder:" + parent : "all")
    }
    function revealList() { trashView = false; listRequested = true; listHidden = false; compactSidebarRequested = false }
    function noteLocation(path) { const parts = String(path || "").split("/"); parts.pop(); return parts.length ? parts.join(" › ") : "未分类" }
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
    property int viewPreferenceRevision: 0
    readonly property string viewPreferenceKey: "noteViewMode/" + encodeURIComponent(controller.library) + "/" + encodeURIComponent(currentFolder ? "folder:" + currentFolder : "all")
    readonly property string noteViewMode: {
        viewPreferenceRevision
        return controller.preference(viewPreferenceKey, controller.preference("noteViewMode", "list")) === "gallery" ? "gallery" : "list"
    }
    function setNoteViewMode(mode) {
        controller.setPreference(viewPreferenceKey, mode)
        if (!currentFolder) controller.setPreference("noteViewMode", mode)
        ++viewPreferenceRevision
    }
    property real galleryWidth: controller.preference("galleryWidth", 448)
    readonly property bool gallery: noteViewMode === "gallery" && !trashView
    readonly property real browserWidth: Math.min(root.gallery ? Math.max(416, root.galleryWidth) : root.listWidth,
        Math.max(root.browserMinimumWidth, root.width - (root.showSidebar ? root.sidebarWidth : 0) - root.editorMinimumWidth))
    readonly property bool hasLibrary: controller.library.length > 0
    readonly property bool threeColumns: width >= editorMinimumWidth + browserMinimumWidth + sidebarWidth
    readonly property bool sidebarAloneFits: width >= editorMinimumWidth + sidebarWidth
    readonly property bool showSidebar: hasLibrary && sidebarRequested && (threeColumns || compactSidebarRequested && sidebarAloneFits)
    readonly property bool showList: hasLibrary && !listHidden && !(showSidebar && !threeColumns) && (
        width >= editorMinimumWidth + browserMinimumWidth
        || (width < 800 && listRequested)
        || (trashView ? !recycleBin.currentEntry : !controller.path))
    readonly property real editorX: (showSidebar ? sidebarWidth : 0) + (showList ? (width < 800 ? width : browserWidth) : 0)
    readonly property bool popupsOpen: (source.popupActive || knowledgePanel.popupActive || smartDialog.visible || missingLinkDialog.visible) || libraryActions.popupActive || organizer.popupActive || recycleBin.popupActive || actionMenu.visible || outputMenu.visible || insertMenu.visible || shortcutsDialog.visible || formatMenu.visible || tableMenu.visible || linkPanel.visible || paragraphMenu.visible || namePanel.visible || recoveryDialog.visible || discardDialog.visible || folderPanel.visible || notesSettings.visible || discardRecoveryDialog.visible
    onClosing: function(close) {
        if (source.exporting) { close.accepted = false; source.notify("正在导出，请完成后再关闭窗口"); return }
        controller.setPreference("width", width); controller.setPreference("height", height)
        if (!allowClose) { close.accepted = false; controller.requestClose() }
    }
    onSidebarRequestedChanged: controller.setPreference("sidebar", sidebarRequested)
    onSidebarWidthChanged: controller.setPreference("sidebarWidth", sidebarWidth)
    onListWidthChanged: controller.setPreference("listWidth", listWidth)
    onGalleryWidthChanged: controller.setPreference("galleryWidth", galleryWidth)
    Binding { target: AppTheme; property: "compactDesign"; value: true }
    Binding { target: LiquidStyle; property: "intensityLevel"; value: "medium" }
    Component.onCompleted: {
        AppTheme.reduceTransparency = controller.preference("reduceTransparency", false)
        AppTheme.reduceMotion = controller.preference("reduceMotion", false)
        AppTheme.appearanceMode = controller.preference("appearance", "system")
    }
    NotesTokens { id: tokens }
    Connections {
        target: controller
        function onFolderCreated(path) {
            root.revealList()
            Qt.callLater(function() { noteList.positionViewAtBeginning() })
        }
        function onCloseReady() { root.allowClose = true; root.close() }
        function onDocumentLoaded(cursor, scroll, matchLength) { root.trashView = false; root.listRequested = false; if (matchLength > 0) root.editNote() }
    }
    CommandRegistry {
        id: commands
        session: root.session; editor: source.editor; controller: root.controller
        historyInputFocused: {
            const item = root.activeFocusItem
            return !!item && typeof item.text === "string" && typeof item.selectAll === "function"
        }
        blocked: root.popupsOpen || search.inputMethodComposing || localFind.inputMethodComposing || replacement.inputMethodComposing
        onSidebarRequested: root.toggleNavigation()
        onSearchRequested: root.openGlobalSearch()
        onNewNoteRequested: root.newNote()
        onFindRequested: root.openFind(false)
        onReplaceRequested: root.openFind(true)
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
            if (event.key === Qt.Key_Escape && organizer.selectionMode) { organizer.clearSelection(); event.accepted = true; return }
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
            if (currentItem && typeof currentItem.openContext === "function" && (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && event.modifiers === Qt.ShiftModifier))) {
                currentItem.openContext(); event.accepted = true; return
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
    // Internal drag payloads never expose disk URLs or move source files.
    component OrganizePointer: MouseArea {
        id: pointer
        property string folderPath: ""
        property string notePath: ""
        property string noteTitle: ""
        property bool didDrag: false
        signal clickedItem(int button, int modifiers, point position)
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        property bool dragEnabled: true
        drag.target: dragEnabled && (pressedButtons & Qt.LeftButton) ? organizePreview : null
        drag.axis: Drag.XAndYAxis
        onPressed: function(mouse) {
            didDrag = false
            const p = mapToItem(organizePreview.parent, mouse.x, mouse.y)
            organizePreview.x = p.x - 12; organizePreview.y = p.y - 16
        }
        onPositionChanged: function(mouse) {
            if (!drag.active) return
            const p = mapToItem(organizePreview.parent, mouse.x, mouse.y)
            organizePreview.x=p.x-12; organizePreview.y=p.y-16
        }
        drag.onActiveChanged: if (drag.active) {
            const p = mapToItem(organizePreview.parent, mouseX, mouseY)
            organizePreview.x=p.x-12; organizePreview.y=p.y-16
            didDrag = true
            if (notePath) organizer.startNoteDrag(notePath, noteTitle)
            else organizer.startFolderDrag(folderPath)
            organizePreview.Drag.active = true
        }
        onReleased: function(mouse) {
            if (didDrag) { organizePreview.Drag.drop(); organizePreview.Drag.active = false; organizer.endDrag() }
        }
        onClicked: function(mouse) { if (!didDrag) clickedItem(mouse.button, mouse.modifiers, mapToItem(parent, mouse.x, mouse.y)) }
        onCanceled: { organizePreview.Drag.cancel(); organizer.endDrag() }
    }
    component FolderDropTarget: DropArea {
        id: dropZone
        property string folderPath: ""
        property bool notesAllowed: true
        property string placement: "into"
        readonly property bool allowed: placement === "into" ? organizer.canDrop(folderPath, notesAllowed)
            : organizer.canPlaceFolder(organizer.parentFolder(folderPath), folderPath, placement === "after")
        anchors.fill: parent
        anchors.topMargin: -1; anchors.bottomMargin: -1
        keys: ["nextnotes-organization"]
        function updatePlacement(event) {
            placement = organizer.dragKind !== "folder" ? "into"
                : event.y < 8 ? "before" : event.y > height - 8 ? "after" : "into"
            if (!allowed) { organizer.clearDropHint(dropZone); return }
            const name = folderPath.split("/").pop()
            organizer.showDropHint(dropZone, placement === "into" ? "放入「" + name + "」"
                : "放在「" + name + "」" + (placement === "after" ? "后面" : "前面"))
        }
        // Accept entry so crossing an invalid centre can still reach a valid edge.
        onEntered: function(event) { event.accepted = event.source === organizer; if (event.accepted) updatePlacement(event) }
        onPositionChanged: function(event) { updatePlacement(event) }
        onExited: organizer.clearDropHint(dropZone)
        Component.onDestruction: if (organizer) organizer.clearDropHint(dropZone)
        onDropped: function(event) {
            event.accepted = false
            updatePlacement(event)
            if (event.source === organizer && allowed && (placement === "into" ? organizer.dropInto(folderPath, notesAllowed)
                    : organizer.placeFolder(organizer.parentFolder(folderPath), folderPath, placement === "after"))) {
                event.accept(Qt.MoveAction)
                if (folderPanel.visible) folderPanel.close()
            }
            organizer.clearDropHint(dropZone)
        }
        Rectangle {
            anchors.fill: parent; anchors.topMargin: 1; anchors.bottomMargin: 1
            radius: 8; color: AppTheme.withAlpha(tokens.primaryYellow, AppTheme.dark ? 0.18 : 0.14)
            border.color: tokens.primaryYellow
            visible: dropZone.containsDrag && dropZone.allowed && dropZone.placement === "into"
        }
        Rectangle {
            x: 8; width: parent.width - 16; height: 2; radius: 1
            y: dropZone.placement === "after" ? parent.height - 2 : 0
            color: tokens.primaryYellow
            visible: dropZone.containsDrag && dropZone.allowed && dropZone.placement !== "into"
        }
        Timer {
            interval: 650
            running: organizer.dragKind === "notes" && dropZone.containsDrag && dropZone.allowed && !!dropZone.folderPath && root.currentFolder !== dropZone.folderPath
            onTriggered: { root.browseCategory("folder:" + dropZone.folderPath); folderPanel.close() }
        }
    }
    component FolderListEndDropTarget: DropArea {
        id: endDropZone
        property string folderPath: ""
        readonly property bool allowed: organizer.canPlaceFolder(folderPath, "", false)
        visible: organizer.dragKind === "folder"
        keys: ["nextnotes-organization"]
        onEntered: function(event) {
            event.accepted = event.source === organizer && allowed
            if (event.accepted) organizer.showDropHint(endDropZone, folderPath ? "放到「" + folderPath.split("/").pop() + "」末尾" : "放到侧栏文件夹列表末尾")
        }
        onExited: organizer.clearDropHint(endDropZone)
        Component.onDestruction: if (organizer) organizer.clearDropHint(endDropZone)
        onDropped: function(event) {
            event.accepted = false
            if (event.source === organizer && organizer.placeFolder(folderPath, "", false)) {
                event.accept(Qt.MoveAction)
                if (folderPanel.visible) folderPanel.close()
            }
            organizer.clearDropHint(endDropZone)
        }
        Rectangle {
            x: 8; width: parent.width - 16; height: 2; radius: 1
            color: tokens.primaryYellow; visible: endDropZone.containsDrag && endDropZone.allowed
        }
    }
    Rectangle {
        id: organizePreview; parent: Overlay.overlay; z: 1000
        visible: organizer.dragging
        width: Math.min(280, dragLabel.implicitWidth + 28); height: organizer.dropHint ? 50 : 34; radius: 9
        color: AppTheme.controlBackground; border.color: AppTheme.border
        Drag.source: organizer; Drag.keys: ["nextnotes-organization"]; Drag.supportedActions: Qt.MoveAction
        Drag.hotSpot: Qt.point(12,16)
        Label { id: dragLabel; anchors.fill: parent; anchors.margins: 8; text: organizer.dragTitle + (organizer.dropHint ? "\n" + organizer.dropHint : ""); font.pixelSize: 12; textFormat: Text.PlainText; elide: Text.ElideRight; color: AppTheme.text }
    }
    Timer {
        interval: 30; repeat: true; running: organizer.dragging
        onTriggered: {
            const view = [compactFolderList, sidebarFolderList, childFolderList, noteList.activeView].find(candidate => {
                if (!candidate.visible) return false
                const point = organizePreview.mapToItem(candidate, 12, 16)
                return point.x >= 0 && point.x <= candidate.width && point.y >= -16 && point.y <= candidate.height + 16
            })
            if (!view) return
            const p = organizePreview.mapToItem(view, 12,16)
            if (p.x < -16 || p.x > view.width + 16) return
            const delta = p.y < 28 ? -10 : p.y > view.height - 28 ? 10 : 0
            if (delta) view.contentY = Math.max(0, Math.min(Math.max(0, view.contentHeight - view.height), view.contentY + delta))
        }
    }
    Shortcut { sequence: "Escape"; enabled: organizer.dragging; onActivated: { organizePreview.Drag.cancel(); organizer.endDrag() } }
    component ToolbarAction: LiquidActionButton {
        property string help: ""
        iconOnly: true; toolbarAction: true
        materialForm: false; backdrop: readingBackdrop; liquidIntensity: "medium"
        focusPolicy: Qt.TabFocus
        Accessible.name: help
        KosToolTip { target: parent; backdrop: readingBackdrop; text: parent.help }
    }
    component HeaderAction: ToolbarAction {
        Layout.minimumHeight: implicitHeight
        Layout.maximumHeight: implicitHeight
    }
    component ToolbarActionGroup: LiquidActionButtonGroup {
        id: toolbarGroup
        materialForm: false; backdrop: readingBackdrop
        dividerColor: "transparent"
        Repeater {
            model: toolbarGroup.icons.length
            Rectangle {
                required property int index
                x: index * toolbarGroup.buttonSize
                y: (toolbarGroup.height - height) / 2
                width: 1; height: 12
                color: AppTheme.border
                opacity: 0.65
                visible: index > 0
            }
        }
    }
    component ListNavigationActions: ToolbarActionGroup {
        id: listActions
        icons: root.currentFolder ? ["back", "more"] : ["more"]
        labels: root.currentFolder
            ? ["返回" + (organizer.parentFolder(root.currentFolder) ? organizer.parentFolder(root.currentFolder).split("/").pop() : "全部笔记"), "列表操作"]
            : ["列表操作"]
        enabledActions: root.currentFolder ? [!controller.busy, true] : [true]
        onTriggered: function(index) {
            if (root.currentFolder && index === 0) root.browseParentFolder()
            else if (root.trashView) recycleBin.openMenu(listActions, null)
            else organizer.openMenu(listActions, "")
        }
    }
    property bool searchOpen: false
    property string searchPreviousFilter: ""
    property bool searchPreviousTrash: false
    function beginSearch() {
        if (!searchOpen) { searchPreviousFilter = controller.filter; searchPreviousTrash = trashView }
        searchOpen = true
        revealList()
        if (controller.filter !== "all") controller.filter = "all"
    }
    function openGlobalSearch() {
        beginSearch()
        Qt.callLater(function() { search.forceActiveFocus(Qt.ShortcutFocusReason) })
    }
    function updateGlobalQuery(value) { beginSearch(); controller.query = value }
    function closeGlobalSearch() {
        if (search.inputMethodComposing) return
        controller.query = ""
        searchOpen = false; search.focus = false
        if (searchPreviousFilter) controller.filter = searchPreviousFilter
        if (searchPreviousTrash) { trashView = true; controller.inspectRecovery() }
        searchPreviousFilter = ""; searchPreviousTrash = false
    }
    function browseCategory(filter) {
        searchOpen = false; searchPreviousFilter = ""; searchPreviousTrash = false
        controller.selectCategory(filter); revealList()
        Qt.callLater(function() { noteList.positionViewAtBeginning() })
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
        focusRingColor: "transparent"
        implicitHeight: tokens.navigationHeight
        specificationSelectedFillColor: tokens.navigationSelected
        specificationSelectedTextColor: tokens.navigationSelectedText
        iconColor: tokens.primaryYellow
    }
    component FolderEntry: Item {
        id: entry
        property string folderPath: ""
        property bool highlightBranch: false
        property bool showMenuButton: true
        property var keyboardView: null
        property int rowIndex: -1
        activeFocusOnTab: false
        onActiveFocusChanged: if (activeFocus) entryButton.forceActiveFocus(Qt.TabFocusReason)
        implicitHeight: tokens.navigationHeight
        function activate() { root.browseCategory("folder:" + folderPath); folderPanel.close() }
        function openContext(position) { organizer.openFolderMenu(folderPath, entryButton, position || Qt.point(entryButton.width / 2, entryButton.height / 2)) }
        HoverHandler { id: entryHover }
        NavigationRow {
            id: entryButton
            width: parent.width - (entryMenu.visible ? 28 : 0)
            iconName: controller.folderIcons[entry.folderPath] || "folder"; text: entry.folderPath.split("/").pop()

            checked: !root.trashView && (controller.filter === "folder:" + entry.folderPath || (entry.highlightBranch && controller.filter.startsWith("folder:" + entry.folderPath + "/")))
            focusPolicy: Qt.StrongFocus
            activeFocusOnTab: !entry.keyboardView
            onActiveFocusChanged: if (activeFocus && entry.keyboardView) { entry.keyboardView.currentIndex = entry.rowIndex; entry.keyboardView.cursorKey = entry.folderPath }
            Accessible.name: "文件夹 " + entry.folderPath.split("/").join(" › ")
            onClicked: entry.activate()
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && event.modifiers === Qt.ShiftModifier)) { entry.openContext(); event.accepted = true }
                else if (entry.keyboardView) entry.keyboardView.handleKey(event)
            }
            OrganizePointer {
                folderPath: entry.folderPath; enabled: !controller.busy
                onClickedItem: function(button, modifiers, position) { entryButton.forceActiveFocus(); if (button === Qt.RightButton) entry.openContext(position); else entry.activate() }
            }
        }
        FolderDropTarget { folderPath: entry.folderPath }
        ToolbarAction {
            id: entryMenu
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            iconName: "more"
            visible: entry.showMenuButton && (entryHover.hovered || entryButton.activeFocus || activeFocus)
            help: entry.folderPath.split("/").pop() + "的文件夹操作"
            enabled: !controller.busy
            onClicked: organizer.openFolderMenu(entry.folderPath, entryMenu)
        }
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
            x: 14; width: root.showSidebar ? root.sidebarWidth - 26 : LiquidStyle.toolbarButtonHeight
            anchors.verticalCenter: parent.verticalCenter; spacing: 8
            Label {
                objectName: "notesTitle"
                visible: root.showSidebar
                text: root.title; color: AppTheme.text
                font.pixelSize: 22; font.weight: Font.Bold
                Layout.fillWidth: true; Layout.preferredHeight: 28
                verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
            }
            KosSidebarToggle {
                id: sidebarButton
                objectName: "sidebarToggleButton"
                implicitWidth: LiquidStyle.toolbarButtonHeight; implicitHeight: LiquidStyle.toolbarButtonHeight
                scale: 1
                // Keep the shared rounded sidebar glyph used by the other apps.
                // Scale from the shared glyph size; do not substitute panelLeft.
                contentItem.scale: LiquidStyle.toolbarIconSize / contentItem.size
                glyphColor: AppTheme.text; glyphPaneColor: glyphColor
                focusPolicy: Qt.TabFocus
                Accessible.name: "显示或隐藏导航 · Ctrl+Shift+B"
                background: LiquidActionButton {
                    enabled: false; Accessible.ignored: true
                    materialForm: false; backdrop: readingBackdrop
                    liquidIntensity: "medium"
                }
                onClicked: root.toggleNavigation()
            }
        }
        RowLayout {
            visible: root.showList && root.width >= 800
            x: root.showSidebar ? root.sidebarWidth + 18 : 54
            width: root.showSidebar ? root.browserWidth - 36 : Math.max(80, root.browserWidth - 66)
            anchors.verticalCenter: parent.verticalCenter
            Label {
                Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 0
                elide: Text.ElideRight; text: root.listTitle
                color: AppTheme.text; font.pixelSize: 16; font.weight: Font.DemiBold
            }
            LiquidActionButton {
                visible: root.trashView ? recycleBin.selectionMode : organizer.selectionMode
                text: "取消"; Accessible.name: "取消选择"
                toolbarAction: true; materialForm: true
                onClicked: root.trashView ? recycleBin.clearSelection() : organizer.clearSelection()
            }
            ListNavigationActions {}
        }
        Item {
            id: headerActions
            visible: root.hasLibrary
            x: root.editorX < root.width ? Math.max(root.showSidebar ? 178 : 54, root.editorX + 12) : (root.showSidebar ? 178 : 54)
            anchors.verticalCenter: parent.verticalCenter
            readonly property real controlsLeft: windowControls.x + (windowControls.buttonLayout.length ? windowControls.buttonLayout[0].x : 0)
            width: Math.max(0, controlsLeft - 10 - x); height: LiquidStyle.toolbarButtonHeight
            readonly property bool searchShown: root.searchOpen || controller.query.length > 0
            readonly property bool searching: searchShown
            Row {
                id: noteActions
                visible: !headerActions.searching
                height: parent.height; spacing: 8
                HeaderAction { objectName: "newNoteButton"; iconName: "editRounded"; help: "新建笔记 · Ctrl+N"; enabled: !controller.busy; onClicked: root.newNote() }
            }
            Flickable {
                id: editorActionsViewport
                visible: !headerActions.searching
                readonly property real availableWidth: Math.max(0, globalActions.x - noteActions.width - 24)
                width: Math.min(editorActions.implicitWidth, availableWidth); height: LiquidStyle.toolbarButtonHeight
                anchors.verticalCenter: parent.verticalCenter
                x: Math.max(noteActions.width + 12, Math.min((parent.width - width) / 2, globalActions.x - 12 - width))
                contentWidth: editorActions.implicitWidth; contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                // Keep the shared surface edges intact when no scrolling is needed.
                clip: contentWidth > width
                onWidthChanged: contentX = Math.max(0, Math.min(contentX, contentWidth - width))
                onContentWidthChanged: contentX = Math.max(0, Math.min(contentX, contentWidth - width))
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded; height: 3 }
                WheelHandler {
                    onWheel: function(event) {
                        if (editorActionsViewport.contentWidth <= editorActionsViewport.width) return
                        const delta = event.pixelDelta.x || event.pixelDelta.y || event.angleDelta.x / 2 || event.angleDelta.y / 2
                        editorActionsViewport.contentX = Math.max(0, Math.min(editorActionsViewport.contentWidth - editorActionsViewport.width, editorActionsViewport.contentX - delta))
                        event.accepted = true
                    }
                }
                Connections {
                    target: root
                    function onActiveFocusItemChanged() {
                        const item = root.activeFocusItem
                        if (!item || !editorActionsViewport.visible) return
                        let ancestor = item
                        while (ancestor && ancestor !== editorActions) ancestor = ancestor.parent
                        if (!ancestor) return
                        const left = item.mapToItem(editorActions, 0, 0).x
                        const desired = left < editorActionsViewport.contentX ? left
                            : Math.max(editorActionsViewport.contentX, left + item.width - editorActionsViewport.width)
                        editorActionsViewport.contentX = Math.max(0, Math.min(editorActionsViewport.contentWidth - editorActionsViewport.width, desired))
                    }
                }
                ToolbarActionGroup {
                    id: editorActions
                    objectName: "bodyEditingButtonGroup"
                    height: LiquidStyle.toolbarButtonHeight
                    anchors.verticalCenter: parent.verticalCenter
                    clip: false
                    materialForm: false; backdrop: readingBackdrop
                    readonly property var actions: [
                        {id: "undo", icon: "undo", label: controller.undoText, enabled: !root.popupsOpen && controller.canUndo},
                        {id: "redo", icon: "redo", label: controller.redoText, enabled: !root.popupsOpen && controller.canRedo}
                    ].concat(session.sourceMode ? [
                        {id: "sourceMode", icon: "", label: "返回正文", enabled: source.visible && !controller.busy && !session.composing}
                    ] : [
                        {id: "paragraph", icon: "", label: "段落样式", enabled: root.canFormat},
                        {id: "bold", icon: "bold", label: "粗体 · Ctrl+B", enabled: root.canFormat},
                        {id: "italic", icon: "italic", label: "斜体 · Ctrl+I", enabled: root.canFormat},
                        {id: "strike", icon: "strikethrough", label: "删除线 · Ctrl+Shift+X", enabled: root.canFormat},
                        {id: "code", icon: "code", label: "行内代码 · Ctrl+E", enabled: root.canFormat},
                        {id: "quote", icon: "text_quote", label: "引用", enabled: root.canFormat},
                        {id: "codeblock", icon: "doc_plaintext", label: "代码块", enabled: root.canFormat},
                        {id: "link", icon: "link", label: "添加链接", enabled: root.canFormat},
                        {id: "task", icon: "checkmark_square", label: "任务清单", enabled: root.canFormat},
                        {id: "list", icon: "list_bullet", label: "列表", enabled: root.canFormat},
                        {id: "table", icon: "table", label: "表格与行列操作", enabled: root.canFormat},
                        {id: "insert", icon: "plus", label: "插入", enabled: root.canFormat}
                    ]).concat([{id: "more", icon: "more", label: "更多", enabled: !!controller.path && !controller.busy}])
                    icons: actions.map(action => action.icon)
                    labels: actions.map(action => action.label)
                    enabledActions: actions.map(action => action.enabled)
                    objectNames: actions.map(action => "editorAction_" + action.id)
                    function actionX(id) { return Math.max(0, actions.findIndex(action => action.id === id)) * buttonSize }
                    onTriggered: function(index) {
                        const action = actions[index].id
                        if (action === "undo") controller.undo()
                        else if (action === "redo") controller.redo()
                        else if (action === "sourceMode") session.execute("sourceMode")
                        else if (action === "paragraph") paragraphMenu.openFromButton(paragraphButton)
                        else if (action === "list") formatMenu.openFromButton(listButton)
                        else if (action === "table") tableMenu.openFromButton(tableButton)
                        else if (action === "insert") insertMenu.openFromButton(insertButton)
                        else if (action === "link") linkPanel.openFromButton(linkButton)
                        else if (action === "more") actionMenu.openFromButton(moreButton)
                        else root.applyFormat(action)
                    }
                    Text {
                        x: editorActions.actionX(session.sourceMode ? "sourceMode" : "paragraph")
                        width: editorActions.buttonSize; height: editorActions.buttonSize
                        text: !session.sourceMode && session.headingLevel ? "H" + session.headingLevel : "正文"
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        color: editorActions.enabledActions[2] ? AppTheme.text : AppTheme.disabledText
                        font.pixelSize: 10; Accessible.ignored: true
                    }
                    Item { id: paragraphButton; x: editorActions.actionX("paragraph"); width: editorActions.buttonSize; height: width }
                    Item { id: listButton; x: editorActions.actionX("list"); width: editorActions.buttonSize; height: width }
                    Item { id: tableButton; x: editorActions.actionX("table"); width: editorActions.buttonSize; height: width }
                    Item { id: insertButton; x: editorActions.actionX("insert"); width: editorActions.buttonSize; height: width }
                    Item { id: linkButton; x: editorActions.actionX("link"); width: editorActions.buttonSize; height: width }
                    Item { id: moreButton; x: editorActions.actionX("more"); width: editorActions.buttonSize; height: width }
                }
            }
            RowLayout {
                id: globalActions
                anchors.right: parent.right
                width: headerActions.searching ? parent.width : implicitWidth
                height: parent.height; spacing: 10
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
                        leftPadding: 30; rightPadding: headerActions.searching ? LiquidStyle.toolbarButtonHeight + 10 : 10
                        VectorIcon {
                            x: 8; anchors.verticalCenter: parent.verticalCenter
                            name: "search"; size: LiquidStyle.toolbarIconSize; color: search.textColor
                        }
                        onTextEdited: if (!inputMethodComposing) root.updateGlobalQuery(text)
                        onInputMethodComposingChanged: if (!inputMethodComposing && text !== controller.query) root.updateGlobalQuery(text)
                        onActiveFocusChanged: if (!activeFocus && text.length === 0 && root.searchOpen) root.closeGlobalSearch()
                        Keys.onEscapePressed: function(event) { if (!inputMethodComposing) { root.closeGlobalSearch(); event.accepted = true } }
                        Accessible.name: "搜索所有笔记"
                    }
                    HeaderAction {
                        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                        visible: headerActions.searching
                        iconName: "plus"; contentItem.rotation: 45; help: "关闭搜索"
                        enabled: !search.inputMethodComposing
                        onClicked: root.closeGlobalSearch()
                    }
                }
                HeaderAction {
                    objectName: "searchToggleButton"
                    visible: !headerActions.searchShown
                    iconName: "search"; help: "搜索所有笔记"; enabled: !controller.busy
                    onClicked: root.openGlobalSearch()
                }
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
            allowRestoreFocus: !root.popupsOpen && !noteNameEditor.editing
            visible: root.hasLibrary && !root.trashView && !!controller.path && root.editorX < root.width
            x: root.editorX; y: 8 + (controller.externalDocument ? 36 : noteNameEditor.height + 8) + (findBar.visible ? findBar.implicitHeight + 8 : 0)
            width: Math.max(0, parent.width - x); height: Math.max(0, parent.height - y - statusArea.height)
            bodySize: notesSettings.bodySize; readingWidth: tokens.readingWidth; inset: tokens.editorInset
            onDocumentReady: if (findBar.visible) root.updateSearch()
        }


    }
    KnowledgePanel { onBrowseRequested: root.revealList(); id: knowledgePanel; anchors.fill: parent; controller: root.controller; session: root.session; sceneItem: readingScene }
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
    LiquidBackdrop { id: readingBackdrop; sourceItem: readingScene; registerAsDefault: false }
    KosEmptyState {
        anchors.centerIn: parent; width: Math.min(420, root.width - 64)
        visible: !root.hasLibrary
        symbol: ""; title: "让想法有处可归"
        description: "写下想法，让灵感与日常记录有处可归。"
        actionText: controller.busy ? "正在准备…" : "开始记录"; actionEnabled: !controller.busy
        onActionTriggered: controller.restoreWorkspace()
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
            Label { text: "视图"; color: AppTheme.mutedText; font.pixelSize: 11; font.weight: Font.DemiBold; Layout.leftMargin: 4; Layout.topMargin: 12; Layout.bottomMargin: 2 }
            NavigationRow { iconName: "album"; Layout.fillWidth: true; text: "全部笔记"; specificationForm: true; checked: !root.trashView && controller.filter === "all"; onClicked: { root.browseCategory("all") } }
            NavigationRow { iconName: "heart"; Layout.fillWidth: true; text: "收藏"; specificationForm: true; checked: !root.trashView && controller.filter === "favorite"; onClicked: { root.browseCategory("favorite") } }
            NavigationRow { iconName: "trashRounded"; Layout.fillWidth: true; text: "最近删除"; specificationForm: true; checked: root.trashView; onClicked: root.showTrash() }
            Repeater { model: Object.keys(controller.smartFolders); delegate: NavigationRow { required property string modelData; Layout.fillWidth: true; text: modelData; specificationForm: true; onClicked: { root.searchOpen = false; root.searchPreviousFilter = ""; root.searchPreviousTrash = false; root.revealList(); controller.openSmartFolder(modelData) } } }
            RowLayout {
                Layout.fillWidth: true; Layout.topMargin: 16; Layout.leftMargin: 4; spacing: 8
                Label { text: "文件夹"; color: AppTheme.mutedText; font.pixelSize: 11 }
                Item { Layout.fillWidth: true }
                LiquidActionButton {
                    objectName: "newTopLevelFolderButton"
                    text: "＋ 新建文件夹"; materialForm: true; flat: false
                    implicitHeight: 20; cornerRadius: 6; leftPadding: 8; rightPadding: 8
                    // Keep this secondary heading action gray in both themes.
                    surfaceColor: AppTheme.withAlpha(AppTheme.text, AppTheme.dark ? 0.08 : 0.05)
                    textColor: enabled ? AppTheme.mutedText : AppTheme.disabledText
                    font.weight: Font.Normal; focusRingColor: "transparent"
                    enabled: !controller.busy
                    Accessible.name: "新建顶层文件夹"
                    onClicked: organizer.newFolder("", this)
                }
            }
            KeyboardList {
                id: sidebarFolderList
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                model: root.topLevelFolders; spacing: 2
                FolderListEndDropTarget {
                    parent: sidebarFolderList
                    y: Math.max(0, sidebarFolderList.originY + sidebarFolderList.contentHeight - sidebarFolderList.contentY + 2)
                    width: sidebarFolderList.width; height: Math.max(0, sidebarFolderList.height - y)
                }
                delegate: FolderEntry {
                    required property string modelData
                    required property int index
                    folderPath: modelData; highlightBranch: true; keyboardView: sidebarFolderList; rowIndex: index
                    showMenuButton: false
                    width: ListView.view.width; height: implicitHeight
                }
            }
            NavigationRow {
                objectName: "settingsButton"
                Layout.fillWidth: true
                iconName: "settings"; text: "设置"
                Accessible.name: "设置 · Ctrl+,"
                onClicked: notesSettings.show(0)
            }
        }
    }
    Rectangle {
        id: listColumn
        visible: root.showList
        x: root.showSidebar ? root.sidebarWidth : 0
        width: root.width < 800 ? root.width : root.browserWidth; height: parent.height
        color: AppTheme.contentBackground
        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: AppTheme.border; opacity: 0.5 }
        ColumnLayout {
            id: noteColumnLayout
            anchors.fill: parent; anchors.margins: 12; spacing: 12
            RowLayout {
                visible: !root.showSidebar && root.width < 800; Layout.fillWidth: true
                LiquidActionButton { text: "全部"; materialForm: true; onClicked: { root.browseCategory("all") } }
                LiquidActionButton { text: "收藏"; materialForm: true; onClicked: { root.browseCategory("favorite") } }
                LiquidActionButton { text: "最近删除"; materialForm: true; onClicked: root.showTrash() }
                LiquidActionButton { id: smallFolderButton;
                    DropArea {
                        id: compactDragEntry; anchors.fill: parent; keys: ["nextnotes-organization"]
                        Timer { interval: 500; running: compactDragEntry.containsDrag; onTriggered: { folderPanel.openFromButton(smallFolderButton) } }
                    }
                    text: "文件夹"; materialForm: true; onClicked: { folderPanel.openFromButton(smallFolderButton) } }
                LiquidActionButton { visible: root.width < 800 && (root.trashView ? recycleBin.selectionMode : organizer.selectionMode); text: "取消选择"; materialForm: true; onClicked: root.trashView ? recycleBin.clearSelection() : organizer.clearSelection() }
                ListNavigationActions { visible: root.width < 800 }
            }
            Item {
                id: childFolderSection
                visible: !!root.currentFolder && (root.childFolders.length > 0 || organizer.dragKind === "folder")
                Layout.fillWidth: true
                Layout.preferredHeight: root.childFoldersExpanded
                    ? Math.min(childFolderHeading.implicitHeight + 6 + childFolderList.contentHeight + 8, Math.max(100, noteColumnLayout.height * 0.35))
                    : childFolderHeading.implicitHeight
                ColumnLayout {
                    anchors.fill: parent; spacing: 6
                    RowLayout {
                        id: childFolderHeading
                        Layout.fillWidth: true; spacing: 4
                        Label {
                            Layout.fillWidth: true; leftPadding: 0
                            text: "子文件夹 · " + root.childFolders.length + (organizer.folderSelectionMode ? " · 已选 " + organizer.selectedFolderPaths.length : "")
                            color: AppTheme.mutedText; font.pixelSize: 11; font.weight: Font.Medium
                            DropArea {
                                id: collapsedFolderDrop
                                anchors.fill: parent; keys: ["nextnotes-organization"]
                                onEntered: function(event) { event.accepted = event.source === organizer }
                                Timer {
                                    interval: 650
                                    running: collapsedFolderDrop.containsDrag && !root.childFoldersExpanded
                                    onTriggered: root.childFoldersExpanded = true
                                }
                            }
                        }
                        ToolbarAction {
                            id: toggleChildFolders
                            implicitWidth: 20; implicitHeight: 20; iconSize: 12
                            materialForm: true; flat: true; iconName: "chevron"
                            iconColor: AppTheme.mutedText
                            contentItem.rotation: root.childFoldersExpanded ? 0 : -90
                            help: root.childFoldersExpanded ? "收起子文件夹" : "展开子文件夹"
                            onClicked: root.childFoldersExpanded = !root.childFoldersExpanded
                        }
                    }
                    Item {
                        visible: root.childFoldersExpanded
                        Layout.fillWidth: true; Layout.fillHeight: true
                        Rectangle {
                            anchors.fill: parent; radius: 10
                            color: AppTheme.dark ? "#303032" : "#f7f7f8"
                        }
                        FolderBrowser {
                            id: childFolderList
                            organization: organizer
                            viewMode: root.noteViewMode
                            visible: childFolderSection.visible && root.childFoldersExpanded
                            anchors.fill: parent
                            anchors.topMargin: 4; anchors.bottomMargin: 4
                            model: root.childFolders
                            Accessible.name: "子文件夹"
                            delegate: ItemDelegate {
                                id: childFolderRow
                                required property string modelData
                                required property int index
                                readonly property var details: controller.folderDetails[modelData] || ({})
                                readonly property string folderIcon: controller.folderIcons[modelData] || "folder"

                                readonly property string summary: (details.createdUtc
                                    ? new Date(details.createdUtc).toLocaleDateString(Qt.locale(), "yyyy/M/d") + " 创建"
                                    : "创建日期未记录") + " · " + (details.noteCount || 0) + " 篇笔记"
                                readonly property bool card: childFolderList.gallery
                                width: childFolderList.cellWidth - (card ? 12 : 0)
                                height: childFolderList.cellHeight - (card ? 12 : 0)
                                leftPadding: organizer.folderSelectionMode ? 36 : 12
                                rightPadding: 12; topPadding: 7; bottomPadding: 7
                                readonly property bool bulkSelected: organizer.selectedFolderPaths.includes(modelData)
                                focusPolicy: Qt.StrongFocus; activeFocusOnTab: false
                                function activate() {
                                    if (organizer.folderSelectionMode) organizer.toggleFolder(modelData)
                                    else root.browseCategory("folder:" + modelData)
                                }
                                function openContext(position) { organizer.openFolderMenu(modelData, childFolderRow, position || Qt.point(width / 2, height / 2)) }
                                Keys.onPressed: function(event) { childFolderList.handleKey(event) }
                                onActiveFocusChanged: if (activeFocus) { childFolderList.currentIndex = index; childFolderList.cursorKey = modelData }
                                background: Item {
                                    Rectangle {
                                        anchors.fill: parent; anchors.margins: 4
                                        radius: 7; border.width: childFolderRow.card && !childFolderRow.bulkSelected ? 1 : 0
                                        border.color: AppTheme.border
                                        color: childFolderRow.bulkSelected ? tokens.navigationSelected : (childFolderRow.card || childFolderRow.hovered) ? AppTheme.controlBackground : "transparent"
                                    }
                                }
                                contentItem: Column {
                                    spacing: 3
                                    RowLayout {
                                        width: parent.width; spacing: 6
                                        Item {
                                            Layout.preferredWidth: 16; Layout.preferredHeight: 16
                                            Layout.alignment: Qt.AlignVCenter
                                            VectorIcon { anchors.centerIn: parent; name: childFolderRow.folderIcon; size: 16; color: tokens.primaryYellow }

                                        }
                                        Label {
                                            Layout.fillWidth: true; Layout.minimumWidth: 0
                                            Layout.alignment: Qt.AlignVCenter
                                            text: childFolderRow.modelData.split("/").pop(); textFormat: Text.PlainText
                                            elide: Text.ElideRight; font.pixelSize: 14; font.weight: Font.DemiBold; color: AppTheme.text
                                        }
                                    }
                                    Label { width: parent.width; text: childFolderRow.summary; elide: Text.ElideRight; wrapMode: childFolderRow.card ? Text.Wrap : Text.NoWrap; maximumLineCount: childFolderRow.card ? 2 : 1; font.pixelSize: 12; color: AppTheme.mutedText }
                                }
                                OrganizePointer {
                                    folderPath: childFolderRow.modelData; enabled: !controller.busy
                                    dragEnabled: !organizer.folderSelectionMode
                                    onClickedItem: function(button, modifiers, position) {
                                        childFolderRow.forceActiveFocus(Qt.MouseFocusReason)
                                        if (button === Qt.RightButton) childFolderRow.openContext(position)
                                        else childFolderRow.activate()
                                    }
                                }
                                NotesSelectionBox {
                                    visible: organizer.folderSelectionMode; enabled: !controller.busy
                                    x: 6; y: 7; width: 24; height: 24; padding: 4
                                    checked: childFolderRow.bulkSelected; text: ""
                                    Accessible.name: "选择文件夹 " + childFolderRow.modelData.split("/").join(" › ")
                                    onClicked: organizer.toggleFolder(childFolderRow.modelData)
                                }
                                FolderDropTarget { folderPath: childFolderRow.modelData }
                                onClicked: activate()
                                Accessible.role: Accessible.ListItem
                                Accessible.selected: bulkSelected
                                Accessible.name: "文件夹 " + modelData.split("/").pop() + "，" + summary
                                Accessible.onPressAction: activate()
                            }
                            footer: Item {
                                width: childFolderList.width
                                height: organizer.dragKind === "folder" ? 12 : 0
                            }
                            FolderListEndDropTarget {
                                parent: childFolderList
                                folderPath: root.currentFolder
                                y: Math.max(0, childFolderList.originY + childFolderList.contentHeight - (childFolderList.footerItem ? childFolderList.footerItem.height : 0) - childFolderList.contentY)
                                width: childFolderList.width; height: Math.max(0, childFolderList.height - y)
                            }
                        }
                    }
                }
            }
            Label {
                visible: childFolderSection.visible
                Layout.fillWidth: true; leftPadding: 0
                text: "笔记 · " + noteList.count
                color: AppTheme.mutedText; font.pixelSize: 11; font.weight: Font.Medium
            }
            Label {
                visible: !root.trashView && !childFolderSection.visible
                Layout.fillWidth: true
                text: noteList.count + " 篇笔记"; color: AppTheme.mutedText; font.pixelSize: 11
            }
            NotesBrowser {
                id: noteList
                visible: !root.trashView
                Layout.fillWidth: true; Layout.fillHeight: true
                controller: root.controller; organization: organizer
                viewMode: root.gallery ? "gallery" : "list"
                noteDelegate: noteRowDelegate

                Label { x: 12; y: Math.max(0, (parent.height - implicitHeight) / 2); width: parent.width - 24; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; visible: noteList.count === 0; text: controller.query ? "没有找到相关笔记" : controller.filter === "favorite" ? "收藏的笔记会显示在这里" : controller.filter.startsWith("folder:") ? "这个文件夹还没有笔记" : "从第一篇笔记开始"; color: AppTheme.mutedText; font.pixelSize: 13 }
            }
        }
    }
    Component {
        id: noteRowDelegate
        ItemDelegate {
            id: noteRow
            readonly property bool card: GridView.view !== null
            required property var modelData
            required property int index
            readonly property string rowStatus: [modelData.favorite ? "收藏" : "", modelData.id === controller.selectedId && modelData.path === controller.path ? controller.status : modelData.readOnlyReason ? "只读" : ""].filter(Boolean).join(" · ")
            focusPolicy: Qt.StrongFocus
            activeFocusOnTab: false
            Keys.onPressed: function(event) { noteList.handleKey(event) }
            onActiveFocusChanged: if (activeFocus) { noteList.setCursor(index) }
            function activate() {
                if (organizer.noteSelectionMode) { organizer.toggle(modelData.path); return }
                organizer.clearSelection()
                organizer.selectionAnchor = modelData.path
                controller.openNote(modelData.path, modelData.match === undefined ? -1 : modelData.match)
            }
            readonly property bool bulkSelected: organizer.selectedPaths.indexOf(modelData.path) >= 0
            readonly property string groupName: root.dateGroup(modelData)
            readonly property bool startsGroup: !noteRow.card && !!groupName && (index === 0 || root.dateGroup(controller.notes[index - 1]) !== groupName)
            // 32px heading plus a 4px gap before the note background.
            readonly property int groupHeight: startsGroup ? 36 : 0
            readonly property int contentTopInset: startsGroup ? 8 : 12
            width: noteRow.card ? GridView.view.cellWidth - 12 : noteList.width
            height: noteRow.card ? GridView.view.cellHeight - 12 : tokens.noteRowHeight + groupHeight - (12 - contentTopInset)
            Label {
                x: 6; y: 4; visible: noteRow.startsGroup; text: noteRow.groupName
                font.pixelSize: 15; font.weight: Font.DemiBold; color: AppTheme.text
            }
            Rectangle {
                x: 6; y: noteRow.groupHeight - 5; width: parent.width - 12; height: 1
                visible: noteRow.startsGroup
                color: AppTheme.border; opacity: 0.6
            }
            // Show selection controls only during an explicit multi-selection.
            leftPadding: organizer.noteSelectionMode ? 36 : 12; topPadding: contentTopInset + groupHeight; bottomPadding: 12
            highlighted: organizer.noteSelectionMode ? bulkSelected : organizer.contextPath ? modelData.path === organizer.contextPath : (modelData.id === controller.selectedId && modelData.path === controller.path)
            background: Rectangle {
                y: noteRow.groupHeight; height: noteRow.height - y
                radius: 10; color: noteRow.highlighted ? tokens.notesYellow : (noteRow.card || noteRow.hovered) ? AppTheme.controlBackground : "transparent"
                border.width: noteRow.card && !noteRow.highlighted ? 1 : 0
                border.color: AppTheme.border
            }
            readonly property string thumbnail: ""
            Image {
                anchors.right: parent.right; anchors.rightMargin: 12
                y: noteRow.groupHeight + noteRow.contentTopInset - 2; width: 48; height: 48
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
                Label { width: parent.width; text: noteRow.card ? root.summaryText(noteRow.modelData.summary) : new Date(noteRow.modelData.displayModifiedUtc).toLocaleDateString(Qt.locale(), "M/d") + "  " + root.summaryText(noteRow.modelData.summary); textFormat: Text.PlainText; elide: Text.ElideRight; wrapMode: noteRow.card ? Text.Wrap : Text.NoWrap; maximumLineCount: noteRow.card ? 2 : 1; font.pixelSize: 12; color: noteRow.highlighted ? "white" : AppTheme.mutedText }
                Label { visible: noteRow.card; text: new Date(noteRow.modelData.displayModifiedUtc).toLocaleDateString(Qt.locale(), "yyyy/M/d"); font.pixelSize: 11; color: noteRow.highlighted ? "white" : AppTheme.mutedText }
            }
            OrganizePointer {
                notePath: noteRow.modelData.path; noteTitle: noteRow.modelData.title; enabled: !controller.busy
                onClickedItem: function(button, modifiers, position) {
                    noteRow.forceActiveFocus(Qt.MouseFocusReason)
                    if (button === Qt.RightButton) organizer.openMenu(noteRow, noteRow.modelData.path, position)
                    else if (!organizer.click(noteRow.modelData, noteRow.index, modifiers)) noteRow.activate()
                }
            }
            NotesSelectionBox {
                id: noteSelectionBox
                visible: organizer.noteSelectionMode
                x: 6; y: noteRow.groupHeight + noteRow.contentTopInset; width: 24; height: 24
                padding: 4
                checked: noteRow.bulkSelected; text: ""
                Accessible.name: "选择笔记 " + noteRow.modelData.title + "，" + root.noteLocation(noteRow.modelData.path)
                onClicked: organizer.toggle(noteRow.modelData.path)
            }
            onClicked: activate()
            Accessible.role: Accessible.ListItem
            Accessible.focusable: true
            Accessible.selected: organizer.noteSelectionMode ? bulkSelected : highlighted
            Accessible.onPressAction: activate()
            Accessible.name: modelData.title + "，" + root.noteLocation(modelData.path) + (rowStatus ? ", " + rowStatus : "")
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
        onPressed: function(mouse) { startWidth = root.gallery ? root.galleryWidth : root.listWidth; startX = mapToItem(root.contentItem, mouse.x, mouse.y).x }
        onPositionChanged: function(mouse) { if (pressed) {
            const next = startWidth + mapToItem(root.contentItem, mouse.x, mouse.y).x - startX
            const maximum = Math.max(root.browserMinimumWidth, root.width - (root.showSidebar ? root.sidebarWidth : 0) - root.editorMinimumWidth)
            if (root.gallery) {
                const limit = Math.min(800, maximum)
                root.galleryWidth = Math.max(Math.min(416, limit), Math.min(limit, next))
            } else root.listWidth = Math.max(240, Math.min(340, maximum, next))
        } }
    }
    Item {
        id: editorChrome
        visible: root.hasLibrary && !root.trashView && root.editorX < root.width
        x: root.editorX; width: root.width - x; height: parent.height
        NoteNameEditor {
            id: noteNameEditor
            visible: source.visible && !controller.externalDocument
            x: tokens.editorInset; y: 8; width: Math.max(0, parent.width - tokens.editorInset * 2)
            height: implicitHeight
            controller: root.controller; canRename: root.canEdit
            onSubmitted: root.editNote()
        }
        ColumnLayout {
            id: findBar; visible: false; x: tokens.editorInset; y: 8 + (controller.externalDocument ? 36 : noteNameEditor.height + 8); width: Math.max(0, parent.width - tokens.editorInset * 2); spacing: 4
            property bool replaceVisible: false
            readonly property bool hasMatches: !!source.editorState.search && source.editorState.search.total > 0
            Keys.onEscapePressed: function(event) {
                if (localFind.inputMethodComposing || replacement.inputMethodComposing) { event.accepted = false; return }
                root.closeFind()
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 4
                LiquidTextField { id: localFind; Layout.fillWidth: true; implicitHeight: 26; materialForm: true; placeholderText: "查找当前正文"; Accessible.name: "正文查找"; onTextEdited: if (!inputMethodComposing) root.updateSearch(); onInputMethodComposingChanged: if (!inputMethodComposing) root.updateSearch(); onAccepted: root.findNext() }
                Label { text: (source.editorState.search ? source.editorState.search.current : 0) + "/" + (source.editorState.search ? source.editorState.search.total : 0); font.pixelSize: 11; color: AppTheme.mutedText }
                LiquidActionButton { id: caseSensitive; text: "Aa"; checkable: true; materialForm: true; implicitWidth: 30; implicitHeight: 26; leftPadding: 0; rightPadding: 0; Accessible.name: "区分大小写"; onToggled: root.updateSearch(); KosToolTip { target: caseSensitive; text: "区分大小写" } }
                ToolbarAction { iconName: "chevron"; contentItem.rotation: 180; help: "上一个匹配"; materialForm: true; implicitHeight: 26; enabled: findBar.hasMatches; onClicked: source.action("findNext", {direction: -1}) }
                ToolbarAction { iconName: "chevron"; help: "下一个匹配"; materialForm: true; implicitHeight: 26; enabled: findBar.hasMatches; onClicked: root.findNext() }
                LiquidActionButton { text: "替换"; checkable: true; checked: findBar.replaceVisible; materialForm: true; implicitWidth: 48; implicitHeight: 26; leftPadding: 6; rightPadding: 6; enabled: root.canFormat; onClicked: findBar.replaceVisible = !findBar.replaceVisible }
                ToolbarAction { iconName: "plus"; contentItem.rotation: 45; help: "关闭正文查找 · Escape"; materialForm: true; implicitHeight: 26; onClicked: root.closeFind() }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 4; visible: findBar.replaceVisible && root.canFormat
                LiquidTextField { id: replacement; Layout.fillWidth: true; implicitHeight: 26; materialForm: true; placeholderText: "替换为"; Accessible.name: "正文替换文本"; onAccepted: if (root.canFormat && findBar.hasMatches && !inputMethodComposing) source.action("replace", {replacement: text}) }
                LiquidActionButton { text: "替换"; materialForm: true; implicitHeight: 26; enabled: root.canFormat && findBar.hasMatches && !replacement.inputMethodComposing; onClicked: source.action("replace", {replacement: replacement.text}) }
                LiquidActionButton { text: "全部替换"; materialForm: true; implicitHeight: 26; enabled: root.canFormat && findBar.hasMatches && !replacement.inputMethodComposing; onClicked: source.action("replaceAll", {replacement: replacement.text}) }
            }
        }
        RowLayout {
            visible: controller.externalDocument
            x: tokens.editorInset; y: 8; width: Math.max(0, parent.width - tokens.editorInset * 2)
            Label { Layout.fillWidth: true; text: "外部文件 · " + controller.path.slice(9); textFormat: Text.PlainText; elide: Text.ElideMiddle; color: AppTheme.mutedText; font.pixelSize: 12 }
            LiquidActionButton { text: "导入为笔记…"; materialForm: true; enabled: !controller.busy; onClicked: controller.importExternal() }
            LiquidActionButton { text: "结束临时编辑"; materialForm: true; enabled: !controller.busy; onClicked: controller.closeExternal() }
        }
        KosEmptyState { id: emptyEditor; anchors.centerIn: parent; width: Math.min(360, parent.width - 48); visible: !controller.path; symbol: ""; title: "一页新的可能"; description: "选择笔记，或写下此刻的想法。" }
        LiquidActionButton {
            anchors.top: emptyEditor.bottom; anchors.topMargin: 8; anchors.horizontalCenter: parent.horizontalCenter
            visible: emptyEditor.visible && controller.filter === "all" && !controller.query && !controller.notes.length
            text: "导入已有笔记…"; materialForm: true; implicitHeight: 26; enabled: !controller.busy
            onClicked: libraryActions.openImportMenu(this)
        }
    }
    ColumnLayout {
        id: statusArea
        x: root.hasLibrary && root.editorX < root.width ? root.editorX : 16; width: Math.max(0, root.width - x - 16)
        anchors.bottom: parent.bottom; spacing: 6
        visible: (!root.trashView && root.editorX < root.width) || !!controller.error
        Label { Layout.fillWidth: true; elide: Text.ElideRight; text: controller.status + (source.visible && session.formatState ? " · " + session.formatState : "") + (controller.path && source.editorState.stats && source.editorState.stats.characters !== undefined ? " · " + source.editorState.stats.characters + " 字 · " + (source.editorState.stats.approximate ? "约 " : "") + source.editorState.stats.words + " 词 · 约 " + source.editorState.stats.minutes + " 分钟" + (source.editorState.stats.selected ? " · 选中 " + source.editorState.stats.selected + " 字" : "") : ""); color: AppTheme.mutedText; font.pixelSize: 10; leftPadding: tokens.editorInset; bottomPadding: controller.error ? 0 : 12 }
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
        if (!root.popupsOpen && !localFind.activeFocus && !noteNameEditor.editing && source.visible)
            source.editor.forceActiveFocus(Qt.OtherFocusReason)
    }
    function updateSearch() { source.action("search", {query: localFind.text, caseSensitive: caseSensitive.checked}) }
    function openFind(withReplacement) {
        findBar.replaceVisible = withReplacement && canFormat
        findBar.visible = true
        updateSearch()
        if (findBar.replaceVisible) replacement.forceActiveFocus()
        else { localFind.forceActiveFocus(); localFind.selectAll() }
    }
    function closeFind() {
        findBar.visible = false
        source.action("search", {query: ""})
        source.editor.forceActiveFocus()
    }
    function findNext() { if (localFind.text) { updateSearch(); source.action("findNext", {direction: 1}) } }
    function showRecovery() { controller.inspectRecovery(); recoveryDialog.open() }
    function askName(kind, anchor, originMenu) {
        if (kind === "move") { Qt.callLater(noteNameEditor.edit); return }
        nameField.text = "笔记副本"
        if (originMenu) namePanel.openFromMenu(originMenu); else namePanel.openFromButton(anchor)
    }
    NotesPopover {
        id: paragraphMenu; anchorItem: paragraphButton; sourceItem: readingScene
        onClosed: Qt.callLater(root.restoreEditorFocus)
        Repeater {
            model: ["正文", "标题 1", "标题 2", "标题 3", "标题 4", "标题 5", "标题 6"]
            LiquidPopoverItem {
                required property string modelData
                required property int index
                text: modelData; font.bold: session.headingLevel === index
                enabled: root.canFormat
                onTriggered: root.applyFormat(index === 0 ? "paragraph" : "heading" + index)
            }
        }
    }
    NotesPopover {
        id: formatMenu; anchorItem: listButton; sourceItem: readingScene
        onClosed: Qt.callLater(root.restoreEditorFocus)
        LiquidPopoverItem { text: "项目列表"; onTriggered: root.applyFormat("bullet") }
        LiquidPopoverItem { text: "编号列表"; onTriggered: root.applyFormat("ordered") }
        LiquidPopoverItem { text: "任务清单"; onTriggered: root.applyFormat("task") }
        LiquidPopoverItem { text: "勾选 / 取消勾选"; onTriggered: root.applyFormat("check") }
        LiquidPopoverItem { text: "增加列表缩进"; enabled: root.canFormat; onTriggered: root.applyFormat("indent") }
        LiquidPopoverItem { text: "减少列表缩进"; enabled: root.canFormat; onTriggered: root.applyFormat("outdent") }
    }
    NotesPopover {
        id: insertMenu; anchorItem: insertButton; sourceItem: readingScene
        onClosed: Qt.callLater(root.restoreEditorFocus)
        LiquidPopoverItem { text: "图片或附件…"; enabled: root.canFormat && !controller.externalDocument; onTriggered: source.action("chooseAttachment") }
        LiquidPopoverItem { text: "数学公式…"; enabled: root.canFormat; onTriggered: source.openTool("math", insertMenu) }
        LiquidPopoverItem { text: "脚注"; enabled: root.canFormat; onTriggered: source.action("footnote") }
        LiquidPopoverItem { text: "自动目录"; enabled: root.canFormat; onTriggered: source.action("insertToc") }
        LiquidPopoverItem { text: "分隔线"; enabled: root.canFormat; onTriggered: root.applyFormat("rule") }
    }
    NotesPopover {
        id: tableMenu; anchorItem: tableButton; sourceItem: readingScene
        readonly property bool inTable: session.formatState.indexOf("表格") >= 0
        readonly property var tableInfo: source.editorState.table || ({row: -1, column: -1, rows: 0, columns: 0})
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
    NotesContentPopover {
        id: linkPanel; sourceItem: readingScene; preferredWidth: 300
        onOpened: linkTarget.forceActiveFocus()
        onClosed: Qt.callLater(root.restoreEditorFocus)
        function submit() { if (root.canFormat && !linkTarget.inputMethodComposing && session.link(linkTarget.text)) { linkTarget.clear(); close(); root.editNote() } }
        contentItem: ColumnLayout {
            Label { text: "添加链接"; color: AppTheme.text; font.weight: Font.DemiBold }
            LiquidTextField { id: linkTarget; Layout.fillWidth: true; materialForm: true; placeholderText: "https://…"; Accessible.name: "链接地址"; onAccepted: linkPanel.submit() }
            LiquidActionButton { text: "添加"; materialForm: true; implicitHeight: 24; enabled: root.canFormat && !!linkTarget.text && !linkTarget.inputMethodComposing; onClicked: linkPanel.submit() }
        }
    }
    NotesPopover {
        id: actionMenu; anchorItem: moreButton; sourceItem: readingScene
        onClosed: Qt.callLater(root.restoreEditorFocus)
        LiquidPopoverItem { text: "保存外部文件"; visible: controller.externalDocument; height: visible ? implicitHeight : 0; enabled: controller.dirty && !controller.busy; onTriggered: controller.save() }
        LiquidPopoverItem { text: "导入为笔记…"; visible: controller.externalDocument; height: visible ? implicitHeight : 0; enabled: !controller.busy; onTriggered: controller.importExternal() }
        LiquidPopoverItem { text: "修改笔记名称…"; enabled: !!controller.path && !controller.externalDocument && !controller.busy; onTriggered: root.askName("move", null, actionMenu) }
        LiquidPopoverItem { text: "笔记信息…"; enabled: !!controller.path && !controller.externalDocument && !controller.busy; onTriggered: knowledgePanel.open("properties") }
        LiquidPopoverItem { text: "分享与导出…"; enabled: !!controller.path && !controller.busy; onTriggered: outputMenu.openFromMenu(actionMenu) }
        LiquidPopoverItem { text: session.sourceMode ? "返回正文" : "查看 Markdown 源码"; enabled: !!controller.path && !controller.busy && !session.composing && !session.webLoading; onTriggered: session.execute("sourceMode") }
        MenuSeparator {}
        LiquidPopoverItem { text: "大纲 / 折叠"; enabled: !session.sourceMode && source.webReady; onTriggered: source.openTool("outline", actionMenu) }
        LiquidPopoverItem { text: "图片属性…"; enabled: !session.sourceMode && !!source.editorState.image; onTriggered: source.openTool("image", actionMenu) }
        LiquidPopoverItem { text: "代码语言与复制…"; enabled: !session.sourceMode && !!source.editorState.code; onTriggered: source.openTool("code", actionMenu) }
        LiquidPopoverItem { text: "附件管理…"; enabled: !session.sourceMode && !controller.externalDocument; onTriggered: source.openTool("attachments", actionMenu) }
    }
    NotesPopover {
        id: outputMenu; sourceItem: readingScene; anchorItem: moreButton
        readonly property bool canRender: !!controller.path && source.webReady && !session.sourceMode && !session.composing && !session.webLoading && !controller.busy
        LiquidPopoverItem { text: "导出为 PDF…"; enabled: outputMenu.canRender; onTriggered: source.chooseExport("pdf") }
        LiquidPopoverItem { text: "导出为 Markdown…"; enabled: !!controller.path && !controller.externalDocument && !controller.busy; onTriggered: libraryActions.exportSelection([controller.path], "") }
        LiquidPopoverItem { text: "导出为 HTML…"; enabled: outputMenu.canRender; onTriggered: source.chooseExport("html") }
        LiquidPopoverItem { text: "打印…"; enabled: outputMenu.canRender; onTriggered: source.printPreview() }
    }
    Shortcut { sequence: "Ctrl+O"; enabled: !root.popupsOpen && !controller.busy && !session.composing; onActivated: libraryActions.openExternalFile() }
    NotesContentPopover {
        id: namePanel; sourceItem: readingScene; preferredWidth: 330
        onOpened: nameField.forceActiveFocus()
        readonly property bool validName: !!nameField.text.trim() && !/[\r\n\\/]/.test(nameField.text) && ![".", "..", ".git", ".nextnotes"].includes(nameField.text.trim())
        contentItem: ColumnLayout {
            Label { text: "保存独立副本"; font.weight: Font.DemiBold; color: AppTheme.text }
            Label { Layout.fillWidth: true; text: "创建一篇独立笔记。"; wrapMode: Text.Wrap; color: AppTheme.mutedText; font.pixelSize: 12 }
            LiquidTextField { id: nameField; Layout.fillWidth: true; materialForm: true; Accessible.name: "副本名称"; placeholderText: Accessible.name; onAccepted: namePanel.submit() }
            LiquidActionButton { text: "确认"; materialForm: true; enabled: namePanel.validName && !nameField.inputMethodComposing && !controller.busy; onClicked: namePanel.submit() }
        }
        function submit() {
            if (!validName || nameField.inputMethodComposing) return
            const value = nameField.text.trim()
            const folder = controller.path.includes("/") ? controller.path.slice(0, controller.path.lastIndexOf("/") + 1) : ""
            controller.saveCopy((controller.externalDocument ? "" : folder) + value.replace(/\.md$/i, "") + ".md")
            close()
        }
    }
    NotesContentPopover {
        id: folderPanel; closePolicy: organizer.dragging ? Popup.CloseOnEscape : Popup.CloseOnEscape | Popup.CloseOnPressOutside; sourceItem: readingScene; preferredWidth: 300
        contentItem: ColumnLayout {
            Label { text: "文件夹"; color: AppTheme.text; font.weight: Font.DemiBold }
            KeyboardList {
                id: compactFolderList
                Layout.fillWidth: true; Layout.preferredHeight: Math.min(260, contentHeight + (organizer.dragKind === "folder" ? 32 : 0)); clip: true
                model: root.topLevelFolders
                FolderListEndDropTarget {
                    parent: compactFolderList
                    y: Math.max(0, compactFolderList.originY + compactFolderList.contentHeight - compactFolderList.contentY + 2)
                    width: compactFolderList.width; height: Math.max(0, compactFolderList.height - y)
                }
                delegate: NavigationRow {
                    iconName: controller.folderIcons[modelData] || "folder"

                    required property var modelData
                    required property int index
                    width: ListView.view.width; text: modelData.split("/").join(" › "); specificationForm: true
                    checked: !root.trashView && controller.filter === "folder:" + modelData
                    focusPolicy: Qt.StrongFocus
                    activeFocusOnTab: false
                    Keys.onPressed: function(event) { ListView.view.handleKey(event) }
                    onActiveFocusChanged: if (activeFocus) { ListView.view.currentIndex = index; ListView.view.cursorKey = ListView.view.keyAt(index) }
                    function activate() { root.browseCategory("folder:" + modelData); folderPanel.close() }
                    function openContext(position) {
                        const path = modelData
                        const point = position || Qt.point(width / 2, height / 2)
                        const mapped = mapToItem(Overlay.overlay, point.x, point.y)
                        folderPanel.close()
                        Qt.callLater(function() { organizer.openFolderMenu(path, Overlay.overlay, mapped) })
                    }
                    onClicked: activate()
                    Accessible.role: Accessible.ListItem
                    Accessible.focusable: true
                    Accessible.selected: checked
                    OrganizePointer { folderPath: parent.modelData; onClickedItem: function(button, modifiers, position) { if (button===Qt.RightButton) parent.openContext(position); else parent.activate() } }
                    FolderDropTarget { folderPath: parent.modelData }
                    Accessible.name: "文件夹 " + modelData.split("/").join(" › ")
                    Accessible.onPressAction: activate()
                }
            }
            LiquidActionButton { text: controller.filter.startsWith("folder:") ? controller.filter.slice(7).split("/").pop() + "的操作…" : "文件夹操作…"; materialForm: true; enabled: controller.filter.startsWith("folder:"); onClicked: { folderPanel.close(); organizer.openFolderMenu(controller.filter.slice(7), smallFolderButton) } }
            LiquidActionButton { text: "新建文件夹…"; materialForm: true; onClicked: { folderPanel.close(); Qt.callLater(function() { organizer.newFolder("", smallFolderButton) }) } }
        }
    }
    NotesSettings {
        id: notesSettings; hostWindow: root; controller: root.controller; sourceItem: readingScene
        onActionRequested: function(action) {
            switch(action) {
            case "importFiles": libraryActions.importFiles(); break
            case "importFolder": libraryActions.importFolder(); break
            case "importHistory": libraryActions.showImportHistory(); break
            case "importLegacy": libraryActions.importLegacy(); break
            case "exportAll": libraryActions.exportSelection([], ""); break
            case "backup": libraryActions.createBackup(); break
            case "restore": libraryActions.restoreBackup(); break
            case "templates": knowledgePanel.open("templates"); break
            case "smartCollection": smartDialog.open(); break
            case "renameTags": organizer.openTagRename(); break
            case "recovery": root.showRecovery(); break
            case "external": libraryActions.openExternalFile(); break
            case "sourceMode": session.execute("sourceMode"); break
            case "shortcuts": shortcutsDialog.open(); break
            }
        }
    }
    LiquidDialog {
        id: shortcutsDialog
        title: "键盘快捷键"; closeText: "完成"; standardButtons: Dialog.Close
        sourceItem: readingScene; anchors.centerIn: parent
        width: Math.min(620, root.width - 40); height: Math.min(580, root.height - 40)
        contentItem: ScrollView {
            id: shortcutScroll
            clip: true; contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: shortcutScroll.availableWidth; spacing: 18
                Repeater {
                    model: [
                        {title: "应用与笔记", rows: ["Ctrl+N　新建笔记", "Ctrl+S　保存", "Ctrl+O　临时编辑外部 Markdown", "Ctrl+Shift+I　导入 Markdown", "Ctrl+,　设置", "Ctrl+Shift+F　搜索全部笔记", "Ctrl+Shift+B　显示或隐藏导航", "Alt+← / →　前一篇 / 后一篇笔记"]},
                        {title: "列表与画廊", rows: ["方向键　移动游标，Enter / 空格打开", "画廊 ↑ / ↓　移动一行；← / → 移动一项", "Home / End　第一项 / 最后一项", "Ctrl+A　选择当前结果（最多 200 篇）", "Ctrl+空格 / Ctrl+点击　切换选中", "Shift+方向键 / Shift+点击　连续选择", "菜单键 / Shift+F10　当前笔记右键菜单", "Esc　取消多选或拖动"]},
                        {title: "正文编辑", rows: ["/　插入与转换内容块", "Ctrl+B / Ctrl+I　粗体 / 斜体", "Ctrl+Shift+X　删除线", "Ctrl+E　行内代码", "Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y　撤销 / 重做", "Ctrl+F / Ctrl+H　查找 / 替换", "Alt+Shift+↑ / ↓　移动块", "Enter / Shift+Enter　分段 / 段内换行", "Tab / Shift+Tab　列表缩进、表格导航", "Ctrl+Enter　退出表格", "Ctrl+点击链接　打开链接", "大纲左右键 / 右键　折叠与展开"]},
                        {title: "Markdown 源码", rows: ["Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y　撤销 / 重做", "Tab / Shift+Tab　增加 / 减少缩进", "使用工具栏「正文」或更多菜单返回正文"]}
                    ]
                    ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true; spacing: 6
                        Label { text: parent.modelData.title; font.weight: Font.DemiBold; color: AppTheme.text }
                        Repeater {
                            model: parent.modelData.rows
                            Label { required property string modelData; Layout.fillWidth: true; text: modelData; wrapMode: Text.Wrap; color: AppTheme.text; font.pixelSize: 12 }
                        }
                    }
                }
            }
        }
    }
    Shortcut { sequence: "Ctrl+,"; enabled: !root.popupsOpen; onActivated: notesSettings.show(0) }
    Shortcut { sequence: "Ctrl+Shift+I"; enabled: !root.popupsOpen && !controller.busy && !session.composing; onActivated: libraryActions.importFiles() }

    LibraryActions { id: libraryActions; anchors.fill: parent; controller: root.controller; sceneItem: readingScene; onBrowseRequested: root.revealList() }
    NotesOrganizer {
        id: organizer; anchors.fill: parent
        controller: root.controller; sceneItem: readingScene
        selectableFolders: root.childFolders
        viewMode: root.noteViewMode
        onViewModeRequested: mode => root.setNoteViewMode(mode)
        onFolderSelectionModeChanged: if (folderSelectionMode) root.childFoldersExpanded = true
        onSmartFolderRequested: smartDialog.open()
        onExportRequested: function(paths, folder) { libraryActions.exportSelection(paths, folder) }
    }
    LiquidDialog {
        acceptText: "放弃修改"; rejectText: "取消"; closeText: "关闭"
        id: discardDialog; title: "放弃当前本地输入？"; sourceItem: readingScene
        width: Math.min(440, root.width - 40); anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label { text: controller.externalDocument ? "重新读取原文件并放弃当前修改。需要保留时，请先创建副本。" : "重新载入已保存的内容，并放弃当前修改和恢复草稿。需要保留时，请先创建副本。"; wrapMode: Text.Wrap; color: AppTheme.text }
        onAccepted: controller.discardAndReload()
    }
    Connections { target: controller; function onMissingLinkRequested() { missingLinkDialog.open() } }
    LiquidDialog {
        acceptText: "创建并打开"; rejectText: "取消"; closeText: "关闭"
        acceptEnabled: !!controller.missingLink.targetPath && !controller.busy
        onAccepted: controller.createMissingLink()
        id: missingLinkDialog; title: "链接笔记尚不存在"; sourceItem: readingScene; anchors.centerIn: parent; width: Math.min(460, root.width - 40); standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: ColumnLayout {
            Label { text: "创建并打开：\n" + (controller.missingLink.targetPath || ""); textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.Wrap; color: AppTheme.text }
        }
    }
    LiquidDialog {
        acceptText: "保存"; rejectText: "取消"; closeText: "关闭"
        acceptEnabled: !!smartName.text.trim() && !smartName.inputMethodComposing && !controller.busy
        id: smartDialog; title: "保存为视图"; sourceItem: readingScene; anchors.centerIn: parent; width: Math.min(440, root.width - 40)
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: ColumnLayout {
            Label { text: "保存当前搜索和筛选条件，自动显示符合条件的笔记。支持 tag:标签、prop:属性=值、folder:目录、is:favorite。"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: AppTheme.text }
            LiquidTextField { id: smartName; Layout.fillWidth: true; materialForm: true; placeholderText: "视图名称" }
            LiquidActionButton { text: "删除同名视图"; materialForm: true; enabled: !!controller.smartFolders[smartName.text]; onClicked: { controller.deleteSmartFolder(smartName.text); smartDialog.close() } }
        }
        onAccepted: if (smartName.text.trim()) controller.saveSmartFolder(smartName.text)
    }
    LiquidDialog {
        acceptText: "确认"; rejectText: "取消"; closeText: "关闭"
        id: recoveryDialog; title: "恢复中心"; sourceItem: readingScene
        onVisibleChanged: controller.setRecoveryVisible(visible)
        width: Math.min(700, root.width - 40); height: Math.min(590, root.height - 60); anchors.centerIn: parent
        standardButtons: Dialog.Close
        contentItem: ScrollView {
            id: recoveryScroll
            clip: true; contentWidth: availableWidth
            ColumnLayout {
                width: recoveryScroll.availableWidth; spacing: 12
                Label { Layout.fillWidth: true; text: "先查看草稿，再选择恢复到笔记或保留为一篇新笔记。已有较新内容时，建议保留副本。"; wrapMode: Text.Wrap; color: AppTheme.mutedText; font.pixelSize: 12 }
                Label { text: "恢复草稿"; color: AppTheme.text; font.weight: Font.DemiBold }
                Repeater {
                    // Hidden TextAreas still lay out their full contents. Create
                    // recovery previews only while the recovery window is open.
                    model: recoveryDialog.visible ? (controller.recovery.drafts || []) : []
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Label { Layout.fillWidth: true; text: (modelData.title || "未命名笔记") + " · " + root.noteLocation(modelData.path) + (modelData.durable === false ? " · 尚未保存，请勿关闭" : "") + (modelData.matchesBase ? " · 可恢复" : " · 已有较新内容，建议保留副本"); wrapMode: Text.Wrap; color: AppTheme.text }
                        RowLayout {
                            LiquidActionButton { focusPolicy: Qt.StrongFocus; onActiveFocusChanged: root.revealRecoveryControl(this); text: "载入草稿"; Accessible.name: text + " " + (modelData.title || "未命名笔记") + "，" + root.noteLocation(modelData.path); materialForm: true; onClicked: { controller.recoverDraft(modelData.path, modelData.text, modelData.baseHash); recoveryDialog.close() } }
                            LiquidActionButton { focusPolicy: Qt.StrongFocus; onActiveFocusChanged: root.revealRecoveryControl(this); text: "将草稿保存为新笔记"; Accessible.name: text + " " + (modelData.title || "未命名笔记") + "，" + root.noteLocation(modelData.path); materialForm: true; onClicked: controller.copyRecovery(modelData.text) }
                            LiquidActionButton { focusPolicy: Qt.StrongFocus; onActiveFocusChanged: root.revealRecoveryControl(this); text: "丢弃…"; Accessible.name: "丢弃恢复草稿 " + (modelData.title || "未命名笔记") + "，" + root.noteLocation(modelData.path); materialForm: true; onClicked: { discardRecoveryDialog.draftPath = modelData.path; discardRecoveryDialog.open() } }
                        }
                        KosTextArea { focusPolicy: Qt.StrongFocus; onActiveFocusChanged: root.revealRecoveryControl(this); Accessible.name: "恢复草稿正文 " + (modelData.title || "未命名笔记"); Layout.fillWidth: true; text: modelData.text; readOnly: true; selectByMouse: true; wrapMode: Text.Wrap; implicitHeight: 80; color: AppTheme.text; background: null }
                    }
                }
                LiquidActionButton { text: "打开最近删除"; materialForm: true; onClicked: { recoveryDialog.close(); root.showTrash() } }
                Label { visible: (controller.recovery.commits || []).length > 0; text: "保留的提交版本"; color: AppTheme.text; font.weight: Font.DemiBold }
                Label { visible: (controller.recovery.commits || []).length > 0; Layout.fillWidth: true; text: "每条 intent 含目标路径、基线及新摘要，最后一行是保留文件的绝对路径。交换前它是新版本，交换后是被替换版本；需比较摘要后手动取回，不自动覆盖。"; wrapMode: Text.Wrap; color: AppTheme.mutedText; font.pixelSize: 12 }
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
                                KosTextArea { Layout.fillWidth: true; text: modelData.text || "版本缺失或校验未通过"; readOnly: true; selectByMouse: true; wrapMode: Text.Wrap; implicitHeight: 80; background: null; color: AppTheme.text; Accessible.name: "提交版本预览" }
                                LiquidActionButton { text: "另存为恢复副本"; materialForm: true; enabled: modelData.available; onClicked: controller.recoverCommit(commitRow.modelData.token, modelData.version) }
                            }
                        }
                    }
                }
            }
        }
    }
    LiquidDialog {
        acceptText: "丢弃"; rejectText: "取消"; closeText: "关闭"
        id: discardRecoveryDialog; property string draftPath: ""
        title: "丢弃恢复草稿？"; sourceItem: readingScene; width: Math.min(420, root.width - 40); anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label { text: "只删除恢复记录，不修改磁盘正文。若尚未另存副本，草稿内容将无法恢复。\n" + discardRecoveryDialog.draftPath; wrapMode: Text.Wrap; color: AppTheme.text }
        onAccepted: controller.discardRecovery(draftPath)
    }
    KosWindowResizeHandles { parent: root.contentItem.parent; targetWindow: root }
}
