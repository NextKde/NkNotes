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
    signal browseRequested()
    property bool advancedProperties: false
    property bool choosingLink: false
    property string tab: "links"
    property string selectedHash: ""
    property string heading: ""
    property var selectedLink: ({})
    function location(path) {
        const parts = String(path || "").split("/"); parts.pop()
        return parts.length ? parts.join(" › ") : "未分类"
    }
    function readableTarget(value) { try { return decodeURIComponent(String(value || "")) } catch (error) { return String(value || "") } }
    function cleanSnippet(text) { return String(text || "").replace(/\[([^\]]+)\]\([^)]*\)/g, "$1").replace(/(^|\s)#{1,6}\s+/g, "$1").slice(0,100) }
    function open(kind) { choosingLink = false; tab = kind; selectedHash = ""; controller.inspectKnowledge(); if (kind === "history") controller.inspectHistory(); if (kind === "links") { selectedLink = ({}); heading = ""; controller.lookupLinks("") }; if (kind === "templates") controller.inspectTemplates(); panel.open() }
    LiquidDialog {
        acceptText: "确认"; rejectText: "取消"; closeText: "关闭"
        id: panel; title: root.tab === "history" ? "笔记历史" : root.tab === "properties" ? "标签与属性" : root.tab === "templates" ? "模板" : "相关笔记"
        sourceItem: root.sceneItem; anchors.centerIn: parent; width: Math.min(root.tab === "history" ? 850 : root.tab === "properties" ? 520 : 640, root.width - 40); height: Math.min((root.tab === "properties" && !root.advancedProperties) || (root.tab === "links" && !root.choosingLink) ? 400 : 600, root.height - 40)
        standardButtons: Dialog.Close
        contentItem: ColumnLayout {
            LiquidSegmentedControl {
                visible: root.tab !== "templates"; Layout.fillWidth: true; implicitHeight: 30; materialForm: true
                labels: ["标签", "关联", "历史"]
                readonly property var sections: ["properties", "links", "history"]
                currentIndex: Math.max(0, sections.indexOf(root.tab))
                onSelectionRequested: function(index) { root.open(sections[index]) }
            }
            ScrollView {
                id: detailsScroll
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true; contentWidth: availableWidth
                ColumnLayout {
                    width: detailsScroll.availableWidth
                    ColumnLayout {
                        visible: root.tab === "templates"; Layout.fillWidth: true
                        LiquidActionButton { text: "打开今日笔记"; materialForm: true; enabled: !root.controller.busy; onClicked: { root.controller.createDailyNote(); panel.close() } }
                        Label { text: "支持 {{date}}、{{time}}、{{title}}。名为“每日笔记”的模板用于新建日记。"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: AppTheme.mutedText }
                        LiquidTextField { id: templateName; Layout.fillWidth: true; materialForm: true; Accessible.name: "模板名称"; placeholderText: "模板名称" }
                        LiquidTextField { id: templateTitle; Layout.fillWidth: true; materialForm: true; Accessible.name: "新笔记标题"; placeholderText: "新笔记标题" }
                        KosTextArea { id: templateBody; Layout.fillWidth: true; Layout.preferredHeight: 200; wrapMode: Text.Wrap; Accessible.name: "模板 Markdown 内容"; placeholderText: "模板 Markdown 内容"; color: AppTheme.text }
                        Flow {
                            Layout.fillWidth: true; spacing: 8
                            LiquidActionButton { text: "保存模板"; materialForm: true; enabled: !!templateName.text && !root.controller.busy; onClicked: root.controller.saveTemplate(templateName.text, templateBody.text) }
                            LiquidActionButton { text: "当前笔记存为模板"; materialForm: true; enabled: !!templateName.text && !!root.controller.path && !root.controller.externalDocument && !root.controller.busy; onClicked: root.controller.saveCurrentTemplate(templateName.text) }
                        }
                        Flow {
                            Layout.fillWidth: true; spacing: 8
                            LiquidActionButton { text: "从模板新建"; materialForm: true; enabled: root.controller.templates[templateName.text] !== undefined && !root.controller.busy; onClicked: { root.controller.createFromTemplate(templateName.text, templateTitle.text || templateName.text); panel.close() } }
                            LiquidActionButton { text: "删除模板"; materialForm: true; enabled: root.controller.templates[templateName.text] !== undefined && !root.controller.busy; onClicked: root.controller.deleteTemplate(templateName.text) }
                        }
                        Repeater { model: Object.keys(root.controller.templates); delegate: LiquidActionButton { required property string modelData; materialForm: true; text: modelData; onClicked: { templateName.text = modelData; templateBody.text = root.controller.templates[modelData] } } }
                    }
                    ColumnLayout {
                        visible: root.tab === "links"; Layout.fillWidth: true; spacing: 10
                        Label { text: "提到的内容"; color: AppTheme.text; font.bold: true }
                        Label { visible: !(root.controller.knowledgeData.outgoing || []).length; text: "还没有添加链接"; color: AppTheme.mutedText }
                        Repeater {
                            model: root.controller.knowledgeData.outgoing || []
                            delegate: LiquidActionButton {
                                required property var modelData
                                Layout.fillWidth: true; materialForm: true
                                text: (modelData.title || root.readableTarget(modelData.target).split("/").pop().replace(/\.md$/i, "")) + (modelData.kind === "note" ? " · " + root.location(modelData.path) : modelData.kind === "attachment" ? " · 附件" : modelData.resolved ? "" : " · 暂时找不到")
                                enabled: !!modelData.resolved && !root.controller.busy
                                onClicked: { root.controller.openLink((modelData.wiki ? "wiki:" : "") + modelData.target); panel.close() }
                            }
                        }
                        Label { text: "提到这篇笔记"; color: AppTheme.text; font.bold: true }
                        Label { visible: !(root.controller.knowledgeData.backlinks || []).length; text: "还没有其他笔记提到这里"; color: AppTheme.mutedText }
                        Repeater {
                            model: root.controller.knowledgeData.backlinks || []
                            delegate: LiquidActionButton {
                                required property var modelData
                                materialForm: true; Layout.fillWidth: true
                                text: modelData.title + " · " + root.location(modelData.path)
                                onClicked: { root.controller.openNote(modelData.path); panel.close() }
                            }
                        }
                        LiquidActionButton {
                            text: root.choosingLink ? "取消添加链接" : "添加笔记链接…"; materialForm: true
                            enabled: !root.controller.busy && !root.session.sourceMode
                            onClicked: { root.choosingLink = !root.choosingLink; root.selectedLink = ({}); root.controller.lookupLinks("") }
                        }
                        ColumnLayout {
                            visible: root.choosingLink; Layout.fillWidth: true; spacing: 8
                            LiquidTextField { Layout.fillWidth: true; materialForm: true; Accessible.name: "搜索要链接的笔记"; placeholderText: "输入笔记标题"; onTextEdited: { root.selectedLink = ({}); root.controller.lookupLinks(text) } }
                            Repeater {
                                model: root.choosingLink ? root.controller.linkCandidates.slice(0,8) : []
                                delegate: LiquidActionButton {
                                    required property var modelData
                                    Layout.fillWidth: true; materialForm: true
                                    text: modelData.title + " · " + root.location(modelData.path)
                                    onClicked: { root.selectedLink = modelData; root.heading = "" }
                                }
                            }
                            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: root.selectedLink.path ? "已选：" + root.selectedLink.title : "选择一篇笔记；输入标题可以缩小范围。"; color: AppTheme.mutedText }
                            LiquidSelect {
                                appearanceMode: AppTheme.appearanceMode; Layout.fillWidth: true; materialForm: true
                                visible: !!root.selectedLink.path
                                model: ["整篇笔记"].concat((root.selectedLink.headings || []).map(h => h.title))
                                onActivated: function(index) { currentIndex = index; root.heading = index ? root.selectedLink.headings[index - 1].title : "" }
                            }
                            LiquidActionButton {
                                text: "添加链接"; materialForm: true; enabled: !!root.selectedLink.path && !root.controller.busy && !root.session.sourceMode
                                onClicked: { root.session.execute("insertNoteLink:" + JSON.stringify({url: "file://" + (root.controller.library + "/" + root.selectedLink.path).split("/").map(encodeURIComponent).join("/"), title: root.selectedLink.title, heading: root.heading})); panel.close() }
                            }
                        }
                    }
                    ColumnLayout {
                        visible: root.tab === "properties"; Layout.fillWidth: true
                        Label { text: "标签（逗号分隔）"; color: AppTheme.text }
                        LiquidTextField { id: tagsField; Layout.fillWidth: true; materialForm: true; text: (root.controller.knowledgeData.properties?.tags || []).join(", "); Accessible.name: "笔记标签" }
                        LiquidActionButton { text: "保存标签"; materialForm: true; enabled: !root.controller.busy && !tagsField.inputMethodComposing; onClicked: root.controller.setNoteProperty("tags", tagsField.text.split(",").map(t => t.trim()).filter(t => t)) }
                        LiquidActionButton { text: root.advancedProperties ? "收起更多属性" : "更多属性…"; materialForm: true; onClicked: root.advancedProperties = !root.advancedProperties }
                        ColumnLayout {
                            visible: root.advancedProperties; Layout.fillWidth: true; spacing: 8
                        Label { text: "更多属性"; color: AppTheme.text }
                        LiquidTextField { id: propertyName; Layout.fillWidth: true; materialForm: true; Accessible.name: "属性名称"; placeholderText: "属性名称" }
                        LiquidTextField { id: propertyValue; Layout.fillWidth: true; materialForm: true; Accessible.name: "属性值"; placeholderText: "属性值" }
                        Flow {
                            Layout.fillWidth: true; spacing: 8
                            LiquidActionButton { text: "保存属性"; materialForm: true; enabled: !!propertyName.text && !root.controller.busy; onClicked: root.controller.setNoteProperty(propertyName.text, propertyValue.text) }
                            LiquidActionButton { text: "删除属性"; materialForm: true; enabled: !!propertyName.text && !root.controller.busy; onClicked: root.controller.setNoteProperty(propertyName.text, "", true) }
                        }
                        Repeater { model: Object.keys(root.controller.knowledgeData.properties || {}).filter(k => ["tags", "title", "nextnotes_id"].indexOf(k) < 0); delegate: Label { required property string modelData; Layout.fillWidth: true; wrapMode: Text.Wrap; text: modelData + ": " + root.controller.knowledgeData.properties[modelData]; color: AppTheme.text } }
                        }
                        Label { text: "全部标签（点击筛选）"; color: AppTheme.text; font.bold: true }
                        Flow {
                            Layout.fillWidth: true; spacing: 6
                            Repeater { model: root.controller.knowledgeData.tags || []; delegate: LiquidActionButton { required property string modelData; materialForm: true; text: modelData; onClicked: { root.controller.filter = "all"; root.controller.query = "tag:" + JSON.stringify(modelData); panel.close(); root.browseRequested() } } }
                        }
                    }
                    ColumnLayout {
                        visible: root.tab === "history"; Layout.fillWidth: true
                        Label { text: "保留最近 50 个不同内容版本。选择版本可与当前内容对照，恢复操作可撤销。"; wrapMode: Text.Wrap; Layout.fillWidth: true; color: AppTheme.mutedText }
                        Repeater {
                            model: root.controller.historyData.versions || []
                            delegate: LiquidActionButton { required property var modelData; materialForm: true; text: Qt.formatDateTime(new Date(modelData.time), "yyyy/M/d HH:mm") + (modelData.current ? " · 当前版本" : ""); onClicked: { root.selectedHash = modelData.hash; root.controller.historyVersion(modelData.hash) } }
                        }
                        GridLayout {
                            columns: panel.width < 650 ? 1 : 2
                            Layout.fillWidth: true
                            ColumnLayout { Layout.fillWidth: true; Label { text: "选择的版本"; color: AppTheme.text } KosTextArea { Layout.fillWidth: true; Layout.preferredHeight: 220; text: panel.visible && root.tab === "history" ? (root.controller.versionData.text || "") : ""; readOnly: true; selectByMouse: true; wrapMode: Text.Wrap; color: AppTheme.text } }
                            ColumnLayout { Layout.fillWidth: true; Label { text: "当前内容"; color: AppTheme.text } KosTextArea { Layout.fillWidth: true; Layout.preferredHeight: 220; text: panel.visible && root.tab === "history" ? root.session.source : ""; readOnly: true; selectByMouse: true; wrapMode: Text.Wrap; color: AppTheme.text } }
                        }
                        LiquidActionButton { text: "恢复所选版本…"; materialForm: true; enabled: !!root.selectedHash && root.controller.versionData.hash === root.selectedHash && !root.controller.busy && !root.controller.readOnly; onClicked: restoreDialog.open() }
                    }
                }
            }
        }
    }
    LiquidDialog {
        acceptText: "恢复"; rejectText: "取消"; closeText: "关闭"
        id: restoreDialog; sourceItem: root.sceneItem; title: "恢复这个历史版本？"; anchors.centerIn: parent; width: Math.min(440, root.width - 40)
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label { text: "先保存当前输入，再载入选中的历史内容。恢复后仍可撤销。"; wrapMode: Text.Wrap; color: AppTheme.text }
        onAccepted: { root.controller.historyVersion(root.selectedHash, true); panel.close() }
    }
}
