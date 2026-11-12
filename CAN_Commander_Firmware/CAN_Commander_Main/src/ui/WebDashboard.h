#pragma once

#include <Arduino.h>

namespace ui {

static const char kWebDashboardHtml[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="theme-color" content="#101820">
<title>CAN Commander</title>
<style>
:root{color-scheme:dark;--bg:#101820;--panel:#1b2833;--line:#344756;--text:#edf4f8;--muted:#a9bac5;--accent:#43c59e;--warn:#ffd166;--bad:#ff6b6b}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:16px/1.45 system-ui,-apple-system,Segoe UI,sans-serif}
main{max-width:980px;margin:auto;padding:20px}header{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:16px}
h1{font-size:1.55rem;margin:0}h2{font-size:1.05rem;margin:0 0 12px}.muted{color:var(--muted);font-size:.92rem}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,300px),1fr));gap:14px}
section,.banner{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:16px;margin-bottom:14px}
.banner{border-color:#80652d;background:#302a1b}.banner[hidden]{display:none}
.row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}.row>*{flex:1}
label{display:block;color:var(--muted);font-size:.9rem;margin:10px 0 5px}
input,select,textarea,button{font:inherit;border-radius:9px;border:1px solid var(--line);padding:10px;background:#111b23;color:var(--text);width:100%}
textarea{min-height:88px;resize:vertical;font-family:ui-monospace,SFMono-Regular,Consolas,monospace}
button{background:#234c45;border-color:#367966;font-weight:650;cursor:pointer}
button.secondary{background:#243441;border-color:var(--line)}button.danger{background:#542c31;border-color:#82434a}
button:disabled{opacity:.55;cursor:wait}.cards{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}
.card{background:#111b23;border:1px solid var(--line);border-radius:10px;padding:10px}.card small{display:block;color:var(--muted)}
pre{white-space:pre-wrap;word-break:break-word;max-height:300px;overflow:auto;background:#111b23;padding:12px;border-radius:9px;margin:10px 0 0}
.ok{color:var(--accent)}.warn{color:var(--warn)}.note{font-size:.85rem;color:var(--muted)}
@media(max-width:500px){main{padding:12px}.cards{grid-template-columns:1fr}header{align-items:flex-start}}
</style>
</head>
<body>
<main>
<header><div><h1>CAN Commander</h1><div class="muted">Local vehicle-bus tools</div></div><button class="secondary" style="width:auto" onclick="refreshStatus()">Refresh</button></header>

<div id="passwordBanner" class="banner" hidden>
  <strong class="warn">Default Wi-Fi password is still active.</strong>
  <p class="note">The AP password is <code>cancommander</code>. Change it below for better privacy. You can dismiss this reminder; the default password will remain active until changed.</p>
  <div class="row"><button onclick="document.querySelector('#newPassword').focus()">Change password</button><button class="secondary" onclick="dismissPasswordPrompt()">Remind me later</button></div>
</div>

<section>
  <h2>Device status</h2>
  <div id="statusCards" class="cards"><div class="card">Connecting…</div></div>
  <pre id="statusText" aria-live="polite">Waiting for device…</pre>
</section>

<section>
  <h2>Live tool output</h2>
  <pre id="liveLog" aria-live="polite">Waiting for CAN and tool output…</pre>
</section>

<div class="grid">
<section>
  <h2>Bus setup</h2>
  <form id="busForm">
    <div class="row">
      <div><label for="bus">Bus</label><select id="bus"><option value="can0">CAN0</option><option value="can1">CAN1</option></select></div>
      <div><label for="bitrate">Bitrate</label><select id="bitrate"><option>125000</option><option>250000</option><option selected>500000</option><option>800000</option><option>1000000</option></select></div>
    </div>
    <label for="mode">Mode</label><select id="mode"><option value="listen">Listen-only (safe observation)</option><option value="normal">Normal (may transmit)</option></select>
    <p class="note">Listen-only is recommended while identifying a vehicle bus.</p>
    <button type="submit">Apply bus settings</button>
  </form>
</section>

<section>
  <h2>Tool controls</h2>
  <form id="startForm">
    <label for="tool">Tool</label>
    <select id="tool">
      <option value="read_all">Read all</option><option value="filtered">Filtered read</option>
      <option value="write">Periodic write</option><option value="speed">Speed test</option>
      <option value="valtrack">Value tracker</option><option value="unique_ids">Unique IDs</option>
      <option value="bittrack">Bit tracker</option><option value="reverse">Reverse engineering</option>
      <option value="obd_pid">OBD-II PID</option><option value="dbc_decode">DBC decode</option>
      <option value="custom_inject">Custom inject</option><option value="replay">Replay</option>
    </select>
    <label for="toolArgs">Tool parameters</label>
    <textarea id="toolArgs" spellcheck="false" placeholder="bus=can0"></textarea>
    <div id="toolHint" class="note"></div>
    <div class="row" style="margin-top:10px"><button type="submit">Start tool</button><button id="stopTool" class="danger" type="button">Stop active tool</button></div>
  </form>
  <form id="configForm">
    <label for="toolConfig">Configure active tool</label>
    <input id="toolConfig" autocomplete="off" placeholder="e.g. cmd=record">
    <button class="secondary" type="submit" style="margin-top:8px">Apply tool config</button>
  </form>
</section>

<section>
  <h2>Wi-Fi password</h2>
  <p class="note">The access point starts automatically as <strong>CanCommander</strong>. A new password is saved to the device, then the device reboots to apply it. Reconnect to the AP using the new password.</p>
  <form id="passwordForm">
    <label for="newPassword">New AP password (8–63 characters, no spaces)</label>
    <input id="newPassword" type="password" minlength="8" maxlength="63" autocomplete="new-password" required>
    <button type="submit" style="margin-top:10px">Save Wi-Fi password</button>
  </form>
</section>
</div>

<section><h2>Command result</h2><pre id="result" aria-live="polite">Ready.</pre></section>
<p class="note">Use only when parked or on a bench. CAN tools can transmit messages that affect connected vehicle modules. Anyone with the AP password can use this dashboard.</p>
</main>
<script>
const $=id=>document.getElementById(id);
const examples={
 read_all:"bus=can0 ascii=0",filtered:"bus=can0 mask=7FF filter=273 ext_match=0 ext=0 ascii=0",
 write:"bus=can0 id=1D5 ext=0 rtr=0 dlc=8 data=102030AABBCCDDEE period_ms=100 count=10",
 speed:"bus=can0",valtrack:"bus=can0 strict=1",unique_ids:"bus=can0",bittrack:"bus=can0 id=273",
 reverse:"bus=can0 calibration_ms=10000 monitor_ms=20000",obd_pid:"bus=can0 pid=rpm interval_ms=200",
 dbc_decode:"bus=can0",custom_inject:"bus=can0",replay:"bus=can0 id=2C1"
};
function updateToolHint(){const name=$("tool").value;$("toolArgs").placeholder=examples[name]||"";$("toolHint").textContent="Example: "+examples[name]+". Parameters follow the firmware tool syntax."}
$("tool").addEventListener("change",updateToolHint);updateToolHint();
async function command(cmd){
 $("result").textContent="Working…";
 try{
  const response=await fetch("/api/command",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:new URLSearchParams({cmd})});
  const text=await response.text();$("result").textContent=text|| (response.ok?"OK":"Request failed");
  if(!response.ok)throw new Error(text||"Request failed");
  return text;
 }catch(error){$("result").textContent=error.message+" If changing Wi-Fi credentials, reconnect to CanCommander with the new password.";throw error}
}
async function refreshStatus(){
 try{
  const response=await fetch("/api/status",{cache:"no-store"});const text=await response.text();
  if(!response.ok)throw new Error(text);
  $("statusText").textContent=text;
  const lines=text.split("\n").filter(Boolean);
  const value=(prefix)=>{const line=lines.find(item=>item.startsWith(prefix));return line||"—"};
  $("statusCards").innerHTML="";
  [value("owner="),value("can0 bitrate="),value("can1 backend="),value("wifi ap="),value("wifi_password_default="),value("tool ")].forEach(item=>{
   const card=document.createElement("div");card.className="card";card.textContent=item; $("statusCards").appendChild(card);
  });
  const isDefault=lines.includes("wifi_password_default=1");
  $("passwordBanner").hidden=!isDefault||localStorage.getItem("cc-password-reminder-dismissed")==="1";
 }catch(error){$("statusText").textContent="Status unavailable: "+error.message}
}
async function refreshEvents(){
 try{
  const response=await fetch("/api/events",{cache:"no-store"});
  if(!response.ok)throw new Error(await response.text());
  const text=await response.text();
  const log=$("liveLog");
  if(log.textContent!==text){log.textContent=text||"No recent output.";log.scrollTop=log.scrollHeight}
 }catch(error){$("liveLog").textContent="Live output unavailable: "+error.message}
}
function dismissPasswordPrompt(){$("passwordBanner").hidden=true;localStorage.setItem("cc-password-reminder-dismissed","1")}
$("busForm").addEventListener("submit",async event=>{event.preventDefault();await command(`bus set bus=${$("bus").value} bitrate=${$("bitrate").value} mode=${$("mode").value}`);refreshStatus()});
$("startForm").addEventListener("submit",async event=>{event.preventDefault();const args=$("toolArgs").value.trim();await command(`tool start ${$("tool").value}${args?" "+args:""}`);refreshStatus()});
$("stopTool").addEventListener("click",async()=>{await command("tool stop");refreshStatus()});
$("configForm").addEventListener("submit",async event=>{event.preventDefault();const args=$("toolConfig").value.trim();if(args)await command("tool config "+args);refreshStatus()});
$("passwordForm").addEventListener("submit",async event=>{
 event.preventDefault();const password=$("newPassword").value;
 if(password.length<8||password.length>63||/\s/.test(password)){ $("result").textContent="Password must be 8–63 characters with no spaces.";return }
 try{await command("wifi set pass="+password);$("passwordBanner").hidden=true;$("newPassword").value="";refreshStatus()}catch(_){}
});
refreshStatus();refreshEvents();setInterval(refreshStatus,3000);setInterval(refreshEvents,1000);
</script>
</body>
</html>
)HTML";

} // namespace ui
