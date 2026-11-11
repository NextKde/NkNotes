import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kos.Ui

Item {
    id: root
    required property var source
    required property var controller
    required property var session
    required property Item sceneItem
    property bool outlineVisible: controller.preference("editorOutline", false)
    readonly property real outlineWidth: outlineVisible && width > 600 ? 200 : 0
    readonly property var state: source.editorState
    readonly property bool editable: !source.readOnly && !session.composing
    readonly property bool popupActive: slashMenu.visible || blockMenu.visible || toolsMenu.visible || mathPanel.visible || codePanel.visible || imagePanel.visible || helpPanel.visible || attachmentsPanel.visible || outlinePanel.visible
    property var block: ({})
    property var spellSuggestions: []
    property int documentEpoch: -1
    property bool dragging: false
    property real dragX: 0
    property real dragY: 0
    property var pressedBlock: null
    property var pendingContext: null
    property int contextSequence: 0
    readonly property var handleBlock: pressedBlock || state.block
    readonly property bool dragInside: dragX >= 0 && dragX < width - outlineWidth && dragY >= 0 && dragY < height
    function cancelDrag() {
        if (handlePointer && handlePointer.pressed) handlePointer.gestureCanceled = true
        dragging = false; pressedBlock = null; post("cancelBlockMove")
    }
    function previewDrag() {
        if (dragInside) post("previewBlockMove", {pos: pressedBlock.pos, y: dragY})
        else post("cancelBlockMove")
    }
    function closeSlash() { slashMenu.suppressOnClose = false; slashMenu.close() }
    function post(name, args) { source.action(name, args || {}) }
    function toggleOutline(anchor, originMenu) {
        if (width > 600) { outlineVisible = !outlineVisible; controller.setPreference("editorOutline", outlineVisible) }
        else if (originMenu) outlinePanel.openFromMenu(originMenu)
        else outlinePanel.openFromButton(anchor)
    }
    function showShortcuts(anchor) { helpPanel.openFromButton(anchor) }
    function openMenu(anchor) { toolsMenu.openFromButton(anchor) }
    function openContext(x, y, suggestions) {
        pendingContext = {x: x, y: y, suggestions: suggestions || [], request: ++contextSequence}
        post("contextBlock", {x: x, y: y, request: contextSequence})
    }
    function openMath(originMenu) {
        const value = state.math || {}; mathPanel.targetPos = value.pos === undefined ? -1 : value.pos
        formula.text = value.source || ""; mathMode.currentIndex = value.display ? 1 : 0
        if (originMenu) mathPanel.openFromMenu(originMenu); else mathPanel.openFromButton(handle.visible ? handle : caretAnchor)
    }
    function openCode(originMenu) {
        const value = originMenu === blockMenu ? block : (state.code || {})
        codePanel.targetPos = value.pos === undefined ? 0 : value.pos
        const index = language.model.indexOf(value.language || "plain"); language.currentIndex = index < 0 ? 0 : index
        if (originMenu) codePanel.openFromMenu(originMenu); else codePanel.openFromButton(handle.visible ? handle : caretAnchor)
    }
    function openImage(originMenu) {
        const value = state.image; if (!value) { source.notify("先点击正文中的图片，再调整图片属性"); return }
        imagePanel.targetPos = value.pos; imagePanel.imageSource = value.src; imageAlt.text = value.alt || ""
        imageSize.value = ((controller.imageWidths()[value.src] || 640) - 64) / 1536
        if (originMenu) imagePanel.openFromMenu(originMenu); else imagePanel.openFromButton(handle.visible ? handle : caretAnchor)
    }
    function uiChanged() {
        if (state.epoch !== undefined && documentEpoch !== state.epoch) {
            documentEpoch = state.epoch
            closeSlash(); blockMenu.close(); toolsMenu.close(); mathPanel.close(); codePanel.close(); imagePanel.close(); helpPanel.close(); attachmentsPanel.close(); outlinePanel.close()
            dragging = false; pressedBlock = null; pendingContext = null
        }
        if (pendingContext && state.context && state.context.request === pendingContext.request) {
            const pending = pendingContext; pendingContext = null
            block = state.context.block || {}; spellSuggestions = pending.suggestions
            if (state.context.block) blockMenu.openAtPoint(root, Qt.point(pending.x, pending.y))
        }
        const slash = state.slash
        if (slash && slash.items.length && editable) {
            caretAnchor.x = Math.max(0, Math.min(width - outlineWidth - 30, slash.x)); caretAnchor.y = Math.max(0, Math.min(height - 22, slash.y))
            slashMenu.currentIndex = slash.index
            if (!slashMenu.visible) slashMenu.openForCaret(caretAnchor)
        } else if (slashMenu.visible) closeSlash()
    }
    onStateChanged: uiChanged()
    onEditableChanged: if (!editable) cancelDrag()
    onVisibleChanged: if (!visible) cancelDrag()
    Shortcut { sequence: "Escape"; enabled: root.dragging; onActivated: root.cancelDrag() }
    Item { id: caretAnchor; width: 8; height: 18; x: 32; y: 18 }
    LiquidActionButton {
        id: handle
        objectName: "editorBlockHandle"
        // Only a fresh paragraph's first editing session suppresses its handle.
        visible: (root.dragging || !!root.state.blockHandleVisible)
            && !!root.handleBlock && root.handleBlock.y >= 0 && root.handleBlock.y < root.height && root.editable
        x: Math.max(12, root.handleBlock ? root.handleBlock.x - width - 8 : 12); y: Math.max(0, root.handleBlock ? root.handleBlock.y : 0)
        implicitWidth: 26; implicitHeight: 26; iconOnly: true; iconName: "more"; iconSize: 16
        materialForm: true
        Accessible.name: "块操作，可拖动块，或使用 Alt+Shift+方向键移动"
        onClicked: { root.block = root.state.block; root.spellSuggestions = []; blockMenu.openFromButton(handle) }
        MouseArea {
            anchors.fill: parent
            id: handlePointer
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            property real startY: 0
            property int startPos: 0
            property bool gestureCanceled: false
            onPressed: function(mouse) {
                gestureCanceled = false
                root.pressedBlock = root.state.block
                startY = mapToItem(root, mouse.x, mouse.y).y; startPos = root.pressedBlock.pos; root.dragging = false
            }
            onPositionChanged: function(mouse) {
                if (!pressed || gestureCanceled) return
                const point = mapToItem(root, mouse.x, mouse.y)
                if (Math.abs(point.y - startY) > 6) root.dragging = true
                if (root.dragging) {
                    root.dragX = point.x; root.dragY = point.y
                    root.previewDrag()
                }
            }
            onReleased: function(mouse) {
                if (gestureCanceled) { root.cancelDrag(); return }
                if (root.dragging) {
                    const point = mapToItem(root, mouse.x, mouse.y)
                    root.dragX = point.x; root.dragY = point.y
                    if (root.dragInside) root.post("moveBlock", {pos: startPos, y: point.y})
                }
                else if (mouse.modifiers & Qt.ShiftModifier) root.post("selectBlock", {pos: startPos, extend: true})
                else if (containsMouse) { root.block = root.pressedBlock; root.spellSuggestions = []; blockMenu.openFromButton(handle) }
                root.cancelDrag()
            }
            onCanceled: root.cancelDrag()
        }
        KosToolTip { target: handle; text: "拖动移动块，点击打开块操作" }
    }
    Timer {
        interval: 40; repeat: true
        running: root.dragging && root.dragInside && (root.dragY < 35 || root.dragY > root.height - 35)
        onTriggered: { root.post("dragScroll", {delta: root.dragY < 35 ? -16 : 16}); root.previewDrag() }
    }
    Rectangle {
        objectName: "editorBlockDropIndicator"
        readonly property var target: root.state.dropTarget
        visible: root.dragging && root.dragInside && !!target && target.y >= 0 && target.y < root.height
        x: target ? target.x : 0; y: target ? target.y : 0
        width: target ? target.width : 0; height: 2; radius: 1; color: AppTheme.mutedText
    }
    Component {
        id: outlineView
        ColumnLayout {
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                Label { text: "大纲"; font.pixelSize: 12; font.bold: true; color: AppTheme.text; Layout.fillWidth: true }
                LiquidActionButton { text: "展开"; implicitHeight: 24; materialForm: true; onClicked: root.post("unfoldAll") }
            }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                model: root.state.outline || []
                delegate: LiquidNavigationButton {
                    required property var modelData
                    width: ListView.view.width; implicitHeight: 28; checkable: false
                    specificationForm: true; specificationFontSize: 11
                    text: (modelData.folded ? "▸ " : "▾ ") + modelData.title
                    leftPadding: 8 + (modelData.level - 1) * 9
                    Accessible.name: modelData.title + "，左右键折叠或展开"
                    onClicked: root.post("jump", {pos: modelData.pos})
                    Keys.onPressed: function(event) { if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) { root.post("toggleFold", {pos: modelData.pos}); event.accepted = true } }
                    TapHandler { acceptedButtons: Qt.RightButton; onTapped: root.post("toggleFold", {pos: parent.modelData.pos}) }
                }
                Label { anchors.centerIn: parent; visible: !parent.count; text: "添加标题后显示大纲"; font.pixelSize: 11; color: AppTheme.mutedText }
                ScrollBar.vertical: ScrollBar {}
            }
            Label { text: "左右键或右键折叠标题"; font.pixelSize: 10; color: AppTheme.mutedText }
        }
    }
    Item {
        anchors.right: parent.right; width: root.outlineWidth; height: parent.height; visible: width > 0
        Rectangle { anchors.fill: parent; color: AppTheme.contentBackground }
        Rectangle { width: 1; height: parent.height; color: AppTheme.border }
        Loader { anchors.fill: parent; anchors.margins: 10; sourceComponent: outlineView }
    }
    NotesContentPopover {
        id: outlinePanel; sourceItem: root.sceneItem; preferredWidth: 250; height: Math.min(430, root.height - 20)
        contentItem: Loader { sourceComponent: outlineView }
    }
    NotesPopover {
        id: slashMenu; sourceItem: root.sceneItem; anchorItem: caretAnchor; focus: false
        maxHeight: 320
        property bool suppressOnClose: true
        onOpened: suppressOnClose = true
        // Filtering, applying an entry and changing documents also close this menu.
        // Only an explicit dismissal should suppress the current slash query.
        onClosed: if (suppressOnClose && root.state.slash && root.state.slash.items.length && root.editable) root.post("dismissSlash")
        Instantiator {
            model: root.state.slash ? root.state.slash.items : []
            delegate: LiquidPopoverItem {
                required property var modelData
                required property int index
                text: modelData.label; highlighted: slashMenu.currentIndex === index
                onTriggered: root.post("slashChoose", {index: index})
            }
            onObjectAdded: function(index, object) { slashMenu.insertItem(index, object) }
            onObjectRemoved: function(index, object) { slashMenu.removeItem(object) }
        }
    }
    NotesPopover {
        id: blockMenu; sourceItem: root.sceneItem
        onClosed: Qt.callLater(function() { if (!root.popupActive) root.source.editor.forceActiveFocus() })
        LiquidPopoverItem { text: "选择块"; enabled: root.editable; onTriggered: root.post("selectBlock", {pos: root.block.pos}) }
        LiquidPopoverItem { text: "复制块"; enabled: root.editable; onTriggered: root.post("copyBlock", {pos: root.block.pos, selection: true}) }
        LiquidPopoverItem { text: "创建块副本"; enabled: root.editable; onTriggered: root.post("duplicateBlock", {pos: root.block.pos, selection: true}) }
        LiquidPopoverItem { text: "粘贴块"; enabled: root.editable && !!root.state.canPasteBlock; onTriggered: root.post("pasteBlock", {pos: root.block.pos}) }
        LiquidPopoverItem { text: "上移块    Alt+Shift+↑"; enabled: root.editable && !!root.block.canMoveUp; onTriggered: root.post("moveBlock", {pos: root.block.pos, direction: "up", selection: true}) }
        LiquidPopoverItem { text: "下移块    Alt+Shift+↓"; enabled: root.editable && !!root.block.canMoveDown; onTriggered: root.post("moveBlock", {pos: root.block.pos, direction: "down", selection: true}) }
        LiquidPopoverItem { text: "转换为正文"; enabled: root.editable && !!root.block.canConvert; onTriggered: root.post("blockType", {pos: root.block.pos, command: "paragraph"}) }
        LiquidPopoverItem { text: "转换为二级标题"; enabled: root.editable && !!root.block.canConvert; onTriggered: root.post("blockType", {pos: root.block.pos, command: "heading2"}) }
        LiquidPopoverItem { text: "折叠 / 展开标题"; enabled: root.block.type === "heading"; onTriggered: root.post("toggleFold", {pos: root.block.pos}) }
        LiquidPopoverItem { text: "删除块"; enabled: root.editable; onTriggered: root.post("deleteBlock", {pos: root.block.pos, selection: true}) }
        LiquidPopoverItem { text: "代码语言与复制…"; visible: root.block.type === "code_block"; onTriggered: root.openCode(blockMenu) }
        LiquidPopoverItem { text: "编辑数学公式…"; visible: !!root.state.math && root.state.math.pos >= root.block.pos && root.state.math.pos < root.block.end; enabled: root.editable; onTriggered: root.openMath(blockMenu) }
        LiquidPopoverItem { text: "图片属性…"; visible: !!root.state.image && root.state.image.pos >= root.block.pos && root.state.image.pos < root.block.end; onTriggered: root.openImage(blockMenu) }
        Instantiator {
            model: root.spellSuggestions.slice(0, 5)
            delegate: LiquidPopoverItem { required property string modelData; text: "更正为：" + modelData; onTriggered: root.source.correctSpelling(modelData) }
            onObjectAdded: function(index, object) { blockMenu.addItem(object) }
            onObjectRemoved: function(index, object) { blockMenu.removeItem(object) }
        }
    }
    NotesPopover {
        id: toolsMenu; sourceItem: root.sceneItem
        LiquidPopoverItem { text: "大纲 / 折叠"; onTriggered: root.toggleOutline(null, toolsMenu) }
        LiquidPopoverItem { text: "插入自动目录"; enabled: root.editable; onTriggered: root.post("insertToc") }
        LiquidPopoverItem { text: "插入图片或附件…"; enabled: root.editable && !root.controller.externalDocument; onTriggered: root.post("chooseAttachment") }
        LiquidPopoverItem { text: "图片属性…"; enabled: !!root.state.image; onTriggered: root.openImage(toolsMenu) }
        LiquidPopoverItem { text: "附件管理…"; enabled: !root.controller.externalDocument; onTriggered: { root.controller.inspectAttachments(); attachmentsPanel.openFromMenu(toolsMenu) } }
        LiquidPopoverItem { text: "数学公式…"; enabled: root.editable; onTriggered: root.openMath(toolsMenu) }
        LiquidPopoverItem { text: "插入脚注"; enabled: root.editable; onTriggered: root.post("footnote") }
        LiquidPopoverItem { text: "代码语言与复制…"; enabled: !!root.state.code; onTriggered: root.openCode(toolsMenu) }
    }
    NotesContentPopover {
        id: mathPanel; sourceItem: root.sceneItem; preferredWidth: 370
        onOpened: formula.forceActiveFocus()
        property int targetPos: -1
        function submit() { if (!formula.inputMethodComposing && formula.text.trim()) { root.post("setMath", {pos: targetPos, source: formula.text, display: mathMode.currentIndex === 1}); close(); source.editor.forceActiveFocus() } }
        contentItem: ColumnLayout {
            Label { text: "数学公式"; font.bold: true; color: AppTheme.text }
            LiquidSelect {
                    appearanceMode: AppTheme.appearanceMode; id: mathMode; model: ["行内公式", "独立公式"]; materialForm: true; Accessible.name: "公式显示方式"; onActivated: function(index) { mathMode.currentIndex = index } }
            LiquidTextField { id: formula; Layout.fillWidth: true; materialForm: true; placeholderText: "例如 \\frac{a}{b} 或 E=mc^2"; Accessible.name: "LaTeX 公式"; onAccepted: mathPanel.submit() }
            LiquidActionButton { text: "应用公式"; materialForm: true; enabled: root.editable && !!formula.text.trim() && !formula.inputMethodComposing; onClicked: mathPanel.submit() }
        }
    }
    NotesContentPopover {
        id: codePanel; sourceItem: root.sceneItem; preferredWidth: 300
        property int targetPos: 0
        contentItem: ColumnLayout {
            Label { text: "代码块"; font.bold: true; color: AppTheme.text }
            LiquidSelect {
                    appearanceMode: AppTheme.appearanceMode; id: language; Layout.fillWidth: true; materialForm: true; model: ["plain", "cpp", "python", "javascript", "typescript", "json", "bash", "qml", "html", "css", "sql", "markdown", "yaml", "mermaid"]; Accessible.name: "代码语言"; enabled: root.editable; onActivated: function(index) { language.currentIndex = index; root.post("codeLanguage", {pos: codePanel.targetPos, language: model[index] === "plain" ? "" : model[index]}) } }
            LiquidActionButton { text: "复制代码"; materialForm: true; onClicked: root.post("copyCode", {pos: codePanel.targetPos}) }
            Label { Layout.fillWidth: true; text: "选择 mermaid 显示图表预览；源码仍可直接编辑。"; wrapMode: Text.WordWrap; font.pixelSize: 11; color: AppTheme.mutedText }
        }
    }
    NotesContentPopover {
        id: imagePanel; sourceItem: root.sceneItem; preferredWidth: 340
        property int targetPos: 0
        property string imageSource: ""
        contentItem: ColumnLayout {
            Label { text: "图片属性"; font.bold: true; color: AppTheme.text }
            LiquidTextField { id: imageAlt; Layout.fillWidth: true; materialForm: true; placeholderText: "替代文本 / 图片说明"; Accessible.name: "图片替代文本" }
            Label { text: "显示宽度：" + Math.round(64 + imageSize.value * 1536) + " px"; color: AppTheme.text; font.pixelSize: 12 }
            LiquidSlider { id: imageSize
                activeFocusOnTab: true; Accessible.role: Accessible.Slider; Accessible.name: "图片显示宽度，" + Math.round(64 + value * 1536) + " 像素"
                function adjust(delta) { value = Math.max(0, Math.min(1, value + delta / 1536)); root.controller.setImageWidth(imagePanel.imageSource, Math.round(64 + value * 1536)) }
                Keys.onLeftPressed: adjust(-32); Keys.onRightPressed: adjust(32)
                Accessible.onIncreaseAction: adjust(32); Accessible.onDecreaseAction: adjust(-32)
                Layout.fillWidth: true; materialForm: true; Layout.preferredHeight: 28; onPreviewChanged: function(value) { imageSize.value = value }; onCommitRequested: function(value) { imageSize.value = value; root.controller.setImageWidth(imagePanel.imageSource, Math.round(64 + value * 1536)) } }
            Label { Layout.fillWidth: true; text: "宽度保存在应用视图设置中，保持 Markdown 图片语法可移植。"; wrapMode: Text.WordWrap; font.pixelSize: 11; color: AppTheme.mutedText }
            RowLayout {
                LiquidActionButton { text: "保存说明"; materialForm: true; enabled: root.editable; onClicked: { root.post("imageAlt", {pos: imagePanel.targetPos, alt: imageAlt.text}); imagePanel.close() } }
                LiquidActionButton { text: "原始宽度"; materialForm: true; onClicked: root.controller.setImageWidth(imagePanel.imageSource, 0) }
            }
        }
    }
    NotesContentPopover {
        id: attachmentsPanel; sourceItem: root.sceneItem; preferredWidth: 440
        contentItem: ColumnLayout {
            Label { text: "附件管理"; font.bold: true; color: AppTheme.text }
            LiquidSelect {
                    appearanceMode: AppTheme.appearanceMode; id: attachmentTab; materialForm: true; model: ["库内附件", "附件回收区"]; onActivated: function(index) { attachmentTab.currentIndex = index } }
            ListView {
                Layout.fillWidth: true; Layout.preferredHeight: Math.min(330, root.height - 140); clip: true
                model: (attachmentTab.currentIndex ? root.controller.attachments.removed : root.controller.attachments.items) || []
                delegate: RowLayout {
                    required property var modelData
                    width: ListView.view.width; height: 46
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 2
                        Label { Layout.fillWidth: true; text: modelData.name; textFormat: Text.PlainText; elide: Text.ElideMiddle; color: AppTheme.text; font.pixelSize: 12 }
                        Label { Layout.fillWidth: true; text: attachmentTab.currentIndex ? modelData.removedUtc : Math.ceil(modelData.bytes / 1024) + " KiB · " + modelData.references + " 篇笔记引用"; color: AppTheme.mutedText; font.pixelSize: 10; elide: Text.ElideRight }
                    }
                    LiquidActionButton { text: attachmentTab.currentIndex ? "恢复" : "回收"; materialForm: true; implicitHeight: 26; enabled: attachmentTab.currentIndex || modelData.references === 0; onClicked: { if (attachmentTab.currentIndex) root.controller.restoreAttachment(modelData.token); else root.controller.quarantineAttachment(modelData.relative) } }
                }
                Label { anchors.centerIn: parent; visible: !parent.count; text: "这里暂无附件"; color: AppTheme.mutedText; font.pixelSize: 12 }
                ScrollBar.vertical: ScrollBar {}
            }
            Label { Layout.fillWidth: true; text: "只回收未被笔记引用的附件，可在附件回收区恢复。"; wrapMode: Text.WordWrap; color: AppTheme.mutedText; font.pixelSize: 11 }
        }
    }
    NotesContentPopover {
        id: helpPanel; sourceItem: root.sceneItem; preferredWidth: 370
        contentItem: ColumnLayout {
            Label { text: "正文快捷键"; font.bold: true; color: AppTheme.text }
            Repeater {
                model: ["/  插入与转换内容块", "Ctrl+B / Ctrl+I  粗体 / 斜体", "Ctrl+Shift+X  删除线", "Ctrl+E  行内代码", "Ctrl+Z / Ctrl+Shift+Z  撤销 / 重做", "Ctrl+F / Ctrl+H  查找 / 替换", "Alt+Shift+↑ / ↓  移动块", "Enter / Shift+Enter  分段 / 段内换行", "Tab / Shift+Tab  列表缩进、表格导航", "Ctrl+Enter  退出表格", "Ctrl+点击链接  打开链接", "大纲左右键 / 右键  折叠与展开"]
                Label { required property string modelData; text: modelData; color: AppTheme.text; font.pixelSize: 12; topPadding: 2; bottomPadding: 2 }
            }
        }
    }
}
