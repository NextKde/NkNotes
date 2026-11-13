import QtQuick
import QtQuick.Window
import QtCore
import QtQuick.Dialogs as SystemDialogs
import Kos.Ui

// Keep selection separate from storage: a system dialog's returned URL is the
// exact path granted by the portal, including its final file name.
QtObject {
    id: root
    property bool systemFileDialogs: false
    property Item sourceItem: null
    property string mode: "openFile" // openFile, openFiles, folder, saveFile
    property string title: ""
    property url currentFolder: toUrl(StandardPaths.writableLocation(StandardPaths.HomeLocation))
    property url selectedFile
    property url selectedFolder
    property var selectedFiles: []
    property var nameFilters: ["*"]
    property string defaultSuffix: ""
    property string acceptText: mode === "saveFile" ? qsTr("Save") : qsTr("Open")
    property string rejectText: qsTr("Cancel")
    property string locationPlaceholder: qsTr("Folder location")
    property string fileNamePlaceholder: qsTr("File name")
    property string unavailableText: qsTr("Folder unavailable")
    property string emptyText: qsTr("No matching files")
    property string overwriteTitle: qsTr("Replace this file?")
    property string overwriteText: qsTr("A file with this name already exists.")
    property string overwriteAcceptText: qsTr("Replace")
    readonly property bool visible: _pending || _finishing || (!!_backend && _backend.visible)
    signal accepted()
    signal rejected()
    signal failed(string message)

    property var _backend: null
    property bool _pending: false
    property bool _finishing: false
    property string _activeMode: ""
    property string _activeSuffix: ""
    property bool _activeSystem: false
    Component.onDestruction: {
        _pending = false
        const dialog = _backend
        _backend = null
        if (dialog) { dialog.close(); dialog.destroy() }
    }
    // Components are replaceable by the contract test; only one is instantiated.
    property Component _fileComponent: Component { SystemDialogs.FileDialog {} }
    property Component _folderComponent: Component { SystemDialogs.FolderDialog {} }
    property Component _localComponent: Component { LiquidFilePicker {} }
    property Connections _backendSignals: Connections {
        target: root._backend
        ignoreUnknownSignals: true
        function onAccepted() { root._complete(root._backend, true) }
        function onRejected() { root._complete(root._backend, false) }
        function onVisibleChanged() {
            if (root._backend && !root._backend.visible) {
                const dialog = root._backend
                Qt.callLater(function() { root._hidden(dialog) })
            }
        }
        function onClosed() { root._hidden(root._backend) }
    }

    function toUrl(path) {
        if (String(path).startsWith("file:")) return path
        return "file://" + String(path).split("/").map(encodeURIComponent).join("/")
    }
    function _localUrl(value) {
        const text = String(value)
        return (/^file:\/\/(?:localhost)?\//i.test(text) || /^file:\/(?!\/)/i.test(text))
            && !/[?#]/.test(text)
    }
    function _parentUrl(value) {
        const text = String(value)
        return text.slice(0, text.lastIndexOf("/") + 1)
    }
    function _suffix() { return defaultSuffix.replace(/^\./, "") }
    function _prefill() {
        let value = String(selectedFile)
        const suffix = _suffix()
        if (value && suffix && !value.toLowerCase().endsWith("." + suffix.toLowerCase()))
            value += "." + encodeURIComponent(suffix)
        return value
    }
    function _systemFilters() {
        // LiquidFilePicker takes a union of glob patterns. Qt takes named
        // alternatives; keep the union together, including *.md and *.MD.
        const patterns = Array.from(nameFilters || [])
        return patterns.length ? [qsTr("Files") + " (" + patterns.join(" ") + ")"] : []
    }
    function open() {
        if (visible) return
        if (["openFile", "openFiles", "folder", "saveFile"].indexOf(mode) < 0) {
            failed(qsTr("Unsupported file selection mode")); rejected(); return
        }
        _activeMode = mode
        _activeSuffix = _suffix()
        _activeSystem = systemFileDialogs
        _pending = true
        let component, properties
        if (systemFileDialogs) {
            properties = {title: title, parentWindow: sourceItem ? sourceItem.Window.window : null,
                currentFolder: currentFolder, acceptLabel: acceptText, rejectLabel: rejectText,
                modality: Qt.WindowModal}
            if (mode === "folder") {
                component = _folderComponent
            } else {
                component = _fileComponent
                properties.fileMode = mode === "openFiles" ? SystemDialogs.FileDialog.OpenFiles
                    : mode === "saveFile" ? SystemDialogs.FileDialog.SaveFile : SystemDialogs.FileDialog.OpenFile
                properties.nameFilters = _systemFilters()
                // Qt can append this suffix after the portal has granted the
                // original URL. Supply it only in the proposed name instead.
                properties.defaultSuffix = ""
                if (mode === "saveFile" && String(selectedFile)) properties.selectedFile = _prefill()
            }
        } else {
            component = _localComponent
            properties = {sourceItem: sourceItem, mode: mode, title: title, currentFolder: currentFolder,
                selectedFile: selectedFile, nameFilters: nameFilters, defaultSuffix: _suffix(),
                acceptText: acceptText, rejectText: rejectText, locationPlaceholder: locationPlaceholder,
                fileNamePlaceholder: fileNamePlaceholder, unavailableText: unavailableText, emptyText: emptyText,
                overwriteTitle: overwriteTitle, overwriteText: overwriteText, overwriteAcceptText: overwriteAcceptText}
        }
        // Recreate native helpers per request: some portal implementations retain
        // the previous save selection and otherwise reuse the old proposed name.
        _backend = component ? component.createObject(sourceItem || root, properties) : null
        if (!_backend) { _fail(qsTr("无法打开文件选择器，请重试。")); return }
        const dialog = _backend
        dialog.open()
        // A dialog that opened successfully can be dismissed before the next
        // event-loop turn; its later hiding is cancellation, not a show failure.
        if (!dialog.visible) {
            Qt.callLater(function() {
                if (root._backend === dialog && root._pending && !dialog.visible)
                    root._fail(qsTr("无法打开文件选择器，请检查系统文件对话框服务。"))
            })
        }
    }
    function close() { reject() }
    function reject() {
        if (!_pending) return
        const dialog = _backend
        if (dialog) dialog.reject()
        // Closing a backend must complete the request even if it only hides.
        if (_pending) _complete(dialog, false)
    }
    function _fail(message) {
        _finishing = true
        _pending = false
        failed(message)
        rejected()
        _finishing = false
        const dialog = _backend
        if (dialog && dialog.visible) dialog.reject()
        if (dialog === _backend && dialog && !dialog.visible) _retire(dialog)
    }
    function _complete(dialog, accept) {
        if (dialog !== _backend || !_pending) return
        if (accept) {
            let values = _activeMode === "folder" ? [dialog.selectedFolder]
                : _activeMode === "openFiles" ? Array.from(dialog.selectedFiles || []) : [dialog.selectedFile]
            if (!values.length || values.some(value => !_localUrl(value))) {
                _fail(qsTr("请选择本地文件或文件夹。")); return
            }
            if (_activeSystem && _activeMode === "saveFile" && _activeSuffix
                    && !String(values[0]).toLowerCase().endsWith("." + encodeURIComponent(_activeSuffix).toLowerCase())) {
                _fail(qsTr("文件名需要以 .%1 结尾，请重新选择保存位置。").arg(_activeSuffix)); return
            }
            if (_activeMode === "folder") {
                selectedFolder = values[0]
                currentFolder = values[0]
            } else {
                selectedFile = values[0]
                selectedFiles = values.map(value => String(value))
                currentFolder = _activeSystem ? _parentUrl(values[0]) : dialog.currentFolder
            }
        } else if (!_activeSystem && dialog && _localUrl(dialog.currentFolder)) {
            currentFolder = dialog.currentFolder
        }
        _finishing = true
        _pending = false
        if (accept) accepted()
        else rejected()
        _finishing = false
        if (dialog && !dialog.visible) _retire(dialog)
    }
    function _hidden(dialog) {
        if (!dialog || dialog !== _backend || dialog.visible) return
        if (_pending) _complete(dialog, false)
        else _retire(dialog)
    }
    function _retire(dialog) {
        if (dialog !== _backend) return
        _backend = null
        dialog.destroy()
    }
}
