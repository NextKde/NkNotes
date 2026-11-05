import {Editor, rootCtx, defaultValueCtx, editorViewCtx, editorViewOptionsCtx, parserCtx, serializerCtx, commandsCtx, remarkCtx, schemaCtx} from '@milkdown/kit/core';
import {ParserState} from '@milkdown/kit/transformer';
import * as cm from '@milkdown/kit/preset/commonmark';
import * as gfm from '@milkdown/kit/preset/gfm';
import {history} from '@milkdown/kit/plugin/history';
import {clipboard} from '@milkdown/kit/plugin/clipboard';
import {cursor} from '@milkdown/kit/plugin/cursor';
import {indent} from '@milkdown/kit/plugin/indent';
import {upload,uploadConfig} from '@milkdown/kit/plugin/upload';
import {refractor as allRefractor} from 'refractor/all';
import {prism,prismConfig} from '@milkdown/plugin-prism';
import {math} from './math.js';
import {wiki,completion} from './wiki.js';
import {sourceSchema,documentSchema} from './source-mode.js';
import {sourceNodeView,sourceView} from './source-view.js';
import {codeNodeView,waitForCodePreviews} from './code-view.js';
import {makeTools} from './tools.js';
import {tocPlugin} from './toc.js';
import {trailing} from '@milkdown/kit/plugin/trailing';
import {$prose} from '@milkdown/kit/utils';
import {Plugin, EditorState, TextSelection, NodeSelection} from '@milkdown/kit/prose/state';
import {Decoration} from '@milkdown/kit/prose/view';
import {undo, redo, undoDepth, redoDepth, closeHistory} from '@milkdown/kit/prose/history';
import {deleteRow, deleteColumn, deleteTable} from '@milkdown/kit/prose/tables';
let tools,toc,wikiCompletion;let imageWidths={};
let editor, view, bridge, epoch=0, sequence=0, loaded=false, readonly=true, protectedDoc=false;
let original='', initialDoc, serializedDoc, serialized='', initialSemantic='', composing=false, pendingFlush=0, pendingRestore=null;
let semanticDoc,semanticOriginal;
let fullPrefix='',sourcePreferred=false,sourceContext=0,sourceEdited=false,previousSource='';const exactDocuments=new Map();
const documentKeys=new WeakMap();
// ProseMirror documents are immutable. Reuse their semantic key for cursor/scroll
// reports and history lookups instead of serializing a long document again.
const documentKey=doc=>{let key=documentKeys.get(doc);if(key===undefined){key=JSON.stringify(doc.toJSON(),(name,value)=>name==='colwidth'?undefined:value);documentKeys.set(doc,key);}return key;};
let publishedDocument=null,lastLoadTimings={};
const sourceMode=()=>view?.state.doc.firstChild?.type.name==='raw_source';
function fullMarkdown(){
 if(sourceMode())return view.state.doc.firstChild.textContent;
 const key=documentKey(view.state.doc);const cached=exactDocuments.get(key);
 if(cached){fullPrefix=cached.prefix;return cached.text;}
 const text=fullPrefix+markdown();exactDocuments.set(key,{text,prefix:fullPrefix,protected:false});return text;
}
function toggleSource(){
 if(!loaded||busy()||tools?.hasPending())return false;
 const text=fullMarkdown(), wasSource=sourceMode();let doc;
 if(wasSource){
  const front=text.match(/^---\n[\s\S]*?\n(?:---|\.\.\.)[^\n]*\n/);fullPrefix=front?.[0]||'';
  const body=text.slice(fullPrefix.length);
  const parsed=parseRich(body,false);
  if(parsed.protected){bridge?.notice('这篇笔记包含正文模式暂不支持的语法，请继续使用源码模式。');return false;}
  doc=parsed.doc;
  doc=doc.type.create({sourceContext:++sourceContext},doc.content);
  exactDocuments.set(documentKey(doc),{text,prefix:fullPrefix});
 }else doc=view.state.schema.nodes.doc.create(null,view.state.schema.nodes.raw_source.create(null,text?view.state.schema.text(text):null));
 const previous=view.state.selection.from,ratio=previous/Math.max(1,view.state.doc.content.size);
 view.dispatch(closeHistory(view.state.tr));
 let tr=view.state.tr.replaceWith(0,view.state.doc.content.size,doc.content).setDocAttribute("sourceContext",doc.attrs.sourceContext);
 tr.setSelection(TextSelection.near(tr.doc.resolve(Math.min(tr.doc.content.size-1,Math.max(1,Math.round(ratio*tr.doc.content.size))))));
 view.dispatch(tr);
 // Trailing-paragraph and table plugins may append transactions during dispatch.
 // Associate the exact source with their settled document, before publishing it.
 if(wasSource)exactDocuments.set(documentKey(view.state.doc),{text,prefix:fullPrefix});
 view.dispatch(closeHistory(view.state.tr));sourcePreferred=!wasSource;
 protectedDoc=false;view.setProps({editable:()=>!readonly&&!protectedDoc});focusEditor();report();return true;
}

let reportQueued=false, loading=false, documentBase='file:///';
const busy=()=>composing || !!view?.composing || !!sourceView()?.cm.composing;
const focusEditor=()=>sourceView()?sourceView().focus():view.focus();
const action=fn=>editor.action(fn);
const cmd=(entry,value)=>action(ctx=>ctx.get(commandsCtx).call(entry.key,value));
let mappedDocument, mappedText;
function plainMap() {
 if(mappedDocument===view.state.doc)return mappedText;
 const parts=[],segments=[];let length=0,previousEnd=0,hasBlock=false;
 const append=(text,from)=>{if(!text.length)return;segments.push({offset:length,from,length:text.length});parts.push(text);length+=text.length;};
 view.state.doc.descendants((node,pos)=>{
  if(!node.isTextblock)return;
  if(hasBlock)append('\n',previousEnd);hasBlock=true;
  node.descendants((child,offset)=>{
   if(child.isText)append(child.text,pos+1+offset);
   else if(child.type.name==='hardbreak')append('\n',pos+1+offset);
   else if(child.type.name==='image')append('\uFFFC',pos+1+offset);
  });
  previousEnd=pos+1+node.content.size;return false;
 });
 const position=index=>{
  if(index>=length||!segments.length)return previousEnd;
  let low=0,high=segments.length;while(low+1<high){const mid=(low+high)>>1;if(segments[mid].offset<=index)low=mid;else high=mid;}
  return segments[low].from+Math.max(0,index-segments[low].offset);
 };
 const offset=pos=>{
  let low=0,high=segments.length;while(low<high){const mid=(low+high)>>1;if(segments[mid].from+segments[mid].length-1<pos)low=mid+1;else high=mid;}
  return low===segments.length?length:segments[low].offset+Math.max(0,pos-segments[low].from);
 };
 mappedDocument=view.state.doc;mappedText={text:parts.join(''),position,offset};return mappedText;
}

function markdown(){
 if(sourceMode())return view.state.doc.firstChild.textContent;
 if(protectedDoc || view.state.doc.eq(initialDoc))return original;
 if(semanticDoc!==view.state.doc){semanticDoc=view.state.doc;semanticOriginal=documentKey(view.state.doc)===initialSemantic;}
 if(semanticOriginal)return original;
 if(serializedDoc!==view.state.doc){serialized=action(ctx=>ctx.get(serializerCtx)(view.state.doc));serializedDoc=view.state.doc;}
 return serialized;
}
function currentFormat(){
 const {state}=view, {$from}=state.selection;
 let heading=0, list='', table=false, code=false;
 for(let d=$from.depth;d>0;d--){const n=$from.node(d);
  if(n.type.name==='heading')heading=n.attrs.level;
  if(n.type.name==='bullet_list')list='项目列表';if(n.type.name==='ordered_list')list='编号列表';
  if(n.type.name==='list_item'&&n.attrs.checked!=null)list='任务清单';
  if(n.type.name==='table')table=true;if(n.type.name==='code_block')code=true;
 }
 const marks=state.storedMarks||$from.marks(), parts=[heading?'标题 '+heading:'正文'];
 for(const [name,label] of [['strong','粗体'],['emphasis','斜体'],['strike_through','删除线'],['inlineCode','行内代码']])
  if(marks.some(m=>m.type.name===name))parts.push(label);
 if(list)parts.push(list);if(table)parts.push('表格');if(code)parts.push('代码块');
 return {heading,format:parts.join(' · '),table};
}
function report(){
 if(view&&!loading&&loaded){
  sourcePreferred=sourceMode();view.dom.setAttribute('aria-label',sourcePreferred?'Markdown 源码编辑器':'笔记所见即所得编辑器');const protection=sourceMode()?false:!!exactDocuments.get(documentKey(view.state.doc))?.protected;
  if(protectedDoc!==protection){protectedDoc=protection;view.setProps({editable:()=>!readonly&&!protectedDoc});}
 }
 reportQueued=false;if(!bridge||!view||loading)return;
 const map=plainMap(), raw=sourceMode(), snapshot={epoch,sequence:++sequence,loaded,sourceMode:raw,protected:raw?false:protectedDoc,composing:busy()||!!tools?.hasPending(),
  canUndo:undoDepth(view.state)>0,canRedo:redoDepth(view.state)>0,...(raw?{heading:0,format:'Markdown 源码'}:currentFormat()),
  anchor:map.offset(view.state.selection.anchor),cursor:map.offset(view.state.selection.head),scroll:window.scrollY};
 // Selection-only snapshots use the native committed cache. A flush always
 // includes the source, even when the document identity has not changed.
 if(!busy()&&!tools?.hasPending()&&(view.state.doc!==publishedDocument||pendingFlush)){snapshot.source=fullMarkdown();publishedDocument=view.state.doc;snapshot.fullSource=true;if(sourceMode()&&snapshot.source!==previousSource)sourceEdited=true;previousSource=snapshot.source;snapshot.sourceEdited=sourceEdited;if(pendingFlush){snapshot.flush=pendingFlush;pendingFlush=0;}}
 bridge.publish(JSON.stringify(snapshot));
}
function scheduleReport(){if(!reportQueued){reportQueued=true;queueMicrotask(report);}}
function settle(){
 if(busy()){setTimeout(settle,20);return;}
 if(pendingRestore){const value=pendingRestore;pendingRestore=null;restore(value);}
 tools?.refresh();report();
}
function protectedTree(tree){
 let unsupported=false;
 const visit=node=>{
  if(node.type==='html'&&!/^<br\s*\/?>$/i.test(node.value.trim()))unsupported=true;

  if(node.type==='paragraph'&&node.children?.[0]?.type==='text'&&node.children[0].value.startsWith(':::'))unsupported=true;
  node.children?.forEach(visit);
 };visit(tree);return unsupported;
}
function parseRich(source,previewUnsupported=true){
 return action(ctx=>{
  // Inspect and transform the same remark tree; parserCtx would parse it twice.
  const remark=ctx.get(remarkCtx),tree=remark.parse(source),protectedContent=protectedTree(tree);
  if(protectedContent&&!previewUnsupported)return {protected:true};
  const doc=new ParserState(ctx.get(schemaCtx)).next(remark.runSync(tree,source)).toDoc();
  return {doc,protected:protectedContent};
 });
}
function load(message){
 if(busy())return false;
 const started=performance.now();let phase=started;lastLoadTimings={};publishedDocument=null;
 const mark=name=>{const now=performance.now();lastLoadTimings[name]=now-phase;phase=now;};
 const resetSequence=message.epoch!==epoch||!loaded;
 wikiCompletion?.reset();tools?.reset();semanticDoc=null;loading=true;loaded=false;documentBase=message.baseUrl||'file:///';window.noteLibrary=message.library||window.noteLibrary||'';epoch=message.epoch;if(resetSequence)sequence=0;original=message.source;serializedDoc=null;
 fullPrefix=message.prefix??fullPrefix;sourceEdited=false;previousSource=fullPrefix+original;sourcePreferred=message.sourceMode??sourcePreferred;exactDocuments.clear();
 const parsed=sourcePreferred?null:parseRich(original);
 protectedDoc=parsed?.protected||false;
 const doc=sourcePreferred?view.state.schema.nodes.doc.create(null,view.state.schema.nodes.raw_source.create(null,(fullPrefix+original)?view.state.schema.text(fullPrefix+original):null)):parsed.doc;initialDoc=doc;
 mark('parseMs');
 view.updateState(EditorState.create({schema:view.state.schema,doc,plugins:view.state.plugins}));mark('viewMs');
 let layout=view.state.tr,tableIndex=0;
 view.state.doc.descendants((node,pos)=>{if(node.type.name!=='table')return;
  const widths=message.tableWidths?.[tableIndex++];if(!widths)return;
  node.firstChild.forEach((cell,offset,index)=>{if(widths[index])layout=layout.setNodeMarkup(pos+2+offset,undefined,{...cell.attrs,colwidth:widths[index]});});
 });
 view.dispatch(layout.setMeta('addToHistory',false));initialDoc=view.state.doc;mark('layoutMs');
 initialSemantic=documentKey(initialDoc);
 if(!sourceMode())exactDocuments.set(documentKey(view.state.doc),{text:fullPrefix+original,prefix:fullPrefix,protected:protectedDoc});
 loaded=true;loading=false;tools?.refresh();view.setProps({editable:()=>!readonly&&!protectedDoc});
 mark('prepareSnapshotMs');report();mark('publishMs');lastLoadTimings.totalMs=performance.now()-started;return true;
}
let restoreSerial=0;
function restore(value){
 if(!loaded||busy()){pendingRestore=value;return;}
 const restoreToken=++restoreSerial, restoreEpoch=epoch;
 if(value.heading){
  const used=new Map();let target=-1;
  view.state.doc.descendants((node,pos)=>{if(node.type.name!=='heading')return;const text=node.textContent;
   const base=text.toLowerCase().replace(/[^\p{L}\p{N}_ -]/gu,'').replace(/ /g,'-');const count=used.get(base)||0;used.set(base,count+1);
   const slug=base+(count?'-'+count:'');if(target<0&&(text===value.heading||slug===value.heading))target=pos;
  });
  if(target>=0){view.dispatch(view.state.tr.setSelection(TextSelection.near(view.state.doc.resolve(target+1))).scrollIntoView());requestAnimationFrame(()=>requestAnimationFrame(()=>{if(restoreToken===restoreSerial&&restoreEpoch===epoch){view.nodeDOM(target)?.scrollIntoView({block:'start'});scheduleReport();}}));}
  return;
 }
 const map=plainMap();
 if(value.query){find(value.query,true);return;}
 const pos=map.position(Math.max(0,Math.min(map.text.length,value.cursor||0)));
 view.dispatch(view.state.tr.setSelection(TextSelection.near(view.state.doc.resolve(pos))));
 requestAnimationFrame(()=>{if(restoreToken!==restoreSerial||restoreEpoch!==epoch)return;window.scrollTo(0,Math.max(0,value.scroll||0));scheduleReport();});
}
function find(query,first=false){
 const map=plainMap(), from=first?0:map.offset(view.state.selection.to), lower=map.text.toLowerCase();
 let at=lower.indexOf(query.toLowerCase(),from);if(at<0)at=lower.indexOf(query.toLowerCase());
 if(at<0)return false;
 view.dispatch(view.state.tr.setSelection(TextSelection.create(view.state.doc,map.position(at),map.position(at+query.length))).scrollIntoView());
 focusEditor();return true;
}
function ancestor(type){const {$from}=view.state.selection;for(let d=$from.depth;d>0;d--)if($from.node(d).type.name===type)return {node:$from.node(d),pos:$from.before(d)};return null;}
function list(name){
 const ordered=name==='ordered', type=ordered?'ordered_list':'bullet_list';
 const item=ancestor('list_item');
 if(ancestor(type)&&item&&((name==='task')===(item.node.attrs.checked!=null)))return cmd(cm.liftListItemCommand);
 const ok=ancestor(type)||cmd(ordered?cm.wrapInOrderedListCommand:cm.wrapInBulletListCommand);
 if(!ok)return false;
 const current=ancestor('list_item');
 if(current)view.dispatch(view.state.tr.setNodeMarkup(current.pos,undefined,{...current.node.attrs,checked:name==='task'?false:null}));
 return true;
}
function command(name,argument='',options={}){
 if(name==='replaceSource'){
  if(!loaded||busy())return false;if(!sourceMode())toggleSource();
  view.dispatch(closeHistory(view.state.tr));view.dispatch(view.state.tr.insertText(argument,1,1+view.state.doc.firstChild.content.size));view.dispatch(closeHistory(view.state.tr));report();return true;
 }
 if(name==='insertNoteLink'){
  if(!loaded||busy()||readonly||protectedDoc||sourceMode())return false;const data=JSON.parse(argument);
  const from=new URL(documentBase).pathname.split('/').slice(0,-1),to=new URL(data.url).pathname.split('/');while(from.length&&to.length&&from[0]===to[0]){from.shift();to.shift();}
  const relative='../'.repeat(from.length)+to.join('/');const target=(relative.includes('/')?relative:'./'+relative)+(data.heading?'#'+data.heading:'');
  view.dispatch(view.state.tr.replaceSelectionWith(view.state.schema.nodes.wiki_link.create({target:decodeURIComponent(target),label:data.title||''})).scrollIntoView());focusEditor();report();return true;
 }
 if(name==='sourceMode')return toggleSource();
 if(!view||!loaded||busy()||readonly||(!sourceMode()&&protectedDoc))return false;
 if(sourceMode()&&!['undo','redo'].includes(name))return false;
 let result=false;
 if(!options.join&&name!=='undo'&&name!=='redo')view.dispatch(closeHistory(view.state.tr));
 if(name==='undo')result=undo(view.state,view.dispatch);
 else if(name==='redo')result=redo(view.state,view.dispatch);
 else if(name==='paragraph')result=cmd(cm.turnIntoTextCommand);
 else if(/^heading[1-6]$/.test(name))result=cmd(cm.wrapInHeadingCommand,Number(name.slice(7)));
 else if(['bullet','ordered','task'].includes(name))result=list(name);
 else if(name==='check'){
  const item=ancestor('list_item');if(item){view.dispatch(view.state.tr.setNodeMarkup(item.pos,undefined,{...item.node.attrs,checked:!item.node.attrs.checked}));result=true;}
 } else if(name==='link'){
  if(!/^(https?:|mailto:)/i.test(argument))return false;
  if(view.state.selection.empty){
   const start=view.state.selection.from;
   view.dispatch(view.state.tr.insertText(argument).addMark(start,start+argument.length,view.state.schema.marks.link.create({href:argument})));
   result=true;
  }else result=cmd(cm.toggleLinkCommand,{href:argument});
 } else if(name==='table'){
  const from=view.state.selection.from;result=cmd(gfm.insertTableCommand,{row:3,col:3});
  let target;
  view.state.doc.descendants((node,pos)=>{if(!target&&node.type.name==='table'&&pos>=from-1){
   node.descendants((child,offset)=>{if(target===undefined&&child.isTextblock)target=pos+offset+2;});return false;
  }});
  if(target!==undefined)view.dispatch(view.state.tr.setSelection(TextSelection.near(view.state.doc.resolve(target))).scrollIntoView());
 }
 else if(['selectRow','selectColumn','selectTable','moveRowUp','moveRowDown','moveColumnLeft','moveColumnRight'].includes(name)){
  const {$from}=view.state.selection;let depth=$from.depth;while(depth>0&&$from.node(depth).type.name!=='table')depth--;
  if(depth){const table=$from.node(depth),row=$from.index(depth),column=$from.index(depth+1);
   if(name==='selectTable')result=cmd(gfm.selectTableCommand);
   if(name==='selectRow')result=cmd(gfm.selectRowCommand,{index:row});
   if(name==='selectColumn')result=cmd(gfm.selectColCommand,{index:column});
   if(name==='moveRowUp'&&row>1)result=cmd(gfm.moveRowCommand,{from:row,to:row-1});
   if(name==='moveRowDown'&&row>0&&row<table.childCount-1)result=cmd(gfm.moveRowCommand,{from:row,to:row+1});
   if(name==='moveColumnLeft'&&column>0)result=cmd(gfm.moveColCommand,{from:column,to:column-1});
   if(name==='moveColumnRight'&&column<table.firstChild.childCount-1)result=cmd(gfm.moveColCommand,{from:column,to:column+1});
  }
 }
 else if(name==='deleteRow')result=deleteRow(view.state,view.dispatch);
 else if(name==='deleteColumn')result=deleteColumn(view.state,view.dispatch);
 else if(name==='deleteTable')result=deleteTable(view.state,view.dispatch);
 else {
  const commands={bold:cm.toggleStrongCommand,italic:cm.toggleEmphasisCommand,strike:gfm.toggleStrikethroughCommand,
   code:cm.toggleInlineCodeCommand,codeblock:cm.createCodeBlockCommand,quote:cm.wrapInBlockquoteCommand,rule:cm.insertHrCommand,
   indent:cm.sinkListItemCommand,outdent:cm.liftListItemCommand,
   rowBefore:gfm.addRowBeforeCommand,rowAfter:gfm.addRowAfterCommand,columnBefore:gfm.addColBeforeCommand,columnAfter:gfm.addColAfterCommand,
   exitTable:gfm.exitTable};
  if(commands[name])result=cmd(commands[name]);
  else if(name.startsWith('align')){
   const {$from}=view.state.selection;let column=-1;
   for(let d=$from.depth;d>0;d--)if(['table_cell','table_header'].includes($from.node(d).type.name)){column=$from.index(d-1);break;}
   if(column>=0){cmd(gfm.selectColCommand,{index:column});result=cmd(gfm.setAlignCommand,name.slice(5).toLowerCase());}
  }
 }
 if(!options.join&&name!=='undo'&&name!=='redo')view.dispatch(closeHistory(view.state.tr));
 focusEditor();report();return !!result;
}
// Complete a directly typed GFM header/separator using the upstream parser.
// Milkdown's existing |3x3| input rule and Markdown paste remain unchanged.
function typedTable(v,event){
 if(event.key!=='Enter'||event.shiftKey||event.ctrlKey||event.metaKey||busy()||!v.state.selection.empty)return false;
 const {$from}=v.state.selection;
 if($from.parent.type.name!=='paragraph'||$from.parentOffset!==$from.parent.content.size)return false;
 const separator=$from.parent.textContent;
 if(!/^\s*\|?\s*:?-{3,}:?\s*(?:\|\s*:?-{3,}:?\s*)+\|?\s*$/.test(separator))return false;
 const parent=$from.node($from.depth-1), index=$from.index($from.depth-1);if(index<1)return false;
 const header=parent.child(index-1);if(header.type.name!=='paragraph'||!header.textContent.includes('|'))return false;
 const columns=separator.trim().replace(/^\||\|$/g,'').split('|').length;
 const parsed=action(ctx=>ctx.get(parserCtx)(header.textContent+'\n'+separator+'\n|'+Array(columns).fill(' ').join('|')+'|\n'));
 if(parsed.firstChild?.type.name!=='table')return false;
 const start=$from.before()-header.nodeSize, end=$from.after(), table=parsed.firstChild;
 let tr=v.state.tr.replaceWith(start,end,table);
 tr=tr.setSelection(TextSelection.near(tr.doc.resolve(Math.min(tr.doc.content.size,start+table.firstChild.nodeSize+4))));
 v.dispatch(tr.scrollIntoView());return true;
}
const hostPlugin=$prose(()=>new Plugin({
 appendTransaction(transactions,old,state){
  if(state.doc.firstChild?.type.name!=='raw_source')return null;
  const end=1+state.doc.firstChild.content.size;
  if(state.selection.from<1||state.selection.to>end)return state.tr.setSelection(TextSelection.create(state.doc,Math.max(1,Math.min(end,state.selection.anchor)),Math.max(1,Math.min(end,state.selection.head)))).setMeta('addToHistory',false);
  return null;
 },
 view(){return {update(){sourceView()?.refresh();if(!loading)scheduleReport();}};},
 props:{
  handleScrollToSelection(){if(!sourceView())return false;sourceView().scrollSelection();return true;},
  handlePaste(v,event){if(!sourceMode())return false;if(readonly)return true;v.dispatch(v.state.tr.insertText(event.clipboardData.getData('text/plain')).scrollIntoView());return true;},
  handleDrop(v,event){if(sourceMode()){event.preventDefault();return true;}return false;},
  nodeViews:{raw_source:sourceNodeView({editable:()=>!readonly&&!protectedDoc,command,report:scheduleReport,composition:value=>{composing=value;if(value)report();else settle();}}),code_block:codeNodeView,image(node,v,getPos){
   const dom=document.createElement('img');
   const update=value=>{if(value.type.name!=='image')return false;node=value;dom.alt=node.attrs.alt||'';dom.dataset.originalSrc=node.attrs.src;dom.style.width=imageWidths[node.attrs.src]?imageWidths[node.attrs.src]+'px':'';
    try{const url=new URL(node.attrs.src,documentBase);dom.src=url.protocol==='file:'?'nkasset:'+url.pathname:url.href;}catch{dom.removeAttribute('src');}return true;};
   dom.addEventListener('dblclick',()=>{view.dispatch(view.state.tr.setSelection(NodeSelection.create(view.state.doc,getPos())));window.nextnotes.openImage();});
   update(node);return {dom,update,ignoreMutation:()=>true};
  }},
  handleKeyDown(v,event){
   if(busy())return false;
   if(sourceMode()) {
    if(readonly)return false;
    if(event.key==='Enter'||event.key==='Tab'){v.dispatch(v.state.tr.insertText(event.key==='Enter'?'\n':'    ').scrollIntoView());return true;}
    if((event.ctrlKey||event.metaKey)&&event.key.toLowerCase()==='a'){v.dispatch(v.state.tr.setSelection(TextSelection.create(v.state.doc,1,1+v.state.doc.firstChild.content.size)));return true;}
    if((event.ctrlKey||event.metaKey)&&event.key.toLowerCase()==='z'){command(event.shiftKey?'redo':'undo');return true;}
    if((event.ctrlKey||event.metaKey)&&event.key.toLowerCase()==='y'){command('redo');return true;}
    return false;
   }
   if((event.ctrlKey||event.metaKey)&&!event.altKey){
    const names={b:'bold',i:'italic',e:'code'};
    if(event.key.toLowerCase()==='x'&&event.shiftKey){command('strike');return true;}
    if(names[event.key.toLowerCase()]){command(names[event.key.toLowerCase()]);return true;}
   }
   return typedTable(v,event);
  },
  handleDOMEvents:{
   compositionstart(){composing=true;report();return false;},
   compositionend(){composing=false;setTimeout(settle,30);return false;},
   focus(){scheduleReport();return false;},blur(){scheduleReport();return false;},
   click(v,event){
    let reference;
    v.state.doc.descendants((node,pos)=>{if(node.type.name==='footnote_reference'){const dom=v.nodeDOM(pos);if(dom===event.target||dom?.contains?.(event.target))reference=node.attrs.label;}});
    if(reference){let position;v.state.doc.descendants((node,pos)=>{if(node.type.name==='footnote_definition'&&node.attrs.label===reference)position=pos;});if(position!==undefined){tools.run('jump',{pos:position});event.preventDefault();return true;}}
    const task=event.target.closest('li[data-checked]');
    if(task&&event.clientX<task.getBoundingClientRect().left&&!readonly&&!protectedDoc){
     const pos=v.posAtDOM(task,0);const $pos=v.state.doc.resolve(pos);
     for(let d=$pos.depth;d>0;d--){const node=$pos.node(d);if(node.type.name==='list_item'){
      v.dispatch(v.state.tr.setNodeMarkup($pos.before(d),undefined,{...node.attrs,checked:!node.attrs.checked}));return true;
     }}
    }
    if(event.target.closest('a')){if(event.ctrlKey||event.metaKey)bridge?.openLink(event.target.closest('a').getAttribute('href'));event.preventDefault();return true;}return false;
   }
  }
 }
}));
async function start(){
 wikiCompletion=completion({busy,epoch:()=>epoch,canEdit:()=>loaded&&!readonly&&!protectedDoc&&!sourceMode(),lookup:(query,id)=>bridge?.lookupWiki(query,id),relative:path=>{
  const current=new URL(documentBase).pathname,base=decodeURIComponent(current).split('/');
  const library=window.noteLibrary||'';const from=decodeURIComponent(current).slice(library.length+1).split('/').slice(0,-1),to=path.split('/');
  while(from.length&&to.length&&from[0]===to[0]){from.shift();to.shift();}const relative='../'.repeat(from.length)+to.join('/');return relative.includes('/')?relative:'./'+relative;
 }});
 tools=makeTools({editor:()=>editor,bridge:()=>bridge,busy,canEdit:()=>loaded&&!readonly&&!protectedDoc&&!sourceMode(),epoch:()=>epoch,command,report,nativeTool:name=>bridge?.requestTool(name),refreshToc:()=>toc?.refresh(),waitForPreview:waitForCodePreviews});
 toc=tocPlugin(()=>view,pos=>tools.run('jump',{pos}));
 editor=await Editor.make().config(ctx=>{
  ctx.set(prismConfig.key,{configureRefractor:()=>allRefractor});
  ctx.update(uploadConfig.key,previous=>({...previous,uploader:(files,schema)=>tools.uploader(files,schema),enableHtmlFileUploader:true,uploadWidgetFactory:(pos,spec)=>{const dom=document.createElement('span');dom.textContent='正在导入附件…';return Decoration.widget(pos,dom,spec);}}));
  ctx.set(rootCtx,document.querySelector('#editor'));ctx.set(defaultValueCtx,'');
  ctx.update(editorViewOptionsCtx,prev=>({...prev,editable:()=>!readonly&&!protectedDoc,attributes:{'aria-label':'笔记所见即所得编辑器',role:'textbox','aria-multiline':'true',spellcheck:'true'}}));
 }).use(cm.commonmark.filter(plugin=>plugin!==cm.docSchema)).use(documentSchema).use(gfm.gfm).use(history).use(upload).use(clipboard).use(cursor).use(indent).use(trailing).use(prism).use(sourceSchema).use(math).use(wiki).use($prose(()=>wikiCompletion.plugin)).use(toc.plugins).use($prose(()=>tools.plugin)).use(hostPlugin).create();
 view=action(ctx=>ctx.get(editorViewCtx));initialDoc=view.state.doc;
 window.nextnotes={openMath:()=>{tools.uiNow();bridge?.requestTool('math');},openImage:()=>{tools.uiNow();bridge?.requestTool('image');},command,load,find,restore,tools:tools.run,toolsState:tools.state,exportBody:()=>tools.exportReady(0),snapshot:()=>({epoch,baseUrl:documentBase,source:sourceMode()?fullMarkdown():fullMarkdown().slice(fullPrefix.length),fullSource:fullMarkdown(),sourceMode:sourceMode(),doc:view.state.doc.toJSON(),composing:busy()||!!tools?.hasPending(),selection:view.state.selection.toJSON()}),
  // Available only through an explicitly enabled Chromium debugging endpoint.
  performance:()=>lastLoadTimings,sourceView:()=>sourceView()?.cm,view:()=>view};
 new QWebChannel(qt.webChannelTransport,channel=>{
  bridge=channel.objects.bridge;
  bridge.loadRequested.connect(message=>{try{load(message);}catch(error){bridge.failed(error.message);console.error(error.stack);}});
  bridge.commandRequested.connect(message=>{if(message.epoch===epoch)command(message.command,message.argument);});
  bridge.flushRequested.connect(token=>{pendingFlush=token;if(!busy())report();});
  bridge.optionsRequested.connect(options=>{
   imageWidths=options.imageWidths||{};
   for(const image of view.dom.querySelectorAll('img[data-original-src]'))image.style.width=imageWidths[image.dataset.originalSrc]?imageWidths[image.dataset.originalSrc]+'px':'';
   readonly=!!options.readonly;view.setProps({editable:()=>!readonly&&!protectedDoc});sourceView()?.refresh();
   for(const [key,value] of Object.entries(options.theme||{}))document.documentElement.style.setProperty('--'+key,value);
  });
  bridge.actionRequested.connect(message=>{if(message.epoch===epoch&&(!sourceMode()||['search','findNext'].includes(message.name)))tools.run(message.name,message.args||{});});
  bridge.attachmentResult.connect((id,generation,result)=>tools.importResult(id,generation,result));
  bridge.prepareExportRequested.connect(token=>{if(sourceMode()){bridge.notice("请先切换到正文模式再导出排版文档。");return;}tools.exportReady(token);});
  bridge.restoreRequested.connect(restore);
  bridge.wikiResults.connect((id,items)=>wikiCompletion.receive(id,items));
  bridge.findRequested.connect(query=>find(query));
  bridge.focusRequested.connect(focusEditor);
  bridge.ready();
 });
 window.addEventListener('scroll',()=>{scheduleReport();tools.ui();},{passive:true});
 window.addEventListener('resize',()=>tools.ui());
}
start().catch(error=>{document.querySelector('#error').textContent='编辑器加载失败：'+error.message;console.error(error.stack);});
