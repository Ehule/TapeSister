#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/spatial.h"
#include "tapesister/config.h"
#include "tapesister/sister_project_state.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void check_close(float a,float b,float epsilon,int line)
{ if(fabsf(a-b)>=epsilon)fprintf(stderr,"line %d: %.9g != %.9g (tolerance %.9g)\n",line,a,b,epsilon);assert(fabsf(a-b)<epsilon); }
#define close_to(a,b,e) check_close(a,b,e,__LINE__)
static void render(TsSpatial *s,TsStereoFrame in,float *out,int channels,int frames,int pair)
{
    for(int n=0;n<frames;++n){memset(out,0,channels*sizeof(*out));out[0]=in.l;out[1]=in.r;
        if(pair){out[pair*2]=.123f;out[pair*2+1]=-.321f;}
        ts_spatial_process(s,in,out,(unsigned)channels,pair);
        for(int ch=0;ch<channels;++ch)assert(isfinite(out[ch])&&fabsf(out[ch])<=1.0001f);
    }
}
static void decoders(void)
{
    TsSpatialControls c;ts_spatial_default(&c);float d[16][3];
    for(int n=3;n<=16;++n)for(int point=0;point<2;++point){
        ts_spatial_ring(&c,n,point);c.decoder=2;assert(ts_spatial_decoder(&c,d));
        /* Decode/re-encode reconstructs the three horizontal components. */
        for(int k=0;k<3;++k)for(int j=0;j<3;++j){double sum=0;
            for(int i=0;i<n;++i){double a=c.azimuth[i]*3.141592653589793/180;
                double basis=j==0?1:j==1?cos(a):sin(a);sum+=basis*d[i][k];}
            close_to((float)sum,j==k?(k==0?1.41421356f:1.f):0.f,.00001f);
        }
    }
    ts_spatial_ring(&c,4,0);c.azimuth[0]=30;c.azimuth[1]=-30;c.azimuth[2]=-150;c.azimuth[3]=150;
    assert(ts_spatial_decoder(&c,d));
    for(int i=0;i<4;++i)c.azimuth[i]=0;
    assert(!ts_spatial_decoder(&c,d));
}
static void transform_vectors(void)
{
    TsSpatialControls c;ts_spatial_default(&c);
    for(int mode=0;mode<4;++mode){
        float f[4]={.70710678f,.3f,-.2f,.1f},original[4];memcpy(original,f,sizeof(f));
        ts_spatial_transform(f,c.value,mode);
        for(int k=0;k<4;++k)close_to(f[k],original[k],.000001f);
    }
    c.value[TS_SPATIAL_ROTATE]=.75f;
    float f[4]={.70710678f,1,0,0};ts_spatial_transform(f,c.value,0);
    close_to(f[1],0,.000001f);close_to(f[2],1,.000001f);
    c.value[TS_SPATIAL_ROTATE]=.5f;c.value[TS_SPATIAL_AMOUNT]=1;
    for(int mode=0;mode<4;++mode){float v[4]={.70710678f,0,1,0};ts_spatial_transform(v,c.value,mode);
        close_to(v[1],1.41421356f*v[0],.000001f);close_to(v[2],0,.000001f);}
}
static void routing(void)
{
    TsSpatial s;ts_spatial_init(&s);assert(ts_spatial_prepare(&s,48000));
    TsStereoFrame in={.25f,-.125f};float out[64];
    render(&s,in,out,4,1,0);assert(out[0]==in.l&&out[1]==in.r&&out[2]==0&&out[3]==0);
    /* Logical quad numbering clockwise; physical order remains FL FR RL RR. */
    const int mapping[4]={0,1,3,2};
    for(int p=0;p<4;++p){TsSpatialControls c=s.controls;c.stereo_pair=p;ts_spatial_set(&s,&c);
        render(&s,in,out,4,20000,0);
        for(int ch=0;ch<4;++ch)close_to(out[ch],ch==mapping[p]?in.l:ch==mapping[(p+1)%4]?in.r:0,.000001f);
    }
    TsSpatialControls c=s.controls;c.enabled=1;c.mono=1;c.stereo_pair=0;ts_spatial_set(&s,&c);
    render(&s,(TsStereoFrame){.5f,.5f},out,4,20000,0);
    assert(s.view.available&&s.view.wet>.999f);
    close_to(out[0],out[1],.00001f);close_to(out[2],out[3],.00001f);assert(out[0]>out[2]+.1f);
    c.value[TS_SPATIAL_ROTATE]=1;ts_spatial_set(&s,&c);
    render(&s,(TsStereoFrame){.5f,.5f},out,4,20000,0);assert(out[2]>out[0]+.1f);
    /* Invalid live remaps stay bounded while the field fades to stereo. */
    TsSpatialControls bad=c;for(int i=0;i<4;++i)bad.output[i]=0;
    ts_spatial_set(&s,&bad);render(&s,(TsStereoFrame){1,1},out,4,4000,0);assert(!s.view.available);
    ts_spatial_set(&s,&c);
    /* A live shared Insert owns its channels even when Spatial wants them. */
    render(&s,in,out,4,20000,1);assert(s.view.conflict&&!s.view.available);
    close_to(out[2],.123f,.000001f);close_to(out[3],-.321f,.000001f);
    close_to(out[0],in.l,.000001f);close_to(out[1],in.r,.000001f);
    /* A stereo-only endpoint gets complete stereo, never a truncated quad. */
    render(&s,in,out,2,20000,0);assert(!s.view.available);
    close_to(out[0],in.l,.000001f);close_to(out[1],in.r,.000001f);
    c.enabled=0;c.stereo_pair=2;ts_spatial_set(&s,&c);
    render(&s,in,out,2,20000,0);assert(!s.stereo_available);
    close_to(out[0],in.l,.000001f);close_to(out[1],in.r,.000001f);
    ts_spatial_free(&s);
}
static void transitions_and_motion(void)
{
    TsSpatial s;ts_spatial_init(&s);assert(ts_spatial_prepare(&s,48000));
    float previous[16]={.2f,.1f},out[16];TsStereoFrame in={.2f,.1f};
    for(int n=0;n<60000;++n){
        if(n%997==0){TsSpatialControls c=s.controls;c.enabled=!c.enabled;c.stereo_pair=(n/997)%4;
            c.value[0]=(n%4000)/4000.f;c.value[3]=(n%9000)/9000.f;c.transform=(n/997)%4;ts_spatial_set(&s,&c);}
        render(&s,in,out,4,1,0);
        for(int i=0;i<4;++i){assert(fabsf(out[i]-previous[i])<.005f);previous[i]=out[i];}
    }
    TsSpatialControls c=s.controls;c.enabled=1;c.value[0]=.98f;
    memcpy(c.state[1],c.value,sizeof(c.value));c.state[1][0]=.02f;c.captured=2;c.morph_seconds=.1f;
    ts_spatial_set(&s,&c);c.morph_target=1;++c.morph_trigger;ts_spatial_set(&s,&c);
    render(&s,in,out,4,2400,0);assert(s.view.value[0]>.95f||s.view.value[0]<.05f);
    render(&s,in,out,4,5000,0);assert(!s.morph_active);close_to(s.view.value[0],.02f,.001f);
    /* High sample rates, calibration delay, degenerate and nonfinite input. */
    for(int rate=44100;rate<=192000;rate*=2){assert(ts_spatial_prepare(&s,(unsigned)rate));
        c=s.controls;ts_spatial_ring(&c,16,1);c.enabled=1;c.lfo_depth=1;c.lfo_rate=1;c.lfo_targets=31;
        c.rise_depth=1;c.rise_loop=1;c.rise_targets=31;
        for(int i=0;i<16;++i){c.delay_ms[i]=20.f*i/15;c.trim_db[i]=6;}
        ts_spatial_set(&s,&c);render(&s,(TsStereoFrame){NAN,INFINITY},out,16,100,0);
        render(&s,(TsStereoFrame){1,1},out,16,rate/3,0);
    }
    /* A vanishing delay at ring position zero must never round to one-past-end. */
    assert(ts_spatial_prepare(&s,48000));c=s.controls;
    for(int i=0;i<16;++i)c.delay_ms[i]=.000001f;
    memset(s.delay_current,0,sizeof(s.delay_current));s.write=0;
    ts_spatial_set(&s,&c);render(&s,(TsStereoFrame){.1f,.1f},out,16,200,0);
    c=s.controls;c.rise_trigger=11;c.morph_trigger=11;c.morph_target=1;c.captured=3;
    ts_spatial_set(&s,&c);assert(s.rise_active&&s.morph_active);
    ts_spatial_recall(&s,&c);
    assert(!s.rise_active&&!s.morph_active&&s.lfo_phase==0&&s.rise_phase==0);
    assert(!s.controls.rise_trigger&&!s.controls.morph_trigger);
    ts_spatial_free(&s);
}
static void persistence(void)
{
    TsConfig c,loaded;ts_config_init(&c);ts_spatial_ring(&c.spatial,8,1);c.spatial.enabled=1;c.spatial.stereo_pair=7;
    c.spatial.value[3]=.42f;c.spatial.lfo_targets=5;c.spatial.captured=3;c.spatial.state[1][2]=.71f;c.spatial.morph_target=1;
    char error[160];assert(ts_config_save(&c,"spatial-test.ini",error,sizeof(error)));
    int ok=ts_config_load(&loaded,"spatial-test.ini",error,sizeof(error));if(!ok)fprintf(stderr,"%s\n",error);assert(ok);
    assert(!memcmp(&c.spatial,&loaded.spatial,sizeof(c.spatial)));
    TsSisterProjectState state,read;ts_sister_project_state_init(&state,48000);state.spatial=c.spatial;
    assert(ts_sister_project_state_save_file(&state,"spatial-project.ini",error,sizeof(error)));int present=0;
    assert(ts_sister_project_state_load_file(&read,"spatial-project.ini",48000,&present,error,sizeof(error))&&present);
    assert(!memcmp(&state.spatial,&read.spatial,sizeof(state.spatial)));
    assert(ts_spatial_read(&read.spatial,"Spatial.Value0","nan")<0);
    assert(ts_spatial_read(&read.spatial,"Spatial.Output0","2.5")<0);
    remove("spatial-test.ini");remove("spatial-project.ini");
}
int main(void)
{
    decoders();transform_vectors();routing();transitions_and_motion();persistence();
    puts("Ambisonics: decoder reconstruction, ATK transforms, stereo sides, output ownership, motion, smoothing and persistence passed");return 0;
}
