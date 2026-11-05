import QtQuick
import QtQuick.Window
import QtWebEngine
import QtTest
import NextNotes.Editor 1.0

TestCase {
    id: testCase
    name: "EditorSurface"
    when: windowShown
    visible: true
    width: 640; height: 480
    SignalSpy { id: stopSpy; signalName: "sceneGraphAboutToStop" }
    Component { id: hostComponent; Window { width: 640; height: 480; visible: true } }
    Component {
        id: viewComponent
        WebEngineView {
            id: browser
            width: 640; height: 480
            readonly property bool retainedSurface: lifecycle.attached
            EditorRenderLifecycle { id: lifecycle; view: browser }
        }
    }
    function inspect(view, expression) {
        let complete = false
        let result
        view.runJavaScript(expression, value => { result = value; complete = true })
        tryVerify(() => complete, 10000)
        return result
    }
    function test_document_survives_window_lifecycle() {
        const host = createTemporaryObject(hostComponent, null)
        stopSpy.target = host
        stopSpy.clear()
        const view = createTemporaryObject(viewComponent, host.contentItem)
        view.loadHtml('<body style="background:white"><textarea id="draft">Original draft</textarea><div style="height:6000px">Synthetic note</div></body>')
        tryCompare(view, "loadProgress", 100, 20000)
        tryCompare(view, "retainedSurface", true)
        verify(waitForRendering(view))
        tryVerify(() => inspect(view, 'window.innerHeight >= 400 && document.documentElement.scrollHeight > 5000'))
        compare(inspect(view, 'window.surfaceMarker = 42; const e = document.getElementById("draft"); e.focus(); document.execCommand("insertText", false, "unsaved "); e.setSelectionRange(2, 5, "backward"); window.scrollTo(0, 700); e.value'), "unsaved Original draft")
        tryVerify(() => view.scrollPosition.y >= 699)
        const state = 'JSON.stringify([window.surfaceMarker, draft.value, draft.selectionStart, draft.selectionEnd, draft.selectionDirection])'
        const before = inspect(view, state)
        for (let cycle = 0; cycle < 6; ++cycle) {
            if (cycle % 2) host.hide()
            else host.showMinimized()
            wait(180)
            host.showNormal()
            wait(250)
            verify(view.retainedSurface)
            compare(inspect(view, state), before) // No reload or loss of unsaved state.
            tryVerify(() => view.scrollPosition.y >= 699)
        }
        verify(stopSpy.count > 0, "The VM must exercise an actual render-loop pause")
        const other = createTemporaryObject(hostComponent, null)
        view.parent = other.contentItem
        tryCompare(view, "retainedSurface", true)
        compare(inspect(view, state), before)
        compare(inspect(view, 'draft.focus(); document.execCommand("undo"); draft.value'), "Original draft")
        compare(inspect(view, 'document.execCommand("redo"); draft.value'), "unsaved Original draft")
        // Replacing a document still works; retention is only a graphics lifetime change.
        view.loadHtml('<body style="background:white"><p id="replacement">Replacement note</p></body>')
        tryVerify(() => inspect(view, '!!document.getElementById("replacement")'), 10000)
        verify(view.retainedSurface)
        compare(inspect(view, 'typeof window.surfaceMarker'), "undefined")
        other.close()
        host.close()
    }
}
