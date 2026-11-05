// VM only. The named disposable note must be open. Restores its content at end.
import assert from 'node:assert/strict';
import fs from 'node:fs';
const file=process.env.NEXTNOTES_TEST_FILE;
assert(file,'Set NEXTNOTES_TEST_FILE to an open disposable note');
const endpoint=process.env.NEXTNOTES_CDP_URL||'http://127.0.0.1:9231';
const targets=await(await fetch(endpoint+'/json/list')).json();
const ws=new WebSocket(targets.find(t=>t.url==='qrc:/editor/index.html').webSocketDebuggerUrl);
await new Promise(resolve=>ws.onopen=resolve);let sequence=0;const pending=new Map();
ws.onmessage=e=>{const r=JSON.parse(e.data);if(r.id){pending.get(r.id)?.(r);pending.delete(r.id);}};
const call=(method,params={})=>new Promise(resolve=>{const id=++sequence;pending.set(id,resolve);ws.send(JSON.stringify({id,method,params}));});
const run=async expression=>{const r=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});assert(!r.result.exceptionDetails,JSON.stringify(r));return r.result.result.value;};
const wait=ms=>new Promise(r=>setTimeout(r,ms));
const snapshot=()=>run('nextnotes.snapshot()');
assert.equal(new URL((await snapshot()).baseUrl).pathname,encodeURI(file));
const original=(await snapshot()).fullSource;
const mode=async raw=>{if((await snapshot()).sourceMode!==raw)assert(await run('nextnotes.command("sourceMode")'));await wait(150);assert.equal((await snapshot()).sourceMode,raw);};
try {
 // Observe the actual WebChannel payload without replacing its native delivery.
 await run(`(()=>{window.sourceTestSnapshots=[];window.sourceTestSend=qt.webChannelTransport.send;
 qt.webChannelTransport.send=function(data){const message=JSON.parse(data);for(const arg of message.args||[]){if(typeof arg!=='string')continue;try{const state=JSON.parse(arg);if(state&&'sequence' in state&&'loaded' in state)sourceTestSnapshots.push(state);}catch{}}
 return sourceTestSend.call(this,data);};return true})()`);
 await mode(false);await mode(true);assert.equal((await snapshot()).fullSource,original);
 await run(`(()=>{sourceTestSnapshots=[];const v=nextnotes.view();v.dispatch(v.state.tr.setSelection(v.state.selection.constructor.near(v.state.doc.resolve(1))));return true})()`);await wait(150);
 const selectionSnapshots=await run('sourceTestSnapshots');
 assert(selectionSnapshots.length>0,'selection state was not published');
 assert(selectionSnapshots.every(s=>!('source' in s)&&!('plain' in s)),'selection-only state retransmitted the full note');
 const prefix='---\nsource_test: retained\n---\n';
 await run(`(()=>{let v=nextnotes.view();v.dispatch(v.state.tr.insertText(${JSON.stringify(prefix)},1,1));return true})()`);await wait(100);
 const changed=prefix+original;assert.equal((await snapshot()).fullSource,changed);
 assert((await run('sourceTestSnapshots')).some(s=>s.fullSource&&s.source===changed),'changed source was not published');
 await mode(false);assert.equal((await snapshot()).fullSource,changed);
 await mode(true);assert.equal((await snapshot()).fullSource,changed);
 // Undo both mode transitions, the source edit, and initial mode transition.
 for(let i=0;i<4;i++){assert(await run('nextnotes.command("undo")'));await wait(100);}
 assert.equal((await snapshot()).sourceMode,false);assert.equal((await snapshot()).fullSource,original);
 for(let i=0;i<4;i++){assert(await run('nextnotes.command("redo")'));await wait(100);}
 assert.equal((await snapshot()).sourceMode,true);assert.equal((await snapshot()).fullSource,changed);
 const unsupported=changed+'\n<div>kept</div>\n[[unknown]]\n';
 await run(`(()=>{const v=nextnotes.view();v.dispatch(v.state.tr.insertText(${JSON.stringify(unsupported)},1,1+v.state.doc.firstChild.content.size));return true})()`);await wait(150);
 assert.equal(await run('nextnotes.command("sourceMode")'),false);assert.equal((await snapshot()).fullSource,unsupported);
 await wait(1300);assert.equal(fs.readFileSync(file,'utf8'),unsupported);
 // Chromium IME events exercise the same source contenteditable as real input.
 await run('nextnotes.view().focus()');
 const beforeIme=fs.readFileSync(file,'utf8');
 await call('Input.imeSetComposition',{text:'中文',selectionStart:2,selectionEnd:2});await wait(900);
 assert.equal((await snapshot()).composing,true);assert.equal(fs.readFileSync(file,'utf8'),beforeIme);
 await call('Input.insertText',{text:'中文'});await wait(1300);
 assert.equal((await snapshot()).composing,false);assert((await snapshot()).fullSource.includes('中文'));
 assert.equal(fs.readFileSync(file,'utf8'),(await snapshot()).fullSource);
 console.log('source_roundtrip_frontmatter_cross_mode_undo_redo_unsupported_save_incremental_bridge_pass');
} finally {
 await mode(true);
 await run(`(()=>{const v=nextnotes.view();v.dispatch(v.state.tr.insertText(${JSON.stringify(original)},1,1+v.state.doc.firstChild.content.size));return true})()`);
 await wait(1200);
 await run('qt.webChannelTransport.send=sourceTestSend;delete window.sourceTestSend;delete window.sourceTestSnapshots;true');
 ws.close();
}
