import {$remark,$nodeSchema,$view} from '@milkdown/kit/utils';
export function tocPlugin(getView,jump){
 const transform=$remark('nextnotesToc',()=>()=>(tree,file)=>{
  const visit=node=>{if(node.children)node.children=node.children.map(child=>{
   if(child.type==='paragraph'&&child.children?.length===1&&child.children[0].type==='text'&&child.children[0].value.trim().toLowerCase()==='[toc]'&&String(file.value).slice(child.position?.start.offset,child.position?.end.offset).trim().toLowerCase()==='[toc]')return {type:'nextnotesToc'};
   visit(child);return child;
  });};visit(tree);
 });
 const node=$nodeSchema('toc',()=>({group:'block',atom:true,selectable:true,
  parseDOM:[{tag:'nav[data-editor-toc]'}],toDOM:()=>['nav',{'data-editor-toc':'true',class:'editor-toc'}],
  parseMarkdown:{match:n=>n.type==='nextnotesToc',runner:(state,n,type)=>state.addNode(type)},
  toMarkdown:{match:n=>n.type.name==='toc',runner:state=>state.addNode('html',undefined,'[toc]')}
 }));
 const views=new Set();let lastDoc;
 function render(dom){
  const view=getView();if(!view)return;dom.replaceChildren();const title=document.createElement('div');title.className='toc-title';title.textContent='目录';dom.append(title);
  view.state.doc.descendants((n,pos)=>{if(n.type.name!=='heading')return;
   const link=document.createElement('a');link.textContent=n.textContent||'无标题';link.style.paddingInlineStart=(n.attrs.level-1)*14+'px';
   const element=view.nodeDOM(pos);link.href='#'+(element?.id||n.textContent);link.addEventListener('click',event=>{event.preventDefault();event.stopPropagation();jump(pos);});dom.append(link);
  });
  if(dom.childNodes.length===1){const empty=document.createElement('span');empty.textContent='添加标题后会自动显示目录';dom.append(empty);}
 }
 const nodeView=$view(node.node,()=>()=>{const dom=document.createElement('nav');dom.className='editor-toc';dom.dataset.editorToc='true';dom.contentEditable='false';views.add(dom);setTimeout(()=>render(dom),0);
  return {dom,ignoreMutation:()=>true,stopEvent:event=>!!event.target.closest('a'),destroy:()=>views.delete(dom)};
 });
 return {plugins:[transform,node,nodeView],refresh:()=>{const doc=getView()?.state.doc;if(lastDoc===doc)return;lastDoc=doc;for(const dom of views)render(dom); }};
}
