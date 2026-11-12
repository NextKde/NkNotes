import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kos.Ui

// Application settings compose public dialog and form components.
LiquidDialog {
    id: root
    required property var hostWindow
    required property var controller
    property int section: 0
    property int bodySize: controller.preference("bodySize", 16)
    signal actionRequested(string action)
    title: "设置"; closeText: "完成"; standardButtons: Dialog.Close
    width: Math.min(620, parent.width - 40)
    height: Math.min(580, parent.height - 40)
    property string pendingAction: ""
    function resetScroll() { settingsScroll.contentItem.contentY = 0 }
    function show(sectionIndex) { section = sectionIndex || 0; pendingAction = ""; resetScroll(); open() }
    function request(action) { pendingAction = action; close() }
    onClosed: {
        const action = pendingAction
        pendingAction = ""
        if (action) actionRequested(action)
    }
    onSectionChanged: resetScroll()
    onHeightChanged: Qt.callLater(revealFocus)
    function revealFocus() {
        const item = hostWindow.activeFocusItem, flick = settingsScroll.contentItem
        if (!visible || !item || !flick || !flick.contentItem) return
        let ancestor = item
        while (ancestor && ancestor !== flick.contentItem) ancestor = ancestor.parent
        if (!ancestor) return
        const top = item.mapToItem(flick.contentItem, 0, 0).y
        if (top < flick.contentY) flick.contentY = Math.max(0, top - 6)
        else if (top + item.height > flick.contentY + flick.height)
            flick.contentY = Math.min(Math.max(0, flick.contentHeight - flick.height), top + item.height - flick.height + 6)
    }
    Connections { target: root.hostWindow; function onActiveFocusItemChanged() { root.revealFocus() } }
    component ActionRow: SettingsItem {
        id: row
        property string actionText: "管理…"
        signal triggered()
        LiquidActionButton { text: row.actionText; Accessible.name: row.title + " · " + row.actionText; materialForm: true; implicitHeight: 26; enabled: row.enabled; onClicked: row.triggered() }
    }
    contentItem: ColumnLayout {
        spacing: 18
        LiquidSegmentedControl {
            Layout.fillWidth: true; implicitHeight: 30; materialForm: true
            labels: ["通用", "笔记管理", "数据"]; currentIndex: root.section
            onSelectionRequested: function(index) { root.section = index }
        }
        ScrollView {
            id: settingsScroll; Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: settingsScroll.availableWidth; spacing: 18
                SettingsGroup {
                    visible: root.section === 0; title: "阅读与外观"
                    SettingsItem {
                        title: "主题"
                        LiquidSelect {
                            appearanceMode: AppTheme.appearanceMode; materialForm: true; model: ["跟随系统", "浅色", "深色"]; Accessible.name: "主题"
                            plainTrigger: true; indicatorStyle: "upDown"; indicatorBackground: true
                            popupMaterialForm: true; compactPopup: true; popupFontWeight: Font.Normal
                            popupBorderWidth: 0; popupShadowEnabled: true; neutralOptionHighlight: true
                            currentIndex: Math.max(0, ["system", "light", "dark"].indexOf(AppTheme.appearanceMode))
                            onActivated: function(index) { AppTheme.appearanceMode = ["system", "light", "dark"][index]; root.controller.setPreference("appearance", AppTheme.appearanceMode) }
                        }
                    }
                    SettingsItem {
                        title: "正文字号"
                        LiquidSelect {
                            Accessible.name: "正文字号"; materialForm: true
                            appearanceMode: AppTheme.appearanceMode
                            plainTrigger: true; indicatorStyle: "upDown"; indicatorBackground: true
                            popupMaterialForm: true; compactPopup: true; popupFontWeight: Font.Normal
                            popupBorderWidth: 0; popupShadowEnabled: true; neutralOptionHighlight: true
                            model: Array.from({length: 13}, (_, i) => String(12 + i))
                            currentIndex: Math.max(0, Math.min(12, root.bodySize - 12))
                            onActivated: function(index) { root.bodySize = 12 + index; root.controller.setPreference("bodySize", root.bodySize) }
                        }
                    }
                    SettingsItem {
                        title: "减少透明效果"
                        SettingsSwitch { accessibleName: "减少透明效果"; checked: AppTheme.reduceTransparency; onToggled: { AppTheme.reduceTransparency = checked; root.controller.setPreference("reduceTransparency", checked) } }
                    }
                    SettingsItem {
                        title: "减少动画"
                        SettingsSwitch { accessibleName: "减少动画"; checked: AppTheme.reduceMotion; onToggled: { AppTheme.reduceMotion = checked; root.controller.setPreference("reduceMotion", checked) } }
                    }
                }
                SettingsGroup {
                    visible: root.section === 0; title: "编辑"
                    SettingsItem { title: "自动保存"; description: "编辑后自动保存，切换笔记时保留最新内容。" }
                    ActionRow { title: "键盘快捷键"; actionText: "查看…"; onTriggered: root.request("shortcuts") }
                }
                SettingsGroup {
                    visible: root.section === 2; title: "导入笔记"
                    ActionRow { title: "从 Markdown 导入"; description: "收集为独立笔记，原文件保持不变。"; actionText: "选择笔记…"; enabled: !root.controller.busy; onTriggered: root.request("importFiles") }
                    ActionRow { title: "整个文件夹"; description: "可保留原有分类层级。"; actionText: "导入…"; enabled: !root.controller.busy; onTriggered: root.request("importFolder") }
                    ActionRow { title: "导入记录"; actionText: "查看…"; enabled: !root.controller.busy; onTriggered: root.request("importHistory") }
                    ActionRow { visible: !!root.controller.legacyLibrary; title: "之前的笔记"; actionText: "迁移…"; enabled: !root.controller.busy; onTriggered: root.request("importLegacy") }
                }
                SettingsGroup {
                    visible: root.section === 2; title: "导出"
                    ActionRow { title: "全部笔记"; description: "导出 Markdown 和关联附件。"; actionText: "导出…"; enabled: !root.controller.busy; onTriggered: root.request("exportAll") }

                }
                SettingsGroup {
                    visible: root.section === 2; title: "备份与恢复"
                    ActionRow { title: "完整备份"; description: "保留笔记、附件、历史和最近删除。"; actionText: "创建备份…"; enabled: !root.controller.busy; onTriggered: root.request("backup") }
                    ActionRow { title: "恢复完整备份"; description: "替换当前空间前，自动保留一份恢复前副本。"; actionText: "选择备份…"; enabled: !root.controller.busy; onTriggered: root.request("restore") }
                }
                SettingsGroup {
                    visible: root.section === 1; title: "整理与记录"
                    ActionRow { title: "笔记模板"; onTriggered: root.request("templates"); enabled: !root.controller.busy }
                    ActionRow { title: "智能视图"; description: "保存当前搜索和分类条件，自动汇集符合条件的笔记。"; actionText: "保存当前搜索…"; enabled: !root.controller.busy; onTriggered: root.request("smartCollection") }
                    ActionRow { title: "批量重命名标签"; enabled: !root.controller.busy; onTriggered: root.request("renameTags") }
                    ActionRow { title: "恢复草稿"; description: "找回未完成保存的内容。"; actionText: "查看…"; enabled: !root.controller.busy; onTriggered: root.request("recovery") }
                }
                SettingsGroup {
                    visible: root.section === 2; title: "外部文件"
                    ActionRow { title: "临时编辑 Markdown"; description: "直接编辑原文件，不加入我的笔记。"; actionText: "选择文件…"; enabled: !root.controller.busy; onTriggered: root.request("external") }
                }
            }
        }
    }
}
