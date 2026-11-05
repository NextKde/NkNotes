// VM-only. Mutates the explicitly named disposable note and restores its bytes.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {spawnSync} from 'node:child_process';
const file=process.env.NEXTNOTES_TEST_FILE;assert(file);
const original=fs.readFileSync(file,'utf8');
const targets=await(await fetch('http://127.0.0.1:9232/json/list')).json();
const ws=new WebSocket(targets.find(t=>t.url==='qrc:/editor/index.html').webSocketDebuggerUrl);
await new Promise(resolve=>ws.onopen=resolve);let serial=0;const pending=new Map();
ws.onmessage=e=>{const m=JSON.parse(e.data);if(m.id){pending.get(m.id)?.(m);pending.delete(m.id);}};
const call=(method,params={})=>new Promise(resolve=>{const id=++serial;pending.set(id,resolve);ws.send(JSON.stringify({id,method,params}));});
const run=async expression=>{const m=await call('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});assert(!m.error&&!m.result.exceptionDetails,JSON.stringify(m));return m.result.result.value;};
const pause=ms=>new Promise(r=>setTimeout(r,ms));
async function saved(text){for(let i=0;i<120;i++){if(fs.readFileSync(file,'utf8')===text)return;await pause(250);}assert.fail(`saved text differs: expected ${text.length} characters, got ${fs.readFileSync(file,'utf8').length}`);}
async function key(key,code,vk,modifiers=0){await call('Input.dispatchKeyEvent',{type:'keyDown',key,code,windowsVirtualKeyCode:vk,modifiers});await call('Input.dispatchKeyEvent',{type:'keyUp',key,code,windowsVirtualKeyCode:vk,modifiers});await pause(100);}
async function select(anchor,head=anchor){await run(`(()=>{const c=nextnotes.sourceView();c.dispatch({selection:{anchor:${anchor},head:${head}},scrollIntoView:true});c.focus();return true})()`);await pause(150);}
const activeFile=decodeURIComponent(new URL(await run('nextnotes.snapshot().baseUrl')).pathname);
if(activeFile!==file){ws.close();throw new Error('Open the named disposable fixture before running this test.');}
try{
 if(!await run('nextnotes.snapshot().sourceMode'))assert(await run('nextnotes.command("sourceMode")'));
 await pause(250);
 assert.equal(await run('nextnotes.sourceView().state.doc.toString()'),original);
 if(original.length>1000000)assert(await run('document.querySelectorAll(".cm-line").length')<500,'source view rendered the whole note');
 const at=Math.floor(original.length/2),marker='VIEWPORT_INPUT_测试';
 await select(at);const began=performance.now();
 await call('Input.insertText',{text:marker});
 const typedMs=Math.round(performance.now()-began),changed=original.slice(0,at)+marker+original.slice(at);
 await saved(changed);assert.equal(await run('nextnotes.snapshot().fullSource'),changed);
 await key('z','KeyZ',90,2);await saved(original);
 assert.equal(await run('nextnotes.sourceView().state.doc.toString()'),original);
 // Reversed selection, clipboard, newlines and indentation cross the viewport.
 const end=original.length,start=Math.max(0,end-2000),pasted='VIEWPORT_SEARCH_TARGET\nsecond line\n';
 await select(end,start);
 assert.deepEqual(await run('({anchor:nextnotes.view().state.selection.anchor,head:nextnotes.view().state.selection.head})'),{anchor:end+1,head:start+1});
 const copied=spawnSync('wl-copy',[],{input:pasted,stdio:['pipe','ignore','ignore'],env:{...process.env,XDG_RUNTIME_DIR:'/run/user/1000',WAYLAND_DISPLAY:'wayland-0'}});assert.equal(copied.status,0);
 await pause(300);
 await key('v','KeyV',86,2);await saved(original.slice(0,start)+pasted);
 await key('Enter','Enter',13);await key('Tab','Tab',9);await call('Input.insertText',{text:'尾部'});
 const tail=await run('nextnotes.snapshot().fullSource');assert(tail.startsWith(original.slice(0,start)+pasted+'\n'));assert(tail.endsWith('尾部'));await saved(tail);
 assert(await run('nextnotes.tools("search",{query:"VIEWPORT_SEARCH_TARGET"})'));
 assert(await run('nextnotes.tools("findNext")'));await pause(300);
 assert.equal(await run('nextnotes.sourceView().state.sliceDoc(nextnotes.sourceView().state.selection.main.from,nextnotes.sourceView().state.selection.main.to)'),'VIEWPORT_SEARCH_TARGET');
 for(let i=0;i<20&&!await run('!!document.querySelector(".source-viewport .editor-search-match")');i++)await pause(100);
 assert(await run('!!document.querySelector(".source-viewport .editor-search-match")'),JSON.stringify(await run(`(()=>{const c=nextnotes.sourceView();return {selection:c.state.selection.main,viewport:c.viewport,lines:document.querySelectorAll('.cm-line').length,facets:c.state.facet(c.constructor.decorations).map(s=>typeof s==='function'?'function':{from:s.iter().from,to:s.iter().to,class:s.iter().value?.spec?.class}),search:nextnotes.toolsState().search}})()`)));
 await select(start);await call('Input.insertText',{text:'prefix '});await pause(300);
 assert.equal(await run('document.querySelector(".source-viewport .editor-search-match")?.textContent'),'VIEWPORT_SEARCH_TARGET','search highlight did not follow editing');
 console.log(JSON.stringify({sourceViewport:true,actualInputRoundtripMs:typedMs,keyboardUndo:true,reversedSelection:true,clipboard:true,enterTab:true,offscreenSearch:true}));
}finally{
 await run(`nextnotes.command("replaceSource",${JSON.stringify(original)})`);await saved(original);
 await run('nextnotes.tools("search",{query:""});true');ws.close();
}
