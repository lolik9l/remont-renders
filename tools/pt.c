// Простой трассировщик путей для видов квартиры: BVH, диффуз + GGX, окна как источники неба, солнце, светильники.
// Вывод: радиальный буфер, альбедо, нормали, глубина (float32) для шумоподавления.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <pthread.h>

typedef struct { float x,y,z; } v3;
static inline v3 V(float x,float y,float z){ v3 r={x,y,z}; return r; }
static inline v3 add(v3 a,v3 b){ return V(a.x+b.x,a.y+b.y,a.z+b.z); }
static inline v3 sub(v3 a,v3 b){ return V(a.x-b.x,a.y-b.y,a.z-b.z); }
static inline v3 mul(v3 a,float k){ return V(a.x*k,a.y*k,a.z*k); }
static inline v3 mulv(v3 a,v3 b){ return V(a.x*b.x,a.y*b.y,a.z*b.z); }
static inline float dot(v3 a,v3 b){ return a.x*b.x+a.y*b.y+a.z*b.z; }
static inline v3 cross(v3 a,v3 b){ return V(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x); }
static inline v3 norm(v3 a){ float l=sqrtf(dot(a,a)); return l>0?mul(a,1.f/l):a; }
static inline float lum(v3 a){ return .2126f*a.x+.7152f*a.y+.0722f*a.z; }

typedef struct { float col[3], colE[3], alpha, emD, emE, gloss, metal, pad; int tex, kind; } Mat;
typedef struct { int w,h; float *px; } Tex;
enum { K_NONE=0,K_PANE=1,K_LAMP=2,K_STRIP=3,K_CEIL=4,K_GLASS=5,K_TULLE=6,K_LED=7,K_MIRROR=8 };

static int NT,NM,NX; static float *P,*N,*U; static int *MI; static Mat *mats; static Tex *texs;
static int EVE=0; static v3 SUN; static v3 SUNC, SKY;
static float LAMPK=1, STRIPK=1;

/* ---------- BVH ---------- */
typedef struct { float mn[3],mx[3]; int l,r,first,cnt; } Node;
static Node *nodes; static int nn=0; static int *idx; static float *cen;
static void bounds(int t,float*mn,float*mx){ for(int k=0;k<3;k++){ mn[k]=1e30f; mx[k]=-1e30f; } for(int v=0;v<3;v++) for(int k=0;k<3;k++){ float q=P[t*9+v*3+k]; if(q<mn[k]) mn[k]=q; if(q>mx[k]) mx[k]=q; } }
static int build(int first,int cnt){
  int id=nn++; Node*n=&nodes[id]; for(int k=0;k<3;k++){ n->mn[k]=1e30f; n->mx[k]=-1e30f; }
  float cmn[3]={1e30f,1e30f,1e30f}, cmx[3]={-1e30f,-1e30f,-1e30f};
  for(int i=first;i<first+cnt;i++){ float a[3],b[3]; bounds(idx[i],a,b); for(int k=0;k<3;k++){ if(a[k]<n->mn[k]) n->mn[k]=a[k]; if(b[k]>n->mx[k]) n->mx[k]=b[k]; float c=cen[idx[i]*3+k]; if(c<cmn[k]) cmn[k]=c; if(c>cmx[k]) cmx[k]=c; } }
  if(cnt<=4){ n->first=first; n->cnt=cnt; n->l=n->r=-1; return id; }
  // бинированный SAH
  int bestAx=-1, bestB=0; float bestC=1e30f; const int NB=12;
  for(int ax=0;ax<3;ax++){ float ext=cmx[ax]-cmn[ax]; if(ext<1e-6f) continue;
    int bc[NB]; float bmn[NB][3],bmx[NB][3]; for(int b=0;b<NB;b++){ bc[b]=0; for(int k=0;k<3;k++){ bmn[b][k]=1e30f; bmx[b][k]=-1e30f; } }
    for(int i=first;i<first+cnt;i++){ int t=idx[i]; int b=(int)((cen[t*3+ax]-cmn[ax])/ext*NB); if(b>=NB) b=NB-1; bc[b]++; float a[3],c[3]; bounds(t,a,c); for(int k=0;k<3;k++){ if(a[k]<bmn[b][k]) bmn[b][k]=a[k]; if(c[k]>bmx[b][k]) bmx[b][k]=c[k]; } }
    for(int s=1;s<NB;s++){ int cl=0,cr=0; float lmn[3]={1e30f,1e30f,1e30f},lmx[3]={-1e30f,-1e30f,-1e30f},rmn[3]={1e30f,1e30f,1e30f},rmx[3]={-1e30f,-1e30f,-1e30f};
      for(int b=0;b<s;b++){ cl+=bc[b]; for(int k=0;k<3;k++){ if(bmn[b][k]<lmn[k]) lmn[k]=bmn[b][k]; if(bmx[b][k]>lmx[k]) lmx[k]=bmx[b][k]; } }
      for(int b=s;b<NB;b++){ cr+=bc[b]; for(int k=0;k<3;k++){ if(bmn[b][k]<rmn[k]) rmn[k]=bmn[b][k]; if(bmx[b][k]>rmx[k]) rmx[k]=bmx[b][k]; } }
      if(!cl||!cr) continue;
      float dl[3],dr[3]; for(int k=0;k<3;k++){ dl[k]=lmx[k]-lmn[k]; dr[k]=rmx[k]-rmn[k]; }
      float c=cl*(dl[0]*dl[1]+dl[1]*dl[2]+dl[2]*dl[0])+cr*(dr[0]*dr[1]+dr[1]*dr[2]+dr[2]*dr[0]);
      if(c<bestC){ bestC=c; bestAx=ax; bestB=s; } } }
  int mid;
  if(bestAx<0){ mid=first+cnt/2; }
  else { float ext=cmx[bestAx]-cmn[bestAx]; int i=first,j=first+cnt-1;
    while(i<=j){ int t=idx[i]; int b=(int)((cen[t*3+bestAx]-cmn[bestAx])/ext*NB); if(b>=NB) b=NB-1; if(b<bestB) i++; else { int q=idx[i]; idx[i]=idx[j]; idx[j]=q; j--; } }
    mid=i; if(mid==first||mid==first+cnt) mid=first+cnt/2; }
  int l=build(first,mid-first); int r=build(mid,first+cnt-mid); n=&nodes[id]; n->l=l; n->r=r; n->cnt=0; return id; }

typedef struct { float t,u,v; int tri; } Hit;
static inline int boxhit(const Node*n,v3 o,v3 inv,float tmax){ float t0=0,t1=tmax;
  float a=(n->mn[0]-o.x)*inv.x, b=(n->mx[0]-o.x)*inv.x; if(a>b){float q=a;a=b;b=q;} if(a>t0)t0=a; if(b<t1)t1=b;
  a=(n->mn[1]-o.y)*inv.y; b=(n->mx[1]-o.y)*inv.y; if(a>b){float q=a;a=b;b=q;} if(a>t0)t0=a; if(b<t1)t1=b;
  a=(n->mn[2]-o.z)*inv.z; b=(n->mx[2]-o.z)*inv.z; if(a>b){float q=a;a=b;b=q;} if(a>t0)t0=a; if(b<t1)t1=b; return t0<=t1; }
// skipmask: биты видов материалов, сквозь которые луч проходит (для теневых лучей)
static int intersect(v3 o,v3 d,float tmax,Hit*h,int skipmask){
  v3 inv=V(1.f/d.x,1.f/d.y,1.f/d.z); int st[64],sp=0; st[sp++]=0; int found=0; h->t=tmax;
  while(sp){ const Node*n=&nodes[st[--sp]]; if(!boxhit(n,o,inv,h->t)) continue;
    if(n->cnt){ for(int i=n->first;i<n->first+n->cnt;i++){ int t=idx[i]; if(skipmask&(1<<mats[MI[t]].kind)) continue;
        v3 a=V(P[t*9],P[t*9+1],P[t*9+2]), e1=V(P[t*9+3]-a.x,P[t*9+4]-a.y,P[t*9+5]-a.z), e2=V(P[t*9+6]-a.x,P[t*9+7]-a.y,P[t*9+8]-a.z);
        v3 pv=cross(d,e2); float det=dot(e1,pv); if(fabsf(det)<1e-12f) continue; float iv=1.f/det; v3 tv=sub(o,a);
        float u=dot(tv,pv)*iv; if(u<0||u>1) continue; v3 q=cross(tv,e1); float v=dot(d,q)*iv; if(v<0||u+v>1) continue;
        float tt=dot(e2,q)*iv; if(tt>1e-4f&&tt<h->t){ h->t=tt; h->u=u; h->v=v; h->tri=t; found=1; } } }
    else { st[sp++]=n->l; st[sp++]=n->r; } }
  return found; }

/* ---------- RNG ---------- */
static inline float rnd(uint64_t*s){ *s^=*s<<13; *s^=*s>>7; *s^=*s<<17; return (float)((*s>>40)&0xFFFFFF)/16777216.f; }

/* ---------- текстуры ---------- */
static v3 texel(int ti,float u,float v){ Tex*t=&texs[ti]; u=u-floorf(u); v=v-floorf(v);
  float x=u*t->w-.5f, y;
  y=v*t->h-.5f;
  int x0=(int)floorf(x), y0=(int)floorf(y); float fx=x-x0, fy=y-y0; v3 r=V(0,0,0);
  for(int j=0;j<2;j++) for(int i=0;i<2;i++){ int xx=((x0+i)%t->w+t->w)%t->w, yy=((y0+j)%t->h+t->h)%t->h; float w=(i?fx:1-fx)*(j?fy:1-fy); float*p=&t->px[(yy*t->w+xx)*3]; r=add(r,V(p[0]*w,p[1]*w,p[2]*w)); }
  return r; }

/* ---------- свет ---------- */
typedef struct { int tri; float area, pw; v3 L; } Light;
static Light *lights; static int NL=0; static float *lcdf; static float ltot=0;
static v3 emission(const Mat*m){ // излучение материала в текущем режиме
  if(m->kind==K_PANE) return EVE?V(.004f,.006f,.012f):SKY;
  if(m->kind==K_CEIL) return V(0,0,0);
  float e=EVE?m->emE:m->emD; if(e<=0) return V(0,0,0);
  v3 c=V(EVE?m->colE[0]:m->col[0],EVE?m->colE[1]:m->col[1],EVE?m->colE[2]:m->col[2]);
  if(m->kind==K_LAMP){ if(!EVE) return V(0,0,0); return mul(c,e*LAMPK); }
  if(m->kind==K_STRIP){ if(!EVE) return V(0,0,0); return mul(c,e*STRIPK); }
  if(m->kind==K_LED) return mul(c,e*.5f);
  return V(0,0,0); }
static v3 tnorm(int t){ v3 a=V(P[t*9],P[t*9+1],P[t*9+2]); v3 e1=V(P[t*9+3]-a.x,P[t*9+4]-a.y,P[t*9+5]-a.z), e2=V(P[t*9+6]-a.x,P[t*9+7]-a.y,P[t*9+8]-a.z); return cross(e1,e2); }
static v3 snorm(int t){ // направление «наружу» по вершинным нормалям
  return norm(V(N[t*9]+N[t*9+3]+N[t*9+6],N[t*9+1]+N[t*9+4]+N[t*9+7],N[t*9+2]+N[t*9+5]+N[t*9+8])); }

/* ---------- камера и рендер ---------- */
static int W=1200,Hh=800,SPP=120; static v3 CO, CF, CR, CU; static float TH;
static float *img,*alb,*nrm,*dep; static int nextRow=0; static pthread_mutex_t mx=PTHREAD_MUTEX_INITIALIZER;
static const int SKIP_SHADOW=(1<<K_PANE)|(1<<K_GLASS)|(1<<K_TULLE);

static v3 sampleGGX(v3 n,float a,uint64_t*s){ float u1=rnd(s),u2=rnd(s); float th=atanf(a*sqrtf(u1/(1-u1+1e-7f))), ph=6.2831853f*u2;
  v3 t=fabsf(n.x)>.5f?V(0,1,0):V(1,0,0); v3 b=norm(cross(n,t)); t=cross(b,n);
  return norm(add(add(mul(t,sinf(th)*cosf(ph)),mul(b,sinf(th)*sinf(ph))),mul(n,cosf(th)))); }
static v3 cosdir(v3 n,uint64_t*s){ float u1=rnd(s),u2=rnd(s); float r=sqrtf(u1), ph=6.2831853f*u2;
  v3 t=fabsf(n.x)>.5f?V(0,1,0):V(1,0,0); v3 b=norm(cross(n,t)); t=cross(b,n);
  return norm(add(add(mul(t,r*cosf(ph)),mul(b,r*sinf(ph))),mul(n,sqrtf(1-u1)))); }

// видимость с пропуском стёкол; тюль ослабляет
static float shadowT(v3 o,v3 d,float tmax){ Hit h; float tr=1; v3 oo=o; float left=tmax;
  for(int k=0;k<6;k++){ if(!intersect(oo,d,left,&h,(1<<K_PANE)|(1<<K_GLASS))) return tr; const Mat*m=&mats[MI[h.tri]];
    if(m->kind==K_TULLE){ tr*=.75f; oo=add(oo,mul(d,h.t+1e-4f)); left-=h.t+1e-4f; continue; } return 0; }
  return tr; }

static v3 radiance(v3 o,v3 d,uint64_t*s,v3*a0,v3*n0,float*d0){
  v3 L=V(0,0,0), T=V(1,1,1); int specPrev=1;
  for(int depth=0;depth<7;depth++){
    Hit h; if(!intersect(o,d,1e30f,&h,0)){ if(depth==0){ *a0=V(1,1,1); *n0=V(0,0,0); *d0=1e3f; } L=add(L,mulv(T,mul(SKY,.3f))); break; }
    int t=h.tri; const Mat*m=&mats[MI[t]]; v3 p=add(o,mul(d,h.t));
    v3 ng=norm(tnorm(t)); float w0=1-h.u-h.v;
    v3 ns=norm(V(N[t*9]*w0+N[t*9+3]*h.u+N[t*9+6]*h.v,N[t*9+1]*w0+N[t*9+4]*h.u+N[t*9+7]*h.v,N[t*9+2]*w0+N[t*9+5]*h.u+N[t*9+8]*h.v));
    if(dot(ns,ng)<0) ng=mul(ng,-1); // ориентировать геометрическую нормаль как вершинную
    int front=dot(ng,d)<0;
    if(!front){ ng=mul(ng,-1); ns=mul(ns,-1); }
    if(dot(ns,d)>0) ns=ng;
    // прозрачные: стекло, тюль
    if(m->kind==K_GLASS||m->kind==K_TULLE||(m->alpha<.99f&&m->kind!=K_PANE)){ float pass=m->kind==K_TULLE?.75f:(1-m->alpha*.5f);
      if(rnd(s)<pass){ o=add(p,mul(d,2e-4f)); continue; }
      if(m->kind==K_GLASS){ d=norm(sub(d,mul(ng,2*dot(d,ng)))); o=add(p,mul(ng,2e-4f)); specPrev=1; continue; } }
    v3 em=emission(m);
    if(em.x+em.y+em.z>0){ // источник: окно с небом, светильник
      int facing=dot(snorm(t),d)<0||m->kind==K_PANE;
      if(depth==0){ *a0=em; *n0=ns; *d0=h.t; }
      if(facing&&(specPrev||depth==0)) L=add(L,mulv(T,em));
      if(m->kind==K_PANE||m->kind==K_LAMP||m->kind==K_STRIP) break;
    }
    v3 albedo=V(m->col[0],m->col[1],m->col[2]); if(EVE) albedo=V(m->colE[0],m->colE[1],m->colE[2]);
    if(m->tex>=0){ float u=U[t*6]*w0+U[t*6+2]*h.u+U[t*6+4]*h.v, v=U[t*6+1]*w0+U[t*6+3]*h.u+U[t*6+5]*h.v; albedo=mulv(albedo,texel(m->tex,u,v)); }
    if(depth==0){ *a0=albedo; *n0=ns; *d0=h.t; }
    float gl=m->gloss, mt=m->metal; float cosi=fmaxf(0.f,-dot(d,ns));
    float F=.04f+.96f*powf(1-cosi,5); float ps= mt>.3f? fminf(.95f,mt) : (gl>0? fminf(.5f,fmaxf(.02f,F*gl*1.5f)) : 0);
    if(m->kind==K_MIRROR) ps=.97f;
    v3 po=add(p,mul(ng,3e-4f));
    if(rnd(s)<ps){ // зеркальная составляющая GGX
      float rough=m->kind==K_MIRROR?.01f:(.03f+(1-gl)*.35f);
      v3 hv=sampleGGX(ns,rough,s); v3 nd=sub(d,mul(hv,2*dot(d,hv))); if(dot(nd,ng)<=0) break;
      if(mt>.3f) T=mulv(T,albedo); d=norm(nd); o=po; specPrev=1; continue; }
    // диффуз: NEE солнце и источники
    if(mt>.3f) albedo=mul(albedo,(1-mt)/(1-ps+1e-4f)+.0f);
    v3 brdf=mul(albedo,1/3.14159265f);
    if(!EVE){ float c=dot(ns,SUN); if(c>0&&dot(ng,SUN)>0){ v3 sd=norm(add(SUN,V((rnd(s)-.5f)*.01f,(rnd(s)-.5f)*.01f,(rnd(s)-.5f)*.01f))); float vis=shadowT(po,sd,1e30f); if(vis>0) L=add(L,mulv(T,mul(mulv(brdf,SUNC),c*vis))); } }
    if(NL){ float r=rnd(s)*ltot; int lo=0,hi=NL-1; while(lo<hi){ int md=(lo+hi)/2; if(lcdf[md]<r) lo=md+1; else hi=md; }
      Light*l=&lights[lo]; int lt=l->tri; float a=rnd(s),b=rnd(s); if(a+b>1){a=1-a;b=1-b;}
      v3 q=V(P[lt*9]+(P[lt*9+3]-P[lt*9])*a+(P[lt*9+6]-P[lt*9])*b, P[lt*9+1]+(P[lt*9+4]-P[lt*9+1])*a+(P[lt*9+7]-P[lt*9+1])*b, P[lt*9+2]+(P[lt*9+5]-P[lt*9+2])*a+(P[lt*9+8]-P[lt*9+2])*b);
      v3 ln=norm(tnorm(lt)); v3 sn=snorm(lt); if(dot(ln,sn)<0) ln=mul(ln,-1);
      v3 dl=sub(q,po); float dist2=dot(dl,dl), dist=sqrtf(dist2); dl=mul(dl,1/dist);
      float c1=dot(ns,dl), c2=-dot(ln,dl);
      if(mats[MI[lt]].kind==K_PANE) c2=fabsf(c2)*(dot(ln,dl)<0?1:0); // окно светит внутрь
      if(c1>0&&c2>0&&dot(ng,dl)>0){ Hit hh; int blocked=intersect(po,dl,dist-2e-3f,&hh,(1<<K_PANE)|(1<<K_GLASS)|(1<<K_TULLE)); if(!blocked){ float pdf=l->pw/ltot/l->area; L=add(L,mulv(T,mul(mulv(brdf,l->L),c1*c2/dist2/pdf))); } } }
    T=mulv(T,albedo); d=cosdir(ns,s); o=po; specPrev=0;
    if(depth>=2){ float q=fmaxf(T.x,fmaxf(T.y,T.z)); if(q<.9f){ if(rnd(s)>q) break; T=mul(T,1/q); } }
  }
  return L; }

static void* worker(void*arg){ (void)arg;
  for(;;){ pthread_mutex_lock(&mx); int y=nextRow++; pthread_mutex_unlock(&mx); if(y>=Hh) break;
    for(int x=0;x<W;x++){ uint64_t s=(uint64_t)(y*W+x)*2654435761ull+88172645463325252ull; v3 acc=V(0,0,0),aa=V(0,0,0),na=V(0,0,0); float da=0;
      for(int k=0;k<SPP;k++){ float px=(x+rnd(&s))/W*2-1, py=1-(y+rnd(&s))/Hh*2; float asp=(float)W/Hh;
        v3 dir=norm(add(CF,add(mul(CR,px*TH*asp),mul(CU,py*TH)))); v3 a,n; float dd;
        v3 c=radiance(CO,dir,&s,&a,&n,&dd); if(!isfinite(c.x)||!isfinite(c.y)||!isfinite(c.z)) c=V(0,0,0);
        float lc=lum(c); if(lc>30) c=mul(c,30/lc); acc=add(acc,c); aa=add(aa,a); na=add(na,n); da+=dd; }
      int i=y*W+x; img[i*3]=acc.x/SPP; img[i*3+1]=acc.y/SPP; img[i*3+2]=acc.z/SPP; alb[i*3]=aa.x/SPP; alb[i*3+1]=aa.y/SPP; alb[i*3+2]=aa.z/SPP; nrm[i*3]=na.x/SPP; nrm[i*3+1]=na.y/SPP; nrm[i*3+2]=na.z/SPP; dep[i]=da/SPP; }
    if(y%80==0){ fprintf(stderr,"row %d\n",y); } }
  return NULL; }

int main(int argc,char**argv){
  if(argc<10){ fprintf(stderr,"pt scene.bin out x y z yaw pitch fov eve [spp w h]\n"); return 1; }
  FILE*f=fopen(argv[1],"rb"); if(!f) return 1; if(fread(&NT,4,1,f)!=1||fread(&NM,4,1,f)!=1||fread(&NX,4,1,f)!=1) return 1;
  P=malloc(NT*9*4); N=malloc(NT*9*4); U=malloc(NT*6*4); MI=malloc(NT*4);
  if(fread(P,4,NT*9,f)!=(size_t)NT*9||fread(N,4,NT*9,f)!=(size_t)NT*9||fread(U,4,NT*6,f)!=(size_t)NT*6||fread(MI,4,NT,f)!=(size_t)NT) return 1;
  mats=malloc(NM*sizeof(Mat)); if(fread(mats,sizeof(Mat),NM,f)!=(size_t)NM) return 1;
  texs=malloc(NX*sizeof(Tex)); for(int i=0;i<NX;i++){ int wh[2]; if(fread(wh,4,2,f)!=2) return 1; texs[i].w=wh[0]; texs[i].h=wh[1]; int n=wh[0]*wh[1]*3; unsigned char*b=malloc(n); if(fread(b,1,n,f)!=(size_t)n) return 1; texs[i].px=malloc(n*4); for(int k=0;k<n;k++) texs[i].px[k]=powf(b[k]/255.f,2.2f); free(b); }
  fclose(f);
  CO=V(atof(argv[3]),atof(argv[4]),atof(argv[5])); float yaw=atof(argv[6]), pitch=atof(argv[7]); TH=tanf(atof(argv[8])/2); EVE=atoi(argv[9]);
  if(argc>10) SPP=atoi(argv[10]); if(argc>12){ W=atoi(argv[11]); Hh=atoi(argv[12]); }
  if(getenv("LAMPK")) LAMPK=atof(getenv("LAMPK")); if(getenv("STRIPK")) STRIPK=atof(getenv("STRIPK"));
  float sk=getenv("SKYK")?atof(getenv("SKYK")):1.6f, sn=getenv("SUNK")?atof(getenv("SUNK")):4.5f;
  CF=V(sinf(yaw)*cosf(pitch),sinf(pitch),cosf(yaw)*cosf(pitch)); CR=norm(cross(CF,V(0,1,0))); CU=cross(CR,CF);
  SUN=norm(V(.5f,.78f,.38f)); SUNC=V(1.0f*sn,.93f*sn,.80f*sn); SKY=V(.80f*sk,.90f*sk,1.05f*sk);
  // BVH
  idx=malloc(NT*4); cen=malloc(NT*12); for(int t=0;t<NT;t++){ idx[t]=t; for(int k=0;k<3;k++) cen[t*3+k]=(P[t*9+k]+P[t*9+3+k]+P[t*9+6+k])/3; }
  nodes=malloc(sizeof(Node)*NT*2); build(0,NT); fprintf(stderr,"bvh %d nodes, %d tris\n",nn,NT);
  // источники
  lights=malloc(sizeof(Light)*NT); lcdf=malloc(4*NT);
  for(int t=0;t<NT;t++){ const Mat*m=&mats[MI[t]]; v3 e=emission(m); float le=lum(e); if(le<=0) continue; if(m->kind==K_LED) continue; float A=sqrtf(dot(tnorm(t),tnorm(t)))*.5f; if(A<1e-7f) continue;
    lights[NL].tri=t; lights[NL].area=A; lights[NL].L=e; lights[NL].pw=le*A; ltot+=lights[NL].pw; lcdf[NL]=ltot; NL++; }
  fprintf(stderr,"lights %d\n",NL);
  img=calloc(W*Hh*3,4); alb=calloc(W*Hh*3,4); nrm=calloc(W*Hh*3,4); dep=calloc(W*Hh,4);
  int nth=getenv("THREADS")?atoi(getenv("THREADS")):2; pthread_t th[16]; for(int i=0;i<nth;i++) pthread_create(&th[i],0,worker,0); for(int i=0;i<nth;i++) pthread_join(th[i],0);
  char fn[512]; sprintf(fn,"%s.bin",argv[2]); FILE*o=fopen(fn,"wb"); int wh[2]={W,Hh}; fwrite(wh,4,2,o); fwrite(img,4,W*Hh*3,o); fwrite(alb,4,W*Hh*3,o); fwrite(nrm,4,W*Hh*3,o); fwrite(dep,4,W*Hh,o); fclose(o);
  return 0; }
