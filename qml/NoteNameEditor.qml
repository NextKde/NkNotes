import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kos.Ui

Item {
    id: root
    required property var controller
    property bool canRename: false
    readonly property string documentKey: controller.library + "\n" + controller.path
    property var drafts: ({})
    property string savedName: ""
    readonly property bool editing: nameInput.activeFocus
    readonly property bool modified: nameInput.text.trim() !== savedName
    NotesTokens { id: tokens }
    implicitHeight: nameColumn.implicitHeight
    signal submitted()

    function synchronize() {
        if (!controller.path || controller.externalDocument) return
        savedName = controller.noteTitle
        let draft = drafts[documentKey]
        if (draft !== undefined && draft.trim() === savedName && !nameInput.activeFocus) {
            delete drafts[documentKey]
            draft = undefined
        }
        const value = draft === undefined ? savedName : draft
        if (nameInput.text !== value) nameInput.text = value
    }
    function edit() { nameInput.forceActiveFocus(Qt.OtherFocusReason); nameInput.selectAll() }
    function submit() {
        const value = nameInput.text.trim()
        if (!canRename || controller.busy || nameInput.inputMethodComposing || !value) return
        if (value !== savedName) controller.renameNote(value)
        else { delete drafts[documentKey]; nameInput.text = savedName }
        submitted()
    }
    onDocumentKeyChanged: Qt.callLater(synchronize)
    Component.onCompleted: synchronize()
    Connections { target: root.controller; function onChanged() { root.synchronize() } }

    ColumnLayout {
        id: nameColumn
        anchors.left: parent.left; anchors.right: parent.right
        spacing: 4
        Label { text: "笔记名称"; color: AppTheme.mutedText; font.pixelSize: 11 }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            TextField {
                id: nameInput
                objectName: "noteNameInput"
                Layout.fillWidth: true; Layout.minimumWidth: 0
                implicitHeight: 38
                leftPadding: 0; rightPadding: 0; topPadding: 4; bottomPadding: 4
                font.pixelSize: 24; font.weight: Font.DemiBold
                color: AppTheme.text; placeholderTextColor: AppTheme.mutedText
                selectionColor: tokens.primaryYellow; selectedTextColor: "#222222"
                background: null
                placeholderText: "未命名笔记"
                Accessible.name: "笔记名称"
                maximumLength: 300; selectByMouse: true
                readOnly: !root.canRename || root.controller.busy
                onTextEdited: root.drafts[root.documentKey] = text
                onActiveFocusChanged: if (!activeFocus) root.synchronize()
                onAccepted: root.submit()
                Keys.onEscapePressed: function(event) {
                    if (inputMethodComposing) return
                    delete root.drafts[root.documentKey]
                    text = root.savedName
                    root.submitted()
                    event.accepted = true
                }
            }
            LiquidActionButton {
                objectName: "saveNoteNameButton"
                visible: root.modified
                implicitHeight: 26; materialForm: true
                text: "保存名称"
                enabled: root.canRename && !root.controller.busy && !!nameInput.text.trim() && !nameInput.inputMethodComposing
                onClicked: root.submit()
            }
        }
    }
}
