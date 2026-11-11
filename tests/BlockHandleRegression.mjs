// VM-only: node tests/BlockHandleRegression.mjs after installing web/editor dependencies.
import assert from 'node:assert/strict';
import {Schema} from '../web/editor/node_modules/@milkdown/prose/lib/model.js';
import {EditorState,TextSelection} from '../web/editor/node_modules/@milkdown/prose/lib/state.js';
import {makeTools} from '../web/editor/tools.js';

// Geometry is irrelevant to this interaction-state regression.
globalThis.Element=class {getBoundingClientRect(){return {left:48,top:20,height:26,bottom:46};}};
globalThis.ResizeObserver=class {observe(){} disconnect(){}};
const schema=new Schema({nodes:{doc:{content:'paragraph+'},paragraph:{content:'text*'},text:{}}});
let focused=true,hitPos=1;
const tools=makeTools({canEdit:()=>true,busy:()=>false,bridge:()=>({ui(){}}),epoch:()=>1,report(){}});
const paragraph=text=>schema.node('paragraph',null,text?schema.text(text):null);
const view={
 state:EditorState.create({schema,doc:schema.node('doc',null,[paragraph('第一段'),paragraph('第二段')]),plugins:[tools.plugin]}),
 dom:new Element(),nodeDOM:()=>new Element(),posAtCoords:()=>({pos:hitPos}),hasFocus:()=>focused,
 dispatch(tr){this.state=this.state.apply(tr);},focus(){focused=true;},
};
const lifecycle=tools.plugin.spec.view(view);
const events=tools.plugin.props.handleDOMEvents;
const visible=()=>{tools.uiNow();return tools.state().blockHandleVisible;};
const hover=pos=>{hitPos=pos;events.mousemove(view,{buttons:0,clientX:60,clientY:25});return visible();};
try{
 assert.equal(hover(1),true,'existing content retains its handle while editing');
 assert.equal(hover(6),true,'show another paragraph while the editor retains focus');
 view.state=view.state.apply(view.state.tr.insertText('字'));
 assert.equal(visible(),true,'typing in the first paragraph must not hide the hovered second paragraph');
 assert.equal(hover(1),true,'typing in existing content must not suppress its handle');
 assert.equal(hover(7),true,'hovering another paragraph restores its handle');
 view.state=view.state.apply(view.state.tr.setSelection(TextSelection.create(view.state.doc,7)));
 assert.equal(visible(),true,'moving the caret into existing content keeps its handle');
 assert.equal(hover(1),true,'the previous paragraph is now available');
 assert.equal(hover(7),true);
 focused=false;events.blur();assert.equal(visible(),true,'leaving the editor makes the current paragraph available');
 focused=true;events.focus();assert.equal(visible(),true,'returning focus to existing content retains its handle');
 const unchanged=JSON.stringify(view.state.doc.toJSON());
 const selectedBlocks=()=>tools.plugin.props.decorations(view.state).find().filter(d=>d.type.attrs?.class==='editor-selected-block');
 tools.run('selectBlock',{pos:0});
 assert.equal(tools.plugin.props.attributes().class,'editor-block-selection');
 assert.deepEqual(selectedBlocks().map(d=>[d.from,d.to]),[[0,view.state.doc.firstChild.nodeSize]],'single block highlight covers the node, not its text');
 tools.run('selectBlock',{pos:view.state.doc.firstChild.nodeSize,extend:true});
 assert.equal(selectedBlocks().length,2,'extended block selection highlights both complete blocks');
 assert.equal(JSON.stringify(view.state.doc.toJSON()),unchanged,'block styling must not change document content');
 view.dispatch(view.state.tr.setSelection(TextSelection.create(view.state.doc,1,2)));
 assert.equal(selectedBlocks().length,0,'ordinary text selection exits block styling');
 assert.equal(tools.plugin.props.attributes().class,undefined);
 tools.reset();assert.equal(visible(),false,'a new document must not inherit a hovered handle');
 // Drag the final block upward; the old deletion boundary used to resolve to the last block.
 view.state=EditorState.create({schema,doc:schema.node('doc',null,[paragraph('A'),paragraph('BB'),paragraph('CCC'),paragraph('DDDD')]),plugins:[tools.plugin]});
 focused=true;
 const position=text=>{let found;view.state.doc.forEach((node,pos)=>{if(node.textContent===text)found=pos;});return found;};
 const expectHandle=text=>{tools.uiNow();assert.equal(tools.state().block.pos,position(text));assert.equal(tools.state().block.title,text);assert.equal(tools.state().blockHandleVisible,true,'moved content remains draggable with editor focus');};
 hover(position('DDDD')+1);
 hitPos=position('BB')+1;
 assert.equal(tools.run('moveBlock',{pos:position('DDDD'),y:20}),true);
 expectHandle('DDDD');
 assert.deepEqual(Array.from({length:view.state.doc.childCount},(_,i)=>view.state.doc.child(i).textContent),['A','DDDD','BB','CCC']);
 hitPos=position('CCC')+1;
 assert.equal(tools.run('moveBlock',{pos:position('DDDD'),y:46}),true);
 expectHandle('DDDD');
 assert.equal(tools.run('moveBlock',{pos:position('DDDD'),direction:'up'}),true);expectHandle('DDDD');
 assert.equal(tools.run('moveBlock',{pos:position('DDDD'),direction:'down'}),true);expectHandle('DDDD');
 // Moving a different block must preserve the actual hovered block too.
 hover(position('BB')+1);
 assert.equal(tools.run('moveBlock',{pos:position('DDDD'),direction:'up'}),true);expectHandle('BB');
 tools.reset();
 const freshDoc=schema.node('doc',null,[paragraph('Existing'),paragraph()]);
 view.state=EditorState.create({schema,doc:freshDoc,selection:TextSelection.create(freshDoc,11),plugins:[tools.plugin]});
 assert.equal(hover(11),false,'new empty paragraph has no handle while starting to type');
 view.dispatch(view.state.tr.insertText('New'));
 assert.equal(hover(11),false,'first typing session stays clear after text appears');
 assert.equal(hover(1),true,'other existing blocks remain available');
 view.dispatch(view.state.tr.setSelection(TextSelection.create(view.state.doc,1)));
 assert.equal(hover(11),true,'leaving the new paragraph completes its first editing session');
 view.dispatch(view.state.tr.setSelection(TextSelection.create(view.state.doc,11)));
 assert.equal(hover(11),true,'re-entering its existing text retains the handle');
 // Enter creates another empty paragraph; typing immediately must also stay clear.
 view.dispatch(view.state.tr.setSelection(TextSelection.create(view.state.doc,14)));
 view.dispatch(view.state.tr.split(14));
 view.dispatch(view.state.tr.insertText('Immediate'));
 assert.equal(hover(view.state.selection.from),false,'first input is tracked without waiting for a UI report');
 focused=false;events.blur();assert.equal(visible(),true,'leaving editing restores the new content handle');
 focused=true;events.focus();assert.equal(visible(),true,'existing content stays draggable after refocus');
 console.log('PASS: new-paragraph typing suppression; existing and moved blocks remain draggable; selection styling.');
}finally{lifecycle.destroy();}
