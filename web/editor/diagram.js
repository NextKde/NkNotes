// Loaded on demand from the application's own qrc resources, never a CDN.
import mermaid from 'mermaid';
mermaid.initialize({startOnLoad:false,securityLevel:'strict',theme:'neutral',flowchart:{htmlLabels:false},maxTextSize:50000,maxEdges:500,suppressErrorRendering:true});
window.nextnotesRenderDiagram=(id,source)=>mermaid.render(id,source);
