#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/ts_mosaic.c"
#include "reference/mosaic_envelopes_208.inc"
#include <assert.h>
#include <time.h>

static float data[131072];
static void same(const void *a,const void *b,size_t n)
{assert(!memcmp(a,b,n));}
static void fades(void)
{
    TsMosaicEvent e={.duration=10};TsMosaicVoice v={0};
    const double fades[]={0,.000001,.01,1,5,7,10};
    for(unsigned i=0;i<7;++i)for(unsigned j=0;j<7;++j) {
        e.fade_in=fades[i];e.fade_out=fades[j];ts_mosaic_event_fades(&e,&v.fade_in,&v.fade_out);
        const double edges[]={0,v.fade_in,e.duration-v.fade_out,e.duration};
        for(unsigned k=0;k<4;++k)for(int d=-1;d<=1;++d) {
            double t=d<0?nextafter(edges[k],-INFINITY):d>0?nextafter(edges[k],INFINITY):edges[k];
            float a=event_fade(&e,&v,t),b=reference_event_fade(&e,&v,t);same(&a,&b,sizeof(a));
        }
        for(int k=0;k<=1000;++k) {
            float a=event_fade(&e,&v,k*.01),b=reference_event_fade(&e,&v,k*.01);same(&a,&b,sizeof(a));
        }
    }
}
static void render(unsigned rate,int mode,int notes,int channels)
{
    TsMosaicSource source={.sample={.data=data,.frames=65536,.channels=channels,.sample_rate=48000}};
    TsMosaicEvent e={.id=1,.source=&source,.first=16,.last=4096,.crossfade=16,
        .duration=.3,.gain=.7f,.pan=-.2f,.note_count=notes,.looping=mode>=0,
        .mode=mode<0?TS_LOOP_FORWARD:(TsLoopMode)mode,.fade_in=.015,.fade_out=.04};
    e.output_route.matrix_enabled=1;e.output_route.matrix_gain[7]=750;e.output_route.matrix_gain[185]=250;
    e.tuning.root_note=60;
    for(int i=0;i<notes;++i)e.notes[i]=60+i*7;
    TsMosaicVoice a={0},b;voice_begin(&a,&e,0,(int)rate);b=a;
    double time=0;
    for(unsigned i=0;i<rate/3;++i) {
        double speed=i<rate/6?.75:1.5;
        if(i==rate/8){e.pan=.4f;e.gain=.3f;}
        TsSourceRouteMix x={0},y={0};int audible=i<rate/5 || i>rate/4;
        TsStereoFrame p=mosaic_event_read(NULL,&e,&a,time,rate,speed,.8f,audible,&x,i/64+1);
        TsStereoFrame q=reference_mosaic_event_read(NULL,&e,&b,time,rate,speed,.8f,audible,&y,i/64+1);
        same(&p,&q,sizeof(p));same(&a,&b,sizeof(a));same(&x,&y,sizeof(x));time+=speed/rate;
    }
}
typedef TsStereoFrame (*Read)(void *,const TsMosaicEvent *,TsMosaicVoice *,double,int,double,float,int,TsSourceRouteMix *,uint64_t);
static volatile float sink;
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
static TsStereoFrame candidate(void *p,const TsMosaicEvent *e,TsMosaicVoice *v,double t,int r,double s,float l,int a,TsSourceRouteMix *m,uint64_t n)
{return mosaic_event_read(p,e,v,t,r,s,l,a,m,n);}
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
static TsStereoFrame reference(void *p,const TsMosaicEvent *e,TsMosaicVoice *v,double t,int r,double s,float l,int a,TsSourceRouteMix *m,uint64_t n)
{return reference_mosaic_event_read(p,e,v,t,r,s,l,a,m,n);}
static double timed(Read read)
{
    TsMosaicSource source={.sample={.data=data,.frames=65536,.channels=2,.sample_rate=48000}};
    TsMosaicEvent e={.id=1,.source=&source,.last=65536,.duration=100,.gain=.5f,.note_count=1,
        .looping=1,.fade_in=.01,.fade_out=.01};
    e.tuning.root_note=60;e.notes[0]=60;
    TsMosaicVoice v[19];for(int i=0;i<19;++i)voice_begin(&v[i],&e,0,48000);
    clock_t begin=clock();float sum=0;
    for(int i=0;i<512*128;++i)for(int j=0;j<19;++j) {
        TsStereoFrame f=read(NULL,&e,&v[j],1+i/48000.0,48000,1,1,1,NULL,i/512+1);sum+=f.l;
    }
    sink=sum;return 1000.0*(clock()-begin)/CLOCKS_PER_SEC/128;
}
int main(int argc,char **argv)
{
    for(int i=0;i<131072;++i)data[i]=(i%251-125)*.001f;
    data[0]=-0.f;data[7]=NAN;data[13]=INFINITY;
    fades();
    for(int mode=-1;mode<TS_LOOP_MODE_COUNT;++mode)for(int notes=1;notes<=5;notes+=4)
        for(int channels=1;channels<=2;++channels)render(mode&1?48000:8000,mode,notes,channels);
    puts("Exact #208 Mosaic fade, release, PCM, phase and routing parity passed");
    if(argc>1 && !strcmp(argv[1],"--benchmark"))for(int i=0;i<5;++i) {
        double old,new;
        if(i&1){new=timed(candidate);old=timed(reference);}else{old=timed(reference);new=timed(candidate);}
        printf("19 cards trial %d: reference %.6f candidate %.6f ms/512 frames\n",i+1,old,new);
    }
    return 0;
}
