import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kos.Ui

Item {
    id: root
    required property var controller
    required property var session
    required property Item sceneItem
    readonly property bool popupActive: panel.visible || restoreDialog.visible
    property string tab: "links"
    property string selectedHash: ""
    property string heading: ""
    property var selectedLink: ({})
    function open(kind) { tab = kind; selectedHash = ""; controller.inspectKnowledge(); if (kind === "history") controller.inspectHistory(); if (kind === "links") { selectedLink = ({}); heading = ""; controller.lookupLinks("") }; if (kind === "templates") controller.inspectTemplates(); panel.open() }
    LiquidDialog {
        id: panel; title: root.tab === "history" ? "笔记历史" : root.tab === "properties" ? "标签与属性" : root.tab === "templates" ? "模板" : "链接与关联"
        sourceItem: root.sceneItem; anchors.centerIn: parent; width: Math.min(850, root.width - 40); height: Math.min(640, root.height - 40)
        standardButtons: Dialog.Close
        contentItem: ColumnLayout {
            RowLayout {
                LiquidActionButton { text: "模板"; materialForm: true; onClicked: { root.tab = "templates"; root.controller.inspectTemplates() } }
                LiquidActionButton { text: "链接与关联"; materialForm: true; onClicked: { root.tab = "links"; root.controller.inspectKnowledge(); root.controller.lookupLinks("") } }
                LiquidActionButton { text: "标签与属性"; materialForm: true; onClicked: { root.tab = "properties"; root.controller.inspectKnowledge() } }
                LiquidActionButton { text: "历史版本"; materialForm: true; onClicked: { root.tab = "history"; root.controller.inspectHistory() } }
            }
            ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true; contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    ColumnLayout {
                        visible: root.tab === "templates"; Layout.fillWidth: true
                        Label { text: "支持 {{date}}、{{time}}、{{title}}。名为“每日笔记”的模板用于新建日记。"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: AppTheme.mutedText }
                        LiquidTextField { id: templateName; Layout.fillWidth: true; materialForm: true; Accessible.name: "模板名称"; placeholderText: "模板名称" }
                        LiquidTextField { id: templateTitle; Layout.fillWidth: true; materialForm: true; Accessible.name: "新笔记标题"; placeholderText: "新笔记标题" }
                        TextArea { id: templateBody; Layout.fillWidth: true; Layout.preferredHeight: 200; wrapMode: Text.Wrap; Accessible.name: "模板 Markdown 内容"; placeholderText: "模板 Markdown 内容"; color: AppTheme.text }
                        RowLayout {
                            LiquidActionButton { text: "保存模板"; materialForm: true; enabled: !!templateName.text && !root.controller.busy; onClicked: root.controller.saveTemplate(templateName.text, templateBody.text) }
                            LiquidActionButton { text: "当前笔记存为模板"; materialForm: true; enabled: !!templateName.text && !!root.controller.path && !root.controller.busy; onClicked: root.controller.saveCurrentTemplate(templateName.text) }
                        }
                        RowLayout {
                            LiquidActionButton { text: "从模板新建"; materialForm: true; enabled: root.controller.templates[templateName.text] !== undefined && !root.controller.busy; onClicked: { root.controller.createFromTemplate(templateName.text, templateTitle.text || templateName.text); panel.close() } }
                            LiquidActionButton { text: "删除模板"; materialForm: true; enabled: root.controller.templates[templateName.text] !== undefined && !root.controller.busy; onClicked: root.controller.deleteTemplate(templateName.text) }
                        }
                        Repeater { model: Object.keys(root.controller.templates); delegate: LiquidActionButton { required property string modelData; materialForm: true; text: modelData; onClicked: { templateName.text = modelData; templateBody.text = root.controller.templates[modelData] } } }
                    }
                    ColumnLayout {
                        visible: root.tab === "links"; Layout.fillWidth: true
                        LiquidTextField { Layout.fillWidth: true; materialForm: true; Accessible.name: "搜索要链接的笔记"; placeholderText: "搜索要链接的笔记"; onTextChanged: root.controller.lookupLinks(text) }
                        Repeater {
                            model: root.controller.linkCandidates
                            delegate: LiquidActionButton {
                                required property var modelData
                                Layout.fillWidth: true; materialForm: true; text: modelData.title + " · " + modelData.path
                                onClicked: { root.selectedLink = modelData; root.heading = "" }
                            }
                        }
                        Label { text: root.selectedLink.path ? "目标：" + root.selectedLink.path : "选择一个目标笔记"; color: AppTheme.text }
                        LiquidSelect { Layout.fillWidth: true; materialForm: true; model: ["整篇笔记"].concat((root.selectedLink.headings || []).map(h => h.title)); onActivated: function(index) { currentIndex = index; root.heading = index ? root.selectedLink.headings[index - 1].title : "" } }
                        LiquidActionButton {
                            text: "插入内部链接"; materialForm: true; enabled: !!root.selectedLink.path && !root.controller.busy && !root.session.sourceMode
                            onClicked: { root.session.execute("insertNoteLink:" + JSON.stringify({url: "file://" + (root.controller.library + "/" + root.selectedLink.path).split("/").map(encodeURIComponent).join("/"), title: root.selectedLink.title, heading: root.heading})); panel.close() }
                        }
                        Label { text: "反向链接"; color: AppTheme.text; font.bold: true }
                        Label { visible: !(root.controller.knowledgeData.backlinks || []).length; text: "暂无其他笔记链接到这里"; color: AppTheme.mutedText }
                        Repeater {
                            model: root.controller.knowledgeData.backlinks || []
                            delegate: LiquidActionButton { required property var modelData; materialForm: true; Layout.fillWidth: true; text: modelData.title + " · " + modelData.path + "\n" + modelData.summary; onClicked: { root.controller.openNote(modelData.path); panel.close() } }
                        }
                        Label { text: "出链"; color: AppTheme.text; font.bold: true }
                        Repeater {
                            model: root.controller.knowledgeData.outgoing || []
                            delegate: Label { required property var modelData; text: modelData.target + (modelData.resolved ? "" : " · 目标缺失或同名歧义"); color: AppTheme.mutedText }
                        }
                    }
                    ColumnLayout {
                        visible: root.tab === "properties"; Layout.fillWidth: true
                        Label { text: "标签（逗号分隔）"; color: AppTheme.text }
                        LiquidTextField { id: tagsField; Layout.fillWidth: true; materialForm: true; text: (root.controller.knowledgeData.properties?.tags || []).join(", "); Accessible.name: "笔记标签" }
                        LiquidActionButton { text: "保存标签"; materialForm: true; enabled: !root.controller.busy && !tagsField.inputMethodComposing; onClicked: root.controller.setNoteProperty("tags", tagsField.text.split(",").map(t => t.trim()).filter(t => t)) }
                        Label { text: "简单属性（文本值，保留其他 front matter）"; color: AppTheme.text }
                        LiquidTextField { id: propertyName; Layout.fillWidth: true; materialForm: true; Accessible.name: "属性名，例如 status、project、date"; placeholderText: "属性名，例如 status、project、date" }
                        LiquidTextField { id: propertyValue; Layout.fillWidth: true; materialForm: true; Accessible.name: "属性值"; placeholderText: "属性值" }
                        RowLayout {
                            LiquidActionButton { text: "保存属性"; materialForm: true; enabled: !!propertyName.text && !root.controller.busy; onClicked: root.controller.setNoteProperty(propertyName.text, propertyValue.text) }
                            LiquidActionButton { text: "删除属性"; materialForm: true; enabled: !!propertyName.text && !root.controller.busy; onClicked: root.controller.setNoteProperty(propertyName.text, "", true) }
                        }
                        Repeater { model: Object.keys(root.controller.knowledgeData.properties || {}); delegate: Label { required property string modelData; text: modelData + ": " + root.controller.knowledgeData.properties[modelData]; color: AppTheme.text } }
                        Label { text: "库内标签（点击筛选）"; color: AppTheme.text; font.bold: true }
                        Repeater { model: root.controller.knowledgeData.tags || []; delegate: LiquidActionButton { required property string modelData; materialForm: true; text: modelData; onClicked: { root.controller.query = "tag:" + JSON.stringify(modelData); panel.close() } } }
                    }
                    ColumnLayout {
                        visible: root.tab === "history"; Layout.fillWidth: true
                        Label { text: "保留最近 50 个不同内容版本。选择版本可与当前内容对照，恢复操作可撤销。"; wrapMode: Text.Wrap; Layout.fillWidth: true; color: AppTheme.mutedText }
                        Repeater {
                            model: root.controller.historyData.versions || []
                            delegate: LiquidActionButton { required property var modelData; materialForm: true; text: modelData.time + (modelData.current ? " · 当前版本" : ""); onClicked: { root.selectedHash = modelData.hash; root.controller.historyVersion(modelData.hash) } }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout { Layout.fillWidth: true; Label { text: "选择的版本"; color: AppTheme.text } TextArea { Layout.fillWidth: true; Layout.preferredHeight: 220; text: panel.visible && root.tab === "history" ? (root.controller.versionData.text || "") : ""; readOnly: true; selectByMouse: true; wrapMode: Text.Wrap; color: AppTheme.text } }
                            ColumnLayout { Layout.fillWidth: true; Label { text: "当前内容"; color: AppTheme.text } TextArea { Layout.fillWidth: true; Layout.preferredHeight: 220; text: panel.visible && root.tab === "history" ? root.session.source : ""; readOnly: true; selectByMouse: true; wrapMode: Text.Wrap; color: AppTheme.text } }
                        }
                        LiquidActionButton { text: "恢复所选版本…"; materialForm: true; enabled: !!root.selectedHash && root.controller.versionData.hash === root.selectedHash && !root.controller.busy && !root.controller.readOnly; onClicked: restoreDialog.open() }
                    }
                }
            }
        }
    }
    LiquidDialog {
        id: restoreDialog; sourceItem: root.sceneItem; title: "恢复这个历史版本？"; anchors.centerIn: parent; width: Math.min(440, root.width - 40)
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label { text: "先保存当前输入，再载入选中的历史内容。恢复后仍可撤销。"; wrapMode: Text.Wrap; color: AppTheme.text }
        onAccepted: { root.controller.historyVersion(root.selectedHash, true); panel.close() }
    }
}
