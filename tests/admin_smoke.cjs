// No dependencies. Fails CI if ADMIN cannot build its navigation or panels.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');

const source = fs.readFileSync('main/web/admin.js', 'utf8');
const html = fs.readFileSync('main/web/admin.html', 'utf8');
const keys = ['sections', 'body', 'title', 'intro', 'message'];
for (const key of keys) {
  assert(html.includes('id="' + key + '"'), 'Admin HTML is missing #' + key);
}

function element() {
  return {
    children: [],
    _text: '',
    innerHTML: '',
    classList: { toggle() {}, add() {}, remove() {} },
    set textContent(value) { this._text = value; },
    get textContent() { return this._text; },
    appendChild(child) { this.children.push(child); return child; },
    querySelectorAll() { return []; },
    querySelector() { return element(); },
    addEventListener() {},
    style: {},
    elements: {},
    setAttribute() {}
  };
}
const els = Object.fromEntries(keys.map(k => [k, element()]));
const document = {
  hidden: false,
  querySelector(selector) { return els[selector.slice(1)] || element(); },
  createElement() { return element(); }
};
const status = {
  product: 'PRISMA-PI', firmware: 'CS010R.1', build: 'CS010R',
  health: 'ready', friendly_name: 'PRISMA', hostname: 'test',
  mdns_url: 'http://test.local/', uptime_s: 15, reset_reason: 'power_on',
  settings_schema: 1, device: {model:'ESP32-S3', cores:2, revision:1, flash_bytes:16777216},
  memory: {heap_free:130000, heap_min:95000, psram_total:8388608, psram_free:7300000},
  display: {state:'ready', width:240, height:320},
  touch: {state:'ready', controller:'CST816D', address:21},
  camera: {state:'ready', sensor:'OV5640'},
  imu: {state:'ready', controller:'QMI8658'},
  i2c: {state:'ready'},
  softap: {enabled:true, ssid:'PRISMA', ip:'192.168.4.1', channel:1, max_clients:4, clients:1, security:'WPA2'},
  sta: {configured:false, connected:false}, mdns: {state:'ready'},
  storage: {internal:{mounted:true,total_bytes:100000,used_bytes:1000,free_bytes:99000},sd:{mounted:false}}
};
const samples = {
  '/api/v1/status':status,
  '/api/v1/config':{friendly_name:'PRISMA',hostname:'test',ap:{ssid:'PRISMA',open:false,channel:1,max_clients:4},sta:{}},
  '/api/v1/health':{status:'ready',checks:{}},
  '/api/v1/security':{enabled:false,admin_auth:'NOT CONFIGURED'}
};
const mockFetch = async url => ({
  ok:true,
  text:async() => JSON.stringify(samples[url] || {})
});
const location = {hash:''};
const sandbox = {
  document, window:{fetch:mockFetch,addEventListener(){}}, fetch:mockFetch,
  location, setTimeout(){}, setInterval(){}, URLSearchParams, Headers, console
};
vm.createContext(sandbox);
vm.runInContext(source,sandbox,{filename:'main/web/admin.js'});
async function run() {
  await vm.runInContext('load()',sandbox);
  const sections = vm.runInContext('SECTIONS', sandbox);
  assert(els.sections.children.length===sections.length, 'ADMIN navigation was not built');
  for (const section of sections) {
    const button = els.sections.children.find(b=>b.textContent===section);
    assert(button && typeof button.onclick==='function', 'Missing tab '+section);
    button.onclick();
    assert(els.body.innerHTML.length>40, 'Empty panel '+section);
    assert(!els.body.innerHTML.includes('No se pudo leer el estado'), 'API status failure '+section);
  }
  console.log('ADMIN SMOKE OK:', sections.length, 'sections rendered');
}
run().catch(error=>{console.error(error);process.exitCode=1});
