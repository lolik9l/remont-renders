import json, struct, sys, base64, io
import numpy as np
from PIL import Image
src, dst = sys.argv[1], sys.argv[2]
d = json.load(open(src))
KIND = {'pane':1,'lamp':2,'strip':3,'ceil':4,'glass':5,'tulle':6,'led':7,'mirror':8}
tris=[]; 
P_all=[];N_all=[];U_all=[];M_all=[]
for o in d['out']:
    M=np.array(o['M'],dtype=np.float64).reshape(4,4).T  # column-major -> row-major
    P=np.array(o['P'],dtype=np.float64).reshape(-1,3); N=np.array(o['N'],dtype=np.float64).reshape(-1,3); U=np.array(o['U'],dtype=np.float64).reshape(-1,2)
    if len(P)==0: continue
    Pw=(M[:3,:3]@P.T).T+M[:3,3]; Nw=(M[:3,:3]@N.T).T
    m=d['mats'][o['mat']]
    if m['rot']>.5: U=U[:,::-1]
    U=U*np.array(m['uv'])
    n=len(P)//3*3
    P_all.append(Pw[:n]); N_all.append(Nw[:n]); U_all.append(U[:n]); M_all.append(np.full(n//3,o['mat'],dtype=np.int32))
P=np.concatenate(P_all).astype(np.float32); N=np.concatenate(N_all).astype(np.float32); U=np.concatenate(U_all).astype(np.float32); MI=np.concatenate(M_all)
ntri=len(MI)
texs=[]
for t in d['texs']:
    im=Image.open(io.BytesIO(base64.b64decode(t.split(',',1)[1]))).convert('RGB'); texs.append(im)
with open(dst,'wb') as f:
    f.write(struct.pack('iii',ntri,len(d['mats']),len(texs)))
    f.write(P.tobytes()); f.write(N.tobytes()); f.write(U.tobytes()); f.write(MI.astype(np.int32).tobytes())
    for m in d['mats']:
        k=KIND.get(m['name'] or '',0)
        if (m['name'] or '')=='' and m['alpha']<1: k=5
        f.write(struct.pack('3f3f6fii',*m['col'],*m['colEve'],m['alpha'],m['emisDay'],m['emisEve'],m['gloss'],m['metal'],0.0,m['tex'],k))
    for im in texs:
        a=np.asarray(im,dtype=np.uint8); f.write(struct.pack('ii',im.width,im.height)); f.write(a.tobytes())
print(ntri,'tris',len(d['mats']),'mats',len(texs),'texs')
