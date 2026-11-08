import QtQuick
QtObject {
    required property var session
    required property var editor
    required property var controller
    property bool historyInputFocused: false
    property bool blocked: false
    signal sidebarRequested()
    signal searchRequested()
    signal findRequested()
    signal replaceRequested()
    signal newNoteRequested()
    readonly property bool editable: editor.activeFocus && !session.composing && !controller.readOnly && !blocked
    readonly property bool globalEnabled: !blocked && !session.composing && !controller.busy
    function run(command) { if (!controller.readOnly && !session.composing) session.execute(command) }
    property list<QtObject> shortcuts: [
        Shortcut { sequence: "Alt+Left"; enabled: globalEnabled && controller.canGoBack; onActivated: controller.navigateBack() },
        Shortcut { sequence: "Alt+Right"; enabled: globalEnabled && controller.canGoForward; onActivated: controller.navigateForward() },
        Shortcut { sequence: "Ctrl+B"; enabled: editable; onActivated: run("bold") },
        Shortcut { sequence: "Ctrl+I"; enabled: editable; onActivated: run("italic") },
        Shortcut { sequence: "Ctrl+Shift+X"; enabled: editable; onActivated: run("strike") },
        Shortcut { sequence: "Ctrl+E"; enabled: editable; onActivated: run("code") },
        Shortcut { sequences: [StandardKey.Undo]; enabled: globalEnabled && !historyInputFocused && controller.canUndo; onActivated: controller.undo() },
        Shortcut { sequences: [StandardKey.Redo]; enabled: globalEnabled && !historyInputFocused && controller.canRedo; onActivated: controller.redo() },
        Shortcut { sequence: "Ctrl+N"; enabled: globalEnabled && !!controller.library; onActivated: newNoteRequested() },
        Shortcut { sequence: "Ctrl+S"; enabled: globalEnabled; onActivated: controller.save() },
        Shortcut { sequence: "Ctrl+H"; enabled: editable; onActivated: replaceRequested() },
        Shortcut { sequence: "Ctrl+F"; enabled: editable; onActivated: findRequested() },
        Shortcut { sequence: "Ctrl+Shift+F"; enabled: globalEnabled; onActivated: searchRequested() },
        Shortcut { sequence: "Ctrl+Shift+B"; enabled: globalEnabled; onActivated: sidebarRequested() }
    ]
}
