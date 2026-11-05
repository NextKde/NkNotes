import {EditorState, Compartment, EditorSelection, StateEffect} from "@codemirror/state";
import {EditorView, Decoration, ViewPlugin, keymap} from "@codemirror/view";
import {history, historyKeymap, undo, redo, isolateHistory} from "@codemirror/commands";
import {markdown} from "@codemirror/lang-markdown";
import {GFM} from "@lezer/markdown";
import {syntaxTree} from "@codemirror/language";
let view, commandTable=[], pendingLive=null, liveMode=true, compositionActive=false;
let settledSource="", settleTimer;
const mode = new Compartment(), theme = new Compartment(), refresh = StateEffect.define();
const composing=()=>compositionActive || !!view?.composing;
function decorations(v) {
 const spans=[], selections=v.state.selection.ranges;
 for(const range of v.visibleRanges) syntaxTree(v.state).iterate({
  from:range.from,to:range.to,enter(n) {
   const cls=({StrongEmphasis:"strong",Emphasis:"em",Strikethrough:"strike",ATXHeading1:"heading",ATXHeading2:"heading"})[n.name];
   if(cls) spans.push(Decoration.mark({class:cls}).range(n.from,n.to));
   if(["EmphasisMark","HeaderMark","StrikethroughMark","CodeMark"].includes(n.name)) {
    const line=v.state.doc.lineAt(n.from);
    if(!selections.some(s=>s.from<=line.to && s.to>=line.from))
     spans.push(Decoration.replace({}).range(n.from,n.to));
   }
  }
 });
 return Decoration.set(spans,true);
}
const livePlugin=ViewPlugin.fromClass(class {
 constructor(v){this.decorations=decorations(v)}
 update(u){
  // Even while DOM composition owns the content, ranges MUST follow every change.
  this.decorations=this.decorations.map(u.changes);
  if(!composing() && (u.docChanged||u.selectionSet||u.viewportChanged||
      u.transactions.some(t=>t.effects.some(e=>e.is(refresh)))))
   this.decorations=decorations(u.view);
 }
},{decorations:p=>p.decorations});
function settleComposition(){
 clearTimeout(settleTimer);
 settleTimer=setTimeout(()=>{
  // CodeMirror clears its composing flag after the final browser input event.
  if(composing()){settleComposition();return;}
  settledSource=view.state.doc.toString();
  view.dispatch({effects:refresh.of(null)});
  if(pendingLive!==null){const next=pendingLive;pendingLive=null;window.setLive(next);}
 },30);
}
window.setLive=live=>{
 if(!view)return false;
 if(composing()){pendingLive=live;return false;}
 const top=view.scrollDOM.scrollTop;
 view.dispatch({effects:mode.reconfigure(live?[livePlugin]:[])});
 liveMode=live;
 requestAnimationFrame(()=>{view.scrollDOM.scrollTop=top});
 return true;
};
window.command=name=>{
 if(!view||composing())return false;
 const entry=commandTable.find(c=>c.id===name);
 if(!entry)return false;
 if(name==="undo")undo(view);
 else if(name==="redo")redo(view);
 else {
  const m=entry.marker;
  if(!m)return false;
  view.dispatch(view.state.changeByRange(r=>({
   changes:r.empty ? [{from:r.from,insert:m+m}] : [{from:r.from,insert:m},{from:r.to,insert:m}],
   range:EditorSelection.range(r.anchor+m.length,r.head+m.length)
  })),{annotations:isolateHistory.of("full"),userEvent:"input.format"});
 }
 view.focus();return true;
};
window.focusEditor=()=>{if(view)view.focus()};
window.snapshot=()=>({
 // During preedit source is explicitly unavailable, not a saveable draft.
 source:composing()?null:view.state.doc.toString(),
 committedSource:composing()?settledSource:view.state.doc.toString(),
 draftSource:view.state.doc.toString(),composing:composing(),live:liveMode,pendingLive,
 ranges:view.state.selection.ranges.map(r=>({anchor:r.anchor,head:r.head})),scroll:view.scrollDOM.scrollTop
});
function hostTheme(t){
 return EditorView.theme({
  "&":{height:"100%",color:t.text,backgroundColor:t.background,fontFamily:JSON.stringify(t.family),fontSize:t.size+"px"},
  ".cm-scroller":{overflow:"auto",fontFamily:"inherit",fontSize:"inherit"},
  ".cm-content":{caretColor:t.text},
  ".cm-cursor, .cm-dropCursor":{borderLeftColor:t.text},
  ".cm-content ::selection, &.cm-focused .cm-content ::selection":{backgroundColor:t.selection,color:t.selectionText},
  ".cm-selectionBackground, &.cm-focused .cm-selectionBackground":{backgroundColor:t.selection},
  ".cm-gutters":{backgroundColor:t.background,color:t.muted,borderColor:t.border},
  "&.cm-focused":{outline:"none"}
 },{dark:t.dark});
}
window.setTheme=t=>{
 document.body.style.backgroundColor=t.background;document.body.style.color=t.text;
 if(view)view.dispatch({effects:theme.reconfigure(hostTheme(t))});
};
// QA operations change the real document through normal transactions. They do not simulate IME.
window.qaResults={};
window.qa={
 host:q=>{console.log("QA_REQUEST "+JSON.stringify(q));return q.id;},
 renderReady:()=>requestAnimationFrame(()=>requestAnimationFrame(()=>console.log("QA_RENDER_READY"))),
 select:(anchor,head,scroll)=>{
  if(composing())return false;
  if(![anchor,head].every(n=>Number.isInteger(n)&&n>=0&&n<=view.state.doc.length))return false;
  view.dispatch({selection:EditorSelection.range(anchor,head)});
  if(Number.isFinite(scroll))view.scrollDOM.scrollTop=scroll;
  return true;
 },
 load:source=>{
  if(composing())return false;
  view.dispatch({changes:{from:0,to:view.state.doc.length,insert:source},selection:{anchor:0},
   annotations:isolateHistory.of("full"),userEvent:"input.qa.load"});
  return true;
 },
 // Readiness checkpoint only, NOT a compositor/presentation timestamp.
 ready:()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(()=>resolve(window.snapshot()))))
};
window.boot=(source,live,t,commands)=>{
 if(view)return;
 commandTable=commands;liveMode=live;settledSource=source;
 view=new EditorView({parent:document.querySelector("#editor"),state:EditorState.create({doc:source,extensions:[
  history(),markdown({extensions:[GFM]}),EditorView.lineWrapping,
  keymap.of(commandTable.flatMap(c=>c.keys.map(key=>({key,run:()=>window.command(c.id)}))).concat(historyKeymap)),
  EditorView.domEventHandlers({
   compositionstart:()=>{settledSource=view.state.doc.toString();compositionActive=true;},
   compositionend:()=>{compositionActive=false;settleComposition();}
  }),
  theme.of(hostTheme(t)),mode.of(live?[livePlugin]:[])
 ]})});
 window.setTheme(t);
};
