import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine
import Kos.Ui
ApplicationWindow {
 id: root
 visible: true; width: 1100; height: 740; minimumWidth: 640; minimumHeight: 480
 title: "M2a editor comparison — pending VM acceptance"
 color: AppTheme.window
 property bool embedded: false
 property bool live: true
 property bool webReady: false
 property int savedAnchor: 0
 property int savedHead: 0
 property real editorSize: hostFontPixelSize
 readonly property var themeTokens: ({background:css(AppTheme.window),text:css(AppTheme.text),
  muted:css(AppTheme.mutedText),border:css(AppTheme.border),selection:css(AppTheme.accent),
  selectionText:css(AppTheme.accentText),dark:AppTheme.dark,family:Application.font.family,size:editorSize})
 function css(c) {return "rgba("+Math.round(c.r*255)+","+Math.round(c.g*255)+","+Math.round(c.b*255)+","+c.a+")"}
 onThemeTokensChanged: if(webReady) web.runJavaScript("window.setTheme("+JSON.stringify(themeTokens)+")")
 function focusEditor() {
  if(embedded) {web.forceActiveFocus(Qt.OtherFocusReason);web.runJavaScript("window.focusEditor()")}
  else text.forceActiveFocus(Qt.OtherFocusReason)
 }
 function command(name, done) {
  const entry=commandDefinitions.find(c=>c.id===name)
  if(!entry) {if(done)done(false);return}
  if(embedded) web.runJavaScript("window.command("+JSON.stringify(name)+")", ok=>{if(ok)focusEditor();if(done)done(ok)})
  else {
   const ok=!text.inputMethodComposing
   if(ok) {nativeSession.command(name,savedAnchor,savedHead,entry.marker||"");focusEditor()}
   if(done)done(ok)
  }
 }
 function setMode(value,done) {
  // Native preedit must not be rehighlighted. Web defers inside its composition lifecycle.
  if(!embedded && text.inputMethodComposing){if(done)done(false);return}
  if(embedded) web.runJavaScript("window.setLive("+value+")",ok=>{
   live=value
   if(ok)focusEditor()
   if(done)done(ok)
  })
  else {live=value;nativeSession.mode(live);focusEditor();if(done)done(true)}
 }
 function switchEditor() {
  // Do not hide a native preedit. Web checks its own composing state before switching.
  if(!embedded) {if(text.inputMethodComposing)return;embedded=true;web.runJavaScript("window.setLive("+live+")");focusEditor()}
  else web.runJavaScript("window.snapshot().composing",busy=>{
   if(busy)return;embedded=false;nativeSession.mode(live);focusEditor()
  })
 }
 function nativeSnapshot() {return {source:text.inputMethodComposing?null:nativeSession.source(),
  composing:text.inputMethodComposing,anchor:savedAnchor,head:savedHead,scroll:scroll.contentItem.contentY,live:live}}
 function inspect() {
  if(embedded)web.runJavaScript("window.snapshot()",v=>console.log(JSON.stringify(v)))
  else console.log(JSON.stringify(nativeSnapshot()))
 }
 // Harness only: a same-fixture load is one undoable replacement, never a state reset.
 function loadSample(large,done) {
  const source=large?fixture.repeat(Math.ceil(1048576/fixtureUtf8Bytes)):fixture
  if(embedded)web.runJavaScript("window.qa.load("+JSON.stringify(source)+")",ok=>{if(ok)focusEditor();if(done)done(ok)})
  else {const ok=!text.inputMethodComposing;if(ok){nativeSession.load(source);focusEditor()}if(done)done(ok)}
 }
 // A single QA bridge enables CDP automation of BOTH panes without AT-SPI tree scans.
 // Endpoint is available only inside this offline development prototype.
 property var measurement: null
 function qaReply(id,value) {web.runJavaScript("window.qaResults["+JSON.stringify(id)+"]="+JSON.stringify(value))}
 function qaRequest(q) {
  if(measurement) {qaReply(q.id,{error:"measurement busy"});return}
  if(q.op==="snapshot") {
   if(embedded)web.runJavaScript("window.snapshot()",v=>qaReply(q.id,v))
   else qaReply(q.id,nativeSnapshot())
   return
  }
  if(q.op==="select") {
   if(embedded)web.runJavaScript("window.qa.select("+JSON.stringify(q.anchor)+","+JSON.stringify(q.head)+","+JSON.stringify(q.scroll)+")",ok=>qaReply(q.id,{ok:ok}))
   else {
    const ok=!text.inputMethodComposing && q.anchor>=0 && q.head>=0 && q.anchor<=text.length && q.head<=text.length
    if(ok){text.select(q.anchor,q.head);if(q.scroll!==undefined)scroll.contentItem.contentY=q.scroll}
    qaReply(q.id,{ok:ok})
   }
   return
  }
  if(q.op==="pane") {
   web.runJavaScript("window.snapshot().composing",busy=>{
    if(busy||text.inputMethodComposing){qaReply(q.id,{ok:false});return}
    embedded=q.web===true
    if(embedded)web.runJavaScript("window.setLive("+live+")")
    else nativeSession.mode(live)
    focusEditor();qaReply(q.id,{ok:true})
   });return
  }
  if(q.op==="theme") {AppTheme.appearanceMode=q.dark?"dark":"light";qaReply(q.id,{ok:true});return}
  if(q.op==="measure") {
   measurement={id:q.id,operation:q.operation,count:Math.max(1,Math.min(20,q.count||5)),samples:[],web:embedded}
   measureNext();return
  }
  qaReply(q.id,{error:"unknown operation"})
 }
 function measureNext() {
  const m=measurement;m.start=nativeSession.now()
  const done=ok=>{
   if(!ok){qaReply(m.id,{error:"operation refused (composition or unknown command)"});measurement=null;return}
   if(embedded)web.runJavaScript("window.qa.renderReady()")
   else Qt.callLater(captureMeasurement)
  }
  if(m.operation==="load-large")loadSample(true,done)
  else if(m.operation==="load-fixture")loadSample(false,done)
  else if(m.operation==="mode")setMode(!live,done)
  else command(m.operation,done)
 }
 function captureMeasurement() {
  if(!measurement)return
  // Same end boundary for both panes: asynchronous Qt scene capture completion.
  // Includes capture + bridge overhead; this is NOT physical presentation latency.
  const accepted=editorPane.grabToImage(result=>{
   const m=measurement;if(!m)return
   m.samples.push(nativeSession.now()-m.start)
   if(m.samples.length<m.count){Qt.callLater(measureNext);return}
   const sorted=m.samples.slice().sort((a,b)=>a-b)
   const report={engine:m.web?"web":"native",operation:m.operation,n:m.count,samplesMs:m.samples,
    p50Ms:sorted[Math.ceil(sorted.length*0.5)-1],p95Ms:sorted[Math.ceil(sorted.length*0.95)-1],
    boundary:"host dispatch → engine layout checkpoint → Qt grabToImage completion (bridge/capture included; not presentation)"}
   qaReply(m.id,report);console.log("QA_MEASURE "+JSON.stringify(report));measurement=null
  })
  if(!accepted){qaReply(measurement.id,{error:"grabToImage refused"});measurement=null}
 }
 ColumnLayout {
  anchors.fill: parent
  Flow {
   Layout.fillWidth: true; implicitHeight: childrenRect.height; spacing: 4
   KosButton { text: root.embedded ? "CodeMirror 6" : "Native Qt"; onClicked: root.switchEditor() }
   KosButton { text: root.live ? "Instant" : "Source"; onClicked: root.setMode(!root.live) }
   Repeater {
    model: commandDefinitions
    KosButton { required property var modelData; text: modelData.id; onClicked: root.command(modelData.id) }
   }
   KosButton { text: "Inspect source"; onClicked: root.inspect() }
   KosButton { text: "Load fixture"; onClicked: root.loadSample(false) }
   KosButton { text: "Load ~1 MB"; onClicked: root.loadSample(true) }
   KosButton { text: AppTheme.dark?"Light":"Dark"; onClicked: AppTheme.appearanceMode=AppTheme.dark?"light":"dark" }
   KosButton { text: "A−"; onClicked: root.editorSize=Math.max(10,root.editorSize-1) }
   KosButton { text: "A+"; onClicked: root.editorSize=Math.min(32,root.editorSize+1) }
  }
  Label { Layout.fillWidth:true; wrapMode: Text.Wrap; text: "Independent buffers / undo. Native: limited highlighting; Web: syntax decorations. Acceptance pending VM."; color: AppTheme.text }
  Item {
   id: editorPane
   Layout.fillWidth: true; Layout.fillHeight: true
   ScrollView {
    id: scroll; anchors.fill: parent; visible: !root.embedded
    TextArea {
     id: text; wrapMode: TextEdit.Wrap; textFormat: TextEdit.PlainText
     color: AppTheme.text; selectionColor:AppTheme.accent; selectedTextColor:AppTheme.accentText
     background: Rectangle { color: AppTheme.window }
     font.family:Application.font.family; font.pixelSize:root.editorSize
     selectByMouse: true; persistentSelection: true
     Component.onCompleted: nativeSession.attach(textDocument,fixture)
     onCursorPositionChanged: remember()
     onSelectionStartChanged: remember()
     onSelectionEndChanged: remember()
     function remember() {root.savedHead=cursorPosition;root.savedAnchor=cursorPosition===selectionStart?selectionEnd:selectionStart}
     Keys.onPressed: event => {
      if(event.modifiers & Qt.ControlModifier) {
       const shift=!!(event.modifiers & Qt.ShiftModifier)
       const c=commandDefinitions.find(c=>(c.qtKey===event.key&&c.shift===shift)||(c.alternateQtKey===event.key&&!shift))
       if(c){root.command(c.id);event.accepted=true}
      }
     }
    }
   }
   WebEngineView {
    id: web; anchors.fill: parent; visible: root.embedded; url: "qrc:/editor/web/index.html"
    settings.localContentCanAccessRemoteUrls: false
    settings.localContentCanAccessFileUrls: false
    settings.javascriptCanOpenWindows: false
    onLoadingChanged: request => { if(request.status===WebEngineView.LoadSucceededStatus) {
     web.runJavaScript("window.boot("+JSON.stringify(fixture)+","+root.live+","+JSON.stringify(root.themeTokens)+","+JSON.stringify(commandDefinitions)+")",()=>{root.webReady=true})
    }}
    onJavaScriptConsoleMessage: (level,message,lineNumber,sourceId)=>{
     if(message.startsWith("QA_REQUEST ")) root.qaRequest(JSON.parse(message.slice(11)))
     else if(message==="QA_RENDER_READY") root.captureMeasurement()
     else console.log("WEB "+message+" ("+sourceId+":"+lineNumber+")")
    }
    onNavigationRequested: request => { if(request.url.toString()!=="qrc:/editor/web/index.html") request.action=WebEngineNavigationRequest.IgnoreRequest }
    onNewWindowRequested: request => {}
   }
  }
 }
 Connections { target: nativeSession; function onSelection(anchor,head) {text.select(anchor,head);root.savedAnchor=anchor;root.savedHead=head} }
}
