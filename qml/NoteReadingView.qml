import QtQuick
import QtQuick.Controls
import Kos.Ui

// Presentation only: source and persistence stay owned by EditorSession.
ScrollView {
    id: root
    required property string source
    required property string noteTitle
    required property string documentPath
    // The controller applies the same manifest and containment policy as export
    // and the editor. Without a resolver, image resources remain disabled.
    property var assetResolver: null
    property string fontFamily: "sans-serif"
    property int titleSize: 24
    property string modified: ""
    required property int bodySize
    required property real readingWidth
    required property int inset
    clip: true
    contentWidth: availableWidth
    function localImage(value) {
        const path = value.trim().replace(/^<|>$/g, "")
        if (!path || !documentPath || typeof assetResolver !== "function") return ""
        try {
            const resolved = assetResolver(path)
            return resolved ? resolved.toString() : ""
        } catch (e) { return "" }
    }
    readonly property var blocks: {
        let markdown = source.replace(/^\uFEFF/, "")
        markdown = markdown.replace(/^---\r?\n[\s\S]*?\r?\n(?:---|\.\.\.)[ \t]*(?:\r?\n|$)/, "")
        markdown = markdown.replace(/^\s*# [^\r\n]*(?:\r?\n|$)/, "")
        // Disable raw HTML resource loading. Inline/reference Markdown images are
        // resolved explicitly below; no Markdown image syntax reaches the Text item.
        markdown = markdown.replace(/<[^>]*>/g, "")
        const definitions = {}
        markdown = markdown.replace(/^\s{0,3}\[([^\]]+)\]:\s*(\S+).*$/gm, function(all, key, value) { definitions[key.toLowerCase()] = value; return "" })
        markdown = markdown.replace(/!\[([^\]]*)\]\[([^\]]*)\]/g, function(all, alt, key) { return "![" + alt + "](" + (definitions[(key || alt).toLowerCase()] || "") + ")" })
        markdown = markdown.replace(/!\[([^\]]+)\](?!\()/g, function(all, alt) { return "![" + alt + "](" + (definitions[alt.toLowerCase()] || "") + ")" })
        const result = [], pattern = /!\[([^\]]*)\]\(([^\r\n]*?)\)/g
        let match, offset = 0
        while ((match = pattern.exec(markdown)) !== null) {
            const before = markdown.slice(offset, match.index).trim()
            if (before) result.push({kind: "text", value: before})
            const target = match[2].replace(/\s+["'][\s\S]*["']\s*$/, "")
            const url = localImage(target)
            result.push(url ? {kind: "image", value: url, alt: match[1]} : {kind: "text", value: match[1] ? "图片：" + match[1] + "（未加载）" : "图片未加载"})
            offset = pattern.lastIndex
        }
        const tail = markdown.slice(offset).trim()
        if (tail) result.push({kind: "text", value: tail})
        return result
    }
    onSourceChanged: contentItem.contentY = 0
    Column {
        width: root.availableWidth
        spacing: 16
        Label {
            width: parent.width; topPadding: 8
            text: root.modified ? new Date(root.modified).toLocaleString(Qt.locale("zh_CN"), "yyyy年M月d日 dddd HH:mm") : ""
            horizontalAlignment: Text.AlignHCenter; color: AppTheme.mutedText; font.pixelSize: 12
        }
        Label {
            x: root.inset; width: parent.width - 2 * x
            text: root.noteTitle; wrapMode: Text.Wrap; color: AppTheme.text
            font.family: root.fontFamily; font.pixelSize: root.titleSize; font.weight: Font.Bold
            bottomPadding: 6
        }
        Repeater {
            model: root.blocks
            delegate: Column {
                id: block
                required property var modelData
                x: root.inset; width: root.availableWidth - 2 * x
                Image {
                    id: picture
                    visible: block.modelData.kind === "image"
                    width: parent.width
                    height: visible && sourceSize.width > 0 ? width * sourceSize.height / sourceSize.width : 0
                    source: visible ? block.modelData.value : ""
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    Accessible.role: Accessible.Graphic
                    Accessible.name: block.modelData.alt || "笔记图片"
                }
                Text {
                    visible: block.modelData.kind === "text" || picture.status === Image.Error
                    width: parent.width
                    text: block.modelData.kind === "text" ? block.modelData.value : "图片无法读取：" + (block.modelData.alt || "本地图片")
                    textFormat: block.modelData.kind === "text" ? Text.MarkdownText : Text.PlainText
                    wrapMode: Text.Wrap; color: AppTheme.text; linkColor: AppTheme.accent
                    font.family: root.fontFamily; font.pixelSize: root.bodySize
                    lineHeight: 1.35
                    Accessible.role: Accessible.StaticText
                    Accessible.name: "只读阅读正文"
                }
            }
        }
        Label { visible: root.blocks.length === 0; x: root.inset; text: "这篇笔记还没有正文。"; color: AppTheme.mutedText }
        Item { width: 1; height: 36 }
    }
}
