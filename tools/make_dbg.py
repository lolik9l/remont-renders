# make_dbg.py — превращает опубликованную 3D-страницу (Artifact read → page.html) в отладочную копию для рендера:
# добавляет HTML-скелет, сохранение canvas текстур, нормали/UV в мешах, FOV для прогулки, window.__walk и window.__dump.
# Использование: python3 make_dbg.py page.html dbg.html
import sys
src,dst=sys.argv[1],sys.argv[2]
s=open(src).read()
HEAD='<!doctype html><html><head><meta charset=utf8><meta name=viewport content="width=device-width,initial-scale=1,viewport-fit=cover"><style>:root{color-scheme:light;box-sizing:border-box;padding-top:env(safe-area-inset-top,0px);padding-bottom:env(safe-area-inset-bottom,0px)}html{scroll-padding-top:env(safe-area-inset-top,0px)}body{margin:0;padding:0;font:14px -apple-system,BlinkMacSystemFont,sans-serif;background:#fff;color:#000}img{max-width:100%}[hidden]:not([hidden=until-found i]){display:none!important}</style></head><body>'
BLOCK="window.__walk=(x,z,yaw,pitch,fov)=>{ setMode('walk'); walk.x=x; walk.z=z; walk.yaw=yaw; walk.pitch=pitch; walk.to=null; showTags=false; window.__fov=fov; dirty=true; render(); };\nwindow.__eye=()=>EYEP;\nwindow.__dump=()=>{ const names=new Map(); Object.entries(MT).forEach(([k,v])=>names.set(v,k)); const mats=[], mi=new Map(), texs=[], ti=new Map();\n  const tid=t=>{ if(!t) return -1; if(!ti.has(t)){ ti.set(t,texs.length); texs.push(t.__c.toDataURL('image/png')); } return ti.get(t); };\n  const out=[]; meshes.forEach(m=>{ if(!shown(m)) return; const t=m.mat; if(!mi.has(t)){ mi.set(t,mats.length); mats.push({name:names.get(t)||null,col:t.colDay||t.col,colEve:t.colEve||t.colDay||t.col,alpha:t.alpha,emisDay:t.emisDay===undefined?t.emis:t.emisDay,emisEve:t.emisEve===undefined?t.emis:t.emisEve,tex:tid(t.tex),uv:t.uv,rot:t.rot,gloss:t.gloss,metal:t.metal,cull:t.cull}); }\n    const M_=m.anim?m.anim.m:ID;\n    out.push({mat:mi.get(t),M:Array.from(M_),P:Array.from(m.P,v=>+v.toFixed(5)),N:Array.from(m.N,v=>+v.toFixed(4)),U:Array.from(m.U,v=>+v.toFixed(5))}); });\n  return JSON.stringify({mats,texs,out}); };"
def rep(a,b):
    global s
    assert s.count(a)==1,(a[:60],s.count(a)); s=s.replace(a,b)
if not s.startswith('<!doctype'): s=HEAD+'\n'+s
rep("gl.texParameterf(gl.TEXTURE_2D,aext.TEXTURE_MAX_ANISOTROPY_EXT,8); return t; }","gl.texParameterf(gl.TEXTURE_2D,aext.TEXTURE_MAX_ANISOTROPY_EXT,8); t.__c=c; return t; }")
rep("const r={P:new Float32Array(g.p),","const r={N:new Float32Array(g.n),U:new Float32Array(g.u),P:new Float32Array(g.p),")
rep("Math.cos(walk.yaw)*cp]); FOV=1.2; }","Math.cos(walk.yaw)*cp]); FOV=window.__fov||1.2; }")
A="\nconst h0=(location.hash||'').slice(1); setRoom("
assert s.count(A)==1
s=s.replace(A,'\n'+BLOCK+A)
open(dst,'w').write(s); print('ok',dst)
