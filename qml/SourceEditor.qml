import QtQuick
import QtQuick.Controls
import Kos.Ui

ScrollView {
    id: root
    required property var session
    property alias editor: input
    property int bodySize: 16
    property real readingWidth: availableWidth
    clip: true
    contentWidth: availableWidth
    function rememberSelection() {
        const anchor = input.cursorPosition === input.selectionStart ? input.selectionEnd : input.selectionStart
        session.select(anchor, input.cursorPosition)
    }
    TextArea {
        id: input
        objectName: "markdownSource"
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.Wrap
        selectByMouse: true
        persistentSelection: true
        color: AppTheme.text
        selectionColor: AppTheme.accent
        selectedTextColor: AppTheme.accentText
        font.pixelSize: root.bodySize
        font.family: "monospace"
        leftPadding: Math.max(28, (width - root.readingWidth) / 2)
        rightPadding: leftPadding
        topPadding: 18; bottomPadding: 60
        background: null
        Accessible.name: "Markdown 源码编辑器"
        onCursorPositionChanged: root.rememberSelection()
        onSelectionStartChanged: root.rememberSelection()
        onSelectionEndChanged: root.rememberSelection()
        onInputMethodComposingChanged: root.session.composing = inputMethodComposing
        Component.onCompleted: {
            root.session.attach(textDocument)
            root.session.loadPrototype("# 留下一个想法\n\n从一个句子开始，慢慢连接成自己的知识。\n\n## 今天的灵感\n\n选中这段文字，尝试加粗或斜体。工具栏与快捷键使用同一份 Markdown。\n\n- 让想法先留下来\n- 为值得回看的内容留一个位置\n\n> 这是编辑器原型，内容仅保留在本次会话中。\n")
            forceActiveFocus()
        }
    }
    Connections {
        target: root.session
        function onSelectionRequested(anchor, position) { input.select(anchor, position) }
    }
}
