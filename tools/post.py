import sys, numpy as np
from PIL import Image
# шумоподавление à-trous по нормали, глубине и альбедо, затем экспозиция и тонмаппинг как в 3D-странице
src, dst = sys.argv[1], sys.argv[2]
ev = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0
with open(src, 'rb') as f:
    W, H = np.frombuffer(f.read(8), dtype=np.int32)
    img = np.frombuffer(f.read(W*H*12), dtype=np.float32).reshape(H, W, 3).copy()
    alb = np.frombuffer(f.read(W*H*12), dtype=np.float32).reshape(H, W, 3).copy()
    nrm = np.frombuffer(f.read(W*H*12), dtype=np.float32).reshape(H, W, 3).copy()
    dep = np.frombuffer(f.read(W*H*4), dtype=np.float32).reshape(H, W).copy()
a = np.maximum(alb, 1e-3)
irr = img / a                      # освещённость без текстуры
def shift(x, dy, dx):
    return np.roll(np.roll(x, dy, 0), dx, 1)
k = [1/16, 1/4, 3/8, 1/4, 1/16]
c = irr.copy()
for it in range(5):
    step = 1 << it
    acc = np.zeros_like(c); wsum = np.zeros((H, W, 1), np.float32)
    lc = c.mean(2)
    for j in range(5):
        for i in range(5):
            dy, dx = (j-2)*step, (i-2)*step
            w = k[i]*k[j]
            cn = shift(c, dy, dx); nn = shift(nrm, dy, dx); dd = shift(dep, dy, dx); aa = shift(alb, dy, dx)
            wn = np.clip((nrm*nn).sum(2), 0, 1)**32
            wd = np.exp(-np.abs(dep-dd)/(0.02*dep+0.01))
            wa = np.exp(-((alb-aa)**2).sum(2)/0.02)
            wl = np.exp(-np.abs(lc-cn.mean(2))/(0.5*lc+0.05))
            ww = (w*wn*wd*wa*(wl if it < 2 else 1))[..., None]
            acc += cn*ww; wsum += ww
    c = acc/np.maximum(wsum, 1e-6)
out = c*a
# экспозиция по медиане яркости
L = (0.2126*out[..., 0]+0.7152*out[..., 1]+0.0722*out[..., 2])
key = np.median(L)
exp = 0.38/max(key, 1e-4) * 2**ev
x = out*exp
x = (x*(2.51*x+.03))/(x*(2.43*x+.59)+.14)
x = np.clip(x, 0, 1)**(1/2.2)
Image.fromarray((x*255+.5).astype(np.uint8)).save(dst, quality=93)
print('key', key, 'exp', exp)
