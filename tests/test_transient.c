#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/transient.h"
#include "tapesister/sister_project_state.h"
#include "tapesister/sister_preset.h"
#include "tapesister/sister_ui.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void near(float a,float b,float tolerance)
{ if(fabsf(a-b)>tolerance)fprintf(stderr,"%.9g != %.9g (%.9g)\n",a,b,tolerance);assert(fabsf(a-b)<=tolerance); }
static float hit(unsigned i,unsigned rate)
{ return .02f*expf(-10.f*i/rate)*cosf(6.283185307f*173*i/rate); }
static void envelopes(void)
{
    const unsigned rates[]={8000,44100,48000,96000,192000};
    for(size_t r=0;r<sizeof(rates)/sizeof(*rates);++r) {
        unsigned rate=rates[r];TsTransient neutral,boost,cut,scaled;
        ts_transient_init(&neutral,rate);ts_transient_init(&boost,rate);
        ts_transient_init(&cut,rate);ts_transient_init(&scaled,rate);
        ts_transient_sync(&boost,(TsTransientControls){15,24,1});
        ts_transient_sync(&cut,(TsTransientControls){-15,-24,1});
        ts_transient_sync(&scaled,boost.controls);
        double dry[2]={0},up[2]={0},down[2]={0};
        for(unsigned i=0;i<rate;++i) {
            float x=hit(i,rate);TsStereoFrame in={x,-.37f*x};
            TsStereoFrame n=ts_transient_process(&neutral,in);
            assert(n.l==in.l && n.r==in.r); /* exact neutral, no lookahead */
            TsStereoFrame a=ts_transient_process(&boost,in),b=ts_transient_process(&cut,in);
            TsStereoFrame quiet=ts_transient_process(&scaled,(TsStereoFrame){x*.01f,-.37f*x*.01f});
            assert(isfinite(a.l+a.r+b.l+b.r));
            near(a.r,-.37f*a.l,2e-7f);near(quiet.l*100,a.l,2e-6f);
            if(i==0)assert(a.l>in.l && b.l<in.l);
            int band=i<rate/100?0:i>rate/10 && i<rate/2?1:-1;
            if(band>=0){dry[band]+=x*x;up[band]+=a.l*a.l;down[band]+=b.l*b.l;}
        }
        assert(up[0]>dry[0]*2 && down[0]<dry[0]*.5);
        assert(up[1]>dry[1]*2 && down[1]<dry[1]*.5);
        for(unsigned i=0;i<rate*3;++i) {
            TsStereoFrame out=ts_transient_process(&boost,(TsStereoFrame){0,0});assert(out.l==0 && out.r==0);
        }
        TsStereoFrame bad=ts_transient_process(&boost,(TsStereoFrame){NAN,INFINITY});assert(isfinite(bad.l+bad.r));
        ts_transient_set(&boost,(TsTransientControls){NAN,INFINITY,0});
        assert(boost.controls.attack_db==0 && boost.controls.sustain_db==0 && boost.controls.response==.25f);
    }
    /* Independent controls: attack changes the onset; sustain changes a tail.
       Response extends the onset window without changing the sample's timing. */
    TsTransient fast,slow;ts_transient_init(&fast,48000);ts_transient_init(&slow,48000);
    ts_transient_sync(&fast,(TsTransientControls){15,0,.25f});
    ts_transient_sync(&slow,(TsTransientControls){15,0,4});
    for(int i=0;i<480;++i){ts_transient_process(&fast,(TsStereoFrame){.02f,-.02f});ts_transient_process(&slow,(TsStereoFrame){.02f,-.02f});}
    assert(slow.attack_activity>fast.attack_activity+.5f);
    /* A settled carrier should not be substantially remodulated by detectors. */
    for(int hz=50;hz<=1000;hz*=2) {
        ts_transient_init(&fast,48000);ts_transient_sync(&fast,(TsTransientControls){15,24,1});float min=100,max=-100;
        for(int i=0;i<96000;++i) {
            float x=.02f*sinf(6.283185307f*hz*i/48000);ts_transient_process(&fast,(TsStereoFrame){x,-x});
            if(i>48000){min=fminf(min,fast.gain_db);max=fmaxf(max,fast.gain_db);}
        }
        assert(max-min<1.5f && max<1.5f);
    }
}
static TsSisterFxControls fifth(void)
{
    TsSisterFxControls c;ts_sister_fx_controls_default(&c);
    for(int i=0;i<TS_SISTER_FX_SLOT_COUNT;++i)c.slot[i].type=TS_SISTER_FX_EMPTY;
    c.enabled=1;c.transition=c.master_transition=0;
    c.slot[4]=(TsSisterFxSlotControls){TS_SISTER_FX_TRANSIENT,1,TS_SISTER_FX_PLACE_POST,0,.5f,.5f,.5f,1};
    return c;
}
static void pedalboard(void)
{
    TsSisterPostFxEngine e={0};assert(ts_sister_post_fx_init(&e,48000));
    TsSisterFxControls c=fifth();
    for(int mix=0;mix<=10;++mix) {
        c.slot[4].mix=mix/10.f;ts_sister_post_fx_sync_controls(&e,&c);
        for(int i=0;i<1000;++i) {
            TsStereoFrame in={hit(i,48000),-.007f},out=ts_sister_post_fx_process(&e,3,in,0);
            assert(out.l==in.l && out.r==in.r);
        }
    }
    c.slot[4].mix=1;c.slot[4].parameter_b=0;ts_sister_post_fx_sync_controls(&e,&c);
    TsStereoFrame out={0};float dry=0;
    for(int i=0;i<12000;++i){dry=.02f*expf(-10.f*i/48000);out=ts_sister_post_fx_process_matrix(&e,(TsStereoFrame){dry,-dry},1);}
    assert(out.l<dry*.3f);near(out.l,-out.r,1e-7f);
    TsSisterFxView view=ts_sister_post_fx_view(&e);assert(view.value[TS_SISTER_FX_TRANSIENT-1][0]>0);
    c.enabled=0;ts_sister_post_fx_set_controls(&e,&c);
    for(int i=0;i<1000;++i)out=ts_sister_post_fx_process_matrix(&e,(TsStereoFrame){.02f,-.01f},1);
    assert(out.l==.02f && out.r==-.01f);
    out=ts_sister_post_fx_process_matrix(&e,(TsStereoFrame){.02f,-.01f},0);assert(out.l==0 && out.r==0);
    /* Every location and every slot owns separate stereo detector history. */
    c=fifth();c.slot[4].parameter_a=1;c.slot[4].placement=TS_SISTER_FX_PLACE_H1|TS_SISTER_FX_PLACE_H2;
    ts_sister_post_fx_sync_controls(&e,&c);
    for(int i=0;i<5000;++i)ts_sister_post_fx_process(&e,0,(TsStereoFrame){.02f,.01f},0);
    out=ts_sister_post_fx_process(&e,1,(TsStereoFrame){.02f,.01f},0);assert(out.l>.08f);
    assert(e.transient[4][2].dsp.attack_activity>.9f && e.transient[4][1].dsp.attack_activity<.01f);
    ts_sister_post_fx_free(&e);
}
static void persistence(void)
{
    TsSisterProjectState a,b;char error[200];int present;
    ts_sister_project_state_init(&a,48000);a.parameters.fx=fifth();a.parameters.fx.slot[4].parameter_a=.8f;
    a.matrix.board_insert=1;a.parameter_locks_high=UINT64_C(1)<<(TS_SISTER_UI_PARAM_SLOT5_A-64);
    assert(ts_sister_project_state_save_file(&a,"transient-state.ini",error,sizeof(error)));
    assert(ts_sister_project_state_load_file(&b,"transient-state.ini",48000,&present,error,sizeof(error)) && present);
    assert(b.parameters.fx.slot[4].type==TS_SISTER_FX_TRANSIENT && b.parameters.fx.slot[4].parameter_a==.8f);
    assert(b.matrix.board_insert==1 && b.parameter_locks_high==a.parameter_locks_high);
    FILE *f=fopen("transient-state.ini","r"),*old=fopen("transient-old.ini","w");assert(f && old);char line[2048];
    while(fgets(line,sizeof(line),f)) {
        if(!strncmp(line,"Slot5",5)||!strncmp(line,"Matrix.BoardInsert",18))continue;
        if(!strncmp(line,"Version=",8))fputs("Version=36\n",old);else fputs(line,old);
    }
    fclose(f);fclose(old);
    assert(ts_sister_project_state_load_file(&b,"transient-old.ini",48000,&present,error,sizeof(error)) && present);
    assert(b.parameters.fx.slot[4].type==TS_SISTER_FX_EMPTY && !b.matrix.board_insert);
    static TsSisterPresetBank bank,copy;ts_sister_preset_bank_init(&bank,48000);
    assert(ts_sister_preset_save_new_with_lock_words(&bank,"Transient",&a.parameters,0,a.parameter_locks_high,48000,error,sizeof(error)));
    assert(ts_sister_preset_save(&bank,"transient-presets.ini",error,sizeof(error)));
    ts_sister_preset_bank_init(&copy,48000);assert(ts_sister_preset_load(&copy,"transient-presets.ini",48000,error,sizeof(error)));
    assert(copy.entries[copy.count-1].parameters.fx.slot[4].type==TS_SISTER_FX_TRANSIENT);
    assert(copy.entries[copy.count-1].parameter_locks_high==a.parameter_locks_high);
    f=fopen("transient-presets.ini","r");old=fopen("transient-old-presets.ini","w");assert(f && old);
    while(fgets(line,sizeof(line),f)) {
        if(!strncmp(line,"slot5_",6))continue;
        if(!strncmp(line,"Version=",8))fputs("Version=20\n",old);else fputs(line,old);
    }
    fclose(f);fclose(old);ts_sister_preset_bank_init(&copy,48000);
    assert(ts_sister_preset_load(&copy,"transient-old-presets.ini",48000,error,sizeof(error)));
    assert(copy.entries[copy.count-1].parameters.fx.slot[4].type==TS_SISTER_FX_EMPTY);
    TsConfig config,restored;ts_config_init(&config);config.matrix=a.matrix;
    assert(ts_config_save(&config,"transient-config.ini",error,sizeof(error)));
    assert(ts_config_load(&restored,"transient-config.ini",error,sizeof(error)) && restored.matrix.board_insert);
    static TsSisterRuntime r;ts_sister_runtime_init(&r);assert(ts_sister_project_state_apply(&a,&r,NULL));
    assert(ts_sister_runtime_reconfigure(&r,96000,2,error,sizeof(error)));
    assert(r.post_fx.transient[4][4].dsp.rate==96000 && r.parameters.fx.slot[4].type==TS_SISTER_FX_TRANSIENT);
    ts_sister_runtime_free(&r);
    remove("transient-state.ini");remove("transient-old.ini");remove("transient-presets.ini");
    remove("transient-old-presets.ini");remove("transient-config.ini");
}
static void long_fades(void)
{
    TsSisterPostFxEngine e={0};assert(ts_sister_post_fx_init(&e,1000));
    TsSisterFxControls c=fifth();c.slot[4].parameter_a=1;c.slot[4].parameter_b=0;
    c.transition=ts_sister_fx_transition_normalized(180000);
    c.master_transition=ts_sister_fx_transition_normalized(120000);
    ts_sister_post_fx_sync_controls(&e,&c);c.slot[4].enabled=0;c.enabled=0;
    ts_sister_post_fx_set_controls(&e,&c);
    TsSisterFxTransitionStatus status=ts_sister_post_fx_effect_transition_status(&e);
    assert(status.active && status.source==TS_SISTER_FX_TRANSITION_SLOT_5);
    unsigned frames=e.slot[4].engage.total;
    assert(frames>179900 && frames<180100);
    for(unsigned i=0;i<frames;++i) {
        TsStereoFrame out=ts_sister_post_fx_process_matrix(&e,(TsStereoFrame){.01f,-.005f},1);
        assert(isfinite(out.l+out.r));
        if(i==60000){near(e.slot[4].engage.current,2.f/3,.003f);near(e.master_engage.current,.5f,.003f);}
    }
    assert(!e.slot[4].engage.remaining && !e.master_engage.remaining);
    TsStereoFrame out=ts_sister_post_fx_process_matrix(&e,(TsStereoFrame){.01f,-.005f},1);
    assert(out.l==.01f && out.r==-.005f);ts_sister_post_fx_free(&e);
}
static void matrix(void)
{
    static TsSisterRuntime r;char error[200];ts_sister_runtime_init(&r);
    assert(ts_sister_runtime_reconfigure(&r,48000,2,error,sizeof(error)));
    ts_sister_runtime_configure_limiter(&r,0,-1,1,50);
    TsSisterParameters p=r.parameters;p.fx=fifth();p.fx.slot[4].parameter_b=0;
    ts_sister_runtime_set_parameters(&r,&p);ts_sister_post_fx_sync_controls(&r.post_fx,&p.fx);
    TsMatrixControls c=r.matrix.controls;c.enabled=c.board_insert=1;
    c.row[TS_MATRIX_FM].matrix_gain[TS_MATRIX_MAIN]=0;c.row[TS_MATRIX_FM].matrix_gain[TS_MATRIX_BOARD]=1000;
    assert(ts_matrix_set(&r.matrix,&c,48000));
    float dry=0;for(int i=0;i<12000;++i) {
        dry=.02f*expf(-10.f*i/48000);TsSisterSourceFrames s={.fm={dry,-dry}};ts_sister_runtime_process_frame(&r,&s);
    }
    assert(r.matrix.output[0].l<dry*.3f && r.matrix.output[0].l>0);
    p.fx.enabled=0;ts_sister_runtime_set_parameters(&r,&p);
    TsSisterSourceFrames s={.fm={.02f,-.01f}};
    for(int i=0;i<2000;++i)ts_sister_runtime_process_frame(&r,&s);
    near(r.matrix.output[0].l,.02f,1e-6f);
    c.board_insert=0;assert(ts_matrix_set(&r.matrix,&c,48000));float previous=.02f;
    for(int i=0;i<12000;++i){ts_sister_runtime_process_frame(&r,&s);float x=r.matrix.output[0].l;assert(fabsf(x-previous)<.0001f);previous=x;}
    assert(r.matrix.output[0].l==0);
    assert(ts_matrix_read(&c,"Matrix.BoardInsert","2")==-1 && !c.board_insert);
    ts_sister_runtime_free(&r);
}
int main(void)
{
    envelopes();pedalboard();persistence();long_fades();matrix();
    puts("Transient envelopes, stereo, fifth slot, recall and matrix Insert/Send passed");return 0;
}
