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
const key=async name=>{xd('keydown','--window',win,name);await wait(120);xd('keyup','--window',win,name);await wait(250)};
const shot=async name=>{await wait(450);execFileSync('spectacle',['-b','-n','-a','-e','-S','-o',base+'/screenshots/'+name+'.png'],{env,stdio:'ignore'});await wait(100)};
const hover=async title=>{const b=(await boxes()).find(x=>x.title===title);assert.ok(b,title);await move(web.x+b.x+70,web.y+b.y+b.height/2-2);await move(web.x+b.x+80,web.y+b.y+b.height/2);assert.equal((await state()).block.title,title);const s=(await state()).block;return {x:web.x+Math.max(12,s.x-34)+13,y:web.y+Math.max(0,s.y)+13}};
const press=async title=>{const h=await hover(title);await move(h.x,h.y);execFileSync('ydotool',['key','272:1'],{env:{...env,YDOTOOL_SOCKET:'/run/user/1000/.ydotool_socket'}});await wait(100)};
const release=async()=>{execFileSync('ydotool',['key','272:0'],{env:{...env,YDOTOOL_SOCKET:'/run/user/1000/.ydotool_socket'}});await wait(320)};
const results=[];
async function test(name,fn){try{await fn();results.push({name,passed:true});console.log('PASS',name)}catch(e){results.push({name,passed:false,error:e.stack});console.error('FAIL',name,e);execFileSync('ydotool',['key','272:0'],{env:{...env,YDOTOOL_SOCKET:'/run/user/1000/.ydotool_socket'}});await key('Escape')}fs.writeFileSync(base+'/native-drag-report.json',JSON.stringify({results},null,2))}
try{
await key('Escape');await click(1219,104);await wait(400);
await test('native drag and repeated drag retain moved block handle',async()=>{
 await load('第一段\n\n第二段\n\n第三段\n\n第四段\n');await click(700,123);
 let b=(await boxes()).find(x=>x.title==='第二段');await press('第四段');await move(web.x+45,web.y+b.y+2);assert.ok((await state()).dropTarget);await shot('drag-boundary');await release();
 assert.deepEqual((await boxes()).map(x=>x.title),['第一段','第四段','第二段','第三段']);assert.equal((await state()).block.title,'第四段');
 b=(await boxes()).find(x=>x.title==='第三段');await press('第四段');await move(web.x+45,web.y+b.bottom-2);await release();assert.deepEqual((await boxes()).map(x=>x.title),['第一段','第二段','第三段','第四段']);assert.equal((await state()).block.title,'第四段');
});
await test('Escape and release outside editor cancel native drag',async()=>{
 const before=await doc();let b=(await boxes()).find(x=>x.title==='第二段');await press('第四段');await move(web.x+45,web.y+b.y+2);assert.ok((await state()).dropTarget);await key('Escape');await release();assert.equal(await doc(),before);assert.equal((await state()).dropTarget,null);
 await press('第四段');await move(web.x-30,web.y+200);await release();assert.equal(await doc(),before);assert.equal((await state()).dropTarget,null);
});
await test('first-block menu disables move-up and empty clipboard paste',async()=>{
 const h=await hover('第一段');await click(h.x,h.y);await shot('block-menu');assert.equal((await state()).block.canMoveUp,false);assert.equal((await state()).canPasteBlock,false);await key('Escape');
});
await test('stationary pointer at edge keeps scrolling and Escape stops it',async()=>{
 await load(Array.from({length:65},(_,i)=>'段落 '+String(i+1).padStart(2,'0')).join('\n\n')+'\n');await press('段落 01');await move(web.x+45,web.y+web.height-18);
 console.log('EDGE before',JSON.stringify(await state()));const s0=await run('scrollY');await wait(650);const s1=await run('scrollY');await wait(500);const s2=await run('scrollY');console.log('EDGE samples',JSON.stringify({s0,s1,s2}));assert.ok(s1>s0+50,{s0,s1,s2});assert.ok(s2>s1+50,{s0,s1,s2});await shot('edge-scroll');await key('Escape');await release();const stop=await run('scrollY');await wait(300);assert.equal(await run('scrollY'),stop);results.push({name:'edge scroll samples',passed:true,s0,s1,s2,stop});
});
}catch(e){console.error(e);results.push({name:'driver',passed:false,error:e.stack})}
finally{execFileSync('ydotool',['key','272:0'],{env:{...env,YDOTOOL_SOCKET:'/run/user/1000/.ydotool_socket'}});fs.writeFileSync(base+'/native-drag-report.json',JSON.stringify({results},null,2));ws.close();setTimeout(()=>process.exit(results.some(x=>x.passed===false)?1:0),200)}
