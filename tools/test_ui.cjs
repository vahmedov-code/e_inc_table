// Executes the actual inline UI against an in-memory API and a real Canvas engine.
// No serial device or browser automation is involved.
const {createRequire}=require('node:module');
const {readFileSync}=require('node:fs');
const {resolve}=require('node:path');
const assert=require('node:assert/strict');
const deps=createRequire(resolve(__dirname,'../.pio/ui-tests/package.json'));
const {JSDOM,VirtualConsole}=deps('jsdom');
const {createCanvas}=deps('@napi-rs/canvas');
const requests=[];const errors=[];
const device={desired:'closed',displayed:'closed',busy:false,displayError:'',hardwareConfirmed:false,storageReady:true,automatic:false,source:'manual',ip:'192.168.1.42',apIP:'192.168.4.1',wifiConnected:true,timeValid:true,unixTime:1792587600,cooldownSeconds:0};
let schedule={enabled:false,utcOffsetMinutes:180,events:[]};
const vc=new VirtualConsole();vc.on('jsdomError',e=>errors.push(e));
const dom=new JSDOM(readFileSync(resolve(__dirname,'../web/index.html'),'utf8'),{
 url:'http://avior.test/',runScripts:'dangerously',virtualConsole:vc,
 beforeParse(w){
   w.setInterval=()=>1;w.Blob=Blob;w.FormData=FormData;
   w.HTMLCanvasElement.prototype.getContext=function(){if(!this.backing)this.backing=createCanvas(this.width,this.height);return this.backing.getContext('2d');};
   w.createImageBitmap=async()=>{const c=createCanvas(16,16);const x=c.getContext('2d');x.fillStyle='#c41e23';x.fillRect(0,0,16,16);c.close=()=>{};return c;};
   w.fetch=async(url,opts)=>{
     requests.push({url,opts});
     if(opts.headers.Authorization!=='Bearer preview')return {ok:false,status:401,json:async()=>({error:'Неверный ключ'})};
     let data=opts.body&&typeof opts.body==='string'?JSON.parse(opts.body):null;
     if(url.endsWith('/status'))Object.assign(device,{desired:data.status,displayed:data.status,automatic:false});
     if(url.endsWith('/schedule')&&opts.method==='PUT'){schedule=data;device.automatic=data.enabled;}
     const value=url.endsWith('/schedule')?schedule:device;
     return {ok:true,status:200,json:async()=>value};
   };
 }
});
const w=dom.window,d=w.document;
const tick=()=>new Promise(r=>setTimeout(r,20));
const click=async(id)=>{d.getElementById(id).click();await tick();};
(async()=>{
 assert.equal(requests.length,0,'no unauthenticated polling');
 d.getElementById('token').value='wrong';d.getElementById('loginForm').dispatchEvent(new w.Event('submit',{cancelable:true}));await tick();
 assert.match(d.getElementById('feedback').textContent,/Неверный ключ/);
 d.getElementById('token').value='preview';d.getElementById('loginForm').dispatchEvent(new w.Event('submit',{cancelable:true}));await tick();
 assert.equal(d.getElementById('login').hidden,true);
 assert.equal(w.sessionStorage.getItem('aviorKey'),'preview');
 d.querySelector('[data-status="open"]').click();await click('publish');
 assert.equal(device.desired,'open');assert.equal(d.querySelector('[data-status="open"]').getAttribute('aria-pressed'),'true');
 await click('addEvent');await click('saveSchedule');assert.equal(schedule.events[0].minute,540);assert.equal(schedule.enabled,true);
 const time=d.querySelector('.event input');time.value='';const count=requests.length;await click('saveSchedule');assert.equal(requests.length,count,'invalid schedule never sent');
 time.value='18:00';await click('pauseSchedule');assert.equal(schedule.enabled,false);
 for(const [css,byte]of [['#000000',0],['#ffffff',0x55],['#ffd500',0xaa],['#c41e23',0xff]]){
   w.eval(`ctx.fillStyle='${css}';ctx.fillRect(0,0,800,480);`);const b=w.pack();assert.equal(b.length,96000);assert(b.every(x=>x===byte),`palette ${css}`);
   w.unpack(b);assert.equal(w.pack()[0],byte,'round trip palette');
 }
 await click('saveTemplate');const upload=requests.findLast(r=>r.url.includes('/assets?slot=open'));
 assert(upload);assert.equal((await upload.opts.body.get('file').arrayBuffer()).byteLength,96000);
 const fileInput=d.getElementById('imageFile');Object.defineProperty(fileInput,'files',{configurable:true,value:[{type:'image/png',size:256}]});fileInput.dispatchEvent(new w.Event('change'));await tick();
 assert.equal(d.querySelector('[data-status="image"]').getAttribute('aria-pressed'),'true');
 await click('uploadImage');assert(requests.some(r=>r.url.endsWith('/assets?slot=image')));
 d.querySelector('[data-status="closed"]').click();d.querySelector('[data-status="image"]').click();const uploads=requests.filter(r=>r.url.includes('/assets')).length;await click('uploadImage');assert.equal(requests.filter(r=>r.url.includes('/assets')).length,uploads,'stale image cannot be uploaded after template reset');
 device.recovery=true;device.resetReason='brownout';await w.refresh();
 assert.equal(d.getElementById('publish').disabled,true);
 assert.equal(d.getElementById('enableHardware').disabled,true);
 assert.equal(d.getElementById('initStorage').disabled,true);
 assert.equal(d.getElementById('wifiForm').querySelector('button').disabled,true);
 assert.match(d.getElementById('deviceState').textContent,/brownout/);
 await click('disconnect');assert.equal(w.sessionStorage.getItem('aviorKey'),null);
 assert.equal(errors.length,0,String(errors));console.log('UI authentication, status, schedule validation, palette, 96000-byte upload and stale-image guard: PASS');dom.window.close();
})().catch(e=>{console.error(e);dom.window.close();process.exitCode=1;});
