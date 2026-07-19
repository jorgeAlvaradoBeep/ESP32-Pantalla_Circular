#include "WebPortal.h"

#include <ESPmDNS.h>
#include <WiFi.h>

#include "Config.h"

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="es">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Termostato ConnectLife</title>
  <style>
    :root{color-scheme:dark;--bg:#111418;--panel:#1c2229;--line:#303842;--text:#f5f7fb;--muted:#9ba8b5;--hot:#ff6b35;--cool:#45b7d1;--ok:#6bd490;--bad:#ff5f73}
    *{box-sizing:border-box}body{margin:0;font-family:Inter,system-ui,-apple-system,Segoe UI,sans-serif;background:radial-gradient(circle at 50% 0,#25303b,#111418 48%);color:var(--text);min-height:100vh}
    header{display:flex;justify-content:space-between;align-items:center;padding:20px clamp(16px,4vw,44px)}a{color:inherit}.brand{font-weight:800;letter-spacing:.04em}.status{color:var(--muted);font-size:14px}
    main{display:grid;grid-template-columns:minmax(280px,460px) minmax(260px,1fr);gap:28px;padding:12px clamp(16px,4vw,44px) 36px;align-items:start}
    .thermo{aspect-ratio:1;border-radius:50%;display:grid;place-items:center;background:conic-gradient(from 180deg,var(--cool),var(--hot),var(--cool));padding:10px;box-shadow:0 26px 70px #0008}
    .dial{width:100%;height:100%;border-radius:50%;background:linear-gradient(145deg,#20272f,#12161b);display:grid;place-items:center;text-align:center;border:1px solid #3a434e}
    .ambient{font-size:15px;color:var(--muted)}.target{font-size:92px;font-weight:800;line-height:1}.target span{font-size:30px;vertical-align:super}.humidity{margin-top:8px;color:var(--muted)}
    .panel{background:color-mix(in srgb,var(--panel) 88%,transparent);border:1px solid var(--line);border-radius:8px;padding:18px;box-shadow:0 18px 40px #0004}.grid{display:grid;gap:16px}
    .row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}.metric{display:grid;grid-template-columns:1fr auto;gap:8px;padding:12px 0;border-bottom:1px solid var(--line);color:var(--muted)}.metric b{color:var(--text)}
    button{border:1px solid var(--line);background:#222a33;color:var(--text);border-radius:8px;padding:11px 14px;font-weight:700;cursor:pointer}button:hover{border-color:#5d6a78}.primary{background:var(--cool);color:#061014}.danger{background:var(--bad);color:#210006}
    .seg{display:grid;grid-template-columns:repeat(5,1fr);gap:8px}.seg.four{grid-template-columns:repeat(4,1fr)}.seg button{padding:10px 8px}.active{outline:2px solid var(--ok)}
    input[type=range]{width:100%;accent-color:var(--hot)}.sliderRow{display:grid;grid-template-columns:44px 1fr 44px;gap:10px;align-items:center}
    .toggles{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}.tiny{font-size:13px;color:var(--muted)}@media(max-width:820px){main{grid-template-columns:1fr}.target{font-size:76px}.seg,.seg.four,.toggles{grid-template-columns:repeat(2,1fr)}}
  </style>
</head>
<body>
  <header><div class="brand">CONNECTLIFE</div><div class="status" id="wifi">--</div><div class="row"><a href="/control">Control autónomo</a><a href="/config">Configuración</a></div></header>
  <main>
    <section class="thermo"><div class="dial"><div><div class="ambient" id="ambient">--.- C ambiente</div><div class="target"><span id="target">--</span><span>C</span></div><div class="humidity" id="humidity">--% humedad</div><div class="humidity" id="acDial">aire --.- C</div></div></div></section>
    <section class="grid">
      <div class="panel">
        <div class="metric"><span>Equipo</span><b id="power">--</b></div>
        <div class="metric"><span>Temperatura del aire</span><b id="acAmbient">--</b></div>
        <div class="metric"><span>Control autónomo</span><b id="auto">--</b></div>
        <div class="metric"><span>ConnectLife</span><b id="cl">--</b></div>
        <div class="metric"><span>Modo</span><b id="mode">--</b></div>
        <div class="metric"><span>Velocidad</span><b id="fan">--</b></div>
        <div class="metric"><span>Oscilación / Sueño / Turbo</span><b id="flags">--</b></div>
        <div class="metric"><span>Última sincronización</span><b id="sync">--</b></div>
      </div>
      <div class="panel grid">
        <div class="row"><button class="primary" onclick="cmd('power',{on:true})">Encender</button><button class="danger" onclick="cmd('power',{on:false})">Apagar</button><button onclick="refresh()">Actualizar</button><button onclick="restart()">Reiniciar ESP</button></div>
        <div class="sliderRow"><button onclick="stepTemp(-1)">-</button><input id="tempSlider" type="range" min="16" max="30" step="1" oninput="target.innerText=this.value" onchange="cmd('temperature',{temperature:Number(this.value)})"><button onclick="stepTemp(1)">+</button></div>
        <div class="tiny">Modo</div><div class="seg"><button onclick="cmd('mode',{mode:'cool'})">Frío</button><button onclick="cmd('mode',{mode:'heat'})">Calor</button><button onclick="cmd('mode',{mode:'dry'})">Seco</button><button onclick="cmd('mode',{mode:'fan'})">Ventilar</button><button onclick="cmd('mode',{mode:'auto'})">Auto</button></div>
        <div class="tiny">Velocidad</div><div class="seg four"><button onclick="cmd('fan',{fan:'auto'})">Auto</button><button onclick="cmd('fan',{fan:'low'})">Baja</button><button onclick="cmd('fan',{fan:'medium'})">Media</button><button onclick="cmd('fan',{fan:'high'})">Alta</button></div>
        <div class="toggles"><button onclick="toggle('swing')">Oscilación</button><button onclick="toggle('sleep')">Sueño</button><button onclick="toggle('turbo')">Turbo</button></div>
      </div>
    </section>
  </main>
  <script>
    const $=id=>document.getElementById(id); let last={swing:false,sleep:false,turbo:false};
    const MODOS={cool:'Frío',heat:'Calor',dry:'Seco',fan:'Ventilar',auto:'Auto',off:'Apagado'};
    const VELOCIDADES={auto:'Auto',low:'Baja',medium:'Media',high:'Alta'};
    const ENCENDIDO={On:'Encendido',Off:'Apagado',Unknown:'Desconocido'};
    const BANDERAS={swing:'Oscilación',sleep:'Sueño',turbo:'Turbo'};
    async function post(url,data={}){const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});const j=await r.json();if(!j.ok) alert(j.message||'Error'); await refresh();}
    async function cmd(action,data){await post('/api/ac/'+action,data)} async function toggle(k){const d={};d[k]=!last[k];await cmd(k,d)} function restart(){post('/api/restart')}
    function stepTemp(n){const s=$('tempSlider');s.value=Number(s.value)+n;$('target').innerText=s.value;cmd('temperature',{temperature:Number(s.value)})}
    async function refresh(){const r=await fetch('/api/state');const s=await r.json();last=s.ac;$('wifi').innerText=s.wifi.status+' '+s.wifi.ip;$('ambient').innerText=s.sensor.valid?s.sensor.temperature.toFixed(1)+' C ambiente':'Sensor sin lectura';$('humidity').innerText=s.sensor.valid?s.sensor.humidity.toFixed(0)+'% humedad':'--% humedad';$('target').innerText=s.ac.targetTemperature;$('tempSlider').value=s.ac.targetTemperature;$('power').innerText=ENCENDIDO[s.ac.power]||s.ac.power;$('cl').innerText=s.connectLife.status;$('mode').innerText=MODOS[s.ac.mode]||s.ac.mode;$('fan').innerText=VELOCIDADES[s.ac.fanSpeed]||s.ac.fanSpeed;$('flags').innerText=['swing','sleep','turbo'].filter(k=>s.ac[k]).map(k=>BANDERAS[k]).join(' / ')||'--';$('sync').innerText=s.ac.lastSyncSecondsAgo>=0?'hace '+s.ac.lastSyncSecondsAgo+' s':'--';const at=s.ac.hasAmbient?s.ac.ambientTemperature.toFixed(1)+' C':'no reportada';$('acAmbient').innerText=at;$('acDial').innerText=s.ac.hasAmbient?'aire '+s.ac.ambientTemperature.toFixed(1)+' C':'aire --.- C';$('auto').innerText=s.control.autoEnabled?'Activo':'Apagado';}
    refresh(); setInterval(refresh,4000);
  </script>
</body>
</html>
)HTML";

static const char CONTROL_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="es">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Control autónomo</title>
  <style>
    :root{color-scheme:dark;--panel:#1b222a;--line:#303b46;--text:#f7f9fc;--muted:#9eabb8;--ok:#70d494;--accent:#45b7d1;--bad:#ff6378;--warn:#ffb347}
    *{box-sizing:border-box}body{margin:0;font-family:Inter,system-ui,-apple-system,Segoe UI,sans-serif;background:#111418;color:var(--text)}
    header,main{max-width:980px;margin:auto;padding:22px}header{display:flex;justify-content:space-between;align-items:center;gap:12px;flex-wrap:wrap}a{color:var(--text)}
    .panel{border:1px solid var(--line);background:var(--panel);border-radius:8px;padding:18px;margin:16px 0}
    h2{margin:0 0 14px;font-size:17px}h3{margin:0 0 10px;font-size:15px;color:var(--muted)}
    button{border:1px solid var(--line);background:#242d37;color:var(--text);border-radius:8px;padding:11px 14px;font-weight:700;cursor:pointer}
    .primary{background:var(--accent);color:#061014}.danger{background:var(--bad);color:#260008}.on{outline:2px solid var(--ok)}
    .row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}
    .metric{display:grid;grid-template-columns:1fr auto;gap:8px;padding:10px 0;border-bottom:1px solid var(--line);color:var(--muted)}.metric b{color:var(--text)}
    .big{font-size:52px;font-weight:800;text-align:center;margin:6px 0}
    input[type=range]{width:100%;accent-color:var(--accent)}
    input[type=time]{border:1px solid var(--line);background:#111820;color:var(--text);border-radius:8px;padding:9px;font:inherit}
    .days{display:flex;gap:6px;flex-wrap:wrap;margin:10px 0}.days button{padding:8px 11px;font-size:13px}
    .sched{border:1px solid var(--line);border-radius:8px;padding:14px;margin:12px 0}
    .tiny{font-size:13px;color:var(--muted)}.warn{color:var(--warn)}
    @media(max-width:700px){.big{font-size:42px}}
  </style>
</head>
<body>
  <header><h1>Control autónomo</h1><div class="row"><a href="/">Termostato</a><a href="/config">Configuración</a></div></header>
  <main>
    <section class="panel">
      <h2>Estado</h2>
      <div class="metric"><span>Control autónomo</span><b id="autoState">--</b></div>
      <div class="metric"><span>Origen del objetivo</span><b id="source">--</b></div>
      <div class="metric"><span>Objetivo del usuario</span><b id="userTarget">--</b></div>
      <div class="metric"><span>Ambiente (sensor local)</span><b id="filtered">--</b></div>
      <div class="metric"><span>Ambiente (sensor del aire)</span><b id="acAmbient">--</b></div>
      <div class="metric"><span>Error / corrección</span><b id="errbias">--</b></div>
      <div class="metric"><span>Setpoint enviado al aire</span><b id="setpoint">--</b></div>
      <div class="metric"><span>Detalle</span><b id="note">--</b></div>
      <p class="tiny" id="timeWarn"></p>
      <div class="row" style="margin-top:14px">
        <button class="primary" onclick="setAuto(true)">Activar control</button>
        <button class="danger" onclick="setAuto(false)">Desactivar</button>
      </div>
    </section>

    <section class="panel">
      <h2>Ajustar ahora</h2>
      <p class="tiny">Tiene prioridad sobre los horarios. Se cancela sola cuando arranca el siguiente bloque programado.</p>
      <div class="big"><span id="instLabel">22.0</span> °C</div>
      <input id="inst" type="range" min="18" max="25" step="0.5" value="22" oninput="instLabel.innerText=Number(this.value).toFixed(1)">
      <div class="row" style="margin-top:12px">
        <button class="primary" onclick="applyInstant(true)">Aplicar ahora</button>
        <button onclick="applyInstant(false)">Cancelar petición</button>
      </div>
    </section>

    <section class="panel">
      <h2>Modo del equipo</h2>
      <div class="row"><button id="m0" onclick="setMode(0)">Frío</button><button id="m1" onclick="setMode(1)">Calor</button><button id="m2" onclick="setMode(2)">Auto</button></div>
    </section>

    <section class="panel">
      <h2>Horarios programados</h2>
      <p class="tiny">Hasta 3 bloques. Si el fin es anterior al inicio, el bloque cruza la medianoche.</p>
      <div id="scheds"></div>
      <div class="row"><button class="primary" onclick="saveScheds()">Guardar horarios</button></div>
    </section>
  </main>
  <script>
    const $=id=>document.getElementById(id);
    const DIAS=['D','L','M','X','J','V','S'];
    const FUENTES={Disabled:'Control apagado',Idle:'Sin horario activo',Schedule:'Horario programado',Instant:'Petición instantánea'};
    let state=null, dirty=false;

    function mm(v){const h=String(Math.floor(v/60)).padStart(2,'0');const m=String(v%60).padStart(2,'0');return h+':'+m}
    function toMin(s){const [h,m]=s.split(':').map(Number);return h*60+m}

    function renderScheds(cfg){
      if(dirty) return;
      $('scheds').innerHTML=cfg.schedules.map((s,i)=>`
        <div class="sched" data-i="${i}">
          <h3>Bloque ${i+1}</h3>
          <div class="row">
            <button class="en ${s.enabled?'on':''}" onclick="togEn(${i})">${s.enabled?'Activo':'Inactivo'}</button>
            <label class="tiny">Inicio <input type="time" class="st" value="${mm(s.startMinute)}"></label>
            <label class="tiny">Fin <input type="time" class="et" value="${mm(s.endMinute)}"></label>
          </div>
          <div class="days">${DIAS.map((d,b)=>`<button class="dy ${(s.daysMask>>b)&1?'on':''}" data-b="${b}" onclick="togDay(${i},${b})">${d}</button>`).join('')}</div>
          <label class="tiny">Objetivo <b class="tl">${s.targetC.toFixed(1)}</b> °C
            <input type="range" class="tg" min="18" max="25" step="0.5" value="${s.targetC}" oninput="this.closest('.sched').querySelector('.tl').innerText=Number(this.value).toFixed(1);dirty=true">
          </label>
        </div>`).join('');
    }
    function togEn(i){dirty=true;const el=document.querySelector(`.sched[data-i="${i}"] .en`);const on=el.classList.toggle('on');el.innerText=on?'Activo':'Inactivo'}
    function togDay(i,b){dirty=true;document.querySelector(`.sched[data-i="${i}"] .dy[data-b="${b}"]`).classList.toggle('on')}

    function collect(){
      return [...document.querySelectorAll('.sched')].map(el=>({
        enabled:el.querySelector('.en').classList.contains('on'),
        daysMask:[...el.querySelectorAll('.dy')].reduce((a,d)=>a|(d.classList.contains('on')?1<<Number(d.dataset.b):0),0),
        startMinute:toMin(el.querySelector('.st').value),
        endMinute:toMin(el.querySelector('.et').value),
        targetC:Number(el.querySelector('.tg').value)
      }));
    }
    async function post(data){const r=await fetch('/api/control',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});const j=await r.json();if(!j.ok)alert(j.message||'Error');dirty=false;await refresh()}
    function setAuto(on){post({autoEnabled:on})}
    function setMode(m){post({mode:m})}
    function applyInstant(on){post({instantActive:on,instantTargetC:Number($('inst').value)})}
    function saveScheds(){post({schedules:collect()})}

    async function refresh(){
      const s=await (await fetch('/api/control')).json(); state=s;
      $('autoState').innerText=s.config.autoEnabled?'Activo':'Apagado';
      $('source').innerText=FUENTES[s.status.source]||s.status.source;
      $('userTarget').innerText=s.status.targetC==null?'--':s.status.targetC.toFixed(1)+' °C';
      $('filtered').innerText=s.status.filteredC==null?'--':s.status.filteredC.toFixed(2)+' °C';
      $('acAmbient').innerText=s.ac.hasAmbient?s.ac.ambientTemperature.toFixed(1)+' °C':'no reportada';
      $('errbias').innerText=s.status.errorC==null?'--':s.status.errorC.toFixed(2)+' °C / '+s.status.biasC.toFixed(2)+' °C';
      $('setpoint').innerText=s.status.hasCommanded?s.status.commandedSetpointC+' °C':'--';
      $('note').innerText=s.status.note||'--';
      $('timeWarn').innerText=s.status.timeSynced?'':'Sin hora sincronizada (NTP): los horarios no se evaluarán.';
      $('timeWarn').className=s.status.timeSynced?'tiny':'tiny warn';
      if(!dirty){$('inst').value=s.config.instantTargetC;$('instLabel').innerText=s.config.instantTargetC.toFixed(1)}
      [0,1,2].forEach(m=>$('m'+m).classList.toggle('on',s.config.mode===m));
      renderScheds(s.config);
    }
    refresh(); setInterval(()=>{if(!dirty)refresh()},5000);
  </script>
</body>
</html>
)HTML";

static const char CONFIG_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="es">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Config ConnectLife</title>
  <style>
    :root{color-scheme:dark;--bg:#111418;--panel:#1b222a;--line:#303b46;--text:#f7f9fc;--muted:#9eabb8;--ok:#70d494;--accent:#45b7d1;--bad:#ff6378}
    *{box-sizing:border-box}body{margin:0;font-family:Inter,system-ui,-apple-system,Segoe UI,sans-serif;background:#111418;color:var(--text)}header,main{max-width:980px;margin:auto;padding:22px}header{display:flex;justify-content:space-between;align-items:center}
    a{color:var(--text)}.panel{border:1px solid var(--line);background:var(--panel);border-radius:8px;padding:18px;margin:16px 0}.grid{display:grid;grid-template-columns:repeat(2,1fr);gap:14px}
    label{display:grid;gap:7px;color:var(--muted);font-size:14px}input{width:100%;border:1px solid var(--line);background:#111820;color:var(--text);border-radius:8px;padding:12px;font:inherit}
    button{border:1px solid var(--line);background:#242d37;color:var(--text);border-radius:8px;padding:11px 14px;font-weight:700;cursor:pointer}.primary{background:var(--accent);color:#061014}.danger{background:var(--bad);color:#260008}
    pre{white-space:pre-wrap;min-height:180px;background:#0d1116;border:1px solid var(--line);border-radius:8px;padding:12px;color:#cbd5df}.row{display:flex;gap:10px;flex-wrap:wrap}.status{color:var(--muted)}@media(max-width:760px){.grid{grid-template-columns:1fr}}
  </style>
</head>
<body>
  <header><h1>Configuración</h1><a href="/">Termostato</a></header>
  <main>
    <section class="panel">
      <div class="grid">
        <label>SSID WiFi<input id="wifiSsid"></label><label>Contraseña WiFi<input id="wifiPassword" type="password"></label>
        <label>Correo ConnectLife<input id="email" type="email"></label><label>Contraseña ConnectLife<input id="password" type="password"></label>
        <label>URL base del API ConnectLife<input id="apiBaseUrl"></label><label>ID del dispositivo<input id="deviceId"></label>
        <label>Zona horaria (POSIX TZ, p. ej. CST6)<input id="timezone"></label>
      </div>
      <p class="status" id="token">Token: --</p>
      <div class="row"><button class="primary" onclick="save()">Guardar</button><button onclick="login()">Iniciar sesión</button><button onclick="refresh()">Actualizar estado</button><button class="danger" onclick="restart()">Reiniciar ESP</button></div>
    </section>
    <section class="panel">
      <h2>Actualizar Firmware OTA</h2>
      <form method="POST" action="/update" enctype="multipart/form-data"><input type="file" name="firmware" accept=".bin"><br><br><button class="primary" type="submit">Actualizar Firmware</button></form>
    </section>
    <section class="panel"><h2>Registro</h2><pre id="logs"></pre></section>
  </main>
  <script>
    const $=id=>document.getElementById(id); let loaded=false; let dirty=false;
    ['wifiSsid','wifiPassword','email','password','apiBaseUrl','deviceId','timezone'].forEach(id=>$(id).addEventListener('input',()=>dirty=true));
    async function refresh(){const s=await (await fetch('/api/state')).json();if(!loaded&&!dirty){$('wifiSsid').value=s.config.wifiSsid||'';$('email').value=s.config.email||'';$('apiBaseUrl').value=s.config.apiBaseUrl||'';$('deviceId').value=s.config.deviceId||'';$('timezone').value=s.config.timezone||'';loaded=true;}$('token').innerText='Token: '+(s.config.hasAccessToken?'guardado':'sin token')+' | ID dispositivo: '+(s.config.deviceId||'--')+' | '+s.connectLife.status;$('logs').innerText=await (await fetch('/api/logs')).text();}
    async function post(url,data={}){const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});const j=await r.json();alert(j.message||'OK');dirty=false;loaded=false;await refresh();}
    function save(){post('/api/config',{wifiSsid:$('wifiSsid').value,wifiPassword:$('wifiPassword').value,email:$('email').value,password:$('password').value,apiBaseUrl:$('apiBaseUrl').value,deviceId:$('deviceId').value,timezone:$('timezone').value})}
    function login(){post('/api/login')} function restart(){post('/api/restart')} refresh(); setInterval(refresh,5000);
  </script>
</body>
</html>
)HTML";

WebPortal::WebPortal(AppConfig &configRef,
                     ConnectLifeClient &connectLifeRef,
                     Sensor &sensorRef,
                     TempControl &tempControlRef,
                     AppLogger &loggerRef)
    : server(80),
      config(configRef),
      connectLife(connectLifeRef),
      sensor(sensorRef),
      tempControl(tempControlRef),
      logger(loggerRef) {}

void WebPortal::begin()
{
  registerRoutes();
  server.begin();
  MDNS.begin("connectlife-esp32");
  logger.info("Web server started on port 80");
}

void WebPortal::handleClient()
{
  server.handleClient();
}

void WebPortal::registerRoutes()
{
  server.on("/", HTTP_GET, [this]() { sendIndex(); });
  server.on("/config", HTTP_GET, [this]() { sendConfig(); });
  server.on("/control", HTTP_GET, [this]() { sendControlPage(); });
  server.on("/api/control", HTTP_GET, [this]() { sendControlState(); });
  server.on("/api/control", HTTP_POST, [this]() { saveControl(); });
  server.on("/generate_204", HTTP_GET, [this]() { redirectToConfig(); });
  server.on("/gen_204", HTTP_GET, [this]() { redirectToConfig(); });
  server.on("/hotspot-detect.html", HTTP_GET, [this]() { redirectToConfig(); });
  server.on("/library/test/success.html", HTTP_GET, [this]() { redirectToConfig(); });
  server.on("/ncsi.txt", HTTP_GET, [this]() { redirectToConfig(); });
  server.on("/connecttest.txt", HTTP_GET, [this]() { redirectToConfig(); });
  server.on("/api/state", HTTP_GET, [this]() { sendState(); });
  server.on("/api/config", HTTP_POST, [this]() { saveConfig(); });
  server.on("/api/login", HTTP_POST, [this]() { loginConnectLife(); });
  server.on("/api/restart", HTTP_POST, [this]() { restartEsp(); });
  server.on("/api/logs", HTTP_GET, [this]() { sendLogs(); });
  server.on("/api/ac/power", HTTP_POST, [this]() { handleAcCommand(); });
  server.on("/api/ac/temperature", HTTP_POST, [this]() { handleAcCommand(); });
  server.on("/api/ac/mode", HTTP_POST, [this]() { handleAcCommand(); });
  server.on("/api/ac/fan", HTTP_POST, [this]() { handleAcCommand(); });
  server.on("/api/ac/swing", HTTP_POST, [this]() { handleAcCommand(); });
  server.on("/api/ac/sleep", HTTP_POST, [this]() { handleAcCommand(); });
  server.on("/api/ac/turbo", HTTP_POST, [this]() { handleAcCommand(); });
  server.on("/update", HTTP_GET, [this]() { sendOtaForm(); });
  server.on("/update", HTTP_POST, [this]() { finishOta(); }, [this]() { handleOtaUpload(); });
  server.onNotFound([this]() { redirectToConfig(); });
}

void WebPortal::sendIndex()
{
  if (config.get().wifiSsid.length() == 0) {
    sendConfig();
    return;
  }
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void WebPortal::sendConfig()
{
  server.send_P(200, "text/html; charset=utf-8", CONFIG_HTML);
}

void WebPortal::sendControlPage()
{
  if (config.get().wifiSsid.length() == 0) {
    sendConfig();
    return;
  }
  server.send_P(200, "text/html; charset=utf-8", CONTROL_HTML);
}

static const char *controlSourceName(ControlSource source)
{
  switch (source) {
    case ControlSource::Idle: return "Idle";
    case ControlSource::Schedule: return "Schedule";
    case ControlSource::Instant: return "Instant";
    case ControlSource::Disabled:
    default: return "Disabled";
  }
}

void WebPortal::sendControlState()
{
  DynamicJsonDocument doc(3072);
  const ControlStatus status = tempControl.getStatus();
  const ControlConfig cfg = tempControl.getConfig();
  const AcState ac = connectLife.getState();

  doc["status"]["source"] = controlSourceName(status.source);
  doc["status"]["targetC"] = status.targetC;
  doc["status"]["filteredC"] = status.filteredC;
  doc["status"]["errorC"] = status.errorC;
  doc["status"]["biasC"] = status.biasC;
  doc["status"]["commandedSetpointC"] = status.commandedSetpointC;
  doc["status"]["hasCommanded"] = status.hasCommanded;
  doc["status"]["scheduleIndex"] = status.scheduleIndex;
  doc["status"]["timeSynced"] = status.timeSynced;
  doc["status"]["note"] = status.note;

  doc["ac"]["hasAmbient"] = ac.hasAmbient;
  doc["ac"]["ambientTemperature"] = ac.ambientTemperature;
  doc["ac"]["targetTemperature"] = ac.targetTemperature;

  doc["config"]["autoEnabled"] = cfg.autoEnabled;
  doc["config"]["mode"] = static_cast<int>(cfg.mode);
  doc["config"]["instantActive"] = cfg.instantActive;
  doc["config"]["instantTargetC"] = cfg.instantTargetC;
  doc["limits"]["minC"] = CONTROL_USER_MIN_C;
  doc["limits"]["maxC"] = CONTROL_USER_MAX_C;

  JsonArray schedules = doc["config"].createNestedArray("schedules");
  for (uint8_t i = 0; i < MAX_SCHEDULES; i++) {
    JsonObject item = schedules.createNestedObject();
    item["enabled"] = cfg.schedules[i].enabled;
    item["daysMask"] = cfg.schedules[i].daysMask;
    item["startMinute"] = cfg.schedules[i].startMinute;
    item["endMinute"] = cfg.schedules[i].endMinute;
    item["targetC"] = cfg.schedules[i].targetC;
  }

  sendJson(doc);
}

void WebPortal::saveControl()
{
  DynamicJsonDocument doc(2048);
  if (!readJsonBody(doc)) {
    sendOk(false, "JSON inválido");
    return;
  }

  if (doc.containsKey("mode")) {
    const int mode = doc["mode"] | 0;
    if (mode < 0 || mode > 2) {
      sendOk(false, "Modo inválido");
      return;
    }
    tempControl.setMode(static_cast<ControlMode>(mode));
  }

  if (doc.containsKey("schedules")) {
    JsonArray items = doc["schedules"].as<JsonArray>();
    if (items.isNull() || items.size() > MAX_SCHEDULES) {
      sendOk(false, "Lista de horarios inválida");
      return;
    }
    uint8_t index = 0;
    for (JsonObject item : items) {
      Schedule schedule;
      schedule.enabled = item["enabled"] | false;
      schedule.daysMask = static_cast<uint8_t>(item["daysMask"] | 0);
      schedule.startMinute = static_cast<uint16_t>(item["startMinute"] | 0);
      schedule.endMinute = static_cast<uint16_t>(item["endMinute"] | 0);
      schedule.targetC = item["targetC"] | 22.0f;
      if (!tempControl.setSchedule(index, schedule)) {
        sendOk(false, "Horario " + String(index + 1) + " inválido");
        return;
      }
      index++;
    }
  }

  // instantActive se procesa antes que autoEnabled: pedir "ahora" enciende el
  // control, y una desactivación explícita en el mismo cuerpo debe ganar.
  if (doc.containsKey("instantActive")) {
    tempControl.setInstant(doc["instantActive"] | false, doc["instantTargetC"] | 22.0f);
  }

  if (doc.containsKey("autoEnabled")) {
    tempControl.setAutoEnabled(doc["autoEnabled"] | false);
  }

  sendOk(true, "Control actualizado");
}

void WebPortal::redirectToConfig()
{
  const String host = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  const String url = "http://" + host + "/config";
  server.sendHeader("Location", url, true);
  server.send(302, "text/plain; charset=utf-8", "Configuración: " + url);
}

void WebPortal::sendState()
{
  DynamicJsonDocument doc(4096);
  const DeviceConfig cfg = config.get();
  const SensorReading reading = sensor.getReading();
  const AcState ac = connectLife.getState();

  doc["wifi"]["status"] = WiFi.status() == WL_CONNECTED ? "WiFi" : "AP/Sin red";
  doc["wifi"]["ip"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  doc["connectLife"]["status"] = connectLife.statusText();

  doc["sensor"]["valid"] = reading.valid;
  doc["sensor"]["temperature"] = reading.temperatureC;
  doc["sensor"]["humidity"] = reading.humidity;
  doc["sensor"]["ageSeconds"] = reading.updatedAtMs == 0 ? -1 : (long)((millis() - reading.updatedAtMs) / 1000);

  doc["ac"]["power"] = ac.power == AcPower::On ? "On" : (ac.power == AcPower::Off ? "Off" : "Unknown");
  doc["ac"]["targetTemperature"] = ac.targetTemperature;
  doc["ac"]["hasAmbient"] = ac.hasAmbient;
  doc["ac"]["ambientTemperature"] = ac.ambientTemperature;
  doc["ac"]["mode"] = ac.mode;
  doc["ac"]["fanSpeed"] = ac.fanSpeed;
  doc["ac"]["swing"] = ac.swing;
  doc["ac"]["sleep"] = ac.sleep;
  doc["ac"]["turbo"] = ac.turbo;
  doc["ac"]["lastSyncSecondsAgo"] = ac.lastSyncMs == 0 ? -1 : (long)((millis() - ac.lastSyncMs) / 1000);

  const ControlStatus controlStatus = tempControl.getStatus();
  doc["control"]["autoEnabled"] = controlStatus.autoEnabled;
  doc["control"]["note"] = controlStatus.note;

  doc["config"]["wifiSsid"] = cfg.wifiSsid;
  doc["config"]["email"] = cfg.connectLifeEmail;
  doc["config"]["deviceId"] = cfg.deviceId;
  doc["config"]["apiBaseUrl"] = cfg.apiBaseUrl;
  doc["config"]["timezone"] = cfg.timezone;
  doc["config"]["hasAccessToken"] = cfg.accessToken.length() > 0;

  sendJson(doc);
}

void WebPortal::saveConfig()
{
  DynamicJsonDocument doc(1024);
  if (!readJsonBody(doc)) {
    sendOk(false, "JSON inválido");
    return;
  }

  if (doc.containsKey("wifiSsid")) {
    config.saveWiFi(doc["wifiSsid"].as<String>(), doc["wifiPassword"].as<String>());
  }
  if (doc.containsKey("email")) {
    config.saveConnectLifeCredentials(doc["email"].as<String>(), doc["password"].as<String>());
  }
  if (doc.containsKey("apiBaseUrl")) {
    config.saveApiBaseUrl(doc["apiBaseUrl"].as<String>());
  }
  if (doc.containsKey("deviceId")) {
    config.saveDeviceId(doc["deviceId"].as<String>());
  }
  if (doc.containsKey("timezone")) {
    const String timezone = doc["timezone"].as<String>();
    if (timezone.length() > 0) {
      config.saveTimezone(timezone);
      // Aplica de inmediato; si no, la zona nueva solo entraría tras reiniciar.
      configTzTime(timezone.c_str(), "pool.ntp.org", "time.nist.gov");
    }
  }
  sendOk(true, "Configuración guardada");
}

void WebPortal::loginConnectLife()
{
  sendOk(connectLife.login(), connectLife.statusText());
}

void WebPortal::restartEsp()
{
  sendOk(true, "Reiniciando ESP32");
  delay(250);
  ESP.restart();
}

void WebPortal::sendLogs()
{
  server.send(200, "text/plain; charset=utf-8", logger.asText());
}

void WebPortal::handleAcCommand()
{
  DynamicJsonDocument doc(512);
  if (!readJsonBody(doc)) {
    sendOk(false, "JSON inválido");
    return;
  }

  const String uri = server.uri();

  // Mandar el aire a mano desde la página de termostato apaga el control
  // autónomo: si no, ambos pelearían por el setpoint.
  tempControl.notifyManualOverride(uri);

  bool ok = false;
  if (uri.endsWith("/power")) {
    ok = connectLife.setPower(doc["on"] | false);
  } else if (uri.endsWith("/temperature")) {
    ok = connectLife.setTargetTemperature(doc["temperature"] | 24);
  } else if (uri.endsWith("/mode")) {
    ok = connectLife.setMode(doc["mode"].as<String>());
  } else if (uri.endsWith("/fan")) {
    ok = connectLife.setFanSpeed(doc["fan"].as<String>());
  } else if (uri.endsWith("/swing")) {
    ok = connectLife.setSwing(doc["swing"] | false);
  } else if (uri.endsWith("/sleep")) {
    ok = connectLife.setSleep(doc["sleep"] | false);
  } else if (uri.endsWith("/turbo")) {
    ok = connectLife.setTurbo(doc["turbo"] | false);
  }

  sendOk(ok, connectLife.statusText());
}

void WebPortal::sendOtaForm()
{
  server.send(200, "text/html; charset=utf-8",
              "<form method='POST' action='/update' enctype='multipart/form-data'>"
              "<input type='file' name='firmware'><button>Actualizar</button></form>");
}

void WebPortal::handleOtaUpload()
{
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    logger.info("OTA upload started: " + upload.filename);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      logger.error("OTA begin failed");
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      logger.error("OTA write failed");
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      logger.info("OTA complete");
    } else {
      logger.error("OTA end failed");
    }
  }
}

void WebPortal::finishOta()
{
  if (Update.hasError()) {
    server.send(500, "text/plain; charset=utf-8", "La actualización OTA falló");
    return;
  }
  server.send(200, "text/plain; charset=utf-8", "OTA correcta. Reiniciando...");
  delay(500);
  ESP.restart();
}

bool WebPortal::readJsonBody(JsonDocument &doc)
{
  const String body = server.arg("plain");
  const DeserializationError error = deserializeJson(doc, body);
  return !error;
}

void WebPortal::sendJson(JsonDocument &doc, int status)
{
  String out;
  serializeJson(doc, out);
  server.send(status, "application/json; charset=utf-8", out);
}

void WebPortal::sendOk(bool ok, const String &message)
{
  StaticJsonDocument<192> doc;
  doc["ok"] = ok;
  doc["message"] = message;
  sendJson(doc, ok ? 200 : 500);
}
