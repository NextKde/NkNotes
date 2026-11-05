import {$nodeSchema} from '@milkdown/kit/utils';
export const sourceSchema=$nodeSchema('raw_source',()=>({
 group:'block',content:'text*',marks:'',code:true,defining:true,isolating:true,
 parseDOM:[{tag:'pre[data-raw-source]'}],toDOM:()=>['pre',{'data-raw-source':'',class:'raw-source','aria-label':'Markdown 源码编辑器',spellcheck:'false'},['code',0]],
 parseMarkdown:{match:()=>false,runner:()=>{}},
 toMarkdown:{match:node=>node.type.name==='raw_source',runner:(state,node)=>state.addNode('text',undefined,node.textContent)}
}));

// Context participates in ProseMirror's history so identical rendered bodies with
// different front matter or Markdown spellings remain distinguishable on undo.
import {$node} from '@milkdown/kit/utils';
export const documentSchema=$node('doc',()=>({
 content:'block+',attrs:{sourceContext:{default:0}},
 parseMarkdown:{match:({type})=>type==='root',runner:(state,node,type)=>state.injectRoot(node,type)},
 toMarkdown:{match:node=>node.type.name==='doc',runner:(state,node)=>{
  state.openNode('root');const last=node.lastChild;
  state.next(last?.type.name==='paragraph'&&!last.content.size?node.content.cut(0,node.content.size-last.nodeSize):node.content);
 }}
}));
