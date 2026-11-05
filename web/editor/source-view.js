import {EditorState, Compartment} from '@codemirror/state';
import {EditorView, keymap, drawSelection, Decoration} from '@codemirror/view';
import {defaultKeymap, insertNewline, indentMore, indentLess} from '@codemirror/commands';
import {TextSelection} from '@milkdown/kit/prose/state';

let active;
export const sourceView=()=>active;

// The raw_source node remains the single source of truth and owns undo history.
// CodeMirror renders only its viewport; no second history or persistence layer.
export function sourceNodeView(env){return (node,outer,getPos,_decorations,inner)=>{
 let current=node,updating=false,destroyed=false,lastEditable,highlights,pendingHighlights=inner,highlightQueued=false;
 const editable=new Compartment(),search=new Compartment();
 const syncSelection=()=>{
  const selection=outer.state.selection,offset=getPos()+1;
  return {anchor:Math.max(0,Math.min(current.content.size,selection.anchor-offset)),head:Math.max(0,Math.min(current.content.size,selection.head-offset))};
 };
 const cm=new EditorView({doc:node.textContent,extensions:[
  EditorView.lineWrapping,drawSelection(),
  EditorView.editorAttributes.of({class:'source-viewport'}),
  EditorView.contentAttributes.of({'aria-label':'Markdown 源码编辑器',spellcheck:'false'}),
  EditorView.theme({'&':{font:'14px/1.65 monospace',backgroundColor:'transparent'},'.cm-content':{padding:'12px',minHeight:'75vh'},'.cm-line':{padding:'0'},'&.cm-focused':{outline:'none'},'.cm-scroller':{font:'inherit'},'.cm-selectionBackground':{backgroundColor:'var(--selection) !important'}}),
  editable.of([]),search.of([]),
  keymap.of([
   {key:'Mod-z',run:()=>env.command('undo')},
   {key:'Mod-Shift-z',run:()=>env.command('redo')},
   {key:'Mod-y',run:()=>env.command('redo')},
   {key:'Enter',run:insertNewline},{key:'Tab',run:indentMore},{key:'Shift-Tab',run:indentLess},
   ...defaultKeymap
  ]),
  EditorView.domEventHandlers({
   compositionstart(){env.composition(true);},
   compositionend(){setTimeout(()=>{if(!destroyed)env.composition(false);},30);},
   focus(){env.report();},blur(){env.report();},
   drop(event){event.preventDefault();return true;}
  }),
  EditorView.updateListener.of(update=>{
   if(updating||destroyed||!update.docChanged&&!update.selectionSet)return;
   const offset=getPos()+1;let tr=outer.state.tr,shift=0;
   update.changes.iterChanges((from,to,fromB,toB,text)=>{
    const value=text.toString();tr=tr.insertText(value,offset+from+shift,offset+to+shift);shift+=(toB-fromB)-(to-from);
   });
   const {anchor,head}=update.state.selection.main;
   if(update.docChanged||tr.selection.anchor!==offset+anchor||tr.selection.head!==offset+head){
    tr.setSelection(TextSelection.create(tr.doc,offset+anchor,offset+head));
    updating=true;try{outer.dispatch(tr);}finally{updating=false;}
   }
  })
 ]});
 function refresh(){
  const allowed=env.editable();if(allowed===lastEditable)return;lastEditable=allowed;
  cm.dispatch({effects:editable.reconfigure([EditorState.readOnly.of(!allowed),EditorView.editable.of(allowed)])});
 }
 function decorate(inner){
  if(inner===highlights)return;highlights=inner;
  const ranges=[];
  inner?.forEachSet(set=>{for(const d of set.find()){if(d.spec.sourceSearch)ranges.push(Decoration.mark({class:'editor-search-match'}).range(d.from,d.to));}});
  cm.dispatch({effects:search.reconfigure(EditorView.decorations.of(Decoration.set(ranges,true)))});
 }
 const api={cm,focus:()=>cm.focus(),refresh,
  scrollSelection(){cm.dispatch({selection:syncSelection(),effects:EditorView.scrollIntoView(syncSelection().head)});},
  dom:cm.dom,stopEvent:()=>true,ignoreMutation:()=>true,
  setSelection(anchor,head){updating=true;try{cm.dispatch({selection:{anchor,head}});cm.focus();}finally{updating=false;}},
  update(next,_decorations,inner){
   if(next.type!==current.type)return false;
   const previous=current;current=next;pendingHighlights=inner;
   if(updating){
    // A CM input dispatches into ProseMirror synchronously. Refresh mapped
    // search ranges after CM finishes its update, never re-enter that update.
    if(!highlightQueued){highlightQueued=true;queueMicrotask(()=>{highlightQueued=false;if(!destroyed)decorate(pendingHighlights);});}
    return true;
   }
   updating=true;
   try{
    if(!previous.content.eq(next.content)){
     const from=previous.content.findDiffStart(next.content);
     if(from!=null){const end=previous.content.findDiffEnd(next.content);const overlap=from-Math.min(end.a,end.b);
      cm.dispatch({changes:{from,to:end.a+Math.max(0,overlap),insert:next.textContent.slice(from,end.b+Math.max(0,overlap))},selection:syncSelection()});
     }
    }
    decorate(inner);refresh();
   }finally{updating=false;}
   return true;
  },
  destroy(){destroyed=true;if(active===api)active=null;cm.destroy();}
 };
 active=api;refresh();decorate(inner);return api;
};}
