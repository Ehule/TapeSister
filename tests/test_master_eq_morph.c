#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/master_eq.h"
#include "tapesister/config.h"
#include "tapesister/sister_project_state.h"
#include <assert.h>
#include <math.h>
#include <string.h>
#include <time.h>

static void setup(TsMasterEq *eq,unsigned rate,double seconds)
{
    ts_master_eq_init(eq);ts_master_eq_prepare(eq,rate);
    TsMasterEqControls c=eq->controls;c.enabled=1;c.solo_band=3;
    c.band[0]=(TsEqBand){1,TS_EQ_LOW_PASS,200,0,.5f};
    c.band[1].gain_db=-6;
    ts_master_eq_set(eq,&c);ts_master_eq_morph_capture(eq,0);
    c.band[0].frequency=12800;c.band[0].q=2;c.band[1].gain_db=6;
    ts_master_eq_set(eq,&c);ts_master_eq_morph_capture(eq,1);
    eq->morph.seconds=seconds;
    assert(!eq->morph.state[0].solo_band && !eq->morph.state[1].solo_band);
}
static void frames(TsMasterEq *eq,unsigned n)
{
    for(unsigned i=0;i<n;++i) {
        float x=.01f*sinf(i*.07f);
        TsStereoFrame y=ts_master_eq_process(eq,(TsStereoFrame){x,-x*.5f});
        assert(isfinite(y.l+y.r) && fabsf(y.l+2*y.r)<1e-6f);
    }
}
static void timeline(void)
{
    const unsigned rates[]={8000,44100,48000,96000,192000};
    for(unsigned r=0;r<sizeof(rates)/sizeof(*rates);++r) {
        TsMasterEq e;setup(&e,rates[r],2);
        assert(ts_master_eq_morph_start(&e));
        assert(!memcmp(&e.live,&e.morph.state[0],sizeof(e.live)));
        TsMasterEqControls hash_anchor=e.controls;
        frames(&e,rates[r]);
        assert(e.morph_running && fabs(e.morph_position-.5)<1e-8);
        /* Log-frequency and log-Q interpolation, linear decibels. */
        assert(fabs(e.live.band[0].frequency-1600)<34);
        assert(fabs(e.live.band[0].q-1)<.008);
        assert(fabs(e.live.band[1].gain_db)<.061);
        assert(!memcmp(&e.controls,&hash_anchor,sizeof(hash_anchor)));
        frames(&e,rates[r]);assert(!e.morph_running && e.morph_position==1);
        assert(!memcmp(&e.live,&e.morph.state[1],sizeof(e.live)));
        e.morph.loop=1;assert(ts_master_eq_morph_start(&e));
        frames(&e,2*rates[r]);assert(e.morph_reverse && e.morph_position==1);
        frames(&e,2*rates[r]);assert(!e.morph_reverse && e.morph_position==0 && e.morph_running);
        frames(&e,rates[r]/2);ts_master_eq_morph_stop(&e);
        TsMasterEqControls frozen=e.live;frames(&e,rates[r]);
        assert(!e.morph_running && !memcmp(&frozen,&e.live,sizeof(frozen)));
        assert(ts_master_eq_morph_start(&e));frames(&e,rates[r]/2);
        unsigned next=rates[(r+1)%5];double position=e.morph_position;
        ts_master_eq_prepare(&e,next);assert(e.morph_position==position && e.morph_running);
        frames(&e,next/2);assert(fabs(e.morph_position-.5)<1e-8);
        e.morph.loop=0;frames(&e,next);assert(!e.morph_running && e.morph_position==1);
        assert(ts_master_eq_morph_recall(&e,0));assert(e.morph_position==0);
        assert(!memcmp(&e.live,&e.morph.state[0],sizeof(e.live)));
    }
    TsMasterEq empty;ts_master_eq_init(&empty);
    assert(!ts_master_eq_morph_start(&empty) && !ts_master_eq_morph_recall(&empty,0));
    ts_master_eq_morph_capture(&empty,0);assert(!ts_master_eq_morph_start(&empty));
}
static void continuous_sweep(void)
{
    TsMasterEq e;setup(&e,48000,240);
    /* Four-minute low-pass sweep through steady DC: frequent filter resets
       would produce periodic gain dips even though a low-pass passes DC. */
    for(int s=0;s<2;++s) {
        e.morph.state[s].band[1].gain_db=0;
        e.morph.state[s].band[0].frequency=s?600:30;
        e.morph.state[s].band[0].q=.70710678f;
    }
    assert(ts_master_eq_morph_start(&e));double worst=0;clock_t start=clock();
    for(unsigned i=0;i<48000u*240u;++i) {
        TsStereoFrame y=ts_master_eq_process(&e,(TsStereoFrame){.02f,-.01f});
        assert(isfinite(y.l+y.r));
        if(i>48000)worst=fmax(worst,fabs(y.l-.02));
    }
    assert(!e.morph_running && e.morph_position==1 && worst<.0001);
    printf("240-second EQ sweep: %.3f CPU seconds; maximum steady DC error %.9f\n",
        (double)(clock()-start)/CLOCKS_PER_SEC,worst);
    /* Four-hour setting retains subsecond resolution at the end of a leg. */
    e.morph.seconds=14400;assert(ts_master_eq_morph_start(&e));
    e.morph_frames=e.morph_total_frames-12000;frames(&e,12000);
    assert(!e.morph_running && e.morph_position==1);
    /* Discrete topology/bypass switches remain finite under every pairing. */
    for(int a=0;a<TS_EQ_TYPE_COUNT;++a)for(int b=0;b<TS_EQ_TYPE_COUNT;++b) {
        e.morph.state[0].band[0]=(TsEqBand){a%2,a,20,-12,8};
        e.morph.state[1].band[0]=(TsEqBand){1,b,20000,12,.3f};
        e.morph.seconds=.1;assert(ts_master_eq_morph_start(&e));frames(&e,5000);
        assert(!e.morph_running && !memcmp(&e.live,&e.morph.state[1],sizeof(e.live)));
    }
}
static void capture_workflow(void)
{
    TsMasterEq e;setup(&e,48000,1);
    assert(e.morph_selected==2 && !e.morph_editing);
    assert(ts_master_eq_morph_recall(&e,0) && e.morph_selected==1 && e.morph_editing);
    TsMasterEqControls a=e.live,b=e.morph.state[1],edited=a;
    edited.band[2].gain_db=3;ts_master_eq_set(&e,&edited);
    assert(!ts_master_eq_matches(&e.live,&e.morph.state[0]));
    assert(ts_master_eq_matches(&a,&e.morph.state[0]));
    ts_master_eq_morph_capture(&e,0);assert(!e.morph_editing && e.morph_selected==1);
    assert(ts_master_eq_matches(&edited,&e.morph.state[0]));
    edited.solo_band=4;assert(ts_master_eq_matches(&edited,&e.morph.state[0]));
    assert(ts_master_eq_morph_start(&e));frames(&e,12000);
    assert(!e.morph_selected && !e.morph_editing);
    uint64_t elapsed=e.morph_frames;
    assert(ts_master_eq_morph_start(&e) && e.morph_frames==elapsed);
    TsMasterEqControls held=e.live;
    ts_master_eq_morph_clear(&e,0);
    assert(!e.morph_running && e.morph_selected==1 && e.morph_editing && e.morph.captured==2);
    assert(ts_master_eq_matches(&held,&e.live) && ts_master_eq_matches(&b,&e.morph.state[1]));
    assert(!ts_master_eq_morph_start(&e));
    assert(!ts_master_eq_morph_recall(&e,0) && ts_master_eq_matches(&held,&e.live));
    ts_master_eq_morph_capture(&e,0);assert(ts_master_eq_morph_start(&e));frames(&e,48000);
    assert(!e.morph_running && e.morph_selected==2 && !e.morph_editing);
    TsEqMorph saved=e.morph;ts_master_eq_morph_set(&e,&saved);
    assert(e.morph_selected==2 && !e.morph_editing);
    ts_master_eq_morph_clear(&e,1);assert(e.morph.captured==1);
    ts_master_eq_morph_clear(&e,0);assert(!e.morph.captured);
    TsMasterEq unprepared;ts_master_eq_init(&unprepared);
    ts_master_eq_morph_capture(&unprepared,0);ts_master_eq_morph_capture(&unprepared,1);
    assert(!ts_master_eq_morph_start(&unprepared));
}
static void persistence(void)
{
    static TsConfig c,restored;ts_config_init(&c);
    assert(c.eq_morph_max_seconds==240 && c.eq_morph.seconds==240 && !c.eq_morph.captured);
    TsMasterEq e;setup(&e,48000,7200);e.morph.loop=1;
    c.eq_morph=e.morph;c.eq_morph_max_seconds=14400;c.master_eq=e.live;
    char error[256];
    assert(ts_config_save(&c,"eq-morph-test.ini",error,sizeof(error)));
    assert(ts_config_load(&restored,"eq-morph-test.ini",error,sizeof(error)));
    assert(!memcmp(&restored.eq_morph,&c.eq_morph,sizeof(c.eq_morph)) && restored.eq_morph_max_seconds==14400);
    remove("eq-morph-test.ini");
    static TsSisterRuntime runtime;ts_sister_runtime_init(&runtime);
    ts_master_eq_prepare(&runtime.master_eq,48000);
    ts_master_eq_morph_set(&runtime.master_eq,&e.morph);
    assert(ts_master_eq_morph_start(&runtime.master_eq));frames(&runtime.master_eq,12000);
    static TsSisterProjectState state,loaded;
    ts_sister_project_state_capture(&state,&runtime,1,"CUSTOM");
    assert(!memcmp(&state.master_eq,&runtime.master_eq.live,sizeof(state.master_eq)));
    assert(ts_sister_project_state_save_file(&state,"eq-morph-test.state",error,sizeof(error)));
    int present=0;
    assert(ts_sister_project_state_load_file(&loaded,"eq-morph-test.state",48000,&present,error,sizeof(error)) && present);
    assert(!memcmp(&loaded.eq_morph,&state.eq_morph,sizeof(state.eq_morph)));
    assert(ts_sister_project_state_apply(&loaded,&runtime,NULL));
    assert(!runtime.master_eq.morph_running && !memcmp(&runtime.master_eq.live,&state.master_eq,sizeof(state.master_eq)));
    FILE *f=fopen("eq-morph-test.state","w");assert(f);
    fputs("TapeSister Sister Project State\nVersion=38\nPageCount=1\nActivePage=0\n",f);fclose(f);
    assert(ts_sister_project_state_load_file(&loaded,"eq-morph-test.state",48000,&present,error,sizeof(error)));
    assert(!loaded.eq_morph.captured && loaded.eq_morph.seconds==240);
    assert(ts_sister_project_state_apply(&loaded,&runtime,NULL));
    assert(!runtime.master_eq.morph.captured && !runtime.master_eq.morph_running);
    remove("eq-morph-test.state");ts_sister_runtime_free(&runtime);
    const char *bad[]={"nan","inf","0","14401","4junk"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i)assert(ts_eq_morph_read(&e.morph,"EqMorph.Seconds",bad[i])<0);
    assert(ts_eq_morph_read(&e.morph,"EqMorph.Captured","4")<0);
    assert(ts_eq_morph_read(&e.morph,"EqMorph.A.Band.1","1,0,nan,0,1")<0);
}
int main(void){timeline();continuous_sweep();capture_workflow();persistence();puts("EQ A/B morph tests passed");return 0;}
