import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import fs from 'node:fs';
const base='/home/amao/nknotes-review-agent-20261111',win='16777260';
const env={...process.env,DISPLAY:':0',XAUTHORITY:'/run/user/1000/xauth_AlxRJL',XDG_RUNTIME_DIR:'/run/user/1000',DBUS_SESSION_BUS_ADDRESS:'unix:path=/run/user/1000/bus'};
const xd=(...args)=>execFileSync('xdotool',args.map(String),{env,encoding:'utf8'});
xd('windowactivate','--sync',win);
const target=(await(await fetch('http://127.0.0.1:9241/json/list')).json()).find(x=>x.url==='qrc:/editor/index.html');
const ws=new WebSocket(target.webSocketDebuggerUrl);await new Promise(r=>ws.onopen=r);
let serial=0;const pending=new Map();
ws.onmessage=e=>{const r=JSON.parse(e.data),p=pending.get(r.id);if(p){pending.delete(r.id);clearTimeout(p.timer);r.error?p.reject(r.error):p.resolve(r.result)}};
const call=(method,params={})=>new Promise((resolve,reject)=>{const id=++serial,timer=setTimeout(()=>{pending.delete(id);reject(Error('Timeout '+method+' '+JSON.stringify(params)))},7000);pending.set(id,{resolve,reject,timer});ws.send(JSON.stringify({id,method,params}))});
const run=async expression=>{const r=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});if(r.exceptionDetails)throw Error(JSON.stringify(r.exceptionDetails));return r.result.value};
const wait=ms=>new Promise(r=>setTimeout(r,ms));
const state=()=>run('nextnotes.toolsState()');
const doc=()=>run('nextnotes.snapshot().source');
const boxes=()=>run('(()=>{const v=nextnotes.view(),out=[];v.state.doc.forEach((node,pos)=>{const r=v.nodeDOM(pos).getBoundingClientRect();out.push({pos,title:node.textContent,x:r.left,y:r.top,height:r.height,bottom:r.bottom})});return out})()');
const load=async text=>{await run('nextnotes.load({epoch:nextnotes.snapshot().epoch,baseUrl:nextnotes.snapshot().baseUrl,source:'+JSON.stringify(text)+'})');await wait(1200);await run('nextnotes.view().focus()');await wait(500)};
let web={x:509,y:91,width:771,height:682};
const move=async(x,y)=>{xd('mousemove','--window',win,Math.round(x),Math.round(y));await wait(420)};
const click=async(x,y)=>{await move(x,y);xd('click','--window',win,1);await wait(220)};
const key=async name=>{assert.equal(name,'Escape');execFileSync('ydotool',['key','1:1','1:0'],{env:{...env,YDOTOOL_SOCKET:'/run/user/1000/.ydotool_socket'}});await wait(220)};
const shot=async name=>{await wait(450);execFileSync('spectacle',['-b','-n','-a','-e','-S','-o',base+'/screenshots/'+name+'.png'],{env,stdio:'ignore'});await wait(100)};
const hover=async title=>{const b=(await boxes()).find(x=>x.title===title);assert.ok(b,title);await move(web.x+b.x+70,web.y+b.y+b.height/2-2);await move(web.x+b.x+80,web.y+b.y+b.height/2);assert.equal((await state()).block.title,title);const s=(await state()).block;return {x:web.x+Math.max(12,s.x-34)+13,y:web.y+Math.max(0,s.y)+13}};
const press=async title=>{const h=await hover(title);await move(h.x,h.y);execFileSync('ydotool',['key','272:1'],{env:{...env,YDOTOOL_SOCKET:'/run/user/1000/.ydotool_socket'}});await wait(100)};
const release=async()=>{execFileSync('ydotool',['key','272:0'],{env:{...env,YDOTOOL_SOCKET:'/run/user/1000/.ydotool_socket'}});await wait(320)};
const results=[];
async function test(name,fn){try{await fn();results.push({name,passed:true});console.log('PASS',name)}catch(e){results.push({name,passed:false,error:e.stack});console.error('FAIL',name,e);execFileSync('ydotool',['key','272:0'],{env:{...env,YDOTOOL_SOCKET:'/run/user/1000/.ydotool_socket'}});await key('Escape')}fs.writeFileSync(base+'/native-report.json',JSON.stringify({results},null,2))}
try{
await load('第一段\n\n第二段\n\n第三段\n\n第四段\n');execFileSync('ydotool',['key','29:0','42:0','54:0','56:0','97:0','100:0'],{env:{...env,YDOTOOL_SOCKET:'/run/user/1000/.ydotool_socket'}});await click(700,123);await run('window.cancelEvents=[];document.addEventListener("keydown",e=>cancelEvents.push({key:e.key,type:e.type}),true);true');
await test('outside release cancels without changing content',async()=>{
 const before=await doc();await press('第四段');await move(web.x+45,web.y+55);assert.ok((await state()).dropTarget);await move(web.x-40,web.y+160);await release();assert.equal(await doc(),before);assert.equal((await state()).dropTarget,null);
});
await test('Escape cancels preview before mouse release',async()=>{
 const before=await doc();await press('第四段');await move(web.x+45,web.y+55);assert.ok((await state()).dropTarget);xd('keydown','--window',win,'Escape');await wait(160);xd('keyup','--window',win,'Escape');await wait(600);console.log('KEY EVENTS',JSON.stringify(await run('cancelEvents')));const afterEscape=await state();console.log('AFTER ESCAPE',JSON.stringify(afterEscape));await release();assert.equal(await doc(),before);assert.equal(afterEscape.dropTarget,null);
});
}catch(e){console.error(e);results.push({name:'driver',passed:false,error:e.stack})}
finally{execFileSync('ydotool',['key','272:0'],{env:{...env,YDOTOOL_SOCKET:'/run/user/1000/.ydotool_socket'}});fs.writeFileSync(base+'/native-cancel-report.json',JSON.stringify({results},null,2));ws.close();setTimeout(()=>process.exit(results.some(x=>x.passed===false)?1:0),200)}
