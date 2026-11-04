import {EditorState, Compartment, EditorSelection} from "@codemirror/state";
import {EditorView, Decoration, ViewPlugin, keymap} from "@codemirror/view";
import {history, historyKeymap, undo, redo, isolateHistory} from "@codemirror/commands";
import {markdown} from "@codemirror/lang-markdown";
import {GFM} from "@lezer/markdown";
import {syntaxTree} from "@codemirror/language";
let view;
const mode = new Compartment();
function decorations(v) {
 const spans=[];
 const selections=v.state.selection.ranges;
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
 update(u){if(!u.view.composing && (u.docChanged||u.selectionSet||u.viewportChanged))this.decorations=decorations(u.view)}
},{decorations:p=>p.decorations});
window.setLive=live=>{
 const top=view.scrollDOM.scrollTop;
 view.dispatch({effects:mode.reconfigure(live?[livePlugin]:[])});
 requestAnimationFrame(()=>{view.scrollDOM.scrollTop=top});
};
window.command=name=>{
 if(!view||view.composing)return false;
 if(name==="undo")undo(view);
 else if(name==="redo")redo(view);
 else {
  const m=({bold:"**",italic:"*",strike:"~~",code:"`"})[name];
  if(!m)return false;
  view.dispatch(view.state.changeByRange(r=>({
   changes:r.empty ? [{from:r.from,insert:m+m}] : [{from:r.from,insert:m},{from:r.to,insert:m}],
   range:EditorSelection.range(r.anchor+m.length,r.head+m.length)
  })),{annotations:isolateHistory.of("full"),userEvent:"input.format"});
 }
 view.focus();return true;
};
window.snapshot=()=>({source:view.state.doc.toString(),ranges:view.state.selection.ranges.map(r=>({anchor:r.anchor,head:r.head})),scroll:view.scrollDOM.scrollTop});
window.boot=(source,live)=>{
 if(view)return;
 view=new EditorView({
  parent:document.querySelector("#editor"),
  state:EditorState.create({doc:source,extensions:[
   history(),markdown({extensions:[GFM]}),EditorView.lineWrapping,
   keymap.of([{key:"Mod-b",run:()=>window.command("bold")},{key:"Mod-i",run:()=>window.command("italic")},...historyKeymap]),
   mode.of(live?[livePlugin]:[])
  ]})
 });
};
