import {$remark,$nodeSchema} from '@milkdown/kit/utils';
import {Plugin,PluginKey} from '@milkdown/kit/prose/state';
const remarkWiki=$remark('nextnotesWiki',()=>function(){
 const data=this.data();(data.toMarkdownExtensions??=[]).push({handlers:{wikiLink:node=>node.value}});
 return (tree,file)=>{function visit(node){if(!node.children||['link','image','code','inlineCode'].includes(node.type))return;
  node.children=node.children.flatMap(child=>{
   if(child.type!=='text'){visit(child);return [child];}
   const raw=String(file.value||'').slice(child.position?.start.offset,child.position?.end.offset);let rawOffset=0;
   const parts=[];let end=0;for(const match of child.value.matchAll(/\[\[([^\]\n]+)\]\]/g)){
    const rawIndex=raw.indexOf(match[0],rawOffset);if(rawIndex>=0){rawOffset=rawIndex+match[0].length;let slashes=0;for(let i=rawIndex-1;i>=0&&raw[i]==='\\';--i)++slashes;if(slashes%2)continue;}
    if(match.index>end)parts.push({type:'text',value:child.value.slice(end,match.index)});
    parts.push({type:'wikiLink',value:match[0],target:match[1].split('|')[0],label:match[1].includes('|')?match[1].slice(match[1].indexOf('|')+1):''});end=match.index+match[0].length;
   }if(!end)return [child];if(end<child.value.length)parts.push({type:'text',value:child.value.slice(end)});return parts;
  });}visit(tree);};
});
const wikiSchema=$nodeSchema('wiki_link',()=>({
 group:'inline',inline:true,atom:true,attrs:{target:{default:''},label:{default:''}},
 parseDOM:[{tag:'a[data-wiki]',getAttrs:dom=>({target:dom.dataset.wiki,label:dom.dataset.label||''})}],
 toDOM:node=>['a',{'data-wiki':node.attrs.target,'data-label':node.attrs.label,href:'wiki:'+node.attrs.target,class:'wiki-link'},node.attrs.label||node.attrs.target],
 parseMarkdown:{match:node=>node.type==='wikiLink',runner:(state,node,type)=>state.addNode(type,{target:node.target,label:node.label})},
 toMarkdown:{match:node=>node.type.name==='wiki_link',runner:(state,node)=>state.addNode('wikiLink',undefined,'[['+node.attrs.target+(node.attrs.label?'|'+node.attrs.label:'')+']]')}
}));
export const wiki=[remarkWiki,wikiSchema];
export function completion(env){
 let view,request=0,trigger=null,rows=[],selected=0,last='',hidden='';
 const panel=document.createElement('div');panel.className='wiki-completion';panel.hidden=true;panel.setAttribute('role','listbox');document.body.append(panel);
 function close(){panel.hidden=true;trigger=null;last='';rows=[];}
 function current(){const state=view.state;if(!state.selection.empty||env.busy()||!env.canEdit())return null;
  const {$from}=state.selection;if($from.parent.type.spec.code||$from.marks().some(mark=>mark.type.name==='inlineCode'))return null;
  const before=$from.parent.textBetween(0,$from.parentOffset,'\n');const m=before.match(/\[\[([^\]\n]*)$/);if(!m||m[1].length>120)return null;let slashes=0;for(let i=m.index-1;i>=0&&before[i]==='\\';--i)++slashes;if(slashes%2)return null;
  return {from:state.selection.from-m[0].length,to:state.selection.from,query:m[1]};
 }
 function choose(index){const row=rows[index],now=current();if(!row||!trigger||!now||now.from!==trigger.from||now.query!==trigger.query)return close();
  const target=env.relative(row.path);view.dispatch(view.state.tr.replaceWith(now.from,now.to,view.state.schema.nodes.wiki_link.create({target,label:row.title})).scrollIntoView());close();view.focus();
 }
 function draw(){panel.replaceChildren();rows.forEach((row,i)=>{const button=document.createElement('button');button.type='button';button.textContent=row.title+' · '+row.path;button.setAttribute('role','option');button.setAttribute('aria-selected',String(i===selected));button.onmousedown=e=>{e.preventDefault();choose(i);};panel.append(button);});
  if(!rows.length){const text=document.createElement('div');text.textContent='没有匹配笔记';panel.append(text);}
  const rect=view.coordsAtPos(trigger.to);panel.style.left=Math.min(rect.left,Math.max(8,innerWidth-360))+'px';panel.style.top=Math.min(rect.bottom+6,Math.max(8,innerHeight-240))+'px';panel.hidden=false;
 }
 return {
  plugin:new Plugin({key:new PluginKey('wikiCompletion'),view(v){view=v;document.body.append(panel);return {update(){const next=current();if(!next){close();hidden='';return;}const key=env.epoch()+':'+next.from+':'+next.query;if(key===hidden)return;if(key===last)return;trigger=next;last=key;env.lookup(next.query,++request);},destroy(){panel.remove();}};},props:{handleKeyDown(v,event){if(panel.hidden)return false;if(event.key==='Escape'){hidden=last;close();return true;}if(event.key==='ArrowDown'||event.key==='ArrowUp'){selected=Math.max(0,Math.min(rows.length-1,selected+(event.key==='ArrowDown'?1:-1)));draw();return true;}if(event.key==='Enter'&&rows.length){choose(selected);return true;}return false;}}}),
  receive(id,items){if(id!==request||!trigger||!current())return;rows=items;selected=0;draw();},
  reset:close
 };
}
