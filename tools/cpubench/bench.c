#include <stdint.h>
#include "rcdata.h"
#define CON (*(volatile uint32_t*)0x80000000u)
static void ps(const char*s){while(*s)CON=*s++;}
static void pd(uint32_t v){char t[12];int n=0;do{t[n++]='0'+v%10;v/=10;}while(v);while(n)CON=t[--n];}
static inline uint32_t cyc(void){uint32_t c;__asm__ volatile("rdcycle %0":"=r"(c));return c;}
static inline uint32_t ins(void){uint32_t c;__asm__ volatile("rdinstret %0":"=r"(c));return c;}
static void rep(const char*n,uint32_t c,uint32_t i,uint32_t units,const char*u){ps(n);ps(" per-");ps(u);ps(": cycles*100 ");pd(c*100/units);ps(" instr*100 ");pd(i*100/units);ps(" (total cycles ");pd(c);ps(")\n");}

/* A: LZMA-style adaptive binary range decoder */
static uint16_t probs[64];
static const unsigned char *rp;
static uint32_t range_, code_;
static inline int dec(uint16_t *p){
  uint32_t bound=(range_>>11)*(*p); int bit;
  if(code_<bound){range_=bound;*p+=(2048-*p)>>5;bit=0;}
  else{range_-=bound;code_-=bound;*p-=*p>>5;bit=1;}
  if(range_<(1u<<24)){range_<<=8;code_=(code_<<8)|*rp++;}
  return bit;
}
/* B: 8x8 integer IDCT, libjpeg-islow 1D (13-bit constants) */
#define FIX_0_298 2446
#define FIX_0_390 3196
#define FIX_0_541 4433
#define FIX_0_765 6270
#define FIX_0_899 7373
#define FIX_1_175 9633
#define FIX_1_501 12299
#define FIX_1_847 15137
#define FIX_1_961 16069
#define FIX_2_053 16819
#define FIX_2_562 20995
#define FIX_3_072 25172
static void idct1(int32_t*d,int s,int sh){ /* in place, stride s */
  int32_t x0=d[0],x1=d[s],x2=d[2*s],x3=d[3*s],x4=d[4*s],x5=d[5*s],x6=d[6*s],x7=d[7*s];
  int32_t z1=(x2+x6)*FIX_0_541, t2=z1+x6*(-FIX_1_847), t3=z1+x2*FIX_0_765;
  int32_t t0=(x0+x4)<<13, t1=(x0-x4)<<13;
  int32_t t10=t0+t3,t13=t0-t3,t11=t1+t2,t12=t1-t2;
  int32_t a0=x7,a1=x5,a2=x3,a3=x1;
  int32_t z1b=a0+a3,z2=a1+a2,z3=a0+a2,z4=a1+a3,z5=(z3+z4)*FIX_1_175;
  a0*=FIX_0_298;a1*=FIX_2_053;a2*=FIX_3_072;a3*=FIX_1_501;
  z1b*=-FIX_0_899;z2*=-FIX_2_562;z3*=-FIX_1_961;z4*=-FIX_0_390;
  z3+=z5;z4+=z5;a0+=z1b+z3;a1+=z2+z4;a2+=z2+z3;a3+=z1b+z4;
  int32_t r=1<<(sh-1);
  d[0]=(t10+a3+r)>>sh;d[7*s]=(t10-a3+r)>>sh;d[s]=(t11+a2+r)>>sh;d[6*s]=(t11-a2+r)>>sh;
  d[2*s]=(t12+a1+r)>>sh;d[5*s]=(t12-a1+r)>>sh;d[3*s]=(t13+a0+r)>>sh;d[4*s]=(t13-a0+r)>>sh;
}
static int32_t blk[64];
static void idct(void){ for(int c=0;c<8;c++) idct1(blk+c,8,11); for(int r=0;r<8;r++) idct1(blk+r*8,1,18); }
/* C: YCbCr -> RGB888 packed bytes */
static inline int clamp(int v){return v<0?0:v>255?255:v;}
static uint8_t Y[128*8],Cb[128*8],Cr[128*8],out[128*8*3];
int main(void){
  /* A */
  rp=rc_data; code_=0; range_=0xFFFFFFFFu;
  for(int i=0;i<64;i++)probs[i]=1024;
  for(int i=0;i<5;i++)code_=(code_<<8)|*rp++;
  uint32_t cs=0,c0=cyc(),i0=ins();
  for(int i=0;i<RC_N;i++){int b=dec(&probs[i&63]);cs=cs*31u+b+i;}
  uint32_t c1=cyc(),i1=ins();
  ps(cs==RC_CS?"RC checksum OK\n":"RC checksum BAD\n");
  rep("range decode",c1-c0,i1-i0,RC_N,"decision");
  /* B */
  for(int i=0;i<64;i++)blk[i]=(i*37)%61-30;
  c0=cyc();i0=ins();
  for(int n=0;n<64;n++){blk[0]=n*8;idct();}
  c1=cyc();i1=ins();
  rep("idct8x8",c1-c0,i1-i0,64,"block");
  /* C */
  for(int i=0;i<128*8;i++){Y[i]=i*3;Cb[i]=128+(i&31);Cr[i]=128-(i&15);}
  c0=cyc();i0=ins();
  for(int rep_=0;rep_<4;rep_++)
   for(int i=0;i<128*8;i++){
    int y=Y[i],cb=Cb[i]-128,cr=Cr[i]-128;
    out[3*i]=clamp(y+((91881*cr)>>16));
    out[3*i+1]=clamp(y-((22554*cb+46802*cr)>>16));
    out[3*i+2]=clamp(y+((116130*cb)>>16));
   }
  c1=cyc();i1=ins();
  rep("ycbcr->rgb888",c1-c0,i1-i0,4*128*8,"pixel");
  return 0;
}
