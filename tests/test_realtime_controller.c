#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#define main tapesister_application_main
#ifndef TS_REALTIME_MAIN_SOURCE
#define TS_REALTIME_MAIN_SOURCE "../src/main_sdl.c"
#endif
#include TS_REALTIME_MAIN_SOURCE
#undef main
#include <assert.h>

/* The actual callback, without an audio device or insert. This executable also
   provides an opt-in timing run: --benchmark <0 dry/1 board/2 Sister> [blocks] [telemetry 0/1]. */
int main(int argc,char **argv)
{
    int benchmark=argc>1 && !strcmp(argv[1],"--benchmark");
    int scenario=argc>2?atoi(argv[2]):0;
    int blocks=argc>3?atoi(argv[3]):2000;
    if(blocks<1 || scenario<0 || scenario>2)return 2;
    AudioState *a=calloc(1,sizeof(*a));TsUiState *u=calloc(1,sizeof(*u));
    assert(a && u);ts_ui_init(u);
    ts_realtime_diagnostics_init(&a->realtime_diagnostics);
    a->realtime_counter_frequency=SDL_GetPerformanceFrequency();
    a->realtime_diagnostics_enabled=argc>4?atoi(argv[4]):!benchmark;
    a->output_rate=48000;a->output_device_channels=2;
    a->fm_output_gain=1;set_fm_output_trim(0,a,u,1,0);
    ts_note_bank_init(&a->notes);a->notes.workbench_loop=1;
    ts_performance_init(&a->performance);ts_performance_init(&a->tile_launchers);
    ts_keyboard_sequence_init(&a->keyboard_sequence);
    ts_capture_init(&a->capture);ts_audio_mixer_init(&a->mixer);
    ts_sister_runtime_init(&a->sister);
    char error[160];assert(ts_sister_runtime_reconfigure(&a->sister,48000,2,error,sizeof(error)));
    TsSample sample;ts_sample_init(&sample);
    sample.frames=48000;sample.sample_rate=48000;sample.channels=1;
    sample.data=calloc(sample.frames,sizeof(float));assert(sample.data);
    for(size_t i=0;i<sample.frames;++i)
        sample.data[i]=benchmark?(float)(.12*sin(6.283185307179586*220*i/48000)):.15f;
    TsTuning tuning={60,0};TsNoteEvent note;
    for(int i=0;i<2;++i) {
        assert(ts_note_event_qwerty(&note,i*7,60));
        assert(ts_note_bank_start_sample_event(&a->notes,&sample,&tuning,&note,1,48000)==TS_NOTE_STARTED);
    }
    TsKeyboardSequenceSource *source=calloc(1,sizeof(*source));assert(source);
    source->count=1;source->fm=1;
    source->voices[0]=(TsNoteVoice){.sample=&sample,.range_last=sample.frames,.step=1,
        .gain=.3f,.looping=1,.direction=1,.active=1,.attack_frames=96};
    TsKeyboardSequenceSettings arp=a->keyboard_sequence.settings;
    arp.notes[0]=60;arp.notes[1]=64;arp.notes[2]=67;arp.count=3;arp.seconds=.137;
    ts_keyboard_sequence_set(&a->keyboard_sequence,&arp);
    ts_keyboard_sequence_source(&a->keyboard_sequence,source);
    ts_keyboard_sequence_select_slot(&a->keyboard_sequence,1);
    arp.notes[1]=63;ts_keyboard_sequence_set(&a->keyboard_sequence,&arp);
    ts_keyboard_sequence_select_slot(&a->keyboard_sequence,0);
    TsKeyboardSlotSequence outer={1,TS_KEYBOARD_SEQUENCE_UP,1,.413};
    ts_keyboard_sequence_set_slot_sequence(&a->keyboard_sequence,&outer);
    assert(ts_keyboard_sequence_play(&a->keyboard_sequence));
    if(scenario==2)assert(ts_sister_runtime_enable(&a->sister,48000,2,2,5,error,sizeof(error)));
    if(scenario==2) {
        ts_sister_runtime_set_sources(&a->sister,TS_SISTER_SOURCE_FM);
        ts_sister_runtime_set_monitor(&a->sister,1);
    }
    TsSisterParameters p=a->sister.parameters;
    p.head1_level=.5f;p.head2_level=.25f;p.head3_level=.25f;
    p.monitor_dry=.25f;p.monitor_wet=.75f;
    if(scenario) {
        p.fx.slot[0]=(TsSisterFxSlotControls){TS_SISTER_FX_DISTORTION,1,TS_SISTER_FX_PLACE_POST,0,.3f,.5f,.5f,.35f};
        p.fx.slot[1]=(TsSisterFxSlotControls){TS_SISTER_FX_DELAY,1,TS_SISTER_FX_PLACE_POST,0,.4f,.45f,.5f,.4f};
        p.fx.slot[2]=(TsSisterFxSlotControls){TS_SISTER_FX_REVERB,1,TS_SISTER_FX_PLACE_POST,0,.6f,.6f,.5f,.35f};
        p.fx.slot[3]=(TsSisterFxSlotControls){TS_SISTER_FX_GRAIN,1,TS_SISTER_FX_PLACE_POST,0,.3f,.4f,.5f,.25f};
        ts_sister_runtime_set_parameters(&a->sister,&p);
    }
    float block[512];
    for(int i=0;i<100;++i)audio_callback(a,(Uint8*)block,sizeof(block));
    if(benchmark) {
        Uint64 frequency=SDL_GetPerformanceFrequency(),total=0,worst=0;
        double checksum=0;
        uint64_t hash=14695981039346656037ull;
        for(int b=0;b<blocks;++b) {
            Uint64 begin=SDL_GetPerformanceCounter();
            audio_callback(a,(Uint8*)block,sizeof(block));
            Uint64 elapsed=SDL_GetPerformanceCounter()-begin;
            total+=elapsed;if(elapsed>worst)worst=elapsed;
            for(int i=0;i<512;++i){assert(isfinite(block[i]));checksum+=block[i];}
            const unsigned char *bytes=(const unsigned char *)block;
            for(size_t i=0;i<sizeof(block);++i){hash^=bytes[i];hash*=1099511628211ull;}
        }
        printf("callback scenario=%d blocks=%d block=256 avg_us=%.3f worst_us=%.3f ns_per_frame=%.3f checksum=%.12f\n",
            scenario,blocks,1e6*total/frequency/blocks,1e6*worst/frequency,1e9*total/frequency/blocks/256,checksum);
        printf("telemetry=%d audio_hash=%016llx\n",a->realtime_diagnostics_enabled,(unsigned long long)hash);
    } else {
        /* FM trim reaches every dry/capture/monitor tap through one ramp. */
        float previous=a->keyboard_dry.l,largest=0;
        set_fm_output_trim(0,a,u,0,0);
        for(int i=0;i<300;++i) {
            audio_callback(a,(Uint8*)block,2*sizeof(float));
            largest=fmaxf(largest,fabsf(a->keyboard_dry.l-previous));previous=a->keyboard_dry.l;
        }
        printf("FM trim maximum dry-tap jump %.8f\n",largest);
        assert(largest<.005f);assert(a->fm_output_gain==0 && a->keyboard_dry.l==0);
        set_fm_output_trim(0,a,u,1,0);
        for(int i=0;i<300;++i)audio_callback(a,(Uint8*)block,2*sizeof(float));
        assert(fabsf(a->fm_output_gain-1)<1e-6f && a->keyboard_dry.l>.1f);
    }
    ts_keyboard_sequence_source_free(ts_keyboard_sequence_source(&a->keyboard_sequence,NULL));
    ts_sister_runtime_free(&a->sister);ts_sample_free(&sample);free(a);free(u);
    return 0;
}
