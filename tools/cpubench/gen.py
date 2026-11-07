import random
random.seed(7)
N=100000
ctxp=[random.choice([0.03,0.1,0.3,0.5,0.7,0.9,0.97]) for _ in range(64)]
bits=[1 if random.random()<ctxp[i%64] else 0 for i in range(N)]
probs=[1024]*64
low=0; rng=0xFFFFFFFF; cache=0; csize=1; out=bytearray()
def shift_low():
    global low,cache,csize
    if low<0xFF000000 or low>=1<<32:
        carry=low>>32; t=cache
        while True:
            out.append((t+carry)&0xFF); t=0xFF; csize-=1
            if csize==0: break
        cache=(low>>24)&0xFF
    csize+=1
    low=(low&0x00FFFFFF)<<8
for i,b in enumerate(bits):
    c=i%64; p=probs[c]; bound=(rng>>11)*p
    if b==0: rng=bound; probs[c]+=(2048-p)>>5
    else: low+=bound; rng-=bound; probs[c]-=p>>5
    while rng<(1<<24): rng=(rng<<8)&0xFFFFFFFF; shift_low()
for _ in range(5): shift_low()
cs=0
for i,b in enumerate(bits): cs=(cs*31+b+i)&0xFFFFFFFF
with open('rcdata.h','w') as f:
    f.write(f"#define RC_N {N}\n#define RC_CS {cs}u\n#define RC_LEN {len(out)}\nstatic const unsigned char rc_data[]={{"+",".join(map(str,out))+"};\n")
print(len(out),"bytes",N,"decisions", 8*len(out)/N,"bits/decision")
