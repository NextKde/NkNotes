import QtQuick
import QtQuick.Window
import QtQuick.Dialogs as SystemDialogs
import QtTest
import NextNotes.ChromeTests

TestCase {
    id: testCase
    name: "NotesFilePicker"
    when: windowShown
    visible: true
    width: 800; height: 700
    Item { id: scene; anchors.fill: parent }
    Component {
        id: pickerComponent
        NotesFilePicker { sourceItem: scene }
    }
    Component {
        id: dialogComponent
        QtObject {
            property bool visible: false
            property var parentWindow: null
            property int modality: Qt.WindowModal
            property string title: ""
            property string acceptLabel: ""
            property string rejectLabel: ""
            property url currentFolder
            property url selectedFile
            property url selectedFolder
            property var selectedFiles: []
            property var nameFilters: []
            property int fileMode: -1
            property string defaultSuffix: ""
            property Item sourceItem: null
            property string mode: ""
            property string acceptText: ""
            property string rejectText: ""
            property string locationPlaceholder: ""
            property string fileNamePlaceholder: ""
            property string unavailableText: ""
            property string emptyText: ""
            property string overwriteTitle: ""
            property string overwriteText: ""
            property string overwriteAcceptText: ""
            signal accepted()
            signal rejected()
            signal closed()
            function open() { visible = true }
            function reject() { visible = false; rejected() }
            function close() { reject() }
            function chooseFiles(values) {
                selectedFiles = values; selectedFile = values[0]
                visible = false; accepted()
            }
            function chooseFolder(value) {
                selectedFolder = value; visible = false; accepted()
            }
        }
    }
    Component {
        id: unavailableComponent
        QtObject {
            property bool visible: false
            property var parentWindow: null
            property int modality: Qt.WindowModal
            property string title: ""
            property string acceptLabel: ""
            property string rejectLabel: ""
            property url currentFolder
            property var nameFilters: []
            property int fileMode: -1
            property string defaultSuffix: ""
            function open() {}
            function close() {}
            function reject() {}
        }
    }
    SignalSpy { id: acceptedSpy; signalName: "accepted" }
    SignalSpy { id: rejectedSpy; signalName: "rejected" }
    SignalSpy { id: failedSpy; signalName: "failed" }
    property var picker: null
    function compareUrl(actual, expected) {
        // QUrl displays Unicode and spaces decoded, but keeps reserved #/%
        // escaped. Compare normalized URLs without decoding path delimiters.
        compare(String(Qt.resolvedUrl(actual)), String(Qt.resolvedUrl(expected)))
    }
    function makePicker(properties) {
        const options = Object.assign({systemFileDialogs: true, _fileComponent: dialogComponent,
            _folderComponent: dialogComponent, _localComponent: dialogComponent,
            currentFolder: "file:///home/example"}, properties || {})
        picker = createTemporaryObject(pickerComponent, scene, options)
        verify(picker !== null)
        acceptedSpy.target = picker; rejectedSpy.target = picker; failedSpy.target = picker
        return picker
    }
    function cleanup() {
        if (picker) picker.close()
        picker = null
        acceptedSpy.target = null; rejectedSpy.target = null; failedSpy.target = null
        acceptedSpy.clear(); rejectedSpy.clear(); failedSpy.clear()
    }
    function test_system_mode_data() {
        return [
            {tag: "single", mode: "openFile", fileMode: SystemDialogs.FileDialog.OpenFile},
            {tag: "multiple", mode: "openFiles", fileMode: SystemDialogs.FileDialog.OpenFiles},
            {tag: "save", mode: "saveFile", fileMode: SystemDialogs.FileDialog.SaveFile}
        ]
    }
    function test_system_mode(data) {
        makePicker({mode: data.mode, title: "选择笔记", acceptText: "选择", rejectText: "取消",
            nameFilters: ["*.md", "*.MD"]})
        picker.open()
        verify(picker.visible)
        const dialog = picker._backend
        compare(dialog.fileMode, data.fileMode)
        compare(dialog.parentWindow, scene.Window.window)
        compare(dialog.modality, Qt.WindowModal)
        compare(dialog.title, "选择笔记")
        compare(dialog.acceptLabel, "选择"); compare(dialog.rejectLabel, "取消")
        compare(dialog.nameFilters.length, 1)
        verify(dialog.nameFilters[0].endsWith("(*.md *.MD)"))
        picker.open()
        compare(picker._backend, dialog)
        const first = "file:///run/user/1000/doc/abcd/%E7%AC%94%E8%AE%B0%20%23%25.md"
        const second = "file:///run/user/1000/doc/efgh/B.MD"
        let activeDuringAccept = false
        picker.accepted.connect(function() { activeDuringAccept = picker.visible })
        dialog.chooseFiles(data.mode === "openFiles" ? [first, second] : [first])
        compare(acceptedSpy.count, 1); compare(rejectedSpy.count, 0)
        verify(activeDuringAccept)
        compareUrl(picker.selectedFile, first)
        verify(String(picker.selectedFile).endsWith("%23%25.md"))
        compare(picker.selectedFiles.length, data.mode === "openFiles" ? 2 : 1)
        compareUrl(picker.selectedFiles[0], first)
        verify(String(picker.selectedFiles[0]).endsWith("%23%25.md"))
        if (data.mode === "openFiles") compareUrl(picker.selectedFiles[1], second)
        compareUrl(picker.currentFolder, "file:///run/user/1000/doc/abcd/")
        verify(!picker.visible)
    }
    function test_directory_uses_folder_result() {
        makePicker({mode: "folder", _fileComponent: null})
        picker.open()
        verify(picker.visible)
        const directory = "file:///run/user/1000/doc/abcd/%E6%96%87%E4%BB%B6%E5%A4%B9%20%23%25"
        picker._backend.chooseFolder(directory)
        compare(acceptedSpy.count, 1)
        compareUrl(picker.selectedFolder, directory)
        compareUrl(picker.currentFolder, directory)
        verify(String(picker.selectedFolder).endsWith("%23%25"))
        compare(picker.selectedFiles.length, 0)
    }
    function test_prefill_and_repeated_save_keep_exact_grant() {
        makePicker({mode: "saveFile", defaultSuffix: "pdf", selectedFile: "file:///home/example/first"})
        picker.open()
        compare(String(picker._backend.selectedFile), "file:///home/example/first.pdf")
        compare(picker._backend.defaultSuffix, "")
        const first = "file:///run/user/1000/doc/abcd/first.pdf"
        picker._backend.chooseFiles([first])
        compare(String(picker.selectedFile), first)
        picker.selectedFile = "file:///run/user/1000/doc/abcd/second.pdf"
        picker.open()
        compare(String(picker._backend.selectedFile), "file:///run/user/1000/doc/abcd/second.pdf")
        picker._backend.chooseFiles(["file:///run/user/1000/doc/efgh/second.PDF"])
        compare(String(picker.selectedFile), "file:///run/user/1000/doc/efgh/second.PDF")
        compare(acceptedSpy.count, 2); compare(failedSpy.count, 0)
    }
    function test_save_never_appends_to_returned_url_data() {
        return [{tag: "missing", name: "report"}, {tag: "different", name: "report.txt"}]
    }
    function test_save_never_appends_to_returned_url(data) {
        makePicker({mode: "saveFile", defaultSuffix: "nkbackup", selectedFile: "file:///home/example/proposed.nkbackup"})
        picker.open()
        picker._backend.chooseFiles(["file:///run/user/1000/doc/abcd/" + data.name])
        compare(acceptedSpy.count, 0); compare(rejectedSpy.count, 1); compare(failedSpy.count, 1)
        verify(failedSpy.signalArguments[0][0].indexOf(".nkbackup") >= 0)
        compare(String(picker.selectedFile), "file:///home/example/proposed.nkbackup")
        verify(!picker.visible)
    }
    function test_cancel_does_not_publish_transient_selection() {
        makePicker({selectedFile: "file:///home/example/previous.md", selectedFiles: ["file:///home/example/previous.md"]})
        picker.open()
        picker._backend.selectedFile = "file:///run/user/1000/doc/abcd/transient.md"
        picker._backend.selectedFiles = ["file:///run/user/1000/doc/abcd/transient.md"]
        picker.close(); picker.close()
        compare(rejectedSpy.count, 1); compare(acceptedSpy.count, 0); compare(failedSpy.count, 0)
        compare(String(picker.selectedFile), "file:///home/example/previous.md")
        compare(picker.selectedFiles[0], "file:///home/example/previous.md")
        verify(!picker.visible)
    }
    function test_hidden_without_result_rejects_once() {
        makePicker()
        picker.open()
        picker._backend.visible = false
        tryCompare(rejectedSpy, "count", 1)
        compare(acceptedSpy.count, 0)
        compare(failedSpy.count, 0)
        verify(!picker.visible)
    }
    function test_retired_backend_cannot_complete_next_request() {
        makePicker()
        picker.open()
        const retired = picker._backend
        picker.reject()
        compare(rejectedSpy.count, 1)
        picker.open()
        const active = picker._backend
        verify(active !== retired)
        // destroy() is deferred. Old backend signals emitted before deletion
        // must not be attributed to the new, already-open request.
        retired.selectedFile = "file:///run/user/1000/doc/old/stale.md"
        retired.accepted()
        retired.rejected()
        retired.closed()
        compare(acceptedSpy.count, 0)
        compare(rejectedSpy.count, 1)
        compare(failedSpy.count, 0)
        verify(picker.visible)
        // Let the old dialog's already-queued hidden callback run as well.
        wait(0)
        compare(picker._backend, active)
        compare(acceptedSpy.count, 0)
        compare(rejectedSpy.count, 1)
        verify(picker.visible)
        active.chooseFiles(["file:///run/user/1000/doc/new/current.md"])
        compare(acceptedSpy.count, 1)
        compare(rejectedSpy.count, 1)
        compare(failedSpy.count, 0)
        compareUrl(picker.selectedFile, "file:///run/user/1000/doc/new/current.md")
    }
    function test_visible_request_keeps_its_mode_and_suffix() {
        makePicker({mode: "saveFile", defaultSuffix: "pdf", selectedFile: "file:///home/example/note.pdf"})
        picker.open()
        picker.mode = "folder"; picker.defaultSuffix = "html"
        picker._backend.chooseFiles(["file:///run/user/1000/doc/abcd/note.pdf"])
        compare(acceptedSpy.count, 1); compare(failedSpy.count, 0)
        compare(String(picker.selectedFile), "file:///run/user/1000/doc/abcd/note.pdf")
    }
    function test_invalid_multi_selection_is_rejected_as_a_whole() {
        makePicker({mode: "openFiles"})
        picker.open()
        picker._backend.chooseFiles(["file:///run/user/1000/doc/abcd/a.md", "https://example.invalid/b.md"])
        compare(acceptedSpy.count, 0); compare(rejectedSpy.count, 1); compare(failedSpy.count, 1)
        compare(picker.selectedFiles.length, 0)
    }
    function test_open_failure_resets_popup_state() {
        makePicker({_fileComponent: unavailableComponent})
        picker.open()
        tryCompare(failedSpy, "count", 1)
        compare(rejectedSpy.count, 1); compare(acceptedSpy.count, 0)
        verify(!picker.visible)
    }
    function test_local_backend_and_exit_transition() {
        makePicker({systemFileDialogs: false, mode: "saveFile", defaultSuffix: "html",
            selectedFile: "file:///home/example/note.html", overwriteTitle: "替换", overwriteText: "同名文件"})
        picker.open()
        const dialog = picker._backend
        compare(dialog.mode, "saveFile"); compare(dialog.defaultSuffix, "html")
        compare(dialog.nameFilters[0], "*"); compare(dialog.sourceItem, scene)
        compare(dialog.overwriteTitle, "替换"); compare(dialog.overwriteText, "同名文件")
        dialog.selectedFile = "file:///home/example/note.html"
        // LiquidDialog emits acceptance before its exit transition is finished.
        dialog.accepted()
        compare(acceptedSpy.count, 1)
        verify(picker.visible)
        dialog.visible = false; dialog.closed()
        verify(!picker.visible)
        compare(rejectedSpy.count, 0)
    }
    function test_real_local_component_opens_and_cancels() {
        picker = createTemporaryObject(pickerComponent, scene, {systemFileDialogs: false})
        verify(picker !== null)
        rejectedSpy.target = picker
        picker.open()
        verify(picker.visible)
        picker.reject()
        tryCompare(picker, "visible", false)
        compare(rejectedSpy.count, 1)
    }
}
