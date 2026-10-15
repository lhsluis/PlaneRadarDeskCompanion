#pragma once
// Página "Debug monitor". Fica em .h porque a Arduino IDE confunde JavaScript dentro de strings do .ino
// com funções C++ ao gerar protótipos.
#include <Arduino.h>

static const char DEBUG_PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="pt"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Debug Monitor - PlaneRadar</title>
<style>
body{font-family:system-ui,sans-serif;background:#0b1a20;color:#e8f3f5;max-width:900px;margin:28px auto;padding:0 16px 40px}
h1{font-size:20px;color:#3de0b5;margin:0 0 4px}
.lk{display:inline-block;padding:8px 14px;border:1px solid #3de0b5;border-radius:8px;color:#3de0b5;text-decoration:none;font-size:13px}
.bar{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:14px 0}
.bar button{padding:9px 14px;border-radius:8px;border:1px solid #1f4047;background:#10262e;color:#e8f3f5;font-size:13px;cursor:pointer}
.bar button:hover{border-color:#3de0b5}.bar label{font-size:13px;color:#9fb3bd}#st{font-size:13px;color:#6f8e96;margin-left:auto}
textarea{width:100%;height:62vh;min-height:320px;box-sizing:border-box;background:#071217;color:#b8f5e3;border:1px solid #1a3c42;border-radius:10px;padding:12px;font:12px/1.5 ui-monospace,Consolas,monospace;resize:vertical;white-space:pre;overflow:auto}
</style></head><body>
<h1>PlaneRadar - Desk Companion</h1>
<div style="color:#6f8e96;font-size:13px;margin-bottom:12px">Debug monitor &mdash; saída do Serial em tempo real</div>
<a class="lk" href="/">&larr; Configuração e firmware</a>
<div class="bar">
<button id="bp">Pausar</button><button id="bc">Limpar tela</button><button id="bd">Baixar .txt</button>
<label><input type="checkbox" id="as" checked> Rolagem automática</label><span id="st">Conectando...</span>
</div>
<textarea id="lg" readonly spellcheck="false"></textarea>
<div style="font-size:12px;color:#6f8e96;margin-top:8px">Mostra o que o sketch imprime via Serial. O histórico guardado no aparelho é limitado e é apagado quando ele reinicia; a tela do navegador mantém o que já recebeu.</div>
<script>
var $=function(i){return document.getElementById(i)};
var next=0,paused=false,MAX=400000;
function add(t){var e=$('lg');e.value+=t;if(e.value.length>MAX)e.value=e.value.slice(e.value.length-MAX);if($('as').checked)e.scrollTop=e.scrollHeight}
function stat(t,e){var s=$('st');s.textContent=t;s.style.color=e?'#ff8a80':'#6f8e96'}
function poll(){
 if(paused){setTimeout(poll,500);return}
 fetch('/debug/log?since='+next,{cache:'no-store'}).then(function(r){
  if(!r.ok)throw new Error(r.status);
  var n=parseInt(r.headers.get('X-Next')),rs=r.headers.get('X-Reset')=='1';
  return r.text().then(function(t){
   if(rs)add('\n--- o aparelho reiniciou ---\n');
   if(t)add(t);
   if(!isNaN(n))next=n;
   stat('Conectado');
  })
 }).catch(function(){stat('Sem conexão (reiniciando?)',1)}).then(function(){setTimeout(poll,1000)})
}
$('bp').onclick=function(){paused=!paused;this.textContent=paused?'Continuar':'Pausar';stat(paused?'Pausado':'Conectado')};
$('bc').onclick=function(){$('lg').value=''};
$('bd').onclick=function(){var b=new Blob([$('lg').value],{type:'text/plain'}),a=document.createElement('a');a.href=URL.createObjectURL(b);a.download='planeradar-debug.txt';a.click()};
poll();
</script></body></html>)HTML";
