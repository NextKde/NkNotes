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
    property real dragY: 0
    function closeSlash() { slashMenu.close() }
    function post(name, args) { source.action(name, args || {}) }
    function toggleOutline(anchor) {
        if (width > 600) { outlineVisible = !outlineVisible; controller.setPreference("editorOutline", outlineVisible) }
        else { outlinePanel.anchorItem = anchor; outlinePanel.open() }
    }
    function openMenu(anchor) { toolsMenu.anchorItem = anchor; toolsMenu.openAtAnchor() }
    function openContext(x, y, suggestions) {
        caretAnchor.x = Math.max(0, Math.min(width - 30, x)); caretAnchor.y = Math.max(0, Math.min(height - 24, y))
        block = state.block || {}; spellSuggestions = suggestions || []; blockMenu.anchorItem = caretAnchor; blockMenu.openAtAnchor()
    }
    function openMath() {
        const value = state.math || {}; mathPanel.targetPos = value.pos === undefined ? -1 : value.pos
        formula.text = value.source || ""; mathMode.currentIndex = value.display ? 1 : 0
        mathPanel.anchorItem = handle.visible ? handle : caretAnchor; mathPanel.open(); formula.forceActiveFocus()
    }
    function openCode() {
        const value = state.code || {}; codePanel.targetPos = value.pos === undefined ? (state.block ? state.block.pos : 0) : value.pos
        const index = language.model.indexOf(value.language || "plain"); language.currentIndex = index < 0 ? 0 : index
        codePanel.anchorItem = handle.visible ? handle : caretAnchor; codePanel.open()
    }
    function openImage() {
        const value = state.image; if (!value) { source.notify("先点击正文中的图片，再调整图片属性"); return }
        imagePanel.targetPos = value.pos; imagePanel.imageSource = value.src; imageAlt.text = value.alt || ""
        imageSize.value = ((controller.imageWidths()[value.src] || 640) - 64) / 1536
        imagePanel.anchorItem = handle.visible ? handle : caretAnchor; imagePanel.open()
    }
    function uiChanged() {
        if (state.epoch !== undefined && documentEpoch !== state.epoch) {
            documentEpoch = state.epoch
            slashMenu.close(); blockMenu.close(); toolsMenu.close(); mathPanel.close(); codePanel.close(); imagePanel.close(); helpPanel.close(); attachmentsPanel.close(); outlinePanel.close()
            dragging = false
        }
        const slash = state.slash
        if (slash && slash.items.length && editable) {
            caretAnchor.x = Math.max(0, Math.min(width - outlineWidth - 30, slash.x)); caretAnchor.y = Math.max(0, Math.min(height - 22, slash.y))
            slashMenu.currentIndex = slash.index
            if (!slashMenu.visible) slashMenu.openAtAnchor()
        } else if (slashMenu.visible) slashMenu.close()
    }
    onStateChanged: uiChanged()
    Item { id: caretAnchor; width: 8; height: 18; x: 32; y: 18 }
    LiquidActionButton {
        id: handle
        visible: !!root.state.block && root.state.block.y >= 0 && root.state.block.y < root.height && root.editable
        x: Math.max(0, root.state.block ? root.state.block.x - 28 : 0); y: Math.max(0, root.state.block ? root.state.block.y : 0)
        implicitWidth: 26; implicitHeight: 26; iconOnly: true; iconName: "more"; iconSize: 16
        materialForm: true
        Accessible.name: "块操作，可拖动块，或使用 Alt+Shift+方向键移动"
        onClicked: { root.block = root.state.block; blockMenu.anchorItem = handle; blockMenu.openAtAnchor() }
        MouseArea {
            anchors.fill: parent
            property real startY: 0
            property int startPos: 0
            onPressed: function(mouse) { startY = mapToItem(root, mouse.x, mouse.y).y; startPos = root.state.block.pos; root.dragging = false }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                const point = mapToItem(root, mouse.x, mouse.y)
                if (Math.abs(point.y - startY) > 6) root.dragging = true
                if (root.dragging) {
                    root.dragY = point.y
                    if (point.y < 35) root.post("dragScroll", {delta: -20})
                    else if (point.y > root.height - 35) root.post("dragScroll", {delta: 20})
                }
            }
            onReleased: function(mouse) {
                if (root.dragging) root.post("moveBlock", {pos: startPos, y: root.dragY})
                else if (mouse.modifiers & Qt.ShiftModifier) root.post("selectBlock", {pos: startPos, extend: true})
                else { root.block = root.state.block; blockMenu.anchorItem = handle; blockMenu.openAtAnchor() }
                root.dragging = false
            }
            onCanceled: root.dragging = false
        }
        KosToolTip { target: handle; text: "拖动移动块，点击打开块操作" }
    }
    Rectangle { visible: root.dragging; x: 28; y: root.dragY; width: Math.max(0, root.width - root.outlineWidth - 52); height: 2; color: AppTheme.border }
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
    LiquidContentPopover {
        id: outlinePanel; sourceItem: root.sceneItem; width: 250; height: Math.min(430, root.height - 20); preferredSide: "bottom"
        contentItem: Loader { sourceComponent: outlineView }
    }
    LiquidPopover {
        id: slashMenu; height: Math.min(implicitHeight, Math.max(100, root.height - 16)); sourceItem: root.sceneItem; anchorItem: caretAnchor; focus: false
        onClosed: root.post("dismissSlash")
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
    LiquidPopover {
        id: blockMenu; height: Math.min(implicitHeight, Math.max(100, root.height - 16)); sourceItem: root.sceneItem
        onClosed: Qt.callLater(function() { if (!root.popupActive) root.source.editor.forceActiveFocus() })
        LiquidPopoverItem { text: "选择块"; enabled: root.editable; onTriggered: root.post("selectBlock", {pos: root.block.pos}) }
        LiquidPopoverItem { text: "复制块"; enabled: root.editable; onTriggered: root.post("copyBlock", {pos: root.block.pos, selection: true}) }
        LiquidPopoverItem { text: "创建块副本"; enabled: root.editable; onTriggered: root.post("duplicateBlock", {pos: root.block.pos, selection: true}) }
        LiquidPopoverItem { text: "粘贴块"; enabled: root.editable; onTriggered: root.post("pasteBlock", {pos: root.block.pos}) }
        LiquidPopoverItem { text: "上移块    Alt+Shift+↑"; enabled: root.editable; onTriggered: root.post("moveBlock", {pos: root.block.pos, direction: "up", selection: true}) }
        LiquidPopoverItem { text: "下移块    Alt+Shift+↓"; enabled: root.editable; onTriggered: root.post("moveBlock", {pos: root.block.pos, direction: "down", selection: true}) }
        LiquidPopoverItem { text: "转换为正文"; enabled: root.editable; onTriggered: root.post("blockType", {pos: root.block.pos, command: "paragraph"}) }
        LiquidPopoverItem { text: "转换为二级标题"; enabled: root.editable; onTriggered: root.post("blockType", {pos: root.block.pos, command: "heading2"}) }
        LiquidPopoverItem { text: "折叠 / 展开标题"; enabled: root.block.type === "heading"; onTriggered: root.post("toggleFold", {pos: root.block.pos}) }
        LiquidPopoverItem { text: "删除块"; enabled: root.editable; onTriggered: root.post("deleteBlock", {pos: root.block.pos, selection: true}) }
        LiquidPopoverItem { text: "代码语言与复制…"; visible: !!root.state.code; onTriggered: root.openCode() }
        LiquidPopoverItem { text: "编辑数学公式…"; visible: !!root.state.math; enabled: root.editable; onTriggered: root.openMath() }
        LiquidPopoverItem { text: "图片属性…"; visible: !!root.state.image; onTriggered: root.openImage() }
        Instantiator {
            model: root.spellSuggestions.slice(0, 5)
            delegate: LiquidPopoverItem { required property string modelData; text: "更正为：" + modelData; onTriggered: root.source.correctSpelling(modelData) }
            onObjectAdded: function(index, object) { blockMenu.addItem(object) }
            onObjectRemoved: function(index, object) { blockMenu.removeItem(object) }
        }
    }
    LiquidPopover {
        id: toolsMenu; height: Math.min(implicitHeight, Math.max(100, root.height - 16)); sourceItem: root.sceneItem
        LiquidPopoverItem { text: "大纲 / 折叠"; onTriggered: root.toggleOutline(toolsMenu.anchorItem) }
        LiquidPopoverItem { text: "插入自动目录"; enabled: root.editable; onTriggered: root.post("insertToc") }
        LiquidPopoverItem { text: "插入图片或附件…"; enabled: root.editable; onTriggered: root.post("chooseAttachment") }
        LiquidPopoverItem { text: "图片属性…"; enabled: !!root.state.image; onTriggered: root.openImage() }
        LiquidPopoverItem { text: "附件管理…"; onTriggered: { root.controller.inspectAttachments(); attachmentsPanel.anchorItem = toolsMenu.anchorItem; attachmentsPanel.open() } }
        LiquidPopoverItem { text: "数学公式…"; enabled: root.editable; onTriggered: root.openMath() }
        LiquidPopoverItem { text: "插入脚注"; enabled: root.editable; onTriggered: root.post("footnote") }
        LiquidPopoverItem { text: "代码语言与复制…"; enabled: !!root.state.code; onTriggered: root.openCode() }
        LiquidPopoverItem { text: "导出 PDF…"; enabled: !root.source.exporting && !root.session.composing; onTriggered: root.source.chooseExport("pdf") }
        LiquidPopoverItem { text: "导出独立 HTML…"; enabled: !root.source.exporting && !root.session.composing; onTriggered: root.source.chooseExport("html") }
        LiquidPopoverItem { text: "打印预览（PDF）…"; enabled: !root.source.exporting && !root.session.composing; onTriggered: root.source.printPreview() }
        LiquidPopoverItem { text: "键盘快捷键…"; onTriggered: { helpPanel.anchorItem = toolsMenu.anchorItem; helpPanel.open() } }
    }
    LiquidContentPopover {
        id: mathPanel; sourceItem: root.sceneItem; preferredSide: "bottom"; width: 370
        property int targetPos: -1
        function submit() { if (!formula.inputMethodComposing && formula.text.trim()) { root.post("setMath", {pos: targetPos, source: formula.text, display: mathMode.currentIndex === 1}); close(); source.editor.forceActiveFocus() } }
        contentItem: ColumnLayout {
            Label { text: "数学公式"; font.bold: true; color: AppTheme.text }
            LiquidSelect { id: mathMode; model: ["行内公式", "独立公式"]; materialForm: true; Accessible.name: "公式显示方式"; onActivated: function(index) { mathMode.currentIndex = index } }
            LiquidTextField { id: formula; Layout.fillWidth: true; materialForm: true; placeholderText: "例如 \\frac{a}{b} 或 E=mc^2"; Accessible.name: "LaTeX 公式"; onAccepted: mathPanel.submit() }
            LiquidActionButton { text: "应用公式"; materialForm: true; enabled: root.editable && !!formula.text.trim() && !formula.inputMethodComposing; onClicked: mathPanel.submit() }
        }
    }
    LiquidContentPopover {
        id: codePanel; sourceItem: root.sceneItem; preferredSide: "bottom"; width: 300
        property int targetPos: 0
        contentItem: ColumnLayout {
            Label { text: "代码块"; font.bold: true; color: AppTheme.text }
            LiquidSelect { id: language; Layout.fillWidth: true; materialForm: true; model: ["plain", "cpp", "python", "javascript", "typescript", "json", "bash", "qml", "html", "css", "sql", "markdown", "yaml", "mermaid"]; Accessible.name: "代码语言"; enabled: root.editable; onActivated: function(index) { language.currentIndex = index; root.post("codeLanguage", {pos: codePanel.targetPos, language: model[index] === "plain" ? "" : model[index]}) } }
            LiquidActionButton { text: "复制代码"; materialForm: true; onClicked: root.post("copyCode", {pos: codePanel.targetPos}) }
            Label { Layout.fillWidth: true; text: "选择 mermaid 显示图表预览；源码仍可直接编辑。"; wrapMode: Text.WordWrap; font.pixelSize: 11; color: AppTheme.mutedText }
        }
    }
    LiquidContentPopover {
        id: imagePanel; sourceItem: root.sceneItem; preferredSide: "bottom"; width: 340
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
    LiquidContentPopover {
        id: attachmentsPanel; sourceItem: root.sceneItem; preferredSide: "bottom"; width: 440
        contentItem: ColumnLayout {
            Label { text: "附件管理"; font.bold: true; color: AppTheme.text }
            LiquidSelect { id: attachmentTab; materialForm: true; model: ["库内附件", "附件回收区"]; onActivated: function(index) { attachmentTab.currentIndex = index } }
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
            Label { Layout.fillWidth: true; text: "只回收磁盘上无引用的文件，可在附件回收区恢复。"; wrapMode: Text.WordWrap; color: AppTheme.mutedText; font.pixelSize: 11 }
        }
    }
    LiquidContentPopover {
        id: helpPanel; sourceItem: root.sceneItem; preferredSide: "bottom"; width: 370
        contentItem: ColumnLayout {
            Label { text: "正文快捷键"; font.bold: true; color: AppTheme.text }
            Repeater {
                model: ["/  插入与转换内容块", "Ctrl+B / Ctrl+I  粗体 / 斜体", "Ctrl+Shift+X  删除线", "Ctrl+E  行内代码", "Ctrl+Z / Ctrl+Shift+Z  撤销 / 重做", "Ctrl+F / Ctrl+H  查找 / 替换", "Alt+Shift+↑ / ↓  移动块", "Enter / Shift+Enter  分段 / 段内换行", "Tab / Shift+Tab  列表缩进、表格导航", "Ctrl+Enter  退出表格", "Ctrl+点击链接  打开链接", "大纲左右键 / 右键  折叠与展开"]
                Label { required property string modelData; text: modelData; color: AppTheme.text; font.pixelSize: 12; topPadding: 2; bottomPadding: 2 }
            }
        }
    }
}
