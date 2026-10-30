#include "WebUi.h"
#include <WiFi.h>

const char WebUi::PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><meta name=viewport content="width=device-width,initial-scale=1"><title>FPVCineCam32</title>
<style>
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Arial,sans-serif;max-width:820px;margin:24px auto;padding:0 16px;background:#111;color:#eee}
h1{margin-bottom:4px}.sub{color:#aaa;margin-bottom:16px}.card{background:#1c1c1e;border-radius:14px;padding:16px;margin:14px 0}h3{margin-top:0}
button,input,select{font-size:16px;padding:10px;margin:5px 5px 5px 0;border-radius:8px;border:1px solid #555;background:#29292c;color:#fff}button{cursor:pointer}.ok{color:#6ee787}.warn{color:#ffd866}.muted{color:#aaa}.grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}.channels{display:grid;grid-template-columns:repeat(4,1fr);gap:7px}.ch{background:#252528;border-radius:8px;padding:8px;text-align:center}.ch b{display:block;font-size:13px;color:#aaa}.ch span{font-size:18px}.selected{outline:2px solid #6ee787}.statusBadge{display:inline-flex;align-items:center;gap:7px;padding:6px 10px;border-radius:999px;font-weight:600;margin-bottom:8px}.statusBadge::before{content:"";width:10px;height:10px;border-radius:50%;background:currentColor}.statusOnline{color:#6ee787;background:#17351f}.statusOffline{color:#ff6b6b;background:#3a1b1b}pre{white-space:pre-wrap;word-break:break-word}@media(max-width:600px){.grid{grid-template-columns:1fr}.channels{grid-template-columns:repeat(2,1fr)}}
.wiring{width:100%;border-collapse:collapse;text-align:left}.wiring th,.wiring td{padding:10px 8px;border-bottom:1px solid #38383b}.wiring th{color:#aaa}
.gpCamera{background:#242427;border:1px solid #39393d;border-radius:10px;padding:16px;margin:12px 0}.gpCamera h4{font-size:20px;margin:0 0 6px}.gpCamera p{margin:8px 0}.gpCamera .utility{margin-top:12px}.gpCamera .utility button{font-size:14px;color:#bbb}.gpCamera .primary{font-weight:600}
</style></head><body>
<h1>FPVCineCam32</h1><div class=sub>Camera control + Betaflight MSP</div>

<div class=card><h3>Camera system</h3>
<select id=selectedCamera aria-label="Camera"><option value=blackmagic>Blackmagic Pocket Cinema Camera 4K</option><option value=gopro>GoPro</option><option value=dji>DJI Osmo Action</option></select>
<button id=saveCamera onclick=saveCamera()>Save &amp; Restart</button>
<p id=cameraSaveStatus class=muted role=status></p>
<p id=restartHelp class=muted hidden>Reconnect to FPVCineCam32 Wi-Fi if required, then reopen this page.</p>
</div>

<div class=card id=goproCard hidden><h3>GoPro</h3>
<button id=gpScan onclick=gpAction('scan')>Add camera</button>
<span id=gpScanStatus class=muted role=status></span>
<div id=gpCameras aria-label="Discovered GoPro cameras"></div>
<p id=gpMessage class=muted role=status></p>
<p class=muted>For first pairing, put the GoPro into pairing mode.</p>
<p id=gpStatus role=status>Offline</p>
<h4>Saved cameras</h4><div id=gpSaved></div>
<p id=gpStorageError class=warn hidden>Saved camera list could not be read or saved. Copy the log before restarting.</p>
<button id=gpCopy onclick=copyGoProLog()>Copy log</button>
<textarea id=gpLog readonly aria-label="GoPro connection log" hidden style="box-sizing:border-box;width:100%;height:180px;background:#111;color:#eee"></textarea>
<p id=gpCopyStatus class=muted role=status></p>
</div>

<div class=card id=djiCard hidden><h3>DJI Osmo Action</h3>
<div id=djiBadge class="statusBadge statusOffline">Camera disconnected</div>
<div id=djiSummary class=muted>Offline</div>
<p><button id=djiScan onclick=djiScan()>Scan for cameras</button><span id=djiCams></span></p>
<p><button onclick="djiRec(1)">REC</button><button onclick="djiRec(0)">STOP</button><button onclick=djiForget()>Forget camera</button></p>
<p id=djiMessage class=muted role=status></p>
</div>

<div class=card id=blackmagicCard><h3 id=blackmagicTitle>Blackmagic Pocket Cinema Camera</h3>
<fieldset id=blackmagicControls style="border:0;padding:0;margin:0">
<div id=camBadge class="statusBadge statusOffline">Camera disconnected</div><div id=camSummary class=muted>Loading...</div>
<div id=pin style="display:none"><p class=warn>Enter the 6-digit PIN shown on the BMPCC 4K:</p><input id=pinval inputmode=numeric maxlength=6 placeholder=123456><button onclick=sendPin()>Submit PIN</button></div>
<p><button onclick=scan()>Scan for cameras</button><span id=cams></span></p>
<p><button onclick="rec(1)">REC</button><button onclick="rec(0)">STOP</button><button onclick=forget()>Forget camera</button></p>
</fieldset>
</div>

<div class=card><h3>Betaflight / MSP</h3>
<table class=wiring><thead><tr><th scope=col>Flight controller</th><th scope=col>ESP32-C3</th></tr></thead><tbody><tr><td>FC TX</td><td>GPIO6 (ESP RX)</td></tr><tr><td>FC RX</td><td>GPIO7 (ESP TX)</td></tr><tr><td>GND</td><td>GND</td></tr></tbody></table>
<p class=muted>Enable MSP at 115200 baud on that Betaflight UART.</p>
<div id=mspSummary class=muted>Waiting for FC...</div>
<hr style="border-color:#333">
<h4>REC / STOP switch mapping</h4>
<div class=grid>
<label>RC channel<select id=ch></select></label>
<label>Threshold<input id=thr type=number min=800 max=2200></label>
<label>Record when<select id=high><option value=1>Above threshold</option><option value=0>Below threshold</option></select></label>
<label>Stop delay (s)<input id=dly type=number min=0 max=60></label>
<label>Live selected channel<input id=selectedValue readonly></label>
</div>
<button onclick=saveMapping()>Save mapping</button><span id=saveMsg class=muted></span>
<h4>Live RC channels</h4><div id=channels class=channels></div>
<p class=muted id=mspStats></p>
</div>

<div class=card><h3>Setup Wi-Fi</h3>
<p class=muted>Wi-Fi is only for configuration. If no phone/computer joins within 90 seconds of boot, it switches off automatically. BLE camera control, MSP and OSD continue normally. Wi-Fi comes back on every reboot.</p>
<button onclick=wifiOff()>Disable Wi-Fi now</button>
</div>

<script>
const el=id=>document.getElementById(id);
let cameraSelectionLoaded=false, cameraSavePending=false, mappingControlsLoaded=false;
function waitForRestart(expectedCamera){
  const helpTimer=setTimeout(()=>{el('restartHelp').hidden=false;},12000);
  async function probe(){
    const controller=new AbortController();
    const timeout=setTimeout(()=>controller.abort(),3000);
    try{
      const response=await fetch('/api/status?restart='+Date.now(),{cache:'no-store',signal:controller.signal});
      if(response.ok){
        const status=await response.json();
        if(status.restartPending===false && status.selectedCamera===expectedCamera){clearTimeout(helpTimer);window.location.reload();return;}
      }
    }catch(e){}
    finally{clearTimeout(timeout);}
    setTimeout(probe,2000);
  }
  setTimeout(probe,1500);
}
async function saveCamera(){
  if(cameraSavePending)return;
  cameraSavePending=true;el('saveCamera').disabled=true;el('cameraSaveStatus').textContent='Saving…';
  try{
    const result=await api('/api/selectCamera',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'camera='+encodeURIComponent(el('selectedCamera').value)});
    if(!result.ok)throw new Error('Save failed');
    el('cameraSaveStatus').textContent='Camera saved. Restarting…';el('selectedCamera').disabled=true;el('blackmagicControls').disabled=true;clearInterval(refreshTimer);waitForRestart(el('selectedCamera').value);
  }catch(e){cameraSavePending=false;el('saveCamera').disabled=false;el('cameraSaveStatus').textContent='Could not save camera: '+e.message;}
}
for(let i=1;i<=16;i++){const o=document.createElement('option');o.value=i;o.textContent='CH'+i;el('ch').appendChild(o)}
async function api(url,opt){const r=await fetch(url,opt);if(!r.ok)throw new Error(`HTTP ${r.status}`);return await r.json()}
let gpRequestPending=false,gpList='',gpCards='',gpScanRequested=false;
function goProTimeText(seconds){return Math.floor(seconds/3600)+'h:'+String(Math.floor((seconds%3600)/60)).padStart(2,'0')}
function drawGoPro(g){
  el('gpStatus').textContent=g.status==='Scanning'?'':g.status;if(g.status==='Scanning')el('gpScanStatus').textContent='Scanning...';el('gpStorageError').hidden=!g.savedListError;
  const blocked=g.busy||gpRequestPending||cameraSavePending;const discoveries=g.cameras.filter(c=>!c.saved);
  if(gpScanRequested&&!gpRequestPending&&!g.busy){el('gpScanStatus').textContent=discoveries.length?'':'No new GoPros found.';gpScanRequested=false;}
  const list=JSON.stringify([discoveries,blocked]);if(list!==gpList){gpList=list;el('gpCameras').replaceChildren();if(!blocked)discoveries.forEach(c=>{const result=document.createElement('button');result.textContent=c.name||('GoPro '+c.address);result.onclick=()=>gpAction('connect',{index:c.index});el('gpCameras').appendChild(result);});}
  el('gpScan').disabled=blocked;const cards=JSON.stringify([g.savedCameras,blocked]);if(cards===gpCards)return;gpCards=cards;el('gpSaved').replaceChildren();if(!g.savedCameras.length)el('gpSaved').textContent='No saved GoPros yet.';
  g.savedCameras.forEach(c=>{const card=document.createElement('div');card.className='gpCamera';function text(tag,value,css){const node=document.createElement(tag);node.textContent=value;if(css)node.className=css;card.appendChild(node);return node;}text('h4',c.friendlyName||c.reportedName||'GoPro');if(c.reportedName)text('p',c.reportedName,'muted');function button(label,action,disabled=blocked){const node=text('button',label);node.disabled=disabled;node.onclick=action;return node;}if(c.connected){text('p','CONNECTED','statusBadge statusOnline');text('p',c.recordingState==='recording'?'REC':c.recordingState==='standby'?'STBY':'--',c.recordingState==='recording'?'warn':'muted');text('p','Battery '+(c.batteryPercent>=0?c.batteryPercent+'%':'--'));text('p','Card '+(c.remainingSeconds!=null?goProTimeText(c.remainingSeconds):'--'));const controls=text('div','');controls.appendChild(button('REC',()=>gpAction('shutter',{id:c.id,on:1}),blocked||!c.controlReady));controls.appendChild(button('STOP',()=>gpAction('shutter',{id:c.id,on:0}),blocked||!c.controlReady));}else{button('Connect',()=>gpAction('connectSaved',{id:c.id})).className='primary';text('p','DISCONNECTED','statusBadge statusOffline');}const utility=text('div','','utility');utility.appendChild(button('Rename',()=>{const name=prompt('Rename camera',c.friendlyName);if(name!==null)gpAction('rename',{id:c.id,name:name.trim()});}));utility.appendChild(button('Forget',()=>{if(confirm('Forget '+(c.friendlyName||c.reportedName||c.address)+' and its pairing?'))gpAction('forget',{id:c.id});}));el('gpSaved').appendChild(card);});
}
async function gpAction(action,params={}){if(gpRequestPending||cameraSavePending)return;gpRequestPending=true;el('gpScan').disabled=true;el('gpCameras').replaceChildren();gpList='';if(action==='scan'){gpScanRequested=true;el('gpScanStatus').textContent='Scanning...';el('gpMessage').textContent='';}el('gpSaved').querySelectorAll('button').forEach(button=>button.disabled=true);try{await api('/api/gopro/'+action,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(params).toString()});el('gpMessage').textContent='';}catch(e){if(action==='scan')el('gpScanStatus').textContent='';gpScanRequested=false;el('gpMessage').textContent='Request failed: '+e.message;}finally{gpRequestPending=false;gpCards='';refresh();}}
async function copyGoProLog(){el('gpCopy').disabled=true;try{const response=await fetch('/api/gopro/log',{cache:'no-store'});if(!response.ok)throw new Error('HTTP '+response.status);const box=el('gpLog');box.value=await response.text();let copied=false;try{if(navigator.clipboard&&window.isSecureContext){await navigator.clipboard.writeText(box.value);copied=true;}}catch(e){}if(!copied){box.hidden=false;box.focus();box.select();box.setSelectionRange(0,box.value.length);try{copied=document.execCommand('copy');}catch(e){}}el('gpCopyStatus').textContent=copied?'Log copied.':'Log selected. Use Copy on your phone or Ctrl/Cmd+C.';}catch(e){el('gpCopyStatus').textContent='Could not read log: '+e.message;}finally{el('gpCopy').disabled=false;}}

function cameraLine(c){const link=c.connected?'Connected':'Offline';const ready=c.controlReady?'Control ready':'Control not ready';const rec=c.recording?'RECORDING':'Standby';return `${link} | ${ready} | ${rec}`;}
function drawDji(c){
  const linked=c.connected&&c.controlReady;el('djiBadge').className='statusBadge '+(linked?'statusOnline':'statusOffline');el('djiBadge').textContent=linked?'Camera connected':'Camera disconnected';
  let line=c.status||'Offline';if(c.model)line+=' | '+c.model;if(c.batteryPercent>=0)line+=' | Battery '+c.batteryPercent+'%';if(c.mediaRemaining&&c.mediaRemaining!=='--')line+=' | SD '+c.mediaRemaining;el('djiSummary').textContent=line;
}
async function djiScan(){el('djiScan').disabled=true;el('djiCams').textContent='Scanning...';el('djiMessage').textContent='';try{const x=await api('/api/dji/scan',{method:'POST'});el('djiCams').replaceChildren();if(!x.length){el('djiCams').textContent=' No DJI cameras found';return;}x.forEach(c=>{const b=document.createElement('button');b.textContent=c.name||('DJI '+c.address);b.onclick=()=>djiConnect(c.address,c.type);el('djiCams').appendChild(b);});}catch(e){el('djiMessage').textContent='Scan failed: '+e.message;}finally{el('djiScan').disabled=false;}}
async function djiConnect(address,type){el('djiMessage').textContent='Connecting...';try{await api('/api/dji/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({address,type}).toString()});el('djiMessage').textContent='Connection queued.';}catch(e){el('djiMessage').textContent='Connect failed: '+e.message;}setTimeout(refresh,500);}
async function djiRec(on){try{await api('/api/dji/record',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'on='+on});}catch(e){el('djiMessage').textContent='REC/STOP failed: '+e.message;}setTimeout(refresh,300);}
async function djiForget(){try{await api('/api/dji/forget',{method:'POST'});el('djiMessage').textContent='Saved DJI camera cleared.';}catch(e){el('djiMessage').textContent='Forget failed: '+e.message;}refresh();}

function drawChannels(s){const selectedChannel=parseInt(el('ch').value||s.settings.channel,10);const box=el('channels');box.innerHTML='';for(let i=0;i<16;i++){const d=document.createElement('div');d.className='ch'+((i+1)==selectedChannel?' selected':'');const v=(s.msp.channels&&i<s.msp.channels.length)?s.msp.channels[i]:0;d.innerHTML=`<b>CH${i+1}</b><span>${v||'--'}</span>`;box.appendChild(d);}const idx=selectedChannel-1;el('selectedValue').value=(s.msp.channels&&idx>=0&&idx<s.msp.channels.length)?s.msp.channels[idx]:'--';}
async function refresh(){
  try{
    const s=await api('/api/status');
    if(!cameraSavePending){
      if(!cameraSelectionLoaded){el('selectedCamera').value=s.selectedCamera;cameraSelectionLoaded=true;}
      const blackmagic=s.selectedCamera==='blackmagic';
      el('blackmagicControls').disabled=!blackmagic;el('blackmagicControls').hidden=!blackmagic;el('blackmagicTitle').hidden=!blackmagic;el('blackmagicCard').hidden=!blackmagic;
      el('goproCard').hidden=s.selectedCamera!=='gopro';el('djiCard').hidden=s.selectedCamera!=='dji';
      const djiRecording=s.dji&&s.dji.recording;el('saveCamera').disabled=s.camera.recording||s.camera.waitingPin||djiRecording;
    }
    if(s.gopro)drawGoPro(s.gopro);if(s.dji)drawDji(s.dji);
    el('pin').style.display=s.camera.waitingPin?'block':'none';el('camSummary').textContent=cameraLine(s.camera)+(s.camera.model?` | ${s.camera.model}`:'');const linked=s.camera.connected&&s.camera.controlReady;el('camBadge').className='statusBadge '+(linked?'statusOnline':'statusOffline');el('camBadge').textContent=linked?'Camera connected':'Camera disconnected';
    el('mspSummary').textContent=s.msp.connected?`MSP connected | API ${s.msp.api} | last RC response ${s.msp.responseMs} ms`:'MSP offline - check UART wiring and Betaflight Ports';el('mspStats').textContent=`Responses: ${s.msp.responses} | Timeouts: ${s.msp.timeouts} | Invalid frames: ${s.msp.invalidFrames}`;if(!mappingControlsLoaded){el('ch').value=s.settings.channel;el('thr').value=s.settings.threshold;el('high').value=s.settings.high?1:0;el('dly').value=s.settings.delay;mappingControlsLoaded=true;}drawChannels(s);
  }catch(e){el('mspSummary').textContent='ESP web API unavailable'}
}
async function scan(){el('cams').textContent='Scanning...';try{const x=await api('/api/scan');el('cams').innerHTML='';if(!x.length){el('cams').textContent=' No Blackmagic cameras found';return}x.forEach(c=>{const b=document.createElement('button');b.textContent=(c.name||'Blackmagic')+' '+c.address;b.onclick=()=>connect(c.address,c.type);el('cams').appendChild(b)})}catch(e){el('cams').textContent='Scan error: '+e.message}}
async function connect(a,t){try{await api('/api/connect?address='+encodeURIComponent(a)+'&type='+t)}catch(e){alert('Connect error: '+e.message)}setTimeout(refresh,250)}
async function sendPin(){const v=el('pinval').value.trim();if(!/^\d{6}$/.test(v)){alert('Enter the 6-digit PIN shown on the camera');return}await api('/api/pin?value='+v);setTimeout(refresh,400)}
async function forget(){await api('/api/forget');refresh()}
async function rec(v){await api('/api/record?on='+v);setTimeout(refresh,250)}
async function saveMapping(){await api(`/api/saveMapping?ch=${el('ch').value}&thr=${el('thr').value}&high=${el('high').value}&dly=${el('dly').value}`);el('saveMsg').textContent='Saved';setTimeout(()=>el('saveMsg').textContent='',1200);refresh()}
async function wifiOff(){try{await api('/api/wifioff');}catch(e){}}
const refreshTimer=setInterval(refresh,1500);refresh();
</script></body></html>)HTML";

void WebUi::begin(const String& apName) {
    const uint32_t t0 = millis();
    WiFi.mode(WIFI_AP);
    const bool apOk = WiFi.softAP(apName.c_str(), "fpvcinecam32");
    Serial.printf("[%8lu ms] WIFI: softAP() returned %s in %lu ms, mode=%d, ip=%s\n",
                  (unsigned long)millis(), apOk ? "TRUE" : "FALSE",
                  (unsigned long)(millis() - t0), (int)WiFi.getMode(),
                  WiFi.softAPIP().toString().c_str());
    routes();server.begin();running=true;
    Serial.printf("[%8lu ms] WIFI: web server started\n", (unsigned long)millis());
}
void WebUi::loop(){if(restartRequested&&(int32_t)(millis()-restartAtMs)>=0)ESP.restart();if(running)server.handleClient();if(stopRequested&&millis()>=stopAtMs){stopRequested=false;stopWifi();}}
void WebUi::stopWifi(){if(!running)return;Serial.printf("[%8lu ms] WIFI: stopping AP, stations=%u\n",(unsigned long)millis(),(unsigned)WiFi.softAPgetStationNum());server.stop();WiFi.softAPdisconnect(true);WiFi.mode(WIFI_OFF);running=false;Serial.printf("[%8lu ms] WIFI: stopped, mode=%d\n",(unsigned long)millis(),(int)WiFi.getMode());}

String WebUi::statusJson(){
    const CameraState& c=cam.state();
    String j="{\"selectedCamera\":\""+savedCamera+"\",\"restartPending\":"+String(restartRequested?"true":"false")+",\"camera\":{";
    j += "\"status\":\""+c.status+"\",\"model\":\""+c.model+"\",\"protocol\":\""+c.protocolVersion+"\",\"connected\":"+String(c.connected?"true":"false")+",\"paired\":"+String(c.paired?"true":"false")+",\"ready\":"+String(c.ready?"true":"false")+",\"controlReady\":"+String(c.controlReady?"true":"false")+",\"recording\":"+String(c.recording?"true":"false")+",\"timecode\":\""+c.timecode+"\",\"mediaRemaining\":\""+c.mediaRemaining+"\",\"activeMediaSlot\":"+String(c.activeMediaSlot)+",\"mediaSlots\":[\""+c.mediaSlotRemaining[0]+"\",\""+c.mediaSlotRemaining[1]+"\",\""+c.mediaSlotRemaining[2]+"\"],\"incomingSubscription\":\""+c.incomingSubscription+"\",\"incomingPackets\":"+String(c.incomingPackets)+",\"lastIncoming\":\""+c.lastIncoming+"\",\"waitingPin\":"+String(cam.waitingForPasskey()?"true":"false")+",\"lastCommand\":\""+c.lastCommand+"\",\"lastWrite\":\""+c.lastWrite+"\"},";
    j += "\"msp\":{\"connected\":"+String(mspClient.connected()?"true":"false")+",\"rcFresh\":"+String(mspClient.rcFresh()?"true":"false")+",\"api\":\""+String(mspClient.apiMajor())+"."+String(mspClient.apiMinor())+"\",\"responseMs\":"+String(mspClient.lastResponseMs())+",\"responses\":"+String(mspClient.responses())+",\"timeouts\":"+String(mspClient.timeouts())+",\"invalidFrames\":"+String(mspClient.invalidFrames())+",\"channels\":[";
    const size_t count=min(mspClient.rcCount(),(size_t)16);for(size_t i=0;i<count;i++){if(i)j+=',';j+=String(mspClient.rcValue(i));}
    j += "]},";
    j += "\"settings\":{\"rx\":6,\"tx\":7,\"baud\":115200,\"channel\":"+String(s.recordChannel)+",\"threshold\":"+String(s.recordThreshold)+",\"high\":"+String(s.recordActiveHigh?"true":"false")+",\"delay\":"+String(s.recordStopDelaySec)+",\"slot\":"+String(s.osdSlot)+"}}";
    if(gp){j.remove(j.length()-1);j+=",\"gopro\":"+gp->statusJson()+"}";}
    if(dji){
        const CameraState& d=dji->state();
        j.remove(j.length()-1);
        j += ",\"dji\":{\"status\":\""+d.status+"\",\"model\":\""+d.model+"\",\"connected\":"+String(d.connected?"true":"false")+",\"paired\":"+String(d.paired?"true":"false")+",\"ready\":"+String(d.ready?"true":"false")+",\"controlReady\":"+String(d.controlReady?"true":"false")+",\"recording\":"+String(d.recording?"true":"false")+",\"batteryPercent\":"+String(d.batteryPercent)+",\"mediaRemaining\":\""+d.mediaRemaining+"\",\"incomingPackets\":"+String(d.incomingPackets)+",\"lastIncoming\":\""+d.lastIncoming+"\",\"lastCommand\":\""+d.lastCommand+"\",\"lastWrite\":\""+d.lastWrite+"\"}}";
    }
    return j;
}

bool WebUi::cameraAvailable(){if(s.selectedCamera=="blackmagic"&&!restartRequested)return true;server.send(409,"application/json","{\"ok\":false}");return false;}
bool WebUi::goProAvailable(){if(gp&&s.selectedCamera=="gopro"&&!restartRequested)return true;server.send(409,"application/json","{\"ok\":false}");return false;}
bool WebUi::djiAvailable(){if(dji&&s.selectedCamera=="dji"&&!restartRequested)return true;server.send(409,"application/json","{\"ok\":false}");return false;}

void WebUi::routes(){
    server.on("/api/gopro/shutter",HTTP_POST,[this](){if(!goProAvailable())return;const String on=server.arg("on");const bool ok=(on=="0"||on=="1")&&gp->setShutter(server.arg("id"),on=="1");server.send(ok?200:409,"application/json",ok?"{\"ok\":true}":"{\"ok\":false}");});
    server.on("/api/gopro/scan",HTTP_POST,[this](){if(!goProAvailable())return;const bool ok=gp->scan();server.send(ok?200:409,"application/json",ok?"{\"ok\":true}":"{\"ok\":false}");});
    server.on("/api/gopro/connect",HTTP_POST,[this](){if(!goProAvailable())return;String index=server.arg("index");bool valid=index.length()>0&&index.length()<=2;for(size_t i=0;i<index.length();++i)if(index[i]<'0'||index[i]>'9')valid=false;const bool ok=valid&&gp->connectDiscovered(index.toInt());server.send(ok?200:409,"application/json",ok?"{\"ok\":true}":"{\"ok\":false}");});
    server.on("/api/gopro/forget",HTTP_POST,[this](){if(!goProAvailable())return;const bool ok=gp->forget(server.arg("id"));server.send(ok?200:409,"application/json",ok?"{\"ok\":true}":"{\"ok\":false}");});
    server.on("/api/gopro/connectSaved",HTTP_POST,[this](){if(!goProAvailable())return;const bool ok=gp->connectSaved(server.arg("id"));server.send(ok?200:409,"application/json",ok?"{\"ok\":true}":"{\"ok\":false}");});
    server.on("/api/gopro/rename",HTTP_POST,[this](){if(!goProAvailable())return;const bool ok=gp->renameSaved(server.arg("id"),server.arg("name"));server.send(ok?200:409,"application/json",ok?"{\"ok\":true}":"{\"ok\":false}");});
    server.on("/api/gopro/log",HTTP_GET,[this](){if(!goProAvailable())return;server.sendHeader("Cache-Control","no-store");server.send(200,"text/plain; charset=utf-8",gp->connectionLog());});

    server.on("/api/dji/scan",HTTP_POST,[this](){if(!djiAvailable())return;String j;const bool ok=dji->startScan(j);server.send(ok?200:409,"application/json",ok?j:"{\"ok\":false}");});
    server.on("/api/dji/connect",HTTP_POST,[this](){if(!djiAvailable())return;const String address=server.arg("address");const uint8_t type=(uint8_t)server.arg("type").toInt();const bool ok=dji->connectTo(address,type);server.send(ok?200:409,"application/json",ok?"{\"ok\":true}":"{\"ok\":false}");});
    server.on("/api/dji/record",HTTP_POST,[this](){if(!djiAvailable())return;const String on=server.arg("on");const bool ok=(on=="0"||on=="1")&&dji->setRecording(on=="1");server.send(ok?200:409,"application/json",ok?"{\"ok\":true}":"{\"ok\":false}");});
    server.on("/api/dji/forget",HTTP_POST,[this](){if(!djiAvailable())return;dji->forgetPairing();server.send(200,"application/json","{\"ok\":true}");});

    server.on("/api/selectCamera",HTTP_POST,[this](){
        const String value=server.arg("camera");
        if(value!="blackmagic"&&value!="gopro"&&value!="dji"){server.send(400,"application/json","{\"ok\":false}");return;}
        const bool djiRecording=dji&&dji->state().recording;
        if(restartRequested||cam.state().recording||cam.waitingForPasskey()||djiRecording){server.send(409,"application/json","{\"ok\":false}");return;}
        if(!prefs.selectCamera(value)){server.send(500,"application/json","{\"ok\":false}");return;}
        savedCamera=value;server.send(200,"application/json","{\"ok\":true}");restartAtMs=millis()+1000;restartRequested=true;
    });
    server.on("/",HTTP_GET,[this](){server.send_P(200,"text/html",PAGE);});
    server.on("/api/wifioff",HTTP_GET,[this](){server.send(200,"application/json","{\"ok\":true}");stopRequested=true;stopAtMs=millis()+250;});
    server.on("/api/status",HTTP_GET,[this](){server.send(200,"application/json",statusJson());});
    server.on("/api/scan",HTTP_GET,[this](){if(!cameraAvailable())return;String j;cam.startScan(j);server.send(200,"application/json",j);});
    server.on("/api/connect",HTTP_GET,[this](){if(!cameraAvailable())return;String a=server.arg("address");uint8_t t=(uint8_t)server.arg("type").toInt();bool ok=cam.connectTo(a,t);if(ok){s.cameraAddress=a;s.cameraAddressType=t;prefs.save(s);cam.setSavedTarget(a,t);}server.send(200,"application/json",String("{\"ok\":")+(ok?"true":"false")+"}");});
    server.on("/api/pin",HTTP_GET,[this](){if(!cameraAvailable())return;uint32_t p=(uint32_t)server.arg("value").toInt();bool ok=cam.submitPasskey(p);server.send(200,"application/json",String("{\"ok\":")+(ok?"true":"false")+"}");});
    server.on("/api/forget",HTTP_GET,[this](){if(!cameraAvailable())return;cam.forgetPairing();prefs.clearCamera();s.cameraAddress="";server.send(200,"application/json","{\"ok\":true}");});
    server.on("/api/record",HTTP_GET,[this](){if(!cameraAvailable())return;bool on=server.arg("on").toInt()!=0;bool ok=cam.setRecording(on);server.send(200,"application/json",String("{\"ok\":")+(ok?"true":"false")+"}");});
    server.on("/api/osdtest",HTTP_GET,[this](){mspClient.setCustomText(s.osdSlot,"REC TEST");if(s.osdSlot<3)mspClient.setCustomText(s.osdSlot+1,"MEDIA TEST");server.send(200,"application/json","{\"ok\":true}");});
    server.on("/api/saveMapping",HTTP_GET,[this](){s.recordChannel=constrain(server.arg("ch").toInt(),1,16);s.recordThreshold=constrain(server.arg("thr").toInt(),800,2200);s.recordActiveHigh=server.arg("high").toInt()!=0;s.recordStopDelaySec=constrain(server.arg("dly").toInt(),0,60);prefs.save(s);server.send(200,"application/json","{\"ok\":true}");});
    server.on("/api/saveOsd",HTTP_GET,[this](){s.osdSlot=constrain(server.arg("slot").toInt(),0,3);prefs.save(s);server.send(200,"application/json","{\"ok\":true}");});
}
