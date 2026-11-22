// Setup page served by the setup portal (SPEC.md 6.9). Saved values arrive from
// /config; secrets never do, only whether they're saved.
#pragma once

#include <pgmspace.h>

static const char SETUP_PAGE[] PROGMEM = R"rawliteral(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CYD Setup</title>
<style>
body{margin:0;background:#000;color:#F2F2F5;font:16px/1.4 system-ui,sans-serif}
main{max-width:480px;margin:0 auto;padding:16px}
h1{font-size:22px;margin:4px 0 12px}
section{background:#111318;border-radius:8px;padding:12px;margin:0 0 12px}
h2{font-size:15px;margin:0 0 4px}
p.note{color:#9AA0B4;font-size:13px;margin:0 0 8px}
label{display:block;color:#9AA0B4;font-size:13px;margin:10px 0 4px}
input{box-sizing:border-box;width:100%;padding:10px;border-radius:6px;border:1px solid #23262F;background:#000;color:#F2F2F5;font-size:16px}
.saved{display:flex;gap:8px;align-items:center}
.saved span{flex:1;padding:10px;border-radius:6px;background:#23262F;color:#3FCF8E}
button{padding:10px 14px;border:0;border-radius:6px;background:#23262F;color:#F2F2F5;font-size:15px}
button.main{width:100%;background:#7C88F7;color:#000;font-weight:600;padding:12px}
.row{display:flex;gap:8px}.row>div{flex:1}.row>div.port{flex:0 0 96px}
</style></head><body><main>
<h1>CYD Victron display setup</h1>
<form method="post" action="/save" autocomplete="off">
<section>
<h2>Cerbo hotspot (local mode)</h2>
<p class="note">Preferred. The display joins the Cerbo's own WiFi and reads its MQTT broker directly.</p>
<label for="hs_ssid">Hotspot network name (SSID)</label><input id="hs_ssid" name="hs_ssid" maxlength="32">
<label>Hotspot password</label><div data-secret="hs_pass"></div>
<div class="row"><div><label for="mq_host">Broker address</label><input id="mq_host" name="mq_host" maxlength="63"></div>
<div class="port"><label for="mq_port">Port</label><input id="mq_port" name="mq_port" inputmode="numeric" maxlength="5"></div></div>
<label for="mq_user">GX username (optional)</label><input id="mq_user" name="mq_user" maxlength="32" autocapitalize="none">
<label>GX password (optional)</label><div data-secret="gx_pass"></div>
<p class="note">Only needed if the Cerbo's security profile is "Weak". Leave the username blank unless MQTT Explorer needed one.</p>
</section>
<section>
<h2>Trailer WiFi and VRM (cloud mode)</h2>
<p class="note">Fallback when the Cerbo hotspot is out of range.</p>
<label for="wf_ssid">Trailer WiFi network name (SSID)</label><input id="wf_ssid" name="wf_ssid" maxlength="32">
<label>Trailer WiFi password</label><div data-secret="wf_pass"></div>
<label for="portal">VRM portal ID</label><input id="portal" name="portal" maxlength="32" autocapitalize="none">
<label for="vrm_user">VRM email</label><input id="vrm_user" name="vrm_user" type="email" maxlength="64" autocapitalize="none">
<label>VRM access token</label><div data-secret="vrm_token"></div>
<p class="note">Create the token for a separate, read-only VRM user that only has access to this installation. Paste just the token.</p>
</section>
<button class="main" type="submit">Save and restart</button>
</form>
<script>
function input(name){var i=document.createElement('input');i.name=name;i.type='password';i.maxLength=128;return i}
fetch('/config').then(function(r){return r.json()}).then(function(c){
 ['hs_ssid','mq_host','mq_port','mq_user','wf_ssid','portal','vrm_user'].forEach(function(k){document.getElementById(k).value=c[k]});
 document.querySelectorAll('[data-secret]').forEach(function(d){
  var k=d.dataset.secret;
  if(!c.saved[k]){d.appendChild(input(k));return}
  d.className='saved';d.innerHTML='<span>Saved</span>';
  var b=document.createElement('button');b.type='button';b.textContent='Replace';
  b.onclick=function(){d.className='';d.innerHTML='';var i=input(k);d.appendChild(i);i.focus()};
  d.appendChild(b);
 });
});
</script>
</main></body></html>)rawliteral";

// Shown after a save attempt; %s is the message block
static const char RESULT_PAGE[] PROGMEM = R"rawliteral(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CYD Setup</title>
<style>body{margin:0;background:#000;color:#F2F2F5;font:16px/1.4 system-ui,sans-serif}
main{max-width:480px;margin:0 auto;padding:16px}p{background:#111318;border-radius:8px;padding:12px}
.err{color:#FF5A5F}.warn{color:#F5A524}.ok{color:#3FCF8E}a{color:#7C88F7}</style></head>
<body><main>%s</main></body></html>)rawliteral";
