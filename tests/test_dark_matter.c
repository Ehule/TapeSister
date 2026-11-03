#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/dark_matter.h"
#include "tapesister/sister_machine.h"
#include "tapesister/sister_project_state.h"
#include "tapesister/sister_preset.h"
#include "tapesister/sister_ui.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void finite_heads(const TsStereoFrame h[3])
{ for(int j=0;j<3;++j)assert(isfinite(h[j].l) && isfinite(h[j].r) && fabsf(h[j].l)<=1 && fabsf(h[j].r)<=1); }
static void streaming(void)
{
    const uint32_t rates[]={8,44100,48000,96000,192000};
    for(unsigned r=0;r<sizeof(rates)/sizeof(*rates);++r) {
        TsDarkMatter d;TsStereoFrame h[3];assert(ts_dark_matter_init(&d,rates[r],5));
        for(int i=0;i<100;++i){ts_dark_matter_process(&d,(TsStereoFrame){0,0},1,0,h);finite_heads(h);assert(h[0].l==0 && h[1].r==0 && h[2].l==0);}
        TsDarkMatterControls c=d.controls;c.feedback=1.05f;c.blur=c.drift=c.drive=1;c.reverse=1;
        ts_dark_matter_set(&d,&c,60);
        for(unsigned i=0;i<rates[r]/2+100;++i) {
            float v=.8f*sinf(i*.17f);
            ts_dark_matter_process(&d,(TsStereoFrame){v,-v},1,0,h);finite_heads(h);
            if(i==50)ts_dark_matter_set(&d,&c,5);
            if(i==80)ts_dark_matter_set(&d,&c,60);
        }
        ts_dark_matter_process(&d,(TsStereoFrame){NAN,INFINITY},1,0,h);finite_heads(h);
        ts_dark_matter_clear(&d);for(size_t i=0;i<d.storage_frames;++i)assert(d.storage[i].l==0 && d.storage[i].r==0);
        ts_dark_matter_free(&d);
    }
}
static void memory_topology(void)
{
    TsDarkMatter a,b;TsStereoFrame ha[3],hb[3];assert(ts_dark_matter_init(&a,1000,5));assert(ts_dark_matter_init(&b,1000,5));
    TsDarkMatterControls c=a.controls;c.feedback=1;c.drive=c.damp=c.blur=c.replace=c.drift=0;
    ts_dark_matter_set(&a,&c,5);ts_dark_matter_clear(&a);c.blur=1;ts_dark_matter_set(&b,&c,5);ts_dark_matter_clear(&b);
    for(size_t i=0;i<a.bank[0].length;++i)a.bank[0].data[i].l=b.bank[0].data[i].l=.2f*sinf(i*.2f);
    for(int i=0;i<5000;++i) {
        ts_dark_matter_process(&a,(TsStereoFrame){0,0},1,0,ha);
        ts_dark_matter_process(&b,(TsStereoFrame){0,0},1,0,hb);
    }
    double ea=0,eb=0;
    for(size_t i=0;i<a.bank[1].length;++i){ea+=fabs(a.bank[1].data[i].l);eb+=fabs(b.bank[1].data[i].l);}
    assert(ea==0 && eb>.01); /* Blur transfers memory, not just output panning. */
    for(int j=0;j<4;++j)assert(a.bank[j].length!=a.bank[(j+1)%4].length);
    ts_dark_matter_clear(&a);ts_dark_matter_clear(&b);
    c.blur=0;c.replace=0;ts_dark_matter_set(&a,&c,5);c.replace=1;ts_dark_matter_set(&b,&c,5);
    ts_dark_matter_clear(&a);ts_dark_matter_clear(&b);
    for(int j=0;j<4;++j)for(size_t i=0;i<a.bank[j].length;++i)a.bank[j].data[i].l=b.bank[j].data[i].l=.2f;
    ts_dark_matter_process(&a,(TsStereoFrame){.2f,0},1,0,ha);
    ts_dark_matter_process(&b,(TsStereoFrame){.2f,0},1,0,hb);
    assert(!memcmp(ha,hb,sizeof(ha))); /* Replace doesn't merely duck playback. */
    assert(b.bank[0].data[0].l<a.bank[0].data[0].l-.01f);
    TsStereoFrame *saved=malloc(a.storage_frames*sizeof(*saved));assert(saved);
    memcpy(saved,a.storage,a.storage_frames*sizeof(*saved));
    for(int i=0;i<3000;++i)ts_dark_matter_process(&a,(TsStereoFrame){.9f,.9f},0,0,ha);
    assert(!memcmp(saved,a.storage,a.storage_frames*sizeof(*saved)));free(saved);
    ts_dark_matter_free(&a);ts_dark_matter_free(&b);
}
static void deterministic_stereo(void)
{
    TsDarkMatter a,b;TsStereoFrame x[3],y[3];assert(ts_dark_matter_init(&a,1000,5));assert(ts_dark_matter_init(&b,1000,5));
    for(int i=0;i<10000;++i) {
        TsStereoFrame v={.3f*sinf(i*.123f),0};
        ts_dark_matter_process(&a,v,1,0,x);ts_dark_matter_process(&b,v,1,0,y);
        assert(!memcmp(x,y,sizeof(x)));for(int j=0;j<3;++j)assert(x[j].r==0);
    }
    TsDarkMatterControls c=b.controls;c.reverse=1;ts_dark_matter_set(&b,&c,5);
    double difference=0;
    for(int i=0;i<3000;++i){ts_dark_matter_process(&a,(TsStereoFrame){0,0},0,0,x);ts_dark_matter_process(&b,(TsStereoFrame){0,0},0,0,y);difference+=fabs(x[0].l-y[0].l);}
    assert(difference>1);
    ts_dark_matter_clear(&a);
    for(int i=0;i<10000;++i){ts_dark_matter_process(&a,(TsStereoFrame){.2f*sinf(i*.123f),-.9f},1,1,x);for(int j=0;j<3;++j)assert(x[j].l==x[j].r);}
    ts_dark_matter_free(&a);ts_dark_matter_free(&b);
}
static void integrated_machine(void)
{
    TsSisterMachine a,b;assert(ts_sister_machine_init(&a,1000,2,5));assert(ts_sister_machine_init(&b,1000,2,5));
    TsSisterParameters p=b.parameters;p.dark.reverse=1;p.dark.blur=1;p.dark.feedback=1.05f;
    ts_sister_machine_set_parameters(&b,&p);
    for(int i=0;i<10000;++i) {
        TsStereoFrame v={.2f*sinf(i*.17f),.1f*cosf(i*.13f)};
        TsSisterOutput x=ts_sister_machine_process_frame(&a,v,v),y=ts_sister_machine_process_frame(&b,v,v);
        assert(!memcmp(&x,&y,sizeof(x))); /* Dark settings leave settled Tape exact. */
    }
    TsStereoFrame *pointer=b.dark.storage;p.memory_mode=TS_SISTER_MEMORY_DARK;
    TsStereoFrame retained=b.dark.bank[0].data[0];
    ts_sister_machine_set_parameters(&b,&p);
    assert(!memcmp(&retained,&b.dark.bank[0].data[0],sizeof(retained)));
    double energy=0;
    for(int i=0;i<1000;++i){TsSisterOutput x=ts_sister_machine_process_frame(&b,(TsStereoFrame){0,0},(TsStereoFrame){0,0});assert(isfinite(x.mix.l) && fabsf(x.mix.l)<=1);energy+=fabs(x.mix.l);}
    assert(energy>.01 && b.dark.storage==pointer && b.dark_blend.current==1);
    TsSisterSnapshot snap;assert(ts_sister_machine_get_snapshot(&b,&snap));
    assert(snap.dark_blend==1 && snap.dark_seconds[0]>0);
    for(int j=0;j<4;++j)assert(snap.dark_position[j]>=0 && snap.dark_position[j]<1);
    ts_sister_machine_set_hold(&b,1);TsStereoFrame first=b.dark.storage[0];
    for(int i=0;i<1000;++i)ts_sister_machine_process_frame(&b,(TsStereoFrame){1,1},(TsStereoFrame){0,0});
    assert(!memcmp(&first,&b.dark.storage[0],sizeof(first)));
    assert(ts_sister_machine_clear_offline(&b));
    for(size_t i=0;i<b.dark.storage_frames;++i)assert(b.dark.storage[i].l==0 && b.dark.storage[i].r==0);
    assert(ts_sister_machine_reconfigure(&b,2000,1,5));assert(b.parameters.memory_mode==TS_SISTER_MEMORY_DARK && b.dark.sample_rate==2000);
    ts_sister_machine_free(&a);ts_sister_machine_free(&b);
}
static void persistence(void)
{
    static TsConfig config,loaded_config;static TsSisterProjectState state,loaded;
    static TsSisterPresetBank bank,read_bank;
    char error[256];const char *project="dark-matter-test.sister",*preset="dark-matter-test.presets",*ini="dark-matter-test.ini";
    int present=0;ts_sister_project_state_init(&state,48000);state.parameters.memory_mode=TS_SISTER_MEMORY_DARK;
    state.parameters.dark.feedback=1.02f;state.parameters.dark.reverse=1;state.parameters.dark.locks=1023;
    state.parameters.dark.algorithm=TS_DARK_GRANULAR;state.parameters.dark.size=2;
    assert(ts_sister_project_state_save_file(&state,project,error,sizeof(error)));
    assert(ts_sister_project_state_load_file(&loaded,project,48000,&present,error,sizeof(error)));
    assert(loaded.parameters.memory_mode==TS_SISTER_MEMORY_DARK && loaded.parameters.dark.reverse && loaded.parameters.dark.locks==1023);
    assert(loaded.parameters.dark.algorithm==TS_DARK_GRANULAR && loaded.parameters.dark.size==2);
    ts_sister_preset_bank_init(&bank,48000);
    assert(ts_sister_preset_save_new(&bank,"DARK TEST",&state.parameters,48000,error,sizeof(error)));
    assert(ts_sister_preset_save(&bank,preset,error,sizeof(error)));
    assert(ts_sister_preset_load(&read_bank,preset,48000,error,sizeof(error)));
    assert(read_bank.entries[read_bank.count-1].parameters.memory_mode==TS_SISTER_MEMORY_DARK);
    assert(read_bank.entries[read_bank.count-1].parameters.dark.locks==1023);
    assert(read_bank.entries[read_bank.count-1].parameters.dark.algorithm==TS_DARK_GRANULAR && read_bank.entries[read_bank.count-1].parameters.dark.size==2);
    ts_config_init(&config);config.sister_memory_mode=TS_SISTER_MEMORY_DARK;config.sister_dark=state.parameters.dark;
    assert(ts_config_save(&config,ini,error,sizeof(error)) && ts_config_load(&loaded_config,ini,error,sizeof(error)));
    assert(loaded_config.sister_memory_mode==TS_SISTER_MEMORY_DARK && loaded_config.sister_dark.reverse && loaded_config.sister_dark.locks==1023);
    assert(loaded_config.sister_dark.algorithm==TS_DARK_GRANULAR && loaded_config.sister_dark.size==2);
    FILE *legacy=fopen(project,"w");assert(legacy);
    fputs("TapeSister Sister Project State\nVersion=46\nPageCount=1\nActivePage=0\n",legacy);fclose(legacy);
    assert(ts_sister_project_state_load_file(&loaded,project,48000,&present,error,sizeof(error)) && present);
    assert(loaded.parameters.memory_mode==TS_SISTER_MEMORY_TAPE && loaded.parameters.dark.locks==0);
    legacy=fopen(preset,"w");assert(legacy);
    fputs("TapeSister Sister Presets\nVersion=23\n[Preset]\nName=LEGACY\nh1_level=0.4\n",legacy);fclose(legacy);
    assert(ts_sister_preset_load(&read_bank,preset,48000,error,sizeof(error)));
    assert(read_bank.entries[read_bank.count-1].parameters.memory_mode==TS_SISTER_MEMORY_TAPE);
    legacy=fopen(ini,"w");assert(legacy);fputs("[Sister Machine]\nsister_buffer_seconds=40\n",legacy);fclose(legacy);
    assert(ts_config_load(&loaded_config,ini,error,sizeof(error)) && loaded_config.sister_memory_mode==TS_SISTER_MEMORY_TAPE);
    TsDarkMatterControls c;
    assert(ts_dark_matter_controls_read(&c,".9,0,0,0,0,0,0,255") && c.algorithm==TS_DARK_LEGACY && c.size==0);
    assert(!ts_dark_matter_controls_read(&c,".9,0,0,0,0,0,0,0,5,0"));
    assert(!ts_dark_matter_controls_read(&c,".9,0,0,0,0,0,0,0,4,3"));
    assert(!ts_dark_matter_controls_read(&c,"nan,0,0,0,0,0,0,0"));
    assert(!ts_dark_matter_controls_read(&c,".9,0,0,0,0,0,0,0 trailing"));
    assert(!ts_dark_matter_controls_read(&c,".9,0,0,0,0,0,0,4294967296000000000000000"));
    assert(!ts_dark_matter_controls_read(&c,".9,0,0,0,0,0,4294967296,0"));
    remove(project);remove(preset);remove(ini);
}
static void algorithm_bank(void)
{
    double hall_density=0,delay_density=0;
    const double two[3]={2.5,9.5,22},four[3]={2.5,8.5,11.5};
    for(int algorithm=TS_DARK_TWO_DELAYS;algorithm<TS_DARK_ALGORITHM_COUNT;++algorithm) {
        size_t previous=0;
        for(int size=0;size<3;++size) {
            TsDarkMatter d;TsStereoFrame h[3];assert(ts_dark_matter_init(&d,1000,40));
            TsDarkMatterControls c=d.controls;c.algorithm=algorithm;c.size=size;
            c.replace=c.drive=c.damp=c.blur=c.drift=0;c.feedback=.86f;
            ts_dark_matter_set(&d,&c,40);ts_dark_matter_clear(&d);
            assert(d.algorithm==algorithm && d.size==size);
            int count=algorithm==TS_DARK_TWO_DELAYS?2:algorithm==TS_DARK_GRANULAR?(size==2?3:2):4;
            assert(d.active_banks==count);
            size_t total=0;
            for(int j=0;j<count;++j) {
                assert(d.bank[j].length<d.bank[j].capacity);total+=d.bank[j].length;
            }
            assert(total>previous);previous=total;
            if(algorithm==TS_DARK_TWO_DELAYS)assert(fabs(total/1000.-two[size])<.05);
            if(algorithm==TS_DARK_FOUR_DELAYS)assert(fabs(total/1000.-four[size])<.05);
            double energy=0,squares=0;
            for(int i=0;i<35000;++i) {
                /* Impulse distinguishes a diffuse hall from discrete echoes;
                   a short phrase makes every grain variant observable. */
                float v=algorithm==TS_DARK_GRANULAR?(i<2000?.2f*sinf(i*.129f):0):(i==0?.2f:0);
                ts_dark_matter_process(&d,(TsStereoFrame){v,0},1,0,h);finite_heads(h);
                float sample=h[0].l+h[1].l+h[2].l;
                for(int j=0;j<3;++j)assert(h[j].r==0);
                energy+=fabs(sample);squares+=(double)sample*sample;
            }
            assert(energy>.001);
            /* Participation ratio weights meaningful reflections; simply
               counting nonzero samples also counts the delay DC-filter tail. */
            double density=energy*energy/squares;
            if(size==0 && algorithm==TS_DARK_GIANT_REVERB)hall_density=density;
            if(size==0 && algorithm==TS_DARK_FOUR_DELAYS)delay_density=density;
            TsStereoFrame *saved=malloc(d.storage_frames*sizeof(*saved));assert(saved);
            memcpy(saved,d.storage,d.storage_frames*sizeof(*saved));
            c.algorithm=(algorithm+1)%TS_DARK_ALGORITHM_COUNT;c.size=(size+1)%3;
            ts_dark_matter_set(&d,&c,40);
            for(int i=0;i<300;++i){ts_dark_matter_process(&d,(TsStereoFrame){.9f,-.8f},0,0,h);finite_heads(h);}
            assert(d.algorithm==c.algorithm && d.size==c.size && d.program_gain==1);
            assert(!memcmp(saved,d.storage,d.storage_frames*sizeof(*saved)));free(saved);
            ts_dark_matter_clear(&d);
            for(int i=0;i<500;++i){ts_dark_matter_process(&d,(TsStereoFrame){.1f,-.5f},1,1,h);for(int j=0;j<3;++j)assert(h[j].l==h[j].r);}
            ts_dark_matter_free(&d);
        }
    }
    printf("Impulse density: hall %.0f frames, four delays %.0f frames\n",hall_density,delay_density);
    assert(hall_density>delay_density*2);
    const uint32_t rates[]={8,44100,48000,96000,192000};
    for(unsigned r=0;r<sizeof(rates)/sizeof(*rates);++r) {
        TsDarkMatter d;TsStereoFrame h[3];assert(ts_dark_matter_init(&d,rates[r],40));
        for(int algorithm=1;algorithm<TS_DARK_ALGORITHM_COUNT;++algorithm)for(int size=0;size<3;++size) {
            TsDarkMatterControls c=d.controls;c.algorithm=algorithm;c.size=size;c.feedback=1.05f;c.blur=c.drift=c.drive=c.reverse=1;
            ts_dark_matter_set(&d,&c,40);
            for(unsigned i=0;i<rates[r]/8+300;++i){float v=.9f*sinf(i*.21f);ts_dark_matter_process(&d,(TsStereoFrame){v,-v},1,0,h);finite_heads(h);}
        }
        ts_dark_matter_free(&d);
    }
}
static void mode_level_matching(void)
{
    /* Reproduce the large jump with a sustained sound and several Tape heads.
       Matching must act on the audible return, never on either memory. */
    const int rate=8000;
    TsSisterMachine m;assert(ts_sister_machine_init(&m,rate,2,5));
    TsSisterParameters p=m.parameters;p.buffer_seconds=5;
    p.head1_level=.58f;p.head2_level=.42f;p.head3_level=.32f;
    ts_sister_machine_set_parameters(&m,&p);
    for(int i=0;i<rate*12;++i) {
        float v=.1f*sinf(i*6.28318530718f*223/rate);
        ts_sister_machine_process_frame(&m,(TsStereoFrame){v,v*.8f},(TsStereoFrame){0,0});
    }
    ts_sister_machine_set_hold(&m,1);
    double tape=0,dark=0;
    for(int i=0;i<rate*4;++i) {
        TsSisterOutput out=ts_sister_machine_process_frame(&m,(TsStereoFrame){0,0},(TsStereoFrame){0,0});
        if(i>=rate*2)tape+=out.mix.l*out.mix.l+out.mix.r*out.mix.r;
    }
    TsStereoFrame retained=m.dark.storage[0];
    p.memory_mode=TS_SISTER_MEMORY_DARK;ts_sister_machine_set_parameters(&m,&p);
    float trim=m.dark_output_gain.target;
    assert(trim>2 && trim<=8); /* Old uncalibrated path is much quieter here. */
    for(int i=0;i<rate*4;++i) {
        TsSisterOutput out=ts_sister_machine_process_frame(&m,(TsStereoFrame){0,0},(TsStereoFrame){0,0});
        assert(isfinite(out.mix.l) && fabsf(out.mix.l)<=1);
        if(i>=rate*2)dark+=out.mix.l*out.mix.l+out.mix.r*out.mix.r;
    }
    float difference=10*log10(dark/tape);
    printf("Mode level match: %+.2f dB, trim %.2f\n",difference,trim);
    assert(fabsf(difference)<3);
    assert(m.dark_output_gain.current==trim); /* Frozen between switches. */
    assert(!memcmp(&retained,&m.dark.storage[0],sizeof(retained)));
    /* Dynamics stay intact: even a later feedback/drive change cannot
       continuously chase the output and flatten Dark Matter's evolution. */
    p.dark.drive=.9f;ts_sister_machine_set_parameters(&m,&p);
    assert(m.dark_output_gain.target==trim);
    p.memory_mode=TS_SISTER_MEMORY_TAPE;ts_sister_machine_set_parameters(&m,&p);
    assert(m.dark_output_gain.target>=.125f && m.dark_output_gain.target<=8);
    ts_sister_machine_free(&m);

    assert(ts_sister_machine_init(&m,rate,2,5));p=m.parameters;
    for(int i=0;i<rate*4;++i)ts_sister_machine_process_frame(&m,(TsStereoFrame){0,0},(TsStereoFrame){0,0});
    p.memory_mode=TS_SISTER_MEMORY_DARK;ts_sister_machine_set_parameters(&m,&p);
    assert(m.dark_output_gain.target==1); /* Silence cannot request a boost. */
    assert(ts_sister_machine_clear_offline(&m));
    assert(m.mode_energy[0]==0 && m.mode_energy[1]==0 &&
           m.mode_meter_frames<(uint64_t)rate && m.dark_output_gain.current==1);
    ts_sister_machine_free(&m);
}
static void program_modulation_and_handoffs(void)
{
    TsDarkMatter a,b;TsStereoFrame x[3],y[3];
    assert(ts_dark_matter_init(&a,1000,40) && ts_dark_matter_init(&b,1000,40));
    TsDarkMatterControls c=a.controls;c.algorithm=TS_DARK_GRANULAR;c.size=1;c.replace=0;
    ts_dark_matter_set(&a,&c,40);ts_dark_matter_set(&b,&c,40);
    ts_dark_matter_clear(&a);ts_dark_matter_clear(&b);
    for(int i=0;i<6000;++i) {
        TsStereoFrame v={.2f*sinf(i*.13f),.15f*cosf(i*.17f)};
        ts_dark_matter_process(&a,v,1,0,x);ts_dark_matter_process(&b,v,1,0,y);
        assert(!memcmp(x,y,sizeof(x)));
    }
    c.blur=1;c.drift=1;ts_dark_matter_set(&b,&c,40);
    double difference=0;
    for(int i=0;i<6000;++i) {
        ts_dark_matter_process(&a,(TsStereoFrame){0,0},0,0,x);
        ts_dark_matter_process(&b,(TsStereoFrame){0,0},0,0,y);
        difference+=fabs(x[0].l-y[0].l);
    }
    assert(difference>.1); /* Scatter and grain motion change what is read. */
    c.algorithm=TS_DARK_GIANT_REVERB;c.size=0;c.reverse=0;
    ts_dark_matter_set(&a,&c,40);ts_dark_matter_set(&b,&c,40);
    ts_dark_matter_clear(&a);ts_dark_matter_clear(&b);
    c.blur=c.drift=0;ts_dark_matter_set(&a,&c,40);
    for(int i=0;i<6000;++i) {
        TsStereoFrame v={.2f*sinf(i*.13f),.15f*cosf(i*.17f)};
        ts_dark_matter_process(&a,v,1,0,x);ts_dark_matter_process(&b,v,1,0,y);
        assert(!memcmp(x,y,sizeof(x))); /* N/A controls leave the hall exact. */
    }
    TsStereoFrame *saved=malloc(a.diffusion_frames*sizeof(*saved));assert(saved);
    memcpy(saved,a.diffusion_storage,a.diffusion_frames*sizeof(*saved));
    for(int request=0;request<40;++request) {
        c.algorithm=request%TS_DARK_ALGORITHM_COUNT;c.size=request%3;
        ts_dark_matter_set(&a,&c,40);
        for(int i=0;i<7;++i){ts_dark_matter_process(&a,(TsStereoFrame){1,-1},0,0,x);finite_heads(x);}
    }
    for(int i=0;i<300;++i)ts_dark_matter_process(&a,(TsStereoFrame){0,0},0,0,x);
    assert(a.algorithm==c.algorithm && a.size==c.size && a.program_gain==1 && !a.program_pending);
    assert(!memcmp(saved,a.diffusion_storage,a.diffusion_frames*sizeof(*saved)));free(saved);
    ts_dark_matter_free(&a);ts_dark_matter_free(&b);
}
static void trim_feedback_isolation(void)
{
    TsSisterMachine a,b;
    assert(ts_sister_machine_init(&a,1000,2,5));
    assert(ts_sister_machine_init(&b,1000,2,5));
    for(int i=0;i<6000;++i) {
        TsStereoFrame v={.1f*sinf(i*.123f),.08f*cosf(i*.147f)};
        ts_sister_machine_process_frame(&a,v,v);
        ts_sister_machine_process_frame(&b,v,v);
    }
    TsSisterParameters p=a.parameters;p.memory_mode=TS_SISTER_MEMORY_DARK;
    ts_sister_machine_set_parameters(&a,&p);ts_sister_machine_set_parameters(&b,&p);
    a.dark_output_gain=(TsSisterRamp){1,1,0,0};
    b.dark_output_gain=(TsSisterRamp){4,4,0,0};
    for(int i=0;i<6000;++i) {
        TsStereoFrame v={.1f*sinf(i*.123f),.08f*cosf(i*.147f)};
        TsSisterOutput x=ts_sister_machine_process_frame(&a,v,v);
        TsSisterOutput y=ts_sister_machine_process_frame(&b,v,v);
        assert(!memcmp(&x.write,&y.write,sizeof(x.write)));
    }
    assert(!memcmp(a.buffer.data,b.buffer.data,a.buffer.storage_frames*2*sizeof(float)));
    assert(!memcmp(a.dark.storage,b.dark.storage,a.dark.storage_frames*sizeof(TsStereoFrame)));
    ts_sister_machine_free(&a);ts_sister_machine_free(&b);
}
static void benchmark(void)
{
    TsDarkMatter d;assert(ts_dark_matter_init(&d,48000,40));TsStereoFrame h[3];
    for(int algorithm=0;algorithm<TS_DARK_ALGORITHM_COUNT;++algorithm)for(int reverse=0;reverse<=1;++reverse) {
        TsDarkMatterControls c=d.controls;c.algorithm=algorithm;c.size=2;c.reverse=reverse;c.drift=c.blur=1;
        ts_dark_matter_set(&d,&c,40);clock_t start=clock();
        for(int i=0;i<1024000;++i)ts_dark_matter_process(&d,(TsStereoFrame){.15f*sinf(i*.07f),.1f*cosf(i*.03f)},1,0,h);
        double us=(double)(clock()-start)*1e6/CLOCKS_PER_SEC/4000;
        printf("Dark Matter %s reverse=%d: %.2f CPU us / 256 frames at 48 kHz; %.2f MiB storage\n",ts_dark_matter_algorithm_name(algorithm),reverse,us,(d.storage_frames+d.diffusion_frames)*sizeof(TsStereoFrame)/1048576.);
    }
    ts_dark_matter_free(&d);
}
int main(int argc,char **argv)
{
    if(argc>1 && !strcmp(argv[1],"--bench")){benchmark();return 0;}
    streaming();memory_topology();deterministic_stereo();integrated_machine();mode_level_matching();trim_feedback_isolation();persistence();algorithm_bank();program_modulation_and_handoffs();
    puts("Dark Matter: memory exchange, replace, reverse, stereo, lifecycle and persistence passed.");return 0;
}
