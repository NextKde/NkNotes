import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine
import Kos.Ui
ApplicationWindow {
 id: root
 visible: true; width: 1100; height: 740; title: "M2a editor comparison — unvalidated"
 color: AppTheme.window
 property bool embedded: false
 property bool live: true
 property int savedAnchor: 0
 property int savedHead: 0
 function command(name) {
  if (embedded) web.runJavaScript("window.command(" + JSON.stringify(name) + ")")
  else if (!text.inputMethodComposing) { nativeSession.command(name,savedAnchor,savedHead); text.forceActiveFocus() }
 }
 ColumnLayout {
  anchors.fill: parent
  RowLayout {
   KosButton { text: root.embedded ? "CodeMirror 6" : "Native Qt"; onClicked: root.embedded = !root.embedded }
   KosButton { text: root.live ? "Instant" : "Source"; onClicked: {
    root.live = !root.live
    nativeSession.mode(root.live)
    web.runJavaScript("window.setLive(" + root.live + ")")
   }}
   Repeater {
    model: ["bold","italic","strike","code","undo","redo"]
    KosButton { required property string modelData; text: modelData; onClicked: root.command(modelData) }
   }
   KosButton { text: "Inspect source"; onClicked: {
    if(root.embedded) web.runJavaScript("window.snapshot()", function(v) { console.log(JSON.stringify(v)) })
    else console.log(JSON.stringify({source:nativeSession.source(),anchor:root.savedAnchor,head:root.savedHead,scroll:scroll.contentItem.contentY}))
   }}
  }
  Label { text: "Independent buffers; same fixture. Native: formatting only. Web: syntax decorations. All acceptance pending VM."; color: AppTheme.text }
  Item {
   Layout.fillWidth: true; Layout.fillHeight: true
   ScrollView {
    id: scroll; anchors.fill: parent; visible: !root.embedded
    TextArea {
     id: text; wrapMode: TextEdit.Wrap; textFormat: TextEdit.PlainText
     color: AppTheme.text; background: Rectangle { color: AppTheme.window }
     font.pixelSize: 16; selectByMouse: true; persistentSelection: true
     Component.onCompleted: nativeSession.attach(textDocument,fixture)
     onCursorPositionChanged: remember()
     onSelectionStartChanged: remember()
     onSelectionEndChanged: remember()
     function remember() { root.savedHead=cursorPosition;root.savedAnchor=cursorPosition===selectionStart ? selectionEnd : selectionStart }
     Keys.onPressed: event => {
      if(event.modifiers & Qt.ControlModifier) {
       const commands = ({66:"bold",73:"italic",90:event.modifiers & Qt.ShiftModifier ? "redo" : "undo",89:"redo"})
       if(commands[event.key]) {root.command(commands[event.key]);event.accepted=true}
      }
     }
    }
   }
   WebEngineView {
    id: web; anchors.fill: parent; visible: root.embedded; url: "qrc:/editor/web/index.html"
    settings.localContentCanAccessRemoteUrls: false
    settings.localContentCanAccessFileUrls: false
    settings.javascriptCanOpenWindows: false
    onLoadingChanged: request => { if(request.status===WebEngineView.LoadSucceededStatus) web.runJavaScript("window.boot("+JSON.stringify(fixture)+","+root.live+")") }
    onNavigationRequested: request => { if(request.url.toString()!=="qrc:/editor/web/index.html") request.action=WebEngineNavigationRequest.IgnoreRequest }
    onNewWindowRequested: request => {}
   }
  }
 }
 Connections { target: nativeSession; function onSelection(anchor,head) {text.select(anchor,head);root.savedAnchor=anchor;root.savedHead=head} }
}
