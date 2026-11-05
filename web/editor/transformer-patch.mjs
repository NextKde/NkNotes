// Milkdown 7.23.0 copies the accumulated children array for every sibling.
// Keep its mark-merging semantics, but append ordinary siblings in place.
// Applied only to the build input; node_modules stays untouched. Fail closed
// on upstream changes so a dependency update cannot silently drop this fix.
export function patchTransformer(source){
 const start=source.indexOf('//#region src/serializer/state.ts');
 if(start<0)throw new Error('Milkdown serializer changed; review transformer-patch.mjs before building.');
 const prefix=source.slice(0,start);source=source.slice(start);
 const replacements=[
  ['this.#openMarks = [];\n\t\tthis.#matchTarget = (node) => {','this.#openMarks = [];\n\t\tconst targets = Object.values({...schema.nodes, ...schema.marks});\n\t\tthis.#matchTarget = (node) => {'],
  ['const result = Object.values({\n\t\t\t\t...this.schema.nodes,\n\t\t\t\t...this.schema.marks\n\t\t\t}).find((x) => {','const result = targets.find((x) => {'],
  ['node.children = children.reduce((nextChildren, child, index) => {','const nextChildren = [];\n\t\t\tchildren.forEach((child) => {'],
  ['\t\t\t\tif (index === 0) return [child];\n',''],
  ['return nextChildren.slice(0, -1).concat(this.#maybeMergeChildren(next));','nextChildren[nextChildren.length - 1] = this.#maybeMergeChildren(next);\n\t\t\t\t\t\treturn;'],
  ['return nextChildren.concat(child);\n\t\t\t}, []);','nextChildren.push(child);\n\t\t\t});\n\t\t\tnode.children = nextChildren;']
 ];
 for(const [before,after] of replacements){
  if(source.split(before).length!==2)throw new Error('Milkdown serializer changed; review transformer-patch.mjs before building.');
  source=source.replace(before,after);
 }
 return prefix+source;
}
export const transformerPlugin={name:'nextnotes-linear-siblings',setup(build){
 build.onLoad({filter:/[/\\]@milkdown[/\\]transformer[/\\]lib[/\\]index\.js$/},async args=>{
  const {readFile}=await import('node:fs/promises');
  return {contents:patchTransformer(await readFile(args.path,'utf8')),loader:'js'};
 });
}};
