import {Plugin, PluginKey, TextSelection, NodeSelection} from '@milkdown/kit/prose/state';
import {Decoration, DecorationSet} from '@milkdown/kit/prose/view';
import {DOMSerializer} from '@milkdown/kit/prose/model';
import {closeHistory} from '@milkdown/kit/prose/history';
import {editorViewCtx, parserCtx, schemaCtx, serializerCtx} from '@milkdown/kit/core';
import * as cm from '@milkdown/kit/preset/commonmark';
import * as gfm from '@milkdown/kit/preset/gfm';
const slashEntries=[
 ['paragraph','正文','text paragraph 正文 段落'],['heading1','标题 1','h1 heading 标题'],['heading2','标题 2','h2 heading 标题'],['heading3','标题 3','h3 heading 标题'],
 ['bullet','项目列表','list bullet 项目 列表'],['ordered','编号列表','ordered number 编号'],['task','任务清单','task todo 任务 待办'],['quote','引用','quote 引用'],
 ['codeblock','代码块','code 代码'],['mermaid','Mermaid 图表','mermaid diagram 图表 流程图'],['table','表格','table 表格'],['rule','分隔线','divider line 分隔线'],
 ['math','数学公式','math latex 数学 公式'],['footnote','脚注','footnote 脚注'],['attachment','图片或附件','image file 图片 附件'],['toc','插入目录','toc contents 目录'],
];
export function makeTools(env){
 let view,hover=0,blockHandleVisible=false,collapsed=new Set(),query='',caseSensitive=false,searchIndex=-1,slashIndex=0,lastSlash='',uiTimer,lastUI='',layoutToken=0;
 let cachedDoc,cachedText='',cachedStats={},cachedOutline=[],cachedMatchesDoc,cachedMatchesQuery,cachedMatchesCase,cachedMatches=[];
 let suppressedSlash=null,blockSelection=false,newParagraphPos=null;let pending=0,request=0,clipboardBlock=null;const imports=new Map(),bookmarks=new Map();
 const key=new PluginKey('nextnotesTools');
 const editable=()=>env.canEdit()&&!env.busy();
 const notify=text=>env.bridge()?.notice(text);
 function topBlock(pos){
  if(!view)return null;const doc=view.state.doc;pos=Math.max(0,Math.min(doc.content.size,pos));
  const $pos=doc.resolve(pos);if($pos.depth){const at=$pos.before(1);return {pos:at,node:doc.nodeAt(at)};}
  const result=doc.childAfter(pos);if(result.node)return {pos:result.offset,node:result.node};
  const last=doc.lastChild;return last?{pos:doc.content.size-last.nodeSize,node:last}:null;
 }
 function targetBlock(args={}){return topBlock(Number.isInteger(args.pos)?args.pos:hover);}
 function trackNewParagraph(state,focused){
  const {$from}=state.selection;
  if(!focused||!(state.selection instanceof TextSelection)||$from.parent.type.name!=='paragraph'){newParagraphPos=null;return;}
  const pos=$from.before();
  if(newParagraphPos!==pos)newParagraphPos=null;
  if(!$from.parent.content.size)newParagraphPos=pos;
 }
 function rootBlocks(){const out=[];view.state.doc.forEach((node,pos)=>out.push({node,pos}));return out;}
 function matches(doc=view.state.doc){
  if(!query)return [];
  if(doc===cachedMatchesDoc&&query===cachedMatchesQuery&&caseSensitive===cachedMatchesCase)return cachedMatches;
  const found=[],expression=new RegExp(query.replace(/[.*+?^${}()|[\]\\]/g,'\\$&'),caseSensitive?'gu':'giu');
  doc.descendants((node,pos)=>{
   if(!node.isTextblock)return;
   const text=[],segments=[];let length=0;
   node.descendants((child,offset)=>{
    const value=child.isText?child.text:child.type.name==='hardbreak'?'\n':child.isLeaf?'\uFFFC':'';
    if(value){segments.push({offset:length,pos:pos+1+offset});text.push(value);length+=value.length;}
   });
   const position=index=>{if(index===length)return pos+1+node.content.size;let low=0,high=segments.length;while(low+1<high){const mid=(low+high)>>1;if(segments[mid].offset<=index)low=mid;else high=mid;}return segments[low].pos+index-segments[low].offset;};
   const raw=text.join('');
   for(const match of raw.matchAll(expression))found.push({from:position(match.index),to:position(match.index+match[0].length)});
   return false;
  });cachedMatchesDoc=doc;cachedMatchesQuery=query;cachedMatchesCase=caseSensitive;cachedMatches=found;return found;
 }
 function decorations(state){
  const items=[];let hiddenUntil=-1;
  const ids=new Map();
  const blocks=[];state.doc.forEach((node,pos)=>blocks.push({node,pos}));
  for(let i=0;i<blocks.length;i++){
   const {node,pos}=blocks[i];
   if(node.type.name==='heading'){
    const base=node.attrs.id||node.textContent.trim().toLowerCase().replace(/\s+/g,'-').replace(/[^\p{L}\p{N}_-]/gu,'')||'heading';
    const count=ids.get(base)||0;ids.set(base,count+1);items.push(Decoration.node(pos,pos+node.nodeSize,{id:count?base+'-'+count:base}));
   }
   if(pos<hiddenUntil){items.push(Decoration.node(pos,pos+node.nodeSize,{class:'folded-content'}));continue;}
   if(node.type.name==='heading'&&collapsed.has(pos)){
    items.push(Decoration.node(pos,pos+node.nodeSize,{class:'folded-heading'}));
    hiddenUntil=state.doc.content.size;
    for(let j=i+1;j<blocks.length;j++)if(blocks[j].node.type.name==='heading'&&blocks[j].node.attrs.level<=node.attrs.level){hiddenUntil=blocks[j].pos;break;}
   }
  }
  // During IME, keep existing decorations mapped; composition owns its DOM.
  if(query&&!env.busy())for(const m of matches(state.doc))items.push(Decoration.inline(m.from,m.to,{class:'editor-search-match'},{sourceSearch:true}));
  return DecorationSet.create(state.doc,items);
 }
 function slash(){
  if(!view||!editable()||!view.state.selection.empty)return null;
  const {$from}=view.state.selection;if($from.parent.type.name!=='paragraph')return null;
  const text=$from.parent.textBetween(0,$from.parentOffset,'\n','\uFFFC');const match=/^\/([^/\n]{0,40})$/.exec(text);if(!match)return null;
  if(text===suppressedSlash)return null;
  if(text!==suppressedSlash)suppressedSlash=null;
  const term=match[1].trim().toLocaleLowerCase();
  const items=slashEntries.filter(entry=>entry[2].includes(term)||entry[1].includes(term)).slice(0,10).map(([command,label])=>({command,label}));
  if(text!==lastSlash){lastSlash=text;slashIndex=0;}
  slashIndex=Math.max(0,Math.min(slashIndex,items.length-1));
  const r=view.coordsAtPos($from.pos);
  return {query:term,items,index:slashIndex,from:$from.start(),to:$from.pos,x:r.left,y:r.bottom};
 }
 function geometry(block){
  if(!block||block.node.type.name==='raw_source')return null;const dom=view.nodeDOM(block.pos);if(!(dom instanceof Element))return null;
  const r=dom.getBoundingClientRect();if(r.height<=0)return null;return {x:r.left,pos:block.pos,type:block.node.type.name,title:block.node.textContent.slice(0,40),y:r.top,height:r.height};
 }
 function ui(){
  uiTimer=null;if(!view||!env.bridge())return;
  const blocks=rootBlocks();
  if(cachedDoc!==view.state.doc){
   cachedDoc=view.state.doc;cachedOutline=blocks.filter(b=>b.node.type.name==='heading').map(b=>({pos:b.pos,level:b.node.attrs.level,title:b.node.textContent||'无标题'}));
   cachedText=view.state.doc.textBetween(0,view.state.doc.content.size,'\n','\uFFFC');
   const characters=Array.from(cachedText.replace(/\s/g,'')).length;
   const approximate=cachedText.length>200000;let words=0;
   if(!approximate&&typeof Intl.Segmenter==='function')for(const segment of new Intl.Segmenter('zh',{granularity:'word'}).segment(cachedText)){if(segment.isWordLike)++words;}
   else for(const match of cachedText.matchAll(/[\p{L}\p{N}_]+/gu))++words;
   cachedStats={characters,words,approximate,minutes:Math.max(1,Math.ceil(characters/500))};
  }
  const outline=cachedOutline.map(b=>({...b,folded:collapsed.has(b.pos)}));
  const selected=view.state.selection.empty?'':view.state.doc.textBetween(view.state.selection.from,view.state.selection.to,'\n');
  // A fresh paragraph stays unobstructed for its first editing session.
  // Existing content remains draggable even while its text has focus.
  trackNewParagraph(view.state,view.hasFocus());
  const editingBlock=newParagraphPos===null?null:topBlock(newParagraphPos+1);
  const state={epoch:env.epoch(),outline,stats:{...cachedStats,selected:Array.from(selected.replace(/\s/g,'')).length},
   block:geometry(topBlock(hover)),blockHandleVisible:blockHandleVisible&&editable()&&editingBlock?.pos!==hover,slash:slash(),search:{total:matches().length,current:searchIndex+1},pending};
  const block=topBlock(view.state.selection.from);
  if(block?.node.type.name==='code_block')state.code={pos:block.pos,source:block.node.textContent,language:block.node.attrs.language||''};
  state.tableWidths=blocks.filter(b=>b.node.type.name==='table').map(b=>Array.from({length:b.node.firstChild.childCount},(_,i)=>b.node.firstChild.child(i).attrs.colwidth));
  const {$from}=view.state.selection;
  for(let d=$from.depth;d>0;d--)if($from.node(d).type.name==='table'){
   const table=$from.node(d);state.table={row:$from.index(d),column:$from.index(d+1),rows:table.childCount,columns:table.firstChild.childCount};break;
  }
  const node=view.state.selection.node;
  if(node?.type.name==='image')state.image={pos:view.state.selection.from,...node.attrs};
  if(node&&['math_inline','math_block'].includes(node.type.name))state.math={pos:view.state.selection.from,source:node.textContent,display:node.type.name==='math_block'};
  const encoded=JSON.stringify(state);if(encoded!==lastUI){lastUI=encoded;env.bridge().ui(encoded);}
 }
 function schedule(){if(!uiTimer)uiTimer=setTimeout(ui,35);}
 function hideBlockHandle(){if(blockHandleVisible){blockHandleVisible=false;schedule();}return false;}
 function refresh(){if(view&&!env.busy())view.dispatch(view.state.tr.setMeta(key,'refresh'));schedule();}
 function boundary(tr){return closeHistory(tr);}
 function commit(tr){view.dispatch(boundary(tr));view.dispatch(closeHistory(view.state.tr));env.report();schedule();}
 function range(args={}){
  const block=targetBlock(args);if(!block)return null;
  if(args.selection&&!view.state.selection.empty){const first=topBlock(view.state.selection.from),last=topBlock(Math.max(view.state.selection.from,view.state.selection.to-1));if(block.pos>=first.pos&&block.pos<=last.pos)return {from:first.pos,to:last.pos+last.node.nodeSize};}
  return {from:block.pos,to:block.pos+block.node.nodeSize};
 }
 function moveBlock(args){
  const r=range(args);if(!r)return false;
  let target;
  const blocks=rootBlocks(),idx=blocks.findIndex(b=>b.pos===r.from);
  if(args.direction==='up')target=blocks[idx-1]?.pos;
  else if(args.direction==='down'){const after=blocks.find(b=>b.pos>=r.to);target=after?after.pos+after.node.nodeSize:undefined;}
  else if(typeof args.y==='number'){
   const hit=view.posAtCoords({left:view.dom.getBoundingClientRect().left+24,top:args.y});
   const b=hit?topBlock(hit.pos):null;
   target=b?(args.y>view.nodeDOM(b.pos).getBoundingClientRect().top+view.nodeDOM(b.pos).getBoundingClientRect().height/2?b.pos+b.node.nodeSize:b.pos):(args.y<0?0:view.state.doc.content.size);
  }
  if(target===undefined||target>=r.from&&target<=r.to)return false;
  const content=view.state.doc.slice(r.from,r.to).content;let tr=view.state.tr.delete(r.from,r.to);
  const at=tr.mapping.map(target,-1);tr=tr.insert(at,content);
  tr.setMeta('nextnotesMovedBlock',{from:r.from,to:r.to,at});
  tr.setSelection(TextSelection.near(tr.doc.resolve(Math.min(tr.doc.content.size,at+1))));commit(tr.scrollIntoView());return true;
 }
 function navigateSearch(direction=1){
  const all=matches();if(!all.length){searchIndex=-1;schedule();return false;}
  const from=view.state.selection.from;
  let index=all.findIndex(m=>m.from===from&&m.to===view.state.selection.to);
  if(index<0)index=direction>0?all.findIndex(m=>m.from>=from):all.findLastIndex(m=>m.to<=view.state.selection.to);
  else index=(index+direction+all.length)%all.length;
  if(index<0)index=direction>0?0:all.length-1;
  searchIndex=index;const match=all[index];
  // Reveal a match hidden by a collapsed section without altering Markdown.
  collapsed.clear();view.dispatch(view.state.tr.setSelection(TextSelection.create(view.state.doc,match.from,match.to)).setMeta(key,'refresh').scrollIntoView());schedule();return true;
 }
 function footnote(text){
  let index=1;const used=new Set();view.state.doc.descendants(node=>{if(node.type.name.startsWith('footnote'))used.add(node.attrs.label||node.attrs.identifier);});while(used.has('note'+index))index++;
  const id='note'+index,parsed=env.editor().action(ctx=>ctx.get(parserCtx)(`x[^${id}]\n\n[^${id}]: ${text||'脚注内容'}\n`));
  let reference,definition;parsed.descendants(node=>{if(node.type.name==='footnote_reference')reference=node;if(node.type.name==='footnote_definition')definition=node;});
  if(!reference||!definition)return false;
  let tr=view.state.tr.replaceSelectionWith(reference);const end=tr.doc.content.size;tr=tr.insert(end,definition);commit(tr);return true;
 }
 function setMath(args){
  const type=view.state.schema.nodes[args.display?'math_block':'math_inline'];
  const node=type.create(null,args.source?view.state.schema.text(args.source):null);
  let tr=view.state.tr;
  const at=Number.isInteger(args.pos)?args.pos:-1,old=at>=0?tr.doc.nodeAt(at):null;
  if(old&&old.type.name.startsWith('math_'))tr=tr.replaceWith(at,at+old.nodeSize,node);
  else tr=tr.replaceSelectionWith(node);
  commit(tr);return true;
 }
 function jump(pos){collapsed.clear();view.dispatch(view.state.tr.setMeta(key,'refresh').setSelection(TextSelection.near(view.state.doc.resolve(Math.min(view.state.doc.content.size,pos+1)))).scrollIntoView());view.focus();schedule();}
 function applySlash(index=slashIndex){
  const state=slash(),entry=state?.items[index];if(!entry)return false;
  view.dispatch(closeHistory(view.state.tr.delete(state.from,state.to)));lastSlash='';
  if(entry.command==='attachment'){chooseAttachment();return true;}
  if(entry.command==='math'){env.nativeTool(entry.command);return true;}
  if(entry.command==='footnote')return footnote('脚注内容');
  if(entry.command==='toc')return run('insertToc');
  if(entry.command==='mermaid'){env.command('codeblock','',{join:true});const b=topBlock(view.state.selection.from);commit(view.state.tr.setNodeMarkup(b.pos,undefined,{...b.node.attrs,language:'mermaid'}));return true;}
  return env.command(entry.command,'',{join:true});
 }
 function run(name,args={}){
  if(!view)return false;
  if(typeof args==='string')args={value:args};
  if(name==='dismissSlash'){const p=view.state.selection.$from;suppressedSlash=p.parent.textBetween(0,p.parentOffset);schedule();return true;}
  if(name==='search'){query=args.query||'';caseSensitive=!!args.caseSensitive;searchIndex=-1;refresh();return true;}
  if(name==='findNext')return navigateSearch(args.direction||1);
  if(name==='dragScroll'){window.scrollBy(0,args.delta||0);schedule();return true;}
  if(name==='jump'){jump(args.pos||0);return true;}
  if(name==='toggleFold'){const b=targetBlock(args);if(b?.node.type.name!=='heading')return false;if(collapsed.has(b.pos))collapsed.delete(b.pos);else collapsed.add(b.pos);refresh();return true;}
  if(name==='unfoldAll'){collapsed.clear();refresh();return true;}
  if(name==='copyCode'){const b=targetBlock(args);if(b?.node.type.name!=='code_block')return false;env.bridge()?.copyText(b.node.textContent);notify('代码已复制');return true;}
  if(name==='replace'||name==='replaceAll'){
   if(!editable())return false;const all=matches();if(!all.length)return false;
   const chosen=name==='replaceAll'?all:all.filter(m=>m.from===view.state.selection.from&&m.to===view.state.selection.to);
   if(!chosen.length)return navigateSearch(1);
   let tr=view.state.tr;for(const m of [...chosen].reverse())tr=tr.insertText(args.replacement||'',m.from,m.to);commit(tr);notify(`已替换 ${chosen.length} 处`);return true;
  }
  if(!editable())return false;
  if(name==='slashChoose')return applySlash(args.index);
  if(name==='selectBlock'){
   const b=targetBlock(args);if(!b)return false;
   const selection=args.extend?TextSelection.between(view.state.doc.resolve(Math.min(view.state.selection.from,b.pos)),view.state.doc.resolve(Math.max(view.state.selection.to,b.pos+b.node.nodeSize))):NodeSelection.create(view.state.doc,b.pos);
   view.dispatch(view.state.tr.setSelection(selection).setMeta(key,'block-selection'));view.focus();return true;
  }
  if(name==='moveBlock')return moveBlock(args);
  if(name==='duplicateBlock'||name==='deleteBlock'||name==='copyBlock'){
   const r=range(args);if(!r)return false;const slice=view.state.doc.slice(r.from,r.to);
   if(name==='copyBlock'){clipboardBlock=slice;env.bridge()?.copyText(env.editor().action(ctx=>ctx.get(serializerCtx)(view.state.schema.topNodeType.create(null,slice.content))));notify('块已复制');return true;}
   commit(name==='deleteBlock'?view.state.tr.deleteRange(r.from,r.to):view.state.tr.insert(r.to,slice.content));return true;
  }
  if(name==='pasteBlock'&&clipboardBlock){const r=range(args);if(r){commit(view.state.tr.insert(r.to,clipboardBlock.content));return true;}}
  if(name==='blockType'){
   const b=targetBlock(args);if(!b?.node.isTextblock)return false;
   view.dispatch(view.state.tr.setSelection(TextSelection.create(view.state.doc,b.pos+1,b.pos+b.node.nodeSize-1)));
   return env.command(args.command,'');
  }
  if(name==='codeLanguage'){
   const b=targetBlock(args);if(b?.node.type.name!=='code_block')return false;commit(view.state.tr.setNodeMarkup(b.pos,undefined,{...b.node.attrs,language:args.language||''}));return true;
  }
  if(name==='setMath')return setMath(args);
  if(name==='footnote')return footnote(args.text);
  if(name==='imageAlt'){
   const node=view.state.doc.nodeAt(args.pos);if(node?.type.name!=='image')return false;commit(view.state.tr.setNodeMarkup(args.pos,undefined,{...node.attrs,alt:args.alt||''}));return true;
  }
  if(name==='insertToc'){
   const type=view.state.schema.nodes.toc;if(!type)return false;commit(view.state.tr.replaceSelectionWith(type.create()));return true;
  }
  if(name==='chooseAttachment'){chooseAttachment();return true;}
  return false;
 }
 function makeImport(name,source,data){
  const id='upload-'+env.epoch()+'-'+(++request),currentEpoch=env.epoch();pending++;env.report();schedule();
  return new Promise(resolve=>{imports.set(id,{resolve,epoch:currentEpoch});env.bridge().importFile(id,currentEpoch,source||'',name,data||'');});
 }
 async function uploader(files,schema){
  const nodes=[];pending++;env.report();schedule();
  try{
   for(const file of Array.from(files)){
    if(file.size>32*1024*1024){notify('单个附件最大 32 MiB');continue;}
    const data=await new Promise((resolve,reject)=>{const reader=new FileReader();reader.onload=()=>resolve(String(reader.result).split(',')[1]);reader.onerror=reject;reader.readAsDataURL(file);});
    const result=await makeImport(file.name,'',data);if(!result.ok)continue;
    nodes.push(result.image?schema.nodes.image.create({src:result.url,alt:file.name}):schema.text(file.name,[schema.marks.link.create({href:result.url})]));
   }
   while(env.busy())await new Promise(resolve=>setTimeout(resolve,30));
   return nodes;
  }catch{notify('附件读取失败，请重新选择文件');return nodes;}
  finally{setTimeout(()=>{pending=Math.max(0,pending-1);env.report();schedule();},0);}
 }
 function chooseAttachment(){
  const id='upload-'+env.epoch()+'-'+(++request);bookmarks.set(id,{epoch:env.epoch(),bookmark:view.state.selection.getBookmark()});pending++;env.report();schedule();env.bridge().chooseFile(id,env.epoch());
 }
 async function importResult(id,generation,result){
  const item=imports.get(id);if(item){imports.delete(id);pending=Math.max(0,pending-1);item.resolve(item.epoch===env.epoch()&&generation===env.epoch()?result:{ok:false});}
  const bookmark=bookmarks.get(id);if(bookmark){
   while(env.busy())await new Promise(resolve=>setTimeout(resolve,30));
   bookmarks.delete(id);
   if(result.ok&&bookmark.epoch===env.epoch()&&generation===env.epoch()){
    const schema=view.state.schema,node=result.image?schema.nodes.image.create({src:result.url,alt:result.name||''}):schema.text(result.name||'附件',[schema.marks.link.create({href:result.url})]);
    const selection=bookmark.bookmark.resolve(view.state.doc);commit(view.state.tr.setSelection(selection).replaceSelectionWith(node));
   }
   pending=Math.max(0,pending-1);
  }
  if(!result.ok&&result.error!=='cancelled')notify('附件导入未完成，请重试或检查文件大小与权限');
  env.report();schedule();
 }
 async function exportReady(token){
  if(env.busy()||pending){setTimeout(()=>exportReady(token),40);return;}
  await document.fonts.ready;
  // Wait for diagram rendering and decode images before PDF capture.
  await env.waitForPreview?.();
  await Promise.all(Array.from(view.dom.querySelectorAll('img')).map(image=>image.decode?.().catch(()=>{})));
  const clone=view.dom.cloneNode(true);clone.classList.remove('ProseMirror-focused','editor-block-selection');
  for(const el of clone.querySelectorAll('.editor-selected-block,.ProseMirror-selectednode'))el.classList.remove('editor-selected-block','ProseMirror-selectednode');
  for(const el of clone.querySelectorAll('[contenteditable]'))el.removeAttribute('contenteditable');
  for(const el of clone.querySelectorAll('.folded-content'))el.classList.remove('folded-content');
  for(const el of clone.querySelectorAll('.editor-search-match'))el.classList.remove('editor-search-match');
  for(const el of clone.querySelectorAll('.editor-empty-line')){el.classList.remove('editor-empty-line');el.removeAttribute('data-placeholder');}
  for(const el of clone.querySelectorAll('.ProseMirror-gapcursor,.ProseMirror-separator,.ProseMirror-trailingBreak,.ProseMirror-widget,.column-resize-handle'))el.remove();
  env.bridge().exportReady(token,env.epoch(),clone.outerHTML);
  return clone.outerHTML;
 }
 const plugin=new Plugin({key,
  state:{init:()=>DecorationSet.empty,apply(tr,old,oldState,newState){
   // Capture an empty paragraph before its first keystroke, even between UI reports.
   trackNewParagraph(oldState,!!view?.hasFocus());
   if(newParagraphPos!==null&&tr.docChanged){
    const mapped=tr.mapping.mapResult(newParagraphPos);
    newParagraphPos=mapped.deleted||tr.getMeta('nextnotesMovedBlock')?null:mapped.pos;
   }
   trackNewParagraph(newState,!!view?.hasFocus());
   if(tr.selectionSet||tr.docChanged)blockSelection=tr.getMeta(key)==='block-selection';
   if(tr.docChanged){
    collapsed=new Set(Array.from(collapsed,p=>tr.mapping.map(p)));
    // A move is delete + insert: ordinary mapping loses the moved block's identity.
    const moved=tr.getMeta('nextnotesMovedBlock');
    hover=moved&&hover>=moved.from&&hover<moved.to?moved.at+hover-moved.from:tr.mapping.map(hover);
    for(const item of bookmarks.values())item.bookmark=item.bookmark.map(tr.mapping);
   }
   return env.busy()?old.map(tr.mapping,tr.doc):(tr.docChanged||tr.getMeta(key)?decorations(tr.doc===view?.state.doc?view.state:{doc:tr.doc}):old);
  }},
  props:{attributes:()=>blockSelection?{class:'editor-block-selection'}:{},
   decorations(state){
    const base=key.getState(state),{$from,empty}=state.selection;
    if(blockSelection&&!empty){
     const items=[];
     state.doc.nodesBetween(state.selection.from,state.selection.to,(node,pos)=>{
      items.push(Decoration.node(pos,pos+node.nodeSize,{class:'editor-selected-block'}));return false;
     });
     return base.add(state.doc,items);
    }
    // Decorate only the current empty paragraph; no text or document mutation.
    if(!editable()||!empty||$from.parent.type.name!=='paragraph'||$from.parent.content.size)return base;
    const pos=$from.before();
    return base.add(state.doc,[Decoration.node(pos,pos+$from.parent.nodeSize,
     {class:'editor-empty-line','data-placeholder':'输入"/"进入选择'})]);
   },
   handleKeyDown(v,event){
    if(env.busy())return false;const state=slash();
    if(state&&['ArrowDown','ArrowUp','Enter','Escape'].includes(event.key)){
     if(event.key==='Escape'){suppressedSlash='/'+state.query;lastSlash='';view.dispatch(view.state.tr.setSelection(TextSelection.near(view.state.doc.resolve(view.state.selection.to))));env.bridge()?.dismissSlash();return true;}
     if(event.key==='Enter')return applySlash();
     slashIndex=(slashIndex+(event.key==='ArrowDown'?1:-1)+Math.max(1,state.items.length))%Math.max(1,state.items.length);schedule();return true;
    }
    if(event.altKey&&event.shiftKey&&['ArrowUp','ArrowDown'].includes(event.key))return moveBlock({pos:topBlock(v.state.selection.from)?.pos,direction:event.key==='ArrowUp'?'up':'down',selection:true});
    return false;
   },
   handleDOMEvents:{
    focus(){schedule();return false;},blur(){newParagraphPos=null;schedule();return false;},
    mousemove(v,event){
     if(event.buttons||!editable())return hideBlockHandle();
     const hit=v.posAtCoords({left:event.clientX,top:event.clientY});
     const b=hit?topBlock(hit.pos):null;
     if(!b||b.node.type.name==='raw_source')return hideBlockHandle();
     if(!blockHandleVisible||b.pos!==hover){hover=b.pos;blockHandleVisible=true;schedule();}
     return false;
    }
   }
  },
  view(v){view=v;const observer=new ResizeObserver(schedule);observer.observe(v.dom);schedule();return {update(){env.refreshToc?.();schedule();},destroy(){observer.disconnect();clearTimeout(uiTimer);uiTimer=null;}};}
 });
 return {plugin,run,refresh,state:()=>lastUI?JSON.parse(lastUI):{},ui:schedule,uiNow:ui,uploader,importResult,exportReady,hasPending:()=>pending>0,
  reset(){clearTimeout(uiTimer);uiTimer=null;collapsed.clear();hover=0;blockHandleVisible=false;blockSelection=false;newParagraphPos=null;query='';lastUI='';searchIndex=-1;lastSlash='';for(const item of imports.values())item.resolve({ok:false});imports.clear();bookmarks.clear();pending=0;},
  selectedCode:()=>topBlock(view.state.selection.from),
 };
}
