import QtQuick
import QtQuick.Controls
import QtWebEngine
import QtWebChannel
import NextNotes.Editor 1.0
import Kos.Ui

Item {
    id: root
    required property var session
    required property var controller
    required property int bodySize
    required property real readingWidth
    required property int inset
    property alias editor: editorFacade
    required property Item sceneItem
    property var editorState: ({outline: [], stats: {}, block: null, slash: null, search: {total: 0, current: 0}})
    signal documentReady()
    readonly property bool popupActive: tools.popupActive || attachmentDialog.visible || exportDialog.visible
    property bool exporting: false
    onExportingChanged: controller.setEditorExporting(exporting)
    property var pendingExport: null
    property int exportSequence: 0
    property string notification: ""
    function notify(text) { notification = text; noticeTimer.restart() }
    function action(name, args) { if (webReady) bridge.actionRequested({epoch: session.webDocument().epoch, name: name, args: args || {}}) }
    function openTool(name, originMenu) { if (!session.sourceMode) tools.openTool(name, originMenu) }
    function toggleOutline(anchor) { tools.toggleOutline(anchor) }
    function correctSpelling(word) { web.replaceMisspelledWord(word); editorFacade.forceActiveFocus() }
    function exportBaseName() { return String(root.controller.noteTitle || "笔记").replace(/[\\/:*?"<>|\u0000-\u001f]/g, "-").replace(/[. ]+$/g, "").slice(0,100) || "笔记" }
    function chooseExport(format) { if (!webReady || session.sourceMode || session.composing || exporting) return; exportDialog.format = format; exportDialog.selectedFile = String(exportDialog.currentFolder) + "/" + encodeURIComponent(root.exportBaseName() + "." + format); exportDialog.open() }
    function beginExport(format, target, printing) {
        if (!webReady || session.sourceMode || session.composing || exporting) return
        const path = printing ? controller.printPreviewPath() : controller.outputPath(target, format)
        if (!path) { notify("请选择有效的输出路径"); return }
        exporting = true
        pendingExport = {token: ++exportSequence, epoch: session.webDocument().epoch, format: format, target: target, path: path, title: root.controller.noteTitle, printing: !!printing}
        bridge.prepareExportRequested(pendingExport.token)
    }
    function printPreview() { beginExport("pdf", "", true) }
    property bool allowRestoreFocus: true
    property bool webReady: false
    readonly property bool retainedSurface: renderLifecycle.attached
    property var pendingRestore: null
    property bool restoring: false
    readonly property bool readOnly: controller.readOnly || session.protectedContent || session.webLoading || exporting
    function css(color) { return "rgba(" + Math.round(color.r * 255) + "," + Math.round(color.g * 255) + "," + Math.round(color.b * 255) + "," + color.a + ")" }
    NotesTokens { id: tokens }
    readonly property var theme: ({
        background: css(AppTheme.contentBackground), text: css(AppTheme.text),
        muted: css(AppTheme.mutedText), border: css(AppTheme.border),
        accent: css(tokens.primaryYellow), selection: css(tokens.primaryYellow), selectionText: "#222222", scheme: AppTheme.dark ? "dark" : "light",
        size: bodySize + "px", width: readingWidth + "px", inset: inset + "px", font: "'Noto Sans CJK SC', sans-serif"
    })
    property string lastOptions: ""
    function updateOptions() {
        if (!webReady) return
        const options = {readonly: readOnly, theme: theme, imageWidths: controller.imageWidths()}
        const encoded = JSON.stringify(options)
        if (encoded === lastOptions) return
        lastOptions = encoded
        bridge.optionsRequested(options)
    }
    onReadOnlyChanged: updateOptions()
    onThemeChanged: updateOptions()
    function findNext(query) { if (webReady && !session.composing) bridge.findRequested(query) }
    QtObject {
        id: editorFacade
        readonly property bool activeFocus: web.activeFocus
        readonly property bool inputMethodComposing: root.session.composing
        function forceActiveFocus(reason) { web.forceActiveFocus(reason === undefined ? Qt.OtherFocusReason : reason); if (root.webReady) bridge.focusRequested() }
    }
    QtObject {
        id: bridge
        WebChannel.id: "bridge"
        signal loadRequested(var document)
        signal commandRequested(var command)
        signal flushRequested(int token)
        signal optionsRequested(var options)
        signal restoreRequested(var position)
        signal wikiResults(int request, var items)
        function lookupWiki(query, request) { root.controller.lookupLinks(query, request) }
        signal findRequested(string query)
        signal focusRequested()
        signal actionRequested(var action)
        signal attachmentResult(string request, int generation, var result)
        signal prepareExportRequested(int token)
        function ui(json) {
            const value = JSON.parse(json)
            if (value.epoch === root.session.webDocument().epoch) { root.editorState = value; if (value.tableWidths) root.controller.setTableWidths(value.tableWidths) }
        }
        function history(redo) { if (redo) root.controller.redo(); else root.controller.undo() }
        function notice(text) { root.notify(text) }
        function dismissSlash() { tools.closeSlash() }
        function requestTool(name) { if (name === "math") tools.openMath(); else if (name === "image") tools.openImage() }
        function copyText(text) { root.controller.copyText(text) }
        function openLink(href) { root.controller.openLink(href) }
        function importFile(request, generation, source, name, data) { root.controller.importAttachment(request, generation, source, name, data) }
        function chooseFile(request, generation) { attachmentDialog.request = request; attachmentDialog.generation = generation; attachmentDialog.open() }
        function exportReady(token, generation, body) {
            const value = root.pendingExport
            if (!value || token !== value.token) return
            if (generation !== value.epoch || generation !== root.session.webDocument().epoch) { root.pendingExport = null; root.exporting = false; root.notify("笔记已切换，请重新导出"); return }
            if (value.format === "html") {
                root.controller.exportHtml(value.target, body, value.title)
                root.pendingExport = null; root.exporting = false
            } else root.controller.exportPdf(value.path, body, value.title, value.printing)
        }
        function ready() {
            root.lastOptions = ""
            root.webReady = true
            root.session.webConnected()
            root.updateOptions()
            root.controller.restoreWorkspace()
        }
        function failed(message) {
            root.webReady = false
            root.session.webDisconnected()
            editorFailure.text = "编辑器无法载入这篇笔记，原文仍保留。" + message
            editorFailure.visible = true
        }
        function publish(json) {
            const state = JSON.parse(json)
            if (state.epoch !== root.session.webDocument().epoch) return
            const wasLoading = root.session.webLoading
            root.session.acceptWebState(state)
            if (!state.loaded) return
            if (wasLoading && !root.session.webLoading) root.documentReady()
            if (root.pendingRestore) {
                const position = root.pendingRestore
                root.pendingRestore = null
                root.restoring = true
                const generation = state.epoch
                Qt.callLater(function() {
                    if (generation === root.session.webDocument().epoch) {
                        if (root.allowRestoreFocus) editorFacade.forceActiveFocus()
                        bridge.restoreRequested(position)
                    }
                    root.restoring = false
                })
            } else if (!root.restoring) root.controller.rememberPosition(state.cursor, state.scroll)
        }
    }
    WebChannel { id: channel; registeredObjects: [bridge] }
    EditorRenderLifecycle { id: renderLifecycle; view: web }
    WebEngineView {
        id: web
        anchors.fill: parent
        anchors.rightMargin: tools.outlineWidth
        url: "qrc:/editor/index.html"
        webChannel: channel
        backgroundColor: AppTheme.contentBackground
        settings.localContentCanAccessRemoteUrls: false
        settings.localContentCanAccessFileUrls: true
        settings.javascriptCanOpenWindows: false
        settings.pluginsEnabled: false
        settings.webGLEnabled: false
        settings.errorPageEnabled: false
        onNavigationRequested: function(request) { if (request.url.toString() !== "qrc:/editor/index.html") request.action = WebEngineNavigationRequest.IgnoreRequest }
        onContextMenuRequested: function(request) {
            request.accepted = true
            if (root.webReady && !root.session.composing) tools.openContext(request.position.x, request.position.y, request.spellCheckerSuggestions || [])
        }
        onPrintRequested: root.printPreview()
        onJavaScriptConsoleMessage: function(level, message, line, sourceId) { console.warn("editor:", message) }
        onRenderProcessTerminated: { root.webReady = false; root.session.webDisconnected(); editorFailure.visible = true }
    }
    EditorTools {
        id: tools
        anchors.fill: parent
        source: root; session: root.session; controller: root.controller; sceneItem: root.sceneItem
    }
    LiquidFilePicker {
        sourceItem: root.sceneItem; acceptText: "选择"; rejectText: "取消"
        locationPlaceholder: "输入文件夹路径"; fileNamePlaceholder: "文件名"; emptyText: "此文件夹中没有可选文件"
        overwriteTitle: "替换这个文件？"; overwriteText: "目标位置已有同名文件，替换后将覆盖原内容。"; overwriteAcceptText: "替换"; unavailableText: "文件夹不存在或无法访问";
        id: attachmentDialog
        title: "导入图片或附件"
        nameFilters: ["*"]
        property string request: ""
        property int generation: 0
        onAccepted: root.controller.importAttachment(request, generation, selectedFile)
        onRejected: bridge.attachmentResult(request, generation, {ok: false, error: "cancelled"})
    }
    LiquidFilePicker {
        sourceItem: root.sceneItem; acceptText: "导出"; rejectText: "取消"
        locationPlaceholder: "输入文件夹路径"; fileNamePlaceholder: "文件名"; emptyText: "此文件夹中没有可选文件"
        overwriteTitle: "替换这个文件？"; overwriteText: "目标位置已有同名文件，替换后将覆盖原内容。"; overwriteAcceptText: "替换"; unavailableText: "文件夹不存在或无法访问";
        id: exportDialog
        property string format: "pdf"
        title: format === "pdf" ? "导出 PDF" : "导出独立 HTML"
        mode: "saveFile"
        nameFilters: format === "pdf" ? ["*.pdf"] : ["*.html"]
        defaultSuffix: format
        onAccepted: root.beginExport(format, selectedFile, false)
    }
    Timer { id: noticeTimer; interval: 4500; onTriggered: root.notification = "" }
    Label { visible: !!root.notification; anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.margins: 16; width: Math.max(0, parent.width - tools.outlineWidth - 32); text: root.notification; wrapMode: Text.WordWrap; color: AppTheme.mutedText; font.pixelSize: 11 }
    Label {
        visible: root.session.protectedContent
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 8
        width: parent.width - 48
        wrapMode: Text.WordWrap
        color: AppTheme.mutedText
        font.pixelSize: 11
        text: "这篇笔记包含尚未支持的扩展内容，已保留原文并设为只读。"
    }
    Label {
        id: editorFailure
        visible: false
        anchors.centerIn: parent
        width: parent.width - 48
        wrapMode: Text.WordWrap
        color: AppTheme.mutedText
        text: "编辑器进程已停止。已确认的正文仍保留在应用中，请保存后重新打开应用。"
    }
    Connections {
        target: root.session
        function onWebLoadRequested(document) {
            if (!root.webReady) return
            document.library = root.controller.assetRoot
            document.tableWidths = root.controller.tableWidths()
            document.baseUrl = root.controller.documentUrl.toString()
            bridge.loadRequested(document)
        }
        function onWebCommandRequested(command) { if (root.webReady) bridge.commandRequested(command) }
        function onWebFlushRequested(token) { if (root.webReady) bridge.flushRequested(token) }
    }
    Connections {
        target: root.controller
        function onChanged() { root.updateOptions() }
        function onPdfExportFinished() { root.pendingExport = null; root.exporting = false }
        function onAttachmentReady(request, generation, result) { bridge.attachmentResult(request, generation, result) }
        function onWikiCandidatesReady(request, items) { bridge.wikiResults(request, items) }
        function onDocumentAnchorRequested(heading) { root.pendingRestore = {heading: heading} }
        function onDocumentLoaded(cursor, scroll, matchLength) {
            root.pendingRestore = {cursor: cursor, scroll: Math.max(0, scroll), query: matchLength > 0 ? root.controller.highlightQuery : ""}
        }
    }
    Component.onCompleted: root.session.enableWeb()
}
