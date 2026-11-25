// MPC Plugins Manager by The Disk King. Copyright (c) 2026 The Disk King. All rights reserved.
// The phone page, served by the MPC itself. Self-contained (no outside files). Plugin text comes from other
// people's GitHub pages, so the script only ever inserts it as text, never as HTML.
#pragma once

namespace tdk {

static const char* const kPageHtml = R"TDKPAGE(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>MPC Plugins Manager</title>
<style>
:root{--bg:#f5f5dc;--ink:#4b3621;--orange:#ea861a;--green:#15803d;--red:#dc2626;--card:#fffdf5}
*{box-sizing:border-box}
body{margin:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--ink)}
header{position:sticky;top:0;z-index:5;background:rgba(245,245,220,.96);border-bottom:1px solid #4b362122;padding:12px 16px}
.logo{font-weight:900;font-size:22px;letter-spacing:-.5px;line-height:1}
.logo b{color:var(--orange)} .by{font-size:10px;font-weight:900;letter-spacing:.3em;opacity:.6;text-transform:uppercase}
.status{margin-top:10px;background:#fff;border-radius:12px;padding:10px 12px;font-weight:700;font-size:14px;border:1px solid #4b362122}
.status small{display:block;font-weight:500;opacity:.75;margin-top:2px}
.bar{height:6px;background:#4b362118;border-radius:9px;margin-top:8px;overflow:hidden;display:none}
.bar i{display:block;height:100%;background:var(--orange);width:0}
.restart{display:none;margin-top:8px;background:var(--orange);color:#fff;border-radius:10px;padding:8px 12px;font-weight:800;font-size:13px}
.tabs{display:flex;gap:6px;overflow-x:auto;margin-top:10px;padding-bottom:2px}
.tabs button{flex:none;border:1px solid #4b362133;background:#fff;color:var(--ink);border-radius:10px;padding:9px 12px;font-weight:900;font-size:12px;text-transform:uppercase;letter-spacing:.05em}
.tabs button.on{background:var(--ink);color:#fff}
.tabs button.red{border:2px solid var(--red);color:var(--red)} .tabs button.red.on{background:var(--red);color:#fff}
.row{display:flex;gap:8px;margin-top:8px}
input,select{flex:1;min-width:0;border:1px solid #4b362133;border-radius:10px;padding:10px;font-size:15px;font-weight:700;background:#fff;color:var(--ink)}
main{padding:12px 16px 40px;max-width:900px;margin:0 auto}
.warn{background:var(--red);color:#fff;border-radius:14px;padding:12px 14px;font-weight:700;font-size:13px;margin-bottom:12px;display:none}
.card{background:var(--card);border:1px solid #4b362122;border-radius:16px;padding:14px;margin-bottom:10px}
.strip{border-radius:8px;padding:3px 8px;font-size:10px;font-weight:900;letter-spacing:.08em;text-transform:uppercase;display:inline-block;color:#fff}
.t-ok{background:var(--green)} .t-un{background:#f59e0b;color:var(--ink)} .t-beta{background:var(--red)}
.name{font-size:19px;font-weight:900;margin:8px 0 2px} .meta{font-size:11px;font-weight:800;letter-spacing:.06em;text-transform:uppercase;opacity:.6}
.about{font-size:14px;opacity:.85;margin:8px 0}
.own{font-size:12px;font-weight:800;color:#92400e;margin-bottom:6px}
.acts{display:flex;gap:8px;flex-wrap:wrap;margin-top:8px}
.acts button{border:0;border-radius:10px;padding:10px 14px;font-weight:900;font-size:13px;text-transform:uppercase;letter-spacing:.05em;color:#fff;background:var(--orange)}
.acts button.rm{background:#fff;color:var(--ink);border:1px solid #4b362144} .acts button.up{background:var(--green)}
.acts button:disabled{opacity:.45}
.note{font-size:12px;opacity:.7}
.empty{text-align:center;opacity:.6;font-weight:700;padding:30px 0}
.shot{width:100%;aspect-ratio:2/1;object-fit:cover;border-radius:12px;background:#111;margin-top:8px;display:block}
.fol{border:1px solid var(--orange);background:#fff;color:var(--orange);border-radius:10px;padding:6px 10px;font-weight:900;font-size:12px;text-transform:uppercase;letter-spacing:.04em;margin-left:6px}
.fol.on{background:var(--orange);color:#fff}
.t-new{background:var(--orange)}
.pick{float:right;width:26px;height:26px;accent-color:var(--orange)}
.tools{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:10px}
.tools button{border:1px solid #4b362144;background:#fff;color:var(--ink);border-radius:10px;padding:9px 12px;font-weight:900;font-size:12px;text-transform:uppercase}
.tools button.go{background:var(--green);color:#fff;border:0}
#selbar{display:none;position:fixed;left:12px;right:12px;bottom:12px;z-index:6;background:var(--ink);color:#fff;border-radius:14px;padding:12px 14px;font-weight:900;align-items:center;justify-content:space-between;gap:10px}
#selbar button{border:0;border-radius:10px;background:var(--orange);color:#fff;font-weight:900;padding:10px 14px;text-transform:uppercase}
#pinbox{position:fixed;inset:0;background:var(--bg);display:flex;align-items:center;justify-content:center;z-index:9}
#pinbox div{max-width:340px;padding:24px;text-align:center}
#pinbox input{font-size:34px;text-align:center;letter-spacing:12px;width:100%;margin:16px 0}
#pinbox button{width:100%;border:0;border-radius:12px;background:var(--orange);color:#fff;font-weight:900;font-size:16px;padding:14px}
footer{font-size:11px;opacity:.6;line-height:1.5;margin-top:24px}
a{color:inherit}
</style></head><body>
<div id="pinbox"><div>
 <div class="logo">MPC <b>PLUGINS</b> MANAGER</div><div class="by">by The Disk King</div>
 <p style="font-weight:700">Type the 4-digit PIN shown on your MPC's screen.</p>
 <input id="pin" inputmode="numeric" maxlength="4" autocomplete="off">
 <button id="go">Open</button><p id="pinerr" style="color:var(--red);font-weight:800"></p>
</div></div>
<header>
 <div class="logo">MPC <b>PLUGINS</b> MANAGER</div><div class="by">by The Disk King</div>
 <div class="status"><span id="st">Connecting...</span><small id="sd"></small><div class="bar" id="bar"><i id="bari"></i></div></div>
 <div class="restart" id="rs">Restart your MPC to load plugin changes. On a USB stick or SD card: unplug it and plug it back in.</div>
 <div class="tabs" id="tabs">
  <button data-t="tested" class="on">&#10003; Tested &amp; Working</button>
  <button data-t="installed">Installed</button>
  <button data-t="updates">Updates</button>
  <button data-t="untested" class="red">&#9888; Untested</button>
  <button data-t="removed">Removed</button>
 </div>
 <div class="row"><input id="q" type="search" placeholder="Search plugins, makers, sounds"><select id="kind"><option value="">All types</option><option value="instrument">Instruments</option><option value="effect">Effects</option><option value="addin">Addins</option><option value="followed">&#9733; Followed makers</option></select></div>
 <div class="row"><select id="sort"><option value="dl">Most downloaded</option><option value="new">Newest</option><option value="az">A to Z</option></select><select id="style"><option value="">All sounds</option></select></div>
 <div class="row"><select id="drive"></select></div>
</header>
<main>
 <div class="warn" id="warn">&#9888; Untested or beta: no hardware test is recorded by the maker for this version. It may crash your MPC or damage projects. Save and back up first.</div>
 <div class="tools" id="tools"></div>
 <div id="list"></div>
 <div id="selbar"><span id="selcount"></span><button id="selgo">Install selected</button></div>
 <footer>Plugins are made by independent developers and downloaded from their own GitHub, then checked against their SHA-256 checksum before anything is copied. MPC Plugins Manager only writes inside VST2 Plugins and its own folder. Provided as is: The Disk King is not responsible in any way for any damage or loss. Back up first. Plugin pages and full details: <a href="https://thediskking.com/plugins/">thediskking.com/plugins</a>.</footer>
</main>
<script>
(function(){
var pin='',tab='tested',state=null,plugins=[],gen='',picking=false,picked={};
function follows(p){return !!(state&&state.followed&&state.followed.indexOf(p.maker)>=0);}
// NEW: from a maker you follow, not seen (opened or installed) yet, and not already installed at this version
function fresh(p){if(!follows(p))return false;var iv=installedVersion(p);if(iv&&!newer(iv.version,p.version))return false;var s=state.seen&&state.seen[p.id];return !s||newer(s,p.version);}
var SOUND={'drum-machine':'Drum Machine',sampler:'Sampler',sequencer:'Sequencer',synth:'Synth',reverb:'Reverb',delay:'Delay',tape:'Tape & Lo-Fi',saturation:'Saturation & Distortion',dynamics:'Compressor & Dynamics',eq:'EQ & Filter',modulation:'Chorus & Modulation',utility:'Utility'};
try{pin=localStorage.getItem('mpm_pin')||'';}catch(e){}
function $(id){return document.getElementById(id);}
function el(t,c,txt){var e=document.createElement(t);if(c)e.className=c;if(txt!=null)e.textContent=txt;return e;}
function api(path,post){return fetch(path+(path.indexOf('?')<0?'?':'&')+'pin='+encodeURIComponent(pin),{method:post?'POST':'GET',cache:'no-store'})
 .then(function(r){return r.json().then(function(j){if(!r.ok)throw j;return j;});});}
function showPin(msg){$('pinbox').style.display='flex';$('pinerr').textContent=msg||'';}
$('go').onclick=function(){pin=$('pin').value.trim();poll(true);};
$('pin').onkeyup=function(e){if(e.key==='Enter')$('go').onclick();};
// Tested counts only when it was tested on this kind of MPC (Gen1 or Gen2), like the model pages on thediskking.com.
function testedHere(p){return p.tier==='verified'&&(!p.tested||!state||p.tested.split(',').indexOf(state.gen)>=0);}
function builtBoth(p){return !testedHere(p)&&p.tier==='verified'&&p.both;}
function newer(a,b){a=String(a||'').replace(/^v/i,'').split(/[-+]/)[0].split('.');b=String(b||'').replace(/^v/i,'').split(/[-+]/)[0].split('.');for(var i=0;i<Math.max(a.length,b.length);i++){var x=parseInt(a[i]||'0',10)||0,y=parseInt(b[i]||'0',10)||0;if(x!==y)return y>x;}return false;}
function installedVersion(p){if(!state)return null;var f=p.folders?p.folders.split('|'):[];for(var i=0;i<f.length;i++){if(f[i]&&state.installed.hasOwnProperty(f[i]))return {folder:f[i],version:state.installed[f[i]]};}return null;}
function act(op,arg,confirmText){if(confirmText&&!confirm(confirmText))return;
 api('/api/do?op='+op+'&arg='+encodeURIComponent(arg),true).then(function(j){if(j.answer!=='OK')alert(j.answer);poll(false);}).catch(function(e){alert(e.error||'Could not reach the MPC');});}
function card(p){
 var c=el('div','card');var iv=installedVersion(p);
 if(fresh(p))c.appendChild(el('span','strip t-new','\u2605 NEW from '+p.maker));
 var st=testedHere(p)?['t-ok','✓ Tested & working on '+(state&&state.gen==='gen2'?'Gen2':'Gen1')]:builtBoth(p)?['t-un','Tested on '+String(p.tested).replace(/gen/g,'Gen')+', built for your MPC']:p.tier==='verified'?['t-un','Tested on '+String(p.tested).replace(/gen/g,'Gen')+' only, not on your MPC']:p.tier==='experimental'?['t-beta','⚠ Beta / unfinished']:['t-un','Not yet tested'];
 c.appendChild(el('span','strip '+st[0],st[1]));
 c.appendChild(el('div','name',p.name));
 var mt=el('div','meta','by '+p.maker+' · '+(p.kind||'plugin')+' · v'+p.version+(iv?' · installed v'+(iv.version||'?'):''));
 var fb=el('button','fol'+(follows(p)?' on':''),follows(p)?'\u2605 Following':'\u2606 Follow maker');
 fb.onclick=function(){act(follows(p)?'unfollow':'follow',p.maker,null);};mt.appendChild(fb);c.appendChild(mt);
 if(picking&&p.can&&!iv){var cb=el('input','pick');cb.type='checkbox';cb.checked=!!picked[p.id];cb.onchange=function(){if(cb.checked)picked[p.id]=1;else delete picked[p.id];selbar();};c.insertBefore(cb,c.firstChild);}
 if(p.shot&&/^https:\/\//.test(p.shot)){var im=el('img','shot');im.loading='lazy';im.alt=p.name+' screen';im.src=p.shot;im.onerror=function(){im.remove();};c.appendChild(im);}
 if(p.about)c.appendChild(el('div','about',p.about));
 if(p.own)c.appendChild(el('div','own','Needs your own files: '+p.own.replace(/;/g,', ')));
 var a=el('div','acts');
 var risky=!testedHere(p)?'This plugin has NO recorded hardware test for this version. It may crash your MPC. Install anyway?':null;
 if(iv){
  if(p.can&&newer(iv.version,p.version)){var u=el('button','up','Update to v'+p.version);u.onclick=function(){act('install',p.id,risky);};a.appendChild(u);}
  var r=el('button','rm','Remove');r.onclick=function(){act('remove',iv.folder,'Remove '+p.name+'? It goes to Removed Plugins, so you can put it back.');};a.appendChild(r);
 }else if(p.can){var b=el('button','','Install');b.onclick=function(){act('install',p.id,risky);};a.appendChild(b);}
 else a.appendChild(el('span','note','This release must be installed with the maker\'s own installer. See its page on thediskking.com.'));
 var more=el('a','note','Details');more.onclick=function(){if(fresh(p))act('seen',p.id,null);};more.href='https://thediskking.com/plugins/'+p.id+'/';more.target='_blank';more.rel='noopener';a.appendChild(more);
 c.appendChild(a);return c;}
function render(){
 if(!state)return;var box=$('list');var q=$('q').value.toLowerCase().trim(),k=$('kind').value;var out=[];
 $('warn').style.display=tab==='untested'?'block':'none';
 var sortBy=$('sort').value;
 plugins.sort(function(a,b){var fa=fresh(a),fb=fresh(b);if(fa!==fb)return fa?-1:1;return sortBy==='az'?a.name.localeCompare(b.name):sortBy==='new'?String(b.date||'').localeCompare(String(a.date||'')):(b.dl-a.dl);});
 if(tab==='removed'){
  state.removed.forEach(function(n){if(q&&n.toLowerCase().indexOf(q)<0)return;var c=el('div','card');c.appendChild(el('div','name',n));
   var a=el('div','acts'),b=el('button','up','Restore');b.onclick=function(){act('restore',n,null);};a.appendChild(b);c.appendChild(a);out.push(c);});
 }else{
  plugins.forEach(function(p){
   var iv=installedVersion(p),tested=testedHere(p);
   var both=builtBoth(p),nw=fresh(p); if(tab==='tested'&&!tested&&!both&&!nw)return; if(tab==='untested'&&(tested||both))return; if(tab==='installed'&&!iv)return;
   if(tab==='updates'&&!(iv&&p.can&&newer(iv.version,p.version)))return;
   if(k==='followed'){if(!follows(p))return;}else if(k&&p.kind!==k)return; var so=$('style').value; if(so&&p.style!==so)return;
   if(q&&(p.name+' '+p.maker+' '+(p.about||'')).toLowerCase().indexOf(q)<0)return;
   out.push(card(p));});
 }
 var tools=$('tools');tools.replaceChildren();
 if(tab==='updates'&&out.length){var ua=el('button','go','Update all ('+out.length+')');ua.onclick=function(){if(!confirm('Update all '+out.length+' plugins? Your own files are kept, and old versions go to Removed Plugins.'))return;plugins.forEach(function(p){var iv=installedVersion(p);if(iv&&p.can&&newer(iv.version,p.version))act('install',p.id,null);});};tools.appendChild(ua);}
 if(tab==='tested'||tab==='untested'){var sp=el('button',null,picking?'Done selecting':'Select several');sp.onclick=function(){picking=!picking;if(!picking)picked={};selbar();render();};tools.appendChild(sp);}
 box.replaceChildren.apply(box,out.length?out:[el('div','empty',tab==='updates'?'Everything is up to date.':'Nothing here.')]);
}
function show(s){
 $('st').textContent=s.status+(s.queued?' ('+s.queued+' waiting)':'');$('sd').textContent=s.error||s.detail||'';
 $('bar').style.display=s.progress>=0?'block':'none';$('bari').style.width=Math.max(0,s.progress)+'%';
 $('rs').style.display=s.restart?'block':'none';
 var d=$('drive'),cur=s.drives.map(function(x){return x.root+(x.current?'*':'');}).join();
 if(d.dataset.k!==cur){d.dataset.k=cur;d.replaceChildren();s.drives.forEach(function(x){var o=el('option',null,'Install to: '+x.label+' ('+x.root+')');o.value=x.root;o.selected=x.current;d.appendChild(o);});}
}
var lastGen=null,lastInst='';
function poll(first){
 var wantList=first||lastGen===null;
 api('/api/state'+(wantList?'?list=1':'')).then(function(s){
  $('pinbox').style.display='none';try{localStorage.setItem('mpm_pin',pin);}catch(e){}
  if(s.plugins){plugins=s.plugins;lastGen=s.generated;fillSounds();}
  else if(s.generated!==lastGen){lastGen=null;}
  state=s;show(s);var inst=JSON.stringify(s.installed)+s.removed.length+JSON.stringify(s.followed||[])+JSON.stringify(s.seen||{});
  if(s.plugins||inst!==lastInst){lastInst=inst;render();}
 }).catch(function(e){if(e&&e.error)showPin(e.error);else{$('st').textContent='Lost contact with the MPC. Is MPC Plugins Manager still on a track?';}});
}
function fillSounds(){var s=$('style'),have={};plugins.forEach(function(p){if(p.style&&p.style!=='other')have[p.style]=(have[p.style]||0)+1;});var keep=s.value;var all=el('option',null,'All sounds');all.value='';s.replaceChildren(all);Object.keys(have).sort().forEach(function(k){var o=el('option',null,(SOUND[k]||k)+' ('+have[k]+')');o.value=k;s.appendChild(o);});s.value=keep;}
function selbar(){var n=Object.keys(picked).length;$('selbar').style.display=picking&&n?'flex':'none';$('selcount').textContent=n+' selected';}
$('selgo').onclick=function(){var ids=Object.keys(picked);var risky=ids.some(function(id){var p=plugins.filter(function(x){return x.id===id;})[0];return p&&!testedHere(p);});if(!confirm('Install '+ids.length+' plugins?'+(risky?' Some are UNTESTED and may crash your MPC.':'')))return;ids.forEach(function(id){act('install',id,null);});picked={};picking=false;selbar();render();};
$('sort').onchange=render;$('style').onchange=render;
$('drive').onchange=function(){act('drive',this.value,null);};
$('tabs').onclick=function(e){var b=e.target.closest('button');if(!b)return;tab=b.getAttribute('data-t');
 Array.prototype.forEach.call(this.children,function(x){x.classList.toggle('on',x===b);});render();};
var t;$('q').oninput=function(){clearTimeout(t);t=setTimeout(render,150);};$('kind').onchange=render;
if(pin)poll(true);else showPin('');
setInterval(function(){if($('pinbox').style.display==='none')poll(false);},1500);
})();
</script></body></html>)TDKPAGE";

} // namespace tdk
