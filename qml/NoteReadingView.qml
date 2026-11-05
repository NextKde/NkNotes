import QtQuick
import QtQuick.Controls
import Kos.Ui

// Presentation only: the original Markdown remains owned by EditorSession.
ScrollView {
    id: root
    required property string source
    required property string noteTitle
    property string fontFamily: "sans-serif"
    property int titleSize: 26
    property string modified: ""
    required property int bodySize
    required property real readingWidth
    required property int inset
    clip: true
    contentWidth: availableWidth
    readonly property string body: {
        let markdown = source.replace(/^\uFEFF/, "")
        // Hide only a complete leading front matter block in the reading surface.
        markdown = markdown.replace(/^---\r?\n[\s\S]*?\r?\n(?:---|\.\.\.)[ \t]*(?:\r?\n|$)/, "")
        markdown = markdown.replace(/^\s*# [^\r\n]*(?:\r?\n|$)/, "")
        // Do not request remote images from a presentation-only reader.
        markdown = markdown.replace(/!\[([^\]]*)\]\([^)]*\)/g, "[$1]")
        return markdown.trim()
    }
    onSourceChanged: contentItem.contentY = 0
    Column {
        width: root.availableWidth
        spacing: 22
        Item { width: 1; height: 4 }
        Column {
            x: Math.max(root.inset, (parent.width - root.readingWidth) / 2)
            width: Math.max(0, parent.width - x * 2)
            spacing: 12
            Label { width: parent.width; text: root.noteTitle; wrapMode: Text.Wrap; color: AppTheme.text; font.family: root.fontFamily; font.pixelSize: root.titleSize; font.weight: Font.Bold; lineHeight: 1.2 }
            Label { width: parent.width; visible: !!root.modified; text: root.modified ? new Date(root.modified).toLocaleDateString(Qt.locale(), "yyyy年M月d日") : ""; color: AppTheme.mutedText; font.pixelSize: 11 }
        }
        Text {
            x: Math.max(root.inset, (parent.width - root.readingWidth) / 2)
            width: Math.max(0, parent.width - x * 2)
            text: root.body || "这篇笔记还没有正文。"
            textFormat: Text.MarkdownText
            wrapMode: Text.Wrap
            color: AppTheme.text
            linkColor: AppTheme.accent
            font.family: root.fontFamily
            font.pixelSize: root.bodySize
            lineHeight: 1.55
            Accessible.role: Accessible.StaticText
            Accessible.name: "只读阅读正文"
        }
        Item { width: 1; height: 60 }
    }
}
