import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Kos.Ui

ApplicationWindow {
    id: root
    required property var session
    width: 1120; height: 740
    minimumWidth: 680; minimumHeight: 480
    visible: true
    title: "NextNotes · 编辑器原型"
    flags: Qt.Window | Qt.FramelessWindowHint
    color: AppTheme.contentBackground
    property bool sidebarRequested: true
    readonly property bool showSidebar: sidebarRequested && width >= 960
    property var windowActions: ["maximize", "minimize", "close"]
    NotesTokens { id: tokens }
    Binding { target: AppTheme; property: "compactDesign"; value: true }
    CommandRegistry {
        id: commands
        session: root.session; editor: source.editor
        onSidebarRequested: root.sidebarRequested = !root.sidebarRequested
    }
    header: Item {
        height: 38
        MouseArea {
            anchors.fill: parent; anchors.rightMargin: windowControls.width + 28
            onPressed: root.startSystemMove()
            onDoubleClicked: {
                if (root.visibility === Window.Maximized) root.showNormal()
                else root.showMaximized()
            }
        }
        Label {
            anchors.left: parent.left; anchors.leftMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            text: "NextNotes"; color: AppTheme.text; font.pixelSize: 12; font.weight: Font.DemiBold
        }
        KosWindowControls {
            id: windowControls
            targetWindow: root; actions: root.windowActions; maximizeMode: true
            circleSize: 14; buttonWidth: 22; buttonHeight: 22; spacing: 6
            anchors.right: parent.right; anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    // Capture contains ordinary reading content only. All controls and popups
    // consuming it are siblings outside this subtree; no desktop sampling.
    Item {
        id: readingScene
        anchors.fill: parent
        Rectangle { anchors.fill: parent; color: AppTheme.contentBackground }
        SourceEditor {
            id: source
            session: root.session
            anchors.top: parent.top; anchors.topMargin: tokens.toolbarHeight + 52
            anchors.bottom: parent.bottom; anchors.bottomMargin: 36
            anchors.left: parent.left
            // Follow the layout's actual geometry throughout the sidebar animation.
            anchors.leftMargin: editorColumn.x
            anchors.right: parent.right
            bodySize: tokens.bodySize
            readingWidth: tokens.readingWidth
        }
    }
    LiquidBackdrop { id: readingBackdrop; sourceItem: readingScene; registerAsDefault: false }
    RowLayout {
        anchors.fill: parent; spacing: 0
        KosSidebar {
            expanded: root.showSidebar; expandedWidth: tokens.sidebarWidth
            floating: false; surfaceColor: "transparent"
            Rectangle {
                anchors.fill: parent; color: AppTheme.contentSecondaryBackground
                // No sidebar underlay is sampled in this layout: use the opaque fallback.
                opacity: 1
            }
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 14; spacing: 6
                Label { text: "我的笔记"; color: AppTheme.text; font.pixelSize: 20; font.weight: Font.DemiBold; Layout.topMargin: 16; Layout.bottomMargin: 18 }
                LiquidNavigationButton { Layout.fillWidth: true; text: "全部笔记"; checked: true; specificationForm: true; onClicked: checked = true }
                Label { text: "文件夹"; color: AppTheme.mutedText; font.pixelSize: 11; Layout.topMargin: 25; Layout.leftMargin: 12 }
                LiquidNavigationButton { Layout.fillWidth: true; text: "原型工作区"; specificationForm: true; checkable: false; onClicked: source.editor.forceActiveFocus() }
                Item { Layout.fillHeight: true }
                Label { text: "本次会话\n尚未连接笔记库"; color: AppTheme.mutedText; font.pixelSize: 11; lineHeight: 1.5; Layout.leftMargin: 12; Layout.bottomMargin: 12 }
            }
        }
        Rectangle {
            Layout.preferredWidth: tokens.listWidth; Layout.fillHeight: true
            color: AppTheme.contentSecondaryBackground
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 16; spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "全部笔记"; color: AppTheme.text; font.pixelSize: 18; font.weight: Font.DemiBold; Layout.fillWidth: true }
                    Label { text: "1"; color: AppTheme.mutedText; font.pixelSize: 12 }
                }
                Label { text: "编辑器研究 · M0"; color: AppTheme.mutedText; font.pixelSize: 11; Layout.bottomMargin: 8 }
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: 102; radius: 10
                    color: AppTheme.controlBackground
                    Column {
                        anchors.fill: parent; anchors.margins: 14; spacing: 9
                        Label { text: "留下一个想法"; color: AppTheme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                        Label { text: "仅在内存中"; color: AppTheme.mutedText; font.pixelSize: 11 }
                        Label { width: parent.width; text: "从一个句子开始，慢慢连接成自己的知识。"; elide: Text.ElideRight; color: AppTheme.mutedText; font.pixelSize: 12 }
                    }
                    Accessible.role: Accessible.ListItem
                    Accessible.name: "留下一个想法，原型文档，未持久化"
                }
                Item { Layout.fillHeight: true }
            }
            Rectangle { anchors.right: parent.right; height: parent.height; width: 1; color: AppTheme.withAlpha(AppTheme.text, 0.07) }
        }
        Item {
            id: editorColumn
            Layout.fillWidth: true; Layout.fillHeight: true
            RowLayout {
                id: toolbar
                anchors.left: parent.left; anchors.right: parent.right
                anchors.top: parent.top; anchors.margins: 14; spacing: 6
                LiquidActionButton {
                    id: sidebarButton
                    text: "侧栏"; implicitWidth: 48; implicitHeight: 26
                    materialForm: false; liquidIntensity: "light"; backdrop: readingBackdrop
                    onClicked: root.sidebarRequested = !root.sidebarRequested
                    Accessible.name: "切换侧栏"
                    KosToolTip { target: sidebarButton; backdrop: readingBackdrop; text: "切换侧栏 · Ctrl+Shift+B" }
                }
                LiquidActionButton {
                    text: "撤销"; implicitWidth: 48; implicitHeight: 26
                    materialForm: true; cornerRadius: 6; focusPolicy: Qt.TabFocus
                    enabled: root.session.canUndo && !root.session.composing
                    onClicked: { root.session.undo(); source.editor.forceActiveFocus() }
                }
                LiquidActionButton {
                    text: "重做"; implicitWidth: 48; implicitHeight: 26
                    materialForm: true; cornerRadius: 6; focusPolicy: Qt.TabFocus
                    enabled: root.session.canRedo && !root.session.composing
                    onClicked: { root.session.redo(); source.editor.forceActiveFocus() }
                }
                Item { Layout.fillWidth: true }
                LiquidActionButton {
                    id: formatButton
                    text: "格式"; implicitWidth: 54; implicitHeight: 26
                    materialForm: false; liquidIntensity: "light"; backdrop: readingBackdrop
                    focusPolicy: Qt.TabFocus
                    enabled: !root.session.composing
                    onClicked: formatMenu.openAtAnchor()
                    Accessible.name: "Markdown 格式"
                    KosToolTip { target: formatButton; backdrop: readingBackdrop; text: "选择 Markdown 格式" }
                }
            }
            RowLayout {
                anchors.top: parent.top; anchors.topMargin: tokens.toolbarHeight + 14
                anchors.left: parent.left; anchors.right: parent.right; anchors.leftMargin: 28; anchors.rightMargin: 28
                Label { text: "MARKDOWN"; color: AppTheme.mutedText; font.pixelSize: 10; font.letterSpacing: 1.8 }
                Item { Layout.fillWidth: true }
                Label { text: "源码"; color: AppTheme.mutedText; font.pixelSize: 11 }
            }
            Label {
                anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.margins: 14
                text: root.session.modified ? "已修改 · 仅在内存中，关闭后不保留" : "原型文档 · 未连接存储"
                color: AppTheme.mutedText; font.pixelSize: 11
            }
        }
    }
    LiquidPopover {
        id: formatMenu
        anchorItem: formatButton; sourceItem: readingScene
        function apply(command) { commands.run(command); source.editor.forceActiveFocus() }
        LiquidPopoverItem { text: "加粗                     Ctrl+B"; onTriggered: formatMenu.apply("bold") }
        LiquidPopoverItem { text: "斜体                     Ctrl+I"; onTriggered: formatMenu.apply("italic") }
        LiquidPopoverItem { text: "删除线             Ctrl+Shift+X"; onTriggered: formatMenu.apply("strike") }
        LiquidPopoverItem { text: "行内代码                  Ctrl+E"; onTriggered: formatMenu.apply("code") }
    }
    KosWindowResizeHandles { parent: root.contentItem.parent; targetWindow: root }
}
