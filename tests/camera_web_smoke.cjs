const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');

const html = fs.readFileSync('main/web/camera-synth.html','utf8');
const match = html.match(/<script>([\s\S]*?)<\/script>/);
assert(match,'missing embedded camera UI');
let requests=[],active=false,snapshots=0,created=0,revoked=0;
const els=new Map();
function node(){
 return {children:[],textContent:'',innerHTML:'',hidden:false,disabled:false,href:'',src:'',
   classList:{toggle(){},add(){},remove(){}},setAttribute(){},
   removeAttribute(name){delete this[name]},appendChild(item){this.children.push(item)},
   onclick:null};
}
for(const name of ['tabs','panel','cameraState','cameraMeta','cameraOn','cameraOff',
                   'takePhoto','cameraAlert','photo','photoDownload','noPhoto'])
 els.set('#'+name,node());
const document={
  hidden:false,
  querySelector(selector){return els.get(selector)||null},
  createElement(){return node()}
};
const fetch=async(url, options={})=>{
 requests.push({url,method:options.method||'GET'});
 if(url==='/api/v1/camera/enable')active=true;
 if(url==='/api/v1/camera/disable')active=false;
 if(url==='/api/v1/camera/enable'||url==='/api/v1/camera/disable'||
    url==='/api/v1/camera/runtime'){
   return {ok:true,json:async()=>({supported:true,enabled:active,
     capture_count:snapshots,failures:0,last_jpeg_bytes:1700,
     last_capture_ms:44,psram_free_bytes:4000000})};
 }
 if(url==='/api/v1/camera/snapshot.jpg'){
   assert(active,'snapshot cannot happen while OFF');
   snapshots++;
   return {ok:true,blob:async()=>({type:'image/jpeg',size:1700})};
 }
 throw Error('unexpected '+url);
};
const url={
 createObjectURL(){created++;return 'blob:test-'+created},
 revokeObjectURL(){revoked++}
};
const sandbox={
 document,window:{addEventListener(){}},fetch,URL:url,
 setInterval(){},console,JSON,Math,Error,Number,String
};
vm.createContext(sandbox);
vm.runInContext(match[1],sandbox,{filename:'camera-synth-inline.js'});
async function flush(){for(let i=0;i<6;i++)await Promise.resolve()}
(async()=>{
 await flush();
 assert.equal(requests.filter(x=>x.url.endsWith('snapshot.jpg')).length,0,'must never auto-capture');
 assert.equal(requests.filter(x=>x.url.endsWith('/enable')).length,0,'must never auto-enable');
 assert(els.get('#cameraState').textContent.includes('OFF'));
 assert.equal(els.get('#takePhoto').disabled,true);

 await els.get('#cameraOn').onclick();
 await flush();
 assert.equal(active,true);
 assert(els.get('#cameraState').textContent.includes('READY'));
 assert.equal(els.get('#takePhoto').disabled,false);

 await els.get('#takePhoto').onclick();
 await flush();
 assert.equal(snapshots,1,'one explicit click = one JPEG request');
 assert.equal(els.get('#photo').hidden,false);
 assert.equal(els.get('#photoDownload').hidden,false);

 await els.get('#cameraOff').onclick();
 await flush();
 assert.equal(active,false);
 assert(revoked>=1,'browser JPEG preview must be released on OFF');
 assert.equal(els.get('#takePhoto').disabled,true);
 assert.equal(snapshots,1,'OFF must not trigger another capture');
 console.log('CAMERA WEB SMOKE PASS: no implicit capture, ON -> PHOTO -> OFF');
})().catch(err=>{console.error(err);process.exitCode=1});
