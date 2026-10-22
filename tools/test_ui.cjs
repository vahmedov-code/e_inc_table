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
const device={crm:{enabled:false,url:'https://cms.avior.moscow',configured:false,error:''},version:'0.3.0',qrEnabled:true,qrUrl:'https://avior.moscow/na-svyazi/',desired:'closed',displayed:'closed',busy:false,displayError:'',hardwareConfirmed:false,storageReady:true,automatic:false,source:'manual',ip:'192.168.1.42',apIP:'192.168.4.1',wifiConnected:true,timeValid:true,unixTime:1792587600,cooldownSeconds:0};
let schedule={enabled:false,utcOffsetMinutes:180,events:[]};
let scanMode='ok',scanPolls=0;
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
     if(url.endsWith('/wifi/scan')){
       if(opts.method==='POST'){scanPolls=0;return {ok:true,status:202,json:async()=>({scanning:true})};}
       if(scanMode==='error')return {ok:false,status:503,json:async()=>({error:'Поиск временно недоступен'})};
       const scanning=scanMode==='ok'&&scanPolls++===0;
       return {ok:true,status:scanning?202:200,json:async()=>({scanning,networks:scanMode==='empty'?[]:[{ssid:'Office-Test',rssi:-42,secure:true},{ssid:'<img src=x onerror=alert(1)>',rssi:-60,secure:false}]})};
     }
     if(url.includes('/asset?slot=')&&opts.method==='GET')return {ok:true,status:200,arrayBuffer:async()=>new Uint8Array(96000).fill(0xff).buffer};
     if(url.endsWith('/qr'))return {ok:true,status:200,json:async()=>device.qrEnabled?{enabled:true,size:3,rows:['101','010','101']}:{enabled:false,size:3,rows:['101','010','101']}};
     if(url.endsWith('/crm'))device.crm={enabled:data.enabled,url:data.url,configured:device.crm.configured||!!data.token,error:'',lastOkSeconds:data.enabled?3:undefined};
     if(url.endsWith('/settings'))Object.assign(device,{qrEnabled:data.qrEnabled,qrUrl:data.qrUrl});
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
const waitFor=async(check)=>{const start=Date.now();while(!check()){if(Date.now()-start>3500)throw Error('Timed out waiting for UI action');await tick();}};
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
 for(const [css,byte]of [['#8a8a8a',0x55],['#707070',0]]){w.eval(`ctx.fillStyle='${css}';ctx.fillRect(0,0,800,480);`);assert.equal(w.pack()[0],byte,`gray ${css} never becomes red`);}
 assert.equal(d.getElementById('version').textContent,'AviorSign · 0.3.0');
 const px=()=>Array.from(w.eval('ctx').getImageData(545,135,1,1).data.slice(0,3));
 d.querySelector('[data-status="closed"]').click();await tick();assert.deepEqual(px(),[0,0,0],'QR module drawn in the built-in preview');
 assert.equal(d.getElementById('qrUrl').value,'https://avior.moscow/na-svyazi/');
 d.getElementById('qrEnabled').checked=false;await click('saveQr');await tick();assert.equal(device.qrEnabled,false);
 d.querySelector('[data-status="closed"]').click();await tick();assert.deepEqual(px(),[255,255,255],'no QR when disabled');
 d.getElementById('qrUrl').value='avior.moscow';d.getElementById('qrEnabled').checked=true;const settingsCalls=requests.filter(r=>r.url.endsWith('/settings')).length;await click('saveQr');
 assert.equal(requests.filter(r=>r.url.endsWith('/settings')).length,settingsCalls,'invalid QR link never sent');
 d.getElementById('qrUrl').value='https://avior.moscow/na-svyazi/';await click('saveQr');await tick();assert.equal(device.qrEnabled,true);
 const fw=d.getElementById('fwFile');Object.defineProperty(fw,'files',{configurable:true,value:[new w.File([new Uint8Array(200000)],'firmware.bin')]});
 await click('fwUpload');await tick();assert.match(d.getElementById('feedback').textContent,/не файл прошивки/,'non-ESP32 image rejected in the browser');
 assert.match(d.getElementById('crmStatus').textContent,/выключена/);
 d.getElementById('crmEnabled').checked=true;d.getElementById('crmToken').value='abc123';await click('saveCrm');await tick();
 const crmReq=requests.findLast(r=>r.url.endsWith('/crm'));assert.equal(JSON.parse(crmReq.opts.body).token,'abc123');
 assert.equal(d.getElementById('crmToken').value,'','token field cleared after save');assert.match(d.getElementById('crmStatus').textContent,/На связи с CRM/);
 await click('saveCrm');assert.equal(JSON.parse(requests.findLast(r=>r.url.endsWith('/crm')).opts.body).token,undefined,'empty field keeps the stored token');
 device.crm.error='CRM ответила 401: Неверный токен';await w.refresh();assert.match(d.getElementById('crmStatus').textContent,/401/);
 d.querySelector('[data-status="open"]').click();await tick();
 assert.match(d.getElementById('clockState').textContent,/МСК/,'clock shown in schedule time zone, not UTC');
 assert.match(d.getElementById('clockState').textContent,/16:00/,'1792587600 is 16:00 Moscow time');
 assert.match(d.getElementById('clockState').textContent,/расходится/,'drift against this device is reported');
 assert.equal(d.getElementById('saveImage').disabled,true,'nothing to save before a new picture');
 await click('makeTemplate');assert.equal(d.getElementById('saveImage').disabled,false);
 await click('saveImage');const upload=requests.findLast(r=>r.url.includes('/assets?slot=open'));
 assert(upload,'template saved for the selected status');assert.equal((await upload.opts.body.get('file').arrayBuffer()).byteLength,96000);
 assert.equal(d.querySelector('[data-kind="open"]').textContent,'своя картинка');
 const fileInput=d.getElementById('imageFile');Object.defineProperty(fileInput,'files',{configurable:true,value:[{type:'image/png',size:256}]});
 d.querySelector('[data-status="soon"]').click();fileInput.dispatchEvent(new w.Event('change'));await tick();
 assert.match(d.getElementById('previewSource').textContent,/не сохранена/);
 await click('saveImage');assert(requests.some(r=>r.url.endsWith('/assets?slot=soon')),'picture replaced for a built-in status');
 d.querySelector('[data-status="closed"]').click();fileInput.dispatchEvent(new w.Event('change'));await tick();
 d.querySelector('[data-status="image"]').click();const uploads=requests.filter(r=>r.url.includes('/assets')).length;await click('saveImage');assert.equal(requests.filter(r=>r.url.includes('/assets')).length,uploads,'unsaved picture is dropped when switching status');
 d.querySelector('[data-status="soon"]').click();await click('resetImage');
 assert(requests.some(r=>r.url.endsWith('/asset?slot=soon')&&r.opts.method==='DELETE'),'reset deletes the saved picture');
 assert.equal(d.querySelector('[data-kind="soon"]').textContent,'стандартная');
 device.storageReady=false;await w.refresh();assert.equal(d.getElementById('storageNotice').hidden,false);
 await click('makeTemplate');const before=requests.filter(r=>r.url.includes('/assets')).length;await click('saveImage');
 assert.equal(requests.filter(r=>r.url.includes('/assets')).length,before,'no upload without storage');assert.match(d.getElementById('feedback').textContent,/хранилище/);
 device.storageReady=true;device.assets={closed:true};await w.refresh();await tick();
 assert.equal(d.querySelector('[data-kind="closed"]').textContent,'своя картинка','saved pictures are loaded as thumbnails');
 assert.equal(d.getElementById('battery').hidden,true,'no battery pill without a reading');
 device.battery={pin:35,mv:3950,percent:71,divider:2};device.adcProbe={'32':0,'35':1975};await w.refresh();
 assert.equal(d.getElementById('battery').textContent,'АКБ ≈71 % · 3,95 В');assert.match(d.getElementById('adcProbe').textContent,/GPIO35 1975/);
 device.battery.percent=10;await w.refresh();assert.match(d.getElementById('battery').className,/bad/,'low battery highlighted');
 device.busy=true;device.hardwareConfirmed=true;await w.refresh();
 assert.match(d.getElementById('deviceState').textContent,/Wi‑Fi временно выключен/);assert.equal(d.getElementById('publish').disabled,true);
 device.busy=false;await w.refresh();
 device.recovery=true;device.resetReason='brownout';await w.refresh();
 assert.equal(d.getElementById('publish').disabled,true);
 assert.equal(d.getElementById('enableHardware').disabled,true);
 assert.equal(d.getElementById('initStorage').disabled,true);
 assert.equal(d.getElementById('wifiConnect').disabled,true);
 assert.equal(d.getElementById('scanWifi').disabled,true);
 assert.match(d.getElementById('deviceState').textContent,/brownout/);
 device.wifiSetupAllowed=true;await w.refresh();
 assert.equal(d.getElementById('wifiConnect').disabled,false);
 assert.equal(d.getElementById('scanWifi').disabled,false);
 assert.equal(d.getElementById('enableHardware').disabled,true);
 assert.equal(d.getElementById('initStorage').disabled,true);
 await click('scanWifi');assert.equal(d.getElementById('scanWifi').disabled,true);
 await waitFor(()=>d.getElementById('wifiScanStatus').textContent.includes('Найдено сетей: 2'));
 assert.equal(d.getElementById('wifiNetworks').options.length,3);
 assert.equal(d.getElementById('wifiNetworks').querySelector('img'),null,'SSID rendered as text, never HTML');
 d.getElementById('wifiNetworks').value='Office-Test';d.getElementById('wifiNetworks').dispatchEvent(new w.Event('change'));
 assert.equal(d.getElementById('ssid').value,'Office-Test','selected SSID fills connection form');
 scanMode='empty';await click('scanWifi');assert.match(d.getElementById('wifiScanStatus').textContent,/Сети не найдены/);
 assert.equal(d.getElementById('ssid').value,'Office-Test','empty scan preserves manual SSID');
 scanMode='error';await click('scanWifi');assert.match(d.getElementById('feedback').textContent,/Поиск временно недоступен/);
 assert.equal(d.getElementById('scanWifi').disabled,false,'scan can be retried after failure');
 d.getElementById('wifiPass').value='example-password';
 d.getElementById('wifiForm').dispatchEvent(new w.Event('submit',{cancelable:true}));await tick();
 const wifiRequest=requests.findLast(r=>r.url.endsWith('/wifi'));
 assert.equal(JSON.parse(wifiRequest.opts.body).ssid,'Office-Test');
 assert.equal(d.getElementById('wifiPass').value,'','credential field cleared after submit');
 device.wifiConnected=true;device.wifiArduinoStatus=255;device.wifiStatus='connected';device.ip='192.168.0.33';await w.refresh();
 assert.equal(d.getElementById('connection').textContent,'В сети · 192.168.0.33','cached Arduino 255 must not override backend evidence');
 device.wifiConnected=false;device.wifiStatus='associated';await w.refresh();
 assert.match(d.getElementById('connection').textContent,/ожидается IP/);
 device.wifiStatus='unknown';await w.refresh();
 assert.match(d.getElementById('connection').textContent,/неизвестно/);
 await click('disconnect');assert.equal(w.sessionStorage.getItem('aviorKey'),null);
 assert.equal(errors.length,0,String(errors));console.log('UI authentication, status, schedule validation, palette, 96000-byte upload and stale-image guard: PASS');dom.window.close();
})().catch(e=>{console.error(e);dom.window.close();process.exitCode=1;});
