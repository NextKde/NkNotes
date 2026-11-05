// Run only in the VM, with web/editor as cwd. Compare the patched serializer
// against the installed, unmodified version, including adjacent/nested marks.
import assert from 'node:assert/strict';
import {build} from '../web/editor/node_modules/esbuild/lib/main.js';
import {mkdtemp,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {pathToFileURL} from 'node:url';
import {Schema} from '../web/editor/node_modules/@milkdown/prose/lib/model.js';
import {SerializerState as Original} from '../web/editor/node_modules/@milkdown/transformer/lib/index.js';
import {transformerPlugin,patchTransformer} from '../web/editor/transformer-patch.mjs';
assert.throws(()=>patchTransformer('changed upstream'),/review/);
const directory=await mkdtemp(join(tmpdir(),'nextnotes-serializer-'));
try{
 const output=join(directory,'patched.mjs');
 await build({entryPoints:['node_modules/@milkdown/transformer/lib/index.js'],bundle:true,format:'esm',platform:'node',outfile:output,plugins:[transformerPlugin]});
 const {SerializerState:Patched}=await import(pathToFileURL(output));
 const block=(name,type)=>({content:'inline*',group:'block',toMarkdown:{match:n=>n.type.name===name,runner:(s,n)=>{s.openNode(type);s.next(n.content);s.closeNode();}}});
 const schema=new Schema({nodes:{
  doc:{content:'block+',toMarkdown:{match:n=>n.type.name==='doc',runner:(s,n)=>{s.openNode('root');s.next(n.content);}}},
  paragraph:block('paragraph','paragraph'),
  text:{group:'inline',toMarkdown:{match:n=>n.type.name==='text',runner:(s,n)=>s.addNode('text',undefined,n.text)}}
 },marks:Object.fromEntries(['strong','emphasis','link'].map(name=>[name,{attrs:name==='link'?{href:{default:'target'}}:{},toMarkdown:{match:m=>m.type.name===name,runner:(s,m)=>{s.withMark(m,name,undefined,{...m.attrs});}}}]))});
 const text=(value,marks=[])=>schema.text(value,marks.map(m=>schema.mark(m)));
 const paragraph=(...content)=>schema.node('paragraph',null,content);
 const cases=[
  [paragraph()],
  [paragraph(text('plain')),paragraph(text('next'))],
  [paragraph(text('one',['strong']),text('two',['strong','emphasis']),text('three',['strong']),text('four',['link']),text(' five'))],
  [paragraph(text(' leading ',['strong']),text('more',['emphasis']),text(' space ',['link','strong']))],
  Array.from({length:16000},(_,i)=>paragraph(text('Paragraph '+i),text(' marked',['strong']))),
 ];
 for(const children of cases){const doc=schema.node('doc',null,children);assert.deepEqual(new Patched(schema).run(doc).build(),new Original(schema).run(doc).build());}
 console.log('serializer_original_equivalence_nested_marks_whitespace_16000_blocks_pass');
}finally{await rm(directory,{recursive:true,force:true});}
