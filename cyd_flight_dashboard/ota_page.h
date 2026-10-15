#pragma once
// Página web de configuração/OTA.
// Fica em um .h de propósito: a Arduino IDE gera protótipos de funções lendo só o .ino e confunde
// o JavaScript ("function nome(){...}") dentro desta string com funções C++.
#include <Arduino.h>

static const char OTA_PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="pt"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>PlaneRadar - Desk Companion</title>
<style>
body{font-family:system-ui,sans-serif;background:#0b1a20;color:#e8f3f5;max-width:520px;margin:28px auto;padding:0 16px 40px}
h1{font-size:20px;color:#3de0b5;margin:0 0 4px}h2{font-size:13px;letter-spacing:2px;text-transform:uppercase;color:#3de0b5;margin:26px 0 0}
section{background:#0e232b;border:1px solid #1a3c42;border-radius:14px;padding:6px 16px 18px;margin-top:18px}
label{display:block;font-size:12px;color:#9fb3bd;margin-top:12px}small{color:#6f8e96}
input,button{width:100%;box-sizing:border-box;margin-top:5px;padding:11px;border-radius:8px;border:1px solid #1f4047;background:#10262e;color:#e8f3f5;font-size:14px}
input:focus{outline:1px solid #3de0b5}input[type=range]{padding:0;height:28px;accent-color:#3de0b5;background:transparent;border:0}.row{display:flex;gap:10px}.row>div{flex:1;min-width:0}
button{background:#3de0b5;color:#04221b;font-weight:600;cursor:pointer;margin-top:18px}
progress{width:100%;margin-top:16px;height:14px}.msg{margin-top:12px;font-size:14px;color:#9fb3bd;min-height:18px}.err{color:#ff8a80}
.warn{font-size:12px;color:#e8a87c;margin-top:8px}
.lk{display:inline-block;padding:8px 14px;border:1px solid #3de0b5;border-radius:8px;color:#3de0b5;text-decoration:none;font-size:13px}
.hint{font-size:12px;color:#6f8e96;margin-top:6px}
.xr{display:flex;gap:8px;margin-top:8px}.xr input{margin-top:0}.xr .xk{flex:0 0 38%}
.xr button{width:42px;margin-top:0;padding:0;background:#10262e;color:#ff8a80;border:1px solid #1f4047}
button.sec{background:#10262e;color:#3de0b5;border:1px solid #3de0b5;margin-top:12px}
</style></head><body>
<h1>PlaneRadar - Desk Companion</h1><div style="color:#6f8e96;font-size:13px">Configuração e atualização do dispositivo</div>
<div style="margin-top:12px"><a class="lk" href="/debug">Debug monitor &rarr;</a></div>

<form id="cf"><section>
<h2>Localização</h2>
<div class="row"><div><label>Latitude</label><input id="lat" name="lat" type="number" step="any" required></div>
<div><label>Longitude</label><input id="lon" name="lon" type="number" step="any" required></div></div>
<label>Raio de busca (dist) <small>— unidade da sua API</small></label><input id="dist" name="dist" type="number" min="1" required>
<h2>Servidor de voos</h2>
<label>URL base da API</label><input id="urlbase" name="urlbase" type="url" required placeholder="https://seu-servidor">
<div class="row"><div><label>Voos a cada (s)</label><input id="flighpooling" name="flighpooling" type="number" min="5" required></div>
<div><label>Clima a cada (s)</label><input id="wheaterpooling" name="wheaterpooling" type="number" min="60" required></div></div>
<h2>Wi-Fi</h2>
<div class="row"><div><label>Rede (SSID)</label><input id="wfname" name="wfname" required></div>
<div><label>Senha</label><input id="wfpass" name="wfpass" type="password" autocomplete="new-password"></div></div>
<div class="warn">Dados de Wi-Fi errados deixam o aparelho inacessível por esta página; a correção então é feita pelo cartão SD.</div>
<h2>Horário e tela</h2>
<label>Fuso horário (TZ)</label><input id="tz" name="tz" required>
<label>Brilho da tela: <b id="bv" style="color:#3de0b5">255</b> <small>(0 = apagada, 255 = máximo)</small></label><input id="screenbrightness" name="screenbrightness" type="range" min="0" max="255" step="1" value="255" oninput="$('bv').textContent=this.value">
<div class="row"><div><label>Apagar tela às <small>(HH:MM, vazio = nunca)</small></label><input id="sleeptimer" name="sleeptimer" placeholder="23:00" pattern="([01][0-9]|2[0-3]):[0-5][0-9]|"></div>
<div><label>Acender às <small>(HH:MM)</small></label><input id="wakeuptimer" name="wakeuptimer" placeholder="07:00" pattern="([01][0-9]|2[0-3]):[0-5][0-9]|"></div></div>
<h2>Acesso a esta página</h2>
<div class="row"><div><label>Usuário</label><input id="otauser" name="otauser" required></div>
<div><label>Senha</label><input id="otapass" name="otapass" type="password" autocomplete="new-password"></div></div>
<label>Nome na rede <small>(nome.local)</small></label><input id="otahost" name="otahost" required pattern="[A-Za-z0-9\-]{1,32}">
<h2>Parâmetros adicionais</h2>
<div class="hint">Parâmetros que o firmware ainda não conhece ficam apenas gravados no param.txt e só têm efeito se o código os ler. Nomes com "pass", "senha", "key" ou "token" são tratados como senha. Use ✕ para remover.</div>
<div id="ex"></div><input type="hidden" name="xs" value="1">
<button type="button" class="sec" onclick="addRow('','',false,true)">+ Adicionar parâmetro</button>
<small style="display:block;margin-top:10px">Senhas nunca são exibidas. Deixe o campo vazio para mantê-la.</small>
<button type="submit">Salvar e reiniciar</button><div class="msg" id="cm"></div>
</section></form>

<section><h2 style="margin-top:12px">Firmware</h2>
<div style="font-size:13px;color:#9fb3bd;margin-top:8px">Instalado atualmente: <b id="fw" style="color:#e8f3f5">...</b></div>
<p style="font-size:14px">Envie o arquivo <b>.ino.bin</b> gerado pela Arduino IDE (Sketch &rarr; Exportar binário compilado). Não envie os arquivos "bootloader", "partitions" ou "merged".</p>
<input type="file" id="f" accept=".bin"><button onclick="up()">Enviar e atualizar</button>
<progress id="p" value="0" max="100" hidden></progress><div class="msg" id="s"></div></section>

<script>
var $=function(i){return document.getElementById(i)};
var F=['lat','lon','dist','urlbase','flighpooling','wheaterpooling','wfname','wfpass','tz','screenbrightness','sleeptimer','wakeuptimer','otauser','otapass','otahost'];
function addRow(k,v,s,n){var xs=document.querySelectorAll('#ex .xr');if(xs.length>=32){cmsg('Limite de 32 parâmetros adicionais',1);return}
 var d=document.createElement('div');d.className='xr';
 var a=document.createElement('input');a.className='xk';a.placeholder='nome';a.value=k;a.readOnly=!n;a.pattern='[A-Za-z0-9_\\-]{1,32}';
 var b=document.createElement('input');b.className='xv';
 if(s){b.type='password';b.placeholder='•••••••• (mantida)'}else{b.placeholder='valor';b.value=v}
 var x=document.createElement('button');x.type='button';x.textContent='✕';x.title='Remover';x.onclick=function(){d.remove()};
 d.appendChild(a);d.appendChild(b);d.appendChild(x);$('ex').appendChild(d);if(n)a.focus()}
function cmsg(t,e){var m=$('cm');m.textContent=t;m.className='msg'+(e?' err':'')}
fetch('/config.json').then(function(r){return r.json()}).then(function(c){
 $('fw').textContent=c.fw||'desconhecido';
 F.forEach(function(k){var e=$(k);if(!e)return;
  if(e.type=='password'){e.placeholder=c[k+'_set']?'•••••••• (mantida)':'(não definida)'}else{e.value=c[k]==null?'':c[k]}});
 $('bv').textContent=$('screenbrightness').value;
 (c.extras||[]).forEach(function(x){addRow(x.k,x.v||'',!!x.s,false)})
}).catch(function(){cmsg('Não foi possível carregar a configuração',1)});
function formBody(){var p=new URLSearchParams(new FormData($('cf')));
 var ks=document.querySelectorAll('#ex .xk'),vs=document.querySelectorAll('#ex .xv');
 for(var i=0;i<ks.length;i++){var k=ks[i].value.trim();if(!k)continue;p.append('ek'+i,k);p.append('ev'+i,vs[i].value)}
 return p}
$('cf').onsubmit=function(ev){ev.preventDefault();cmsg('Salvando...');
 fetch('/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:formBody()})
 .then(function(r){return r.text().then(function(t){
   if(r.ok){cmsg('Salvo! O dispositivo está reiniciando. Se mudou usuário/senha, entre novamente com os novos.')}else{cmsg('Erro: '+t,1)}})})
 .catch(function(){cmsg('Sem resposta (o aparelho pode já estar reiniciando).')})};
function up(){var f=$('f').files[0];if(!f){return}
 var x=new XMLHttpRequest(),d=new FormData(),p=$('p'),s=$('s');d.append('firmware',f,f.name);p.hidden=false;
 x.upload.onprogress=function(e){if(e.lengthComputable){p.value=e.loaded*100/e.total;s.textContent=Math.round(p.value)+'%'}};
 x.onload=function(){s.textContent=x.status==200?'Concluído! O dispositivo vai reiniciar.':'Erro: '+x.responseText};
 x.onerror=function(){s.textContent='Falha na conexão'};x.open('POST','/update');x.send(d)}
</script></body></html>)HTML";
