import QtQuick
QtObject {
    required property var session
    required property var editor
    signal sidebarRequested()
    readonly property bool editable: editor.activeFocus && !session.composing
    function run(command) { session.execute(command) }
    property list<QtObject> shortcuts: [
        Shortcut { sequence: "Ctrl+B"; enabled: editable; onActivated: run("bold") },
        Shortcut { sequence: "Ctrl+I"; enabled: editable; onActivated: run("italic") },
        Shortcut { sequence: "Ctrl+Shift+X"; enabled: editable; onActivated: run("strike") },
        Shortcut { sequence: "Ctrl+E"; enabled: editable; onActivated: run("code") },
        Shortcut { sequences: [StandardKey.Undo]; enabled: editable; onActivated: session.undo() },
        Shortcut { sequences: [StandardKey.Redo]; enabled: editable; onActivated: session.redo() },
        Shortcut { sequence: "Ctrl+Shift+B"; onActivated: sidebarRequested() }
    ]
}
