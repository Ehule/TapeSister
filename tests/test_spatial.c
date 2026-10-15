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
    c.speakers.count=10;c.speakers.output[9]=15;snprintf(c.speakers.name[9],TS_SPEAKER_NAME,"REAR WALL");
    char error[160];assert(ts_config_save(&c,"spatial-test.ini",error,sizeof(error)));
    int ok=ts_config_load(&loaded,"spatial-test.ini",error,sizeof(error));if(!ok)fprintf(stderr,"%s\n",error);assert(ok);
    assert(!memcmp(&c.spatial,&loaded.spatial,sizeof(c.spatial)));
    assert(!memcmp(&c.speakers,&loaded.speakers,sizeof(c.speakers)));
    TsSisterProjectState state,read;ts_sister_project_state_init(&state,48000);state.spatial=c.spatial;
    assert(ts_sister_project_state_save_file(&state,"spatial-project.ini",error,sizeof(error)));int present=0;
    assert(ts_sister_project_state_load_file(&read,"spatial-project.ini",48000,&present,error,sizeof(error))&&present);
    assert(!memcmp(&state.spatial,&read.spatial,sizeof(state.spatial)));
    assert(ts_spatial_read(&read.spatial,"Spatial.Value0","nan")<0);
    assert(ts_spatial_read(&read.spatial,"Spatial.Output0","2.5")<0);
    remove("spatial-test.ini");remove("spatial-project.ini");
}
static void capture_workflow(void)
{
    TsSpatial s;ts_spatial_init(&s);assert(ts_spatial_prepare(&s,48000));
    assert(!ts_spatial_morph_to(&s,0));
    TsSpatialControls c=s.controls;c.value[0]=.98f;c.value[3]=.2f;
    c.lfo_depth=.5f;c.lfo_targets=1;c.morph_seconds=.1f;ts_spatial_set(&s,&c);
    assert(ts_spatial_capture_state(&s,0));
    c=s.controls;c.value[0]=.02f;c.value[3]=.8f;ts_spatial_set(&s,&c);
    assert(ts_spatial_capture_state(&s,1));
    float saved[2][5];memcpy(saved,s.controls.state,sizeof(saved));
    ts_spatial_edit_state(&s,0);
    assert(s.view.capture_selected==1 && s.view.capture_editing && !s.morph_active);
    assert(!memcmp(s.controls.value,saved[0],sizeof(c.value)));
    assert(ts_spatial_morph_to(&s,1));assert(s.view.morph_active && s.view.morph==0);
    float out[4];render(&s,(TsStereoFrame){.1f,.1f},out,4,3000,0);
    assert(s.base[0]>1 && s.base[3]>.2f && s.base[3]<.8f);
    assert(!ts_spatial_capture_state(&s,0)); /* Never silently capture the destination mid-flight. */
    assert(!memcmp(saved,s.controls.state,sizeof(saved)));
    float live[5];memcpy(live,s.base,sizeof(live));
    ts_spatial_stop_morph(&s);
    assert(!s.morph_active && !s.view.morph_active && !s.view.capture_selected);
    close_to(s.controls.value[0],live[0]-1,.000001f);
    close_to(s.controls.value[3],live[3],.000001f);
    assert(!memcmp(saved,s.controls.state,sizeof(saved)));
    assert(ts_spatial_capture_state(&s,0)); /* Stores held base, not the LFO excursion. */
    close_to(s.controls.state[0][0],live[0]-1,.000001f);
    assert(ts_spatial_morph_to(&s,1));
    ts_spatial_edit_state(&s,1); /* Destination already equals controls.value: must still stop. */
    assert(!s.morph_active && !s.view.morph_active && s.view.capture_editing);
    assert(!memcmp(s.controls.value,saved[1],sizeof(c.value)));
    ts_spatial_clear_state(&s,0);assert(s.controls.captured==2 && s.view.capture_selected==1);
    assert(!memcmp(s.controls.value,saved[1],sizeof(c.value)));
    ts_spatial_edit_state(&s,0);assert(!memcmp(s.controls.value,saved[1],sizeof(c.value)));
    assert(!memcmp(s.controls.state[1],saved[1],sizeof(c.value)));
    ts_spatial_free(&s);
}

static void additive_bus(void)
{
    TsSpeakerSetup setup;ts_speakers_default(&setup);
    /* Every supported array count, with spatial and fixed signals sharing outputs. */
    for(int count=3;count<=16;++count) {
        TsSpatial s;ts_spatial_init(&s);assert(ts_spatial_prepare(&s,48000));
        TsSpatialControls c=s.controls;ts_spatial_ring(&c,count,0);c.enabled=1;
        ts_spatial_set(&s,&c);float out[16],pure[16];
        TsSpatial reference;ts_spatial_init(&reference);assert(ts_spatial_prepare(&reference,48000));ts_spatial_set(&reference,&c);
        for(int frame=0;frame<14000;++frame) {
            for(int ch=0;ch<16;++ch){out[ch]=.01f*(ch+1);pure[ch]=0;}
            ts_spatial_process_bus(&s,(TsStereoFrame){.17f,.09f},out,16,&setup);
            ts_spatial_process_bus(&reference,(TsStereoFrame){.17f,.09f},pure,16,&setup);
        }
        assert(s.view.available);
        for(int ch=0;ch<16;++ch)close_to(out[ch],pure[ch]+.01f*(ch+1),.000001f);
        c.enabled=0;ts_spatial_set(&s,&c);
        for(int frame=0;frame<14000;++frame) {
            for(int ch=0;ch<16;++ch)out[ch]=.01f*(ch+1);
            ts_spatial_process_bus(&s,(TsStereoFrame){.17f,.09f},out,16,&setup);
        }
        for(int ch=0;ch<16;++ch)close_to(out[ch],.01f*(ch+1),.000001f);
        /* Missing array output mutes the bus; never falls back to OUT 1/2. */
        c.enabled=1;ts_spatial_set(&s,&c);setup.count=count-1;
        memset(out,0,sizeof(out));ts_spatial_process_bus(&s,(TsStereoFrame){.2f,.3f},out,16,&setup);
        assert(!s.view.available);for(int ch=0;ch<16;++ch)close_to(out[ch],0,.000001f);
        ts_speakers_default(&setup);ts_spatial_free(&s);ts_spatial_free(&reference);
    }
    /* Named logical speaker maps to a different physical socket. */
    TsSpatial s;ts_spatial_init(&s);assert(ts_spatial_prepare(&s,48000));
    TsSpatialControls c=s.controls;c.enabled=1;c.output[0]=1;c.output[1]=3;c.output[2]=6;c.output[3]=9;
    setup.count=10;setup.output[1]=14;assert(ts_speakers_valid(&setup));ts_spatial_set(&s,&c);
    float out[16];for(int f=0;f<14000;++f){memset(out,0,sizeof(out));ts_spatial_process_bus(&s,(TsStereoFrame){.2f,.1f},out,16,&setup);}
    assert(s.view.available && fabsf(out[14])>.001f && out[1]==0 && out[0]==0);
    for(int ch=0;ch<16;++ch)if(ch!=14 && ch!=3 && ch!=6 && ch!=9)assert(out[ch]==0);
    int old=c.output[0];float trim=c.trim_db[0]=-.7f,delay=c.delay_ms[0]=2.5f;
    ts_spatial_resize(&c,5,10);assert(c.speakers==5 && c.output[0]==old && c.trim_db[0]==trim && c.delay_ms[0]==delay);
    ts_spatial_resize(&c,2,10);assert(c.speakers==3);ts_spatial_resize(&c,16,10);assert(c.speakers==10);
    for(int point=0;point<2;++point)for(int direction=-1;direction<=1;direction+=2) {
        c.point=point;
        for(int n=direction<0?15:3;n>=3 && n<=16;n+=direction) {
            ts_spatial_resize(&c,n,16);assert(c.speakers==n);
            TsSpatialControls ring=c;ts_spatial_ring(&ring,n,point);
            assert(!memcmp(c.azimuth,ring.azimuth,sizeof(c.azimuth)));
            assert(c.output[0]==old && c.trim_db[0]==trim && c.delay_ms[0]==delay);
            float decoder[16][3];assert(ts_spatial_decoder(&c,decoder));
            for(int i=0;i<n;++i)for(int j=0;j<i;++j)assert(c.output[i]!=c.output[j]);
        }
    }
    c.azimuth[0]=17;TsSpatialControls custom=c;
    ts_spatial_resize(&c,16,16);assert(!memcmp(&c,&custom,sizeof(c))); /* At the limit, preserve manual angles. */
    ts_spatial_free(&s);
}
int main(void)
{
    decoders();transform_vectors();routing();transitions_and_motion();persistence();capture_workflow();additive_bus();
    puts("Ambisonics: decoder reconstruction, ATK transforms, stereo sides, output ownership, motion, smoothing and persistence passed");return 0;
}
