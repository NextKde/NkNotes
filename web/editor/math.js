// Markdown AST integration uses remark-math; rendering uses KaTeX (both MIT).
// All ProseMirror types come from the same Milkdown kit instance.
import remarkMath from 'remark-math';
import katex from 'katex';
import {$remark, $nodeSchema, $inputRule, $view} from '@milkdown/kit/utils';
import {NodeSelection} from '@milkdown/kit/prose/state';
import {Fragment} from '@milkdown/kit/prose/model';
import {InputRule} from '@milkdown/kit/prose/inputrules';
const remarkMathPlugin=$remark('nextnotesRemarkMath',()=>remarkMath);
function schema(name,inline){return $nodeSchema(name,()=>({
 group:inline?'inline':'block',inline,atom:true,code:true,isolating:true,content:'text*',marks:'',
 parseDOM:[{tag: inline?'span[data-math-source]':'div[data-math-source]',getAttrs:()=>null,getContent:(dom,schema)=>dom.dataset.mathSource?Fragment.from(schema.text(dom.dataset.mathSource)):Fragment.empty}],
 toDOM:node=>[inline?'span':'div',{'data-math-source':node.textContent,class:inline?'math-inline':'math-block'},node.textContent],
 parseMarkdown:{match:node=>node.type===(inline?'inlineMath':'math'),runner:(state,node,type)=>{state.openNode(type);if(node.value)state.addText(node.value);state.closeNode();}},
 toMarkdown:{match:node=>node.type.name===name,runner:(state,node)=>{state.addNode(inline?'inlineMath':'math',undefined,node.textContent);}}
}));}
export const inlineMathSchema=schema('math_inline',true),blockMathSchema=schema('math_block',false);
function mathView(type,display){return $view(type.node,()=>(node,view,getPos)=>{
 const dom=document.createElement(display?'div':'span');dom.className=display?'math-block':'math-inline';dom.contentEditable='false';
 const render=value=>{node=value;dom.dataset.mathSource=node.textContent;
  katex.render(node.textContent||'\\square',dom,{displayMode:display,throwOnError:false,trust:false,strict:'warn',maxExpand:1000});};
 render(node);
 dom.addEventListener('dblclick',()=>{view.dispatch(view.state.tr.setSelection(NodeSelection.create(view.state.doc,getPos())));window.nextnotes.openMath();});
 return {dom,update:next=>{if(next.type!==node.type)return false;if(next.textContent!==node.textContent)render(next);node=next;return true;},
  selectNode:()=>dom.classList.add('ProseMirror-selectednode'),deselectNode:()=>dom.classList.remove('ProseMirror-selectednode'),ignoreMutation:()=>true};
});}
const inlineRule=$inputRule(ctx=>new InputRule(/\$([^$\s](?:[^$\n]*[^$\s])?)\$$/,(state,match,start,end)=>{
 if(start>0&&state.doc.textBetween(start-1,start)==='\\')return null;
 return state.tr.replaceWith(start,end,inlineMathSchema.type(ctx).create(null,state.schema.text(match[1])));
}));
export const math=[remarkMathPlugin,inlineMathSchema,blockMathSchema,mathView(inlineMathSchema,false),mathView(blockMathSchema,true),inlineRule];
