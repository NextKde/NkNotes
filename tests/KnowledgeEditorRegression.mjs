// VM only: set NEXTNOTES_TEST_FILE to the open disposable a.md with a b.md sibling.
import assert from 'node:assert/strict';
import fs from 'node:fs';
const file=process.env.NEXTNOTES_TEST_FILE;assert(file);
const targets=await(await fetch((process.env.NEXTNOTES_CDP_URL||'http://127.0.0.1:9232')+'/json/list')).json();
const ws=new WebSocket(targets.find(t=>t.url==='qrc:/editor/index.html').webSocketDebuggerUrl);await new Promise(r=>ws.onopen=r);
let seq=0;const pending=new Map();ws.onmessage=e=>{const m=JSON.parse(e.data);if(m.id){pending.get(m.id)?.(m);pending.delete(m.id);}};
const call=(method,params={})=>new Promise(r=>{const id=++seq;pending.set(id,r);ws.send(JSON.stringify({id,method,params}));});
const run=async expression=>{const r=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});assert(!r.result.exceptionDetails,JSON.stringify(r));return r.result.result.value;};
const wait=ms=>new Promise(r=>setTimeout(r,ms));
try {
 const initial=await run('nextnotes.snapshot()');assert.equal(decodeURIComponent(new URL(initial.baseUrl).pathname),file);
 if(initial.sourceMode){assert(await run('nextnotes.command("sourceMode")'));await wait(150);}
 assert.equal(await run('document.querySelectorAll("a[data-wiki]").length'),1);
 await run(`(()=>{const v=nextnotes.view(),p=v.state.doc.content.size;let tr=v.state.tr.insert(p,v.state.schema.nodes.paragraph.create(null,v.state.schema.text('[[Be')));tr.setSelection(v.state.selection.constructor.create(tr.doc,p+5));v.dispatch(tr);v.focus();return true})()`);
 await wait(600);
 const candidates=await run('Array.from(document.querySelectorAll(".wiki-completion button")).map(b=>b.textContent)');assert(candidates.some(s=>s.includes('Beta')));
 await call('Input.dispatchKeyEvent',{type:'keyDown',key:'Enter',code:'Enter',windowsVirtualKeyCode:13});await call('Input.dispatchKeyEvent',{type:'keyUp',key:'Enter',code:'Enter',windowsVirtualKeyCode:13});await wait(1300);
 assert.equal(await run('document.querySelectorAll("a[data-wiki]").length'),2);
 assert(fs.readFileSync(file,'utf8').includes('[[./b.md|Beta]]'));
 assert(fs.readFileSync(file,'utf8').includes('`[[not a link]]`'));
 await run('document.querySelector("a[data-wiki]").dispatchEvent(new MouseEvent("click",{bubbles:true,ctrlKey:true}))');await wait(800);
 assert((await run('nextnotes.snapshot().baseUrl')).endsWith('/b.md'));
 const beforeTarget=await run('nextnotes.snapshot().fullSource');
 try {
  const mixed=String.raw`\[[literal]] [[./a.md|Alpha]]`+'\n\n`[[code]]`\n';
  await run(`nextnotes.command("replaceSource",${JSON.stringify(mixed)})`);await wait(100);
  assert(await run('nextnotes.command("sourceMode")'));await wait(200);
  assert.equal(await run('document.querySelectorAll("a[data-wiki]").length'),1);
  assert.equal(await run('document.querySelector("a[data-wiki]").dataset.wiki'),'./a.md');
  assert((await run('nextnotes.snapshot().fullSource')).includes('\\[[literal]]'));
 }finally {await run(`nextnotes.command("replaceSource",${JSON.stringify(beforeTarget)})`);await wait(1300);}
 console.log('wiki_parse_escaped_mixed_text_code_exclusion_completion_keyboard_serialization_navigation_pass');
}finally{ws.close();}
