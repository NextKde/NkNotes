let id=0,loader;const active=new Set();
function renderer(){
 if(!loader)loader=new Promise((resolve,reject)=>{const script=document.createElement('script');script.src='qrc:/editor/dist/diagram.js';script.onload=()=>resolve(window.nextnotesRenderDiagram);script.onerror=()=>reject(new Error('图表组件加载失败'));document.head.append(script);});
 return loader;
}
export async function waitForCodePreviews(){await Promise.all(Array.from(active,read=>read()));}
export function codeNodeView(initial,view,getPos){
 let node=initial,timer,version=0,resolvePending;let pending=Promise.resolve();const readPending=()=>pending;active.add(readPending);
 const dom=document.createElement('figure');dom.className='editor-code';
 const pre=document.createElement('pre'),contentDOM=document.createElement('code');contentDOM.spellcheck=false;pre.append(contentDOM);dom.append(pre);
 const preview=document.createElement('div');preview.className='diagram-preview';preview.contentEditable='false';dom.append(preview);
 function render(){
  clearTimeout(timer);resolvePending?.();const thisVersion=++version;
  dom.dataset.language=node.attrs.language||'plain';contentDOM.dataset.language=node.attrs.language||'plain';
  preview.hidden=node.attrs.language!=='mermaid';
  if(preview.hidden)return;
  pending=new Promise(resolve=>{resolvePending=resolve;});
  const finish=resolvePending;
  timer=setTimeout(async()=>{
   const source=node.textContent;if(source.length>50000){preview.textContent='图表内容过长，请缩小图表。';finish?.();return;}
   try{const render=await renderer();const {svg}=await render('nk-diagram-'+(++id),source);
    if(thisVersion!==version)return;
    const image=document.createElement('img');image.alt='Mermaid 图表';image.src='data:image/svg+xml;charset=utf-8,'+encodeURIComponent(svg);preview.replaceChildren(image);
   }catch{if(thisVersion===version)preview.textContent='图表语法尚未完整，请继续编辑上方源码。';}
   finally{finish?.();}
  },250);
 }
 render();
 return {dom,contentDOM,update:next=>{if(next.type!==node.type)return false;const changed=next.textContent!==node.textContent||next.attrs.language!==node.attrs.language;node=next;if(changed)render();return true;},
  ignoreMutation:mutation=>mutation.type==='selection'?false:!contentDOM.contains(mutation.target),destroy:()=>{clearTimeout(timer);resolvePending?.();active.delete(readPending);++version;}};
}
