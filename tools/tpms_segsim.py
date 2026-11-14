import random
random.seed(1)
V=[0x12,0x34,0x56,0x78,0x7C,0x47,0x04,0xDB]
def ford_ok(b): return len(b)>=8 and (sum(b[:7])&0xFF)==b[7]
def frame_pulses(T,jit):
    H=[1,0]*12 + [0,0,0]   # alt preamble then sync-ish
    for by in V:
        for k in range(7,-1,-1):
            H += [1,0] if (by>>k)&1 else [0,1]
    H += [1,0,1,0]
    P=[];j=0
    while j<len(H):
        r=1
        while j+r<len(H) and H[j+r]==H[j]: r+=1
        P.append(max(1,round(r*T+random.uniform(-jit,jit)))); j+=r
    return P
def noise(n): return [int(random.expovariate(1/60))+1 for _ in range(n)]
def fits(d,T):
    r=d/T;n=round(r);return 1<=n<=8 and abs(r-n)<=0.40
def demod(p,T0):
    one=[d for d in p if round(d/T0)==1]; T=sum(one)/len(one) if len(one)>=8 else T0
    half=[];lvl=1
    for d in p:
        n=min(8,max(1,round(d/T))); half+= [lvl]*n; lvl^=1
    if len(half)<128: return None
    i=0;run=1
    while i+1<len(half) and half[i]==half[i+1] and run<64: i+=1;run+=1
    if run>8: i+=1
    bits=[]
    while i+1<len(half):
        a,b=half[i],half[i+1]
        if (a,b)==(1,0): bits.append(1);i+=2
        elif (a,b)==(0,1): bits.append(0);i+=2
        else: i+=1
    for inv in (0,1):
        for off in range(0,len(bits)-63):
            by=[int(''.join(str(x^inv) for x in bits[off+8*B:off+8*B+8]),2) for B in range((len(bits)-off)//8)][:12]
            if ford_ok(by) and by[:4]==V[:4]: return by
    return None
def run(T,jit,trials=200,min_seg=48):
    hit=fp=0
    for t in range(trials):
        st=noise(400)+frame_pulses(T,jit)+noise(400)
        seg=[];found=False;falsepos=0
        for d in st+[99999]:
            if fits(d,T) and len(seg)<600: seg.append(d)
            else:
                if len(seg)>=min_seg:
                    r=demod(seg,T)
                    if r: found=True
                    else: falsepos+=1  # non-decoding long segment
                seg=[]
        hit+=found; fp+=falsepos
    return hit,fp
for T in (52,120):
  for jit in (0,8,15):
    h,f=run(T,jit*T/52); print(f"T={T} jitter=±{jit*T/52:.0f}us decoded {h}/200, noise-only long segs={f}")
# noise-only: how often does pure noise make a >=48 run?
c=0
for _ in range(200):
    seg=0
    for d in noise(5000):
        if fits(d,52): seg+=1
        else:
            if seg>=48: c+=1
            seg=0
print("pure-noise 52us segments>=48 in 1M pulses:",c)
