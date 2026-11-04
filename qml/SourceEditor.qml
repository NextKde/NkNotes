import QtQuick
import QtQuick.Controls
import Kos.Ui

ScrollView {
    id: root
    required property var session
    property alias editor: input
    required property var controller
    property bool restoring: false
    function rememberPosition() { if (!restoring) controller.rememberPosition(input.cursorPosition, contentItem.contentY) }
    required property int bodySize
    required property real readingWidth
    required property int inset
    clip: true
    contentWidth: availableWidth
    function rememberSelection() {
        const anchor = input.cursorPosition === input.selectionStart ? input.selectionEnd : input.selectionStart
        session.select(anchor, input.cursorPosition)
    }
    TextArea {
        id: input
        objectName: "markdownSource"
        readOnly: root.controller.readOnly
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.Wrap
        selectByMouse: true
        persistentSelection: true
        color: AppTheme.text
        selectionColor: AppTheme.accent
        selectedTextColor: AppTheme.accentText
        font.pixelSize: root.bodySize
        font.family: "monospace"
        leftPadding: Math.max(root.inset, (width - root.readingWidth) / 2)
        rightPadding: leftPadding
        topPadding: 18; bottomPadding: 60
        background: null
        Accessible.name: "Markdown 源码编辑器"
        onCursorPositionChanged: { root.rememberSelection(); root.rememberPosition() }
        onSelectionStartChanged: root.rememberSelection()
        onSelectionEndChanged: root.rememberSelection()
        onInputMethodComposingChanged: root.session.composing = inputMethodComposing
        Component.onCompleted: {
            root.session.attach(textDocument)
            Qt.callLater(root.controller.restoreWorkspace)

        }
    }
    Connections {
        target: root.contentItem
        function onContentYChanged() { root.rememberPosition() }
    }
    Connections {
        target: root.controller
        function onDocumentLoaded(cursor, scroll, matchLength) {
            root.restoring = true
            Qt.callLater(function() {
                input.cursorPosition = Math.min(input.length, cursor)
                if (matchLength > 0) input.select(cursor, Math.min(input.length, cursor + matchLength))
                if (scroll >= 0) root.contentItem.contentY = Math.max(0, Math.min(scroll, root.contentItem.contentHeight - root.availableHeight))
                else { const r = input.positionToRectangle(cursor); root.contentItem.contentY = Math.max(0, r.y - 80) }
                input.forceActiveFocus()
                root.restoring = false
            })
        }
    }
    Connections {
        target: root.session
        function onSelectionRequested(anchor, position) { input.select(anchor, position) }
    }
}
