import {build} from 'esbuild';
import {transformerPlugin} from './transformer-patch.mjs';
await build({entryPoints:['editor.js','diagram.js'],bundle:true,format:'iife',target:'chrome120',outdir:'dist',legalComments:'eof',logLevel:'info',plugins:[transformerPlugin]});
