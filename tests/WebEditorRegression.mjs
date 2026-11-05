import assert from 'node:assert/strict';
import fs from 'node:fs';
import {spawnSync} from 'node:child_process';
const target=(await (await fetch((process.env.NEXTNOTES_CDP_URL||'http://127.0.0.1:9225')+'/json/list')).json()).find(x=>x.url==='qrc:/editor/index.html');
const ws=new WebSocket(target.webSocketDebuggerUrl);await new Promise((r,j)=>{ws.onopen=r;ws.onerror=j;});
let seq=0;const pending=new Map(),results=[];
ws.onmessage=e=>{const m=JSON.parse(e.data);if(m.id){const p=pending.get(m.id);pending.delete(m.id);m.error?p.reject(m.error):p.resolve(m.result);}};
const call=(method,params={})=>new Promise((resolve,reject)=>{const id=++seq;pending.set(id,{resolve,reject});ws.send(JSON.stringify({id,method,params}));});
const run=async expression=>{const r=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});if(r.exceptionDetails)throw Error(JSON.stringify(r.exceptionDetails));return r.result.value;};
const pause=ms=>new Promise(r=>setTimeout(r,ms));
const snap=()=>run('window.nextnotes.snapshot()');
async function key(key,code,keyCode,modifiers=0){await call('Input.dispatchKeyEvent',{type:'keyDown',key,code,windowsVirtualKeyCode:keyCode,modifiers,text:modifiers===0&&key.length===1?key:undefined});await call('Input.dispatchKeyEvent',{type:'keyUp',key,code,windowsVirtualKeyCode:keyCode,modifiers});await pause(30);}
const enter=()=>key('Enter','Enter',13),space=()=>key(' ','Space',32);
async function type(text){
 const symbols={'#':[51,'Digit3',8],'*':[56,'Digit8',8],'`':[192,'Backquote',0],'~':[192,'Backquote',8],'-':[189,'Minus',0],'>':[190,'Period',8],'[':[219,'BracketLeft',0],']':[221,'BracketRight',0],'|':[220,'Backslash',8],':':[186,'Semicolon',8],' ':[32,'Space',0],'.':[190,'Period',0]};
 for(const ch of text){if(ch.charCodeAt(0)>127){await call('Input.insertText',{text:ch});continue;}
 const [vk,code,modifiers]=symbols[ch]||[ch.toUpperCase().charCodeAt(0),'Key'+ch.toUpperCase(),ch!==ch.toLowerCase()?8:0];
 await call('Input.dispatchKeyEvent',{type:'keyDown',key:ch,code,windowsVirtualKeyCode:vk,modifiers,text:ch});
 await call('Input.dispatchKeyEvent',{type:'keyUp',key:ch,code,windowsVirtualKeyCode:vk,modifiers});await pause(20);
 }}
async function clear(){await run('window.nextnotes.view().focus()');await key('a','KeyA',65,2);await key('Backspace','Backspace',8);}
const command=(name,arg='')=>run(`window.nextnotes.command(${JSON.stringify(name)},${JSON.stringify(arg)})`);
function ok(name){results.push({name,passed:true});console.log(name);}
const sourceFile=process.env.NEXTNOTES_TEST_FILE;
assert.ok(sourceFile, 'Set NEXTNOTES_TEST_FILE to the disposable fixture open in the VM app.');
const reportFile=process.env.NEXTNOTES_TEST_REPORT;
assert.equal(decodeURIComponent(new URL((await snap()).baseUrl).pathname),sourceFile,'The active document must be the disposable fixture.');
assert.ok(fs.readFileSync(sourceFile,'utf8').startsWith('---\ncustom: untouched\n---\n'),'Use the documented disposable fixture.');
try {
 await run("window.editorErrors=[];window.onerror=(m,s,l,c,e)=>window.editorErrors.push({message:m,stack:e?.stack})");
 await clear();await type('##');await space();await type('标题');await enter();await type('普通段落');await enter();await type('下一段');
 let s=await snap();assert.equal(s.doc.content[0].type,'heading');assert.equal(s.doc.content[0].attrs.level,2);assert.equal(s.doc.content[1].type,'paragraph');assert.match(s.source,/## 标题\n\n普通段落\n\n下一段/);ok('heading_paragraph_enter');
 await clear();await type('**bold**');await space();await type('*italic*');await space();await type('~~strike~~');await space();await type('`code`');await space();
 s=await snap();for(const m of ['strong','emphasis','strike_through','inlineCode'])assert.ok(JSON.stringify(s.doc).includes('"'+m+'"'),m);ok('inline_markdown_input_rules');
 await clear();await type('-');await space();await type('one');await enter();await type('two');await enter();await enter();await type('outside');
 s=await snap();assert.equal(s.doc.content[0].type,'bullet_list');assert.equal(s.doc.content[0].content.length,2);assert.equal(s.doc.content[1].type,'paragraph');ok('list_continuation_empty_exit');
 await clear();await type('-');await space();await type('[ ]');await space();await type('task');assert.equal((await snap()).doc.content[0].content[0].attrs.checked,false);await command('check');assert.equal((await snap()).doc.content[0].content[0].attrs.checked,true);ok('task_input_and_toggle');
 await clear();await type('>');await space();await type('quote');assert.equal((await snap()).doc.content[0].type,'blockquote');ok('blockquote_input');
 await clear();await command('codeblock');await type('## literal **code**');s=await snap();assert.equal(s.doc.content[0].type,'code_block');assert.ok(!JSON.stringify(s.doc).includes('"strong"'));ok('code_block_literal_content');
 await clear();await type('| A | B |');await enter();await type('| --- | --- |');await enter();
 s=await snap();let table=s.doc.content.find(n=>n.type==='table');assert.ok(table);assert.equal(table.content.length,2);assert.equal(table.content[0].content.length,2);ok('typed_gfm_table');
 await command('rowAfter');s=await snap();table=s.doc.content.find(n=>n.type==='table');assert.equal(table.content.length,3);ok('table_add_row');
 await command('columnAfter');s=await snap();table=s.doc.content.find(n=>n.type==='table');assert.equal(table.content[0].content.length,3);ok('table_add_column');
 await command('alignCenter');assert.ok(JSON.stringify((await snap()).doc).includes('"alignment":"center"'));ok('table_alignment');
 const before=(await snap()).source;await command('deleteColumn');table=(await snap()).doc.content.find(n=>n.type==='table');assert.equal(table.content[0].content.length,2);
 await command('undo');assert.equal((await snap()).source,before);await command('redo');table=(await snap()).doc.content.find(n=>n.type==='table');assert.equal(table.content[0].content.length,2);ok('table_delete_undo_redo');
 await command('exitTable');await type('after table');assert.match((await snap()).source,/after table/);ok('table_exit');
 await clear();spawnSync('wl-copy',['# Pasted\n\n- [ ] task\n\n| H1 | H2 |\n| --- | --- |\n| a | b |\n'],{env:{...process.env,XDG_RUNTIME_DIR:'/run/user/1000',WAYLAND_DISPLAY:'wayland-0'},stdio:'ignore'});await key('v','KeyV',86,2);await pause(100);
 s=await snap();assert.equal(s.doc.content[0].type,'heading');assert.ok(s.doc.content.some(n=>n.type==='table'));assert.ok(JSON.stringify(s.doc).includes('"checked":false'));ok('markdown_clipboard_table_task');
 await pause(1000);assert.ok(fs.readFileSync(sourceFile,'utf8').startsWith('---\ncustom: untouched\n---\n'));ok('front_matter_preserved');
 // End in a normal paragraph for the separate physical fcitx test.
 await command('exitTable');await key('End','End',35,2);await enter();await type('IME: ');await pause(1000);
 if(reportFile)fs.writeFileSync(reportFile,JSON.stringify({results,final:await snap()},null,2));
} catch(error){if(reportFile)fs.writeFileSync(reportFile,JSON.stringify({results,error:error.stack,state:await snap(),errors:await run("window.editorErrors")},null,2));console.error(error);process.exitCode=1;}
ws.close();
