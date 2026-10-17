#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#define TAPEHEAD_EMBEDDED
/* Inspect private IPC/window state only in the fixture. Production exposes
   none of these handles or renderer objects to the application callback. */
#include "../src/ts_mandala_sdl.c"
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>
#include "../third_party/tapehead/application/src/ft2_replayer.h"
#include "../third_party/tapehead/application/src/ft2_structs.h"

static AudioState a;
static TsUiState u;
static TsInstrument bank;
static TsSamplePages pages;
static char error[160];
static float out[512*4];
static double timings[200000],arrivals[200000];
static Uint64 callback_starts[200000];
static atomic_uint timing_count;
static Uint64 last_start;
static unsigned input_phase;
static void measured_callback(void *unused,Uint8 *output,int bytes)
{
    (void)unused;
    /* A synthetic external source uses the real matrix capture path. */
    float external[512*2];
    unsigned frames=(unsigned)bytes/(sizeof(float)*a.output_device_channels);
    assert(frames<=512);
    for(unsigned i=0;i<frames;++i) {
        external[i*2]=.08f*sinf((input_phase+i)*.031f);
        external[i*2+1]=.09f*sinf((input_phase+i)*.041f);
    }
    input_phase+=frames;
    ts_insert_capture(&a.sister.insert,external,frames,2);
    Uint64 start=SDL_GetPerformanceCounter();
    audio_callback(&a,output,bytes);
    unsigned index=atomic_load_explicit(&timing_count,memory_order_relaxed);
    if(index<200000) {
        double frequency=(double)SDL_GetPerformanceFrequency();
        timings[index]=(SDL_GetPerformanceCounter()-start)*1e6/frequency;
        arrivals[index]=last_start?(start-last_start)*1e6/frequency:0;
        callback_starts[index]=start;
        atomic_store_explicit(&timing_count,index+1,memory_order_release);
    }
    last_start=start;
}
static void setup(SDL_Window *window)
{
    ts_mandala_host_init();ts_ui_init(&u);ts_instrument_init(&bank);
    assert(ts_sample_pages_init(&pages,error,sizeof(error)));u.tracker=&pages.tracker;
    ts_note_bank_init(&a.notes);ts_performance_init(&a.performance);ts_performance_init(&a.tile_launchers);
    ts_keyboard_sequence_init(&a.keyboard_sequence);ts_tracker_playback_init(&a.tracker);ts_audio_mixer_init(&a.mixer);
    ts_capture_init(&a.capture);ts_sister_runtime_init(&a.sister);
    a.output_rate=48000;a.output_device_channels=4;
    ts_realtime_diagnostics_init(&a.realtime_diagnostics);a.realtime_diagnostics_enabled=1;
    a.realtime_counter_frequency=SDL_GetPerformanceFrequency();
    assert(ts_sister_runtime_reconfigure(&a.sister,48000,2,error,sizeof(error)));
    assert(ts_sister_runtime_enable(&a.sister,48000,2,2,5,error,sizeof(error)));
    assert(ts_instrument_activate_silence(&bank,48000,48000,error,sizeof(error)));
    for(unsigned f=0;f<48000;++f)bank.current.data[f]=.10f*sinf(f*.028797933f)+.04f*sinf(f*.0863938f);
    bank.has_loop=1;bank.loop_first=0;bank.loop_last=48000;
    assert(ts_instrument_sync_selected(&bank,error,sizeof(error)));
    assert(embedded_open(window,0,&a,&u,&pages,&bank,48000));
    for(int row=0;row<64;row+=4)for(int ch=0;ch<4;++ch)
        pattern[editor.editPattern][row*MAX_CHANNELS+ch]=(note_t){.note=(uint8_t)(37+ch*7+row%12),.instr=1,.vol=0x28};
    song.songLength=1;song.orders[0]=editor.editPattern;startPlaying(PLAYMODE_SONG,0);
    assert(ts_note_bank_start(&a.notes,&bank,TS_AUDITION_CURRENT,0,0,48000)==TS_NOTE_STARTED);
    TsKeyboardSequenceSource *arp=calloc(1,sizeof(*arp));assert(arp);
    assert(ts_sample_clone(&arp->sample,&bank.current,error,sizeof(error)));
    arp->count=1;arp->voices[0]=a.notes.voices[0];arp->voices[0].sample=&arp->sample;
    TsKeyboardSequenceSettings settings=a.keyboard_sequence.settings;
    settings.notes[0]=48;settings.notes[1]=55;settings.notes[2]=60;settings.count=3;settings.seconds=.25f;
    ts_keyboard_sequence_set(&a.keyboard_sequence,&settings);ts_keyboard_sequence_source(&a.keyboard_sequence,arp);
    assert(ts_keyboard_sequence_play(&a.keyboard_sequence));
    a.mosaic=ts_mosaic_create();assert(a.mosaic);
    TsMosaicSource *source=ts_mosaic_source(a.mosaic,&bank.current,error,sizeof(error));assert(source);
    for(int i=0;i<16;++i) {
        TsMosaicEvent *e=ts_mosaic_add(a.mosaic,source,0,i);assert(e);
        e->duration=1800;e->gain=.03f;e->looping=1;e->notes[0]=36+i;e->note_count=1;
    }
    a.mosaic->playing=1;a.mosaic->repeat=1;
    TsSisterParameters p=a.sister.parameters;
    p.prism.enabled=1;p.prism.lenses=8;p.prism.mix=.4f;
    p.fx.enabled=1;p.fx.transition=ts_sister_fx_transition_normalized(240000);
    p.fx.slot[0]=(TsSisterFxSlotControls){TS_SISTER_FX_DELAY,1,TS_SISTER_FX_PLACE_POST,0,.35f,.4f,.3f,.3f};
    p.fx.slot[1]=(TsSisterFxSlotControls){TS_SISTER_FX_REVERB,1,TS_SISTER_FX_PLACE_POST,0,.3f,.5f,.5f,.3f};
    p.fx.fallout.enabled=1;
    ts_sister_runtime_set_parameters(&a.sister,&p);
    TsMatrixControls matrix;ts_matrix_default(&matrix);matrix.enabled=1;
    for(int node=TS_MATRIX_SISTER;node<=TS_MATRIX_FALLOUT;++node)matrix.row[node].matrix_gain[TS_MATRIX_MAIN]=0;
    matrix.row[TS_MATRIX_SISTER].matrix_gain[TS_MATRIX_EQ]=300;
    matrix.row[TS_MATRIX_MAIN].matrix_gain[TS_MATRIX_PRISM]=300;
    matrix.row[TS_MATRIX_MAIN].matrix_gain[TS_MATRIX_SISTER]=150;
    matrix.row[TS_MATRIX_PRISM].matrix_gain[TS_MATRIX_BOARD]=500;
    matrix.row[TS_MATRIX_BOARD].matrix_gain[TS_MATRIX_FALLOUT]=800;
    matrix.row[TS_MATRIX_FALLOUT].matrix_gain[TS_MATRIX_EQ]=800;
    matrix.row[TS_MATRIX_NODES].matrix_gain[TS_MATRIX_EQ]=500;
    matrix.row[TS_MATRIX_NODES].matrix_gain[TS_MATRIX_NODES+1]=500;
    assert(ts_matrix_set(&a.sister.matrix,&matrix,48000));
    ts_insert_capture_prepare(&a.sister.insert,2,48000,512);
    for(int i=0;i<8;++i)measured_callback(NULL,(Uint8*)out,sizeof(out));
}
static void external_tap_checks(void)
{
    ts_tapehead_stop();ts_note_bank_clear(&a.notes);ts_keyboard_sequence_stop(&a.keyboard_sequence);
    a.mosaic->playing=0;
    TsMatrixControls matrix;ts_matrix_default(&matrix);matrix.enabled=1;
    for(int row=0;row<TS_MATRIX_ROWS;++row)memset(matrix.row[row].matrix_gain,0,sizeof(matrix.row[row].matrix_gain));
    matrix.row[TS_MATRIX_NODES].matrix_gain[TS_MATRIX_NODES]=1000;
    matrix.row[TS_MATRIX_NODES].matrix_gain[TS_MATRIX_NODES+1]=1000;
    assert(ts_matrix_set(&a.sister.matrix,&matrix,48000));
    atomic_store(&audio_queue.enabled,1);
    TsMandalaPacket packet;TsMandalaFeatures f={0};
    for(int i=0;i<50;++i) {
        measured_callback(NULL,(Uint8*)out,sizeof(out));assert(ts_mandala_latest(&audio_queue,&packet));
        assert(packet.channels==4);ts_mandala_analyze(&packet,&f,.05f);
    }
    assert(f.level>.1f);
    ts_sister_runtime_set_master_output_gain(&a.sister,0);
    for(int i=0;i<50;++i) {measured_callback(NULL,(Uint8*)out,sizeof(out));ts_mandala_latest(&audio_queue,&packet);}
    for(unsigned i=0;i<packet.count;++i)for(unsigned ch=0;ch<packet.channels;++ch)assert(packet.samples[i*packet.channels+ch]==0);
    a.output_device_channels=2;ts_sister_runtime_set_master_output_gain(&a.sister,1);
    for(int i=0;i<50;++i) {
        measured_callback(NULL,(Uint8*)out,512*2*sizeof(float));ts_mandala_latest(&audio_queue,&packet);
    }
    assert(packet.channels==2);atomic_store(&audio_queue.enabled,0);
    puts("external-only physical output and post-fader silence in stereo/multichannel: PASS");
}
static int compare_double(const void *a0,const void *b0)
{ double a1=*(const double*)a0,b1=*(const double*)b0;return (a1>b1)-(a1<b1); }
static void report(unsigned start,unsigned end,const char *label)
{
    assert(end>start);unsigned n=end-start,miss=0,late=0;
    double *sorted=malloc(n*sizeof(double)),sum=0;assert(sorted);
    for(unsigned i=0;i<n;++i) {
        sorted[i]=timings[start+i];sum+=sorted[i];miss+=sorted[i]>10666.667;late+=arrivals[start+i]>16000;
    }
    qsort(sorted,n,sizeof(double),compare_double);
    printf("trial=%s callbacks=%u mean_us=%.3f p50_us=%.3f p95_us=%.3f p99_us=%.3f max_us=%.3f deadline_misses=%u gaps_over_16ms=%u visual_dropped=%u\n",
        label,n,sum/n,sorted[n/2],sorted[n*95/100],sorted[n*99/100],sorted[n-1],miss,late,atomic_load(&audio_queue.dropped));
    fflush(stdout);free(sorted);
}
static void lifecycle(SDL_Window *window)
{
    TsMandalaPreferences prefs={0,99,1}; /* Missing projector must recover. */
    TsMatrixControls before=a.sister.matrix.controls;
    assert(ts_mandala_open(&a.sister.mandala,&prefs,error,sizeof(error)));
    Uint32 started=SDL_GetTicks();
    while(!atomic_load(&host.shared->ready) && SDL_GetTicks()-started<5000) {ts_mandala_poll(&a.sister.mandala,&prefs);SDL_Delay(10);}
    assert(host.shared && atomic_load(&host.shared->ready));
    ts_mandala_poll(&a.sister.mandala,&prefs);assert(prefs.display==0 && !prefs.fullscreen);
    double time=a.mosaic->time;
    for(int i=0;i<20;++i){measured_callback(NULL,(Uint8*)out,sizeof(out));ts_mandala_poll(&a.sister.mandala,&prefs);SDL_Delay(5);}
    assert(a.mosaic->time>time && ts_tapehead_running() && a.keyboard_sequence.running);
    ts_mandala_freeze();ts_mandala_follow_effects(&a.sister);
    assert(ts_mandala_frozen() && !atomic_load(&audio_queue.enabled) && !atomic_load(&a.sister.snapshot.visual_enabled));
    unsigned write=atomic_load(&audio_queue.write);
    for(int i=0;i<100;++i)measured_callback(NULL,(Uint8*)out,sizeof(out));
    assert(atomic_load(&audio_queue.write)==write);
    ts_mandala_freeze();ts_mandala_follow_effects(&a.sister);
    assert(!ts_mandala_frozen() && atomic_load(&a.sister.snapshot.visual_enabled));
#ifndef _WIN32
    /* Kernel suspension is a genuine render-process stall, not a mock flag. */
    assert(!kill(host.pid,SIGSTOP));
    for(int i=0;i<1000;++i){measured_callback(NULL,(Uint8*)out,sizeof(out));ts_mandala_poll(&a.sister.mandala,&prefs);}
    assert(a.mosaic->playing && ts_tapehead_running() && a.keyboard_sequence.running);
    assert(!kill(host.pid,SIGKILL));
#else
    assert(TerminateProcess(host.process,1));
#endif
    started=SDL_GetTicks();while(host.shared && SDL_GetTicks()-started<3000){ts_mandala_poll(&a.sister.mandala,&prefs);SDL_Delay(10);}
    assert(!host.shared && !atomic_load(&audio_queue.enabled));
    assert(!memcmp(&before,&a.sister.matrix.controls,sizeof(before)));
#ifndef _WIN32
    assert(ts_mandala_open(&a.sister.mandala,&prefs,error,sizeof(error)));
    started=SDL_GetTicks();
    while(host.shared && !atomic_load(&host.shared->ready) && SDL_GetTicks()-started<5000) {ts_mandala_poll(&a.sister.mandala,&prefs);SDL_Delay(10);}
    assert(host.shared && atomic_load(&host.shared->ready));
    assert(!kill(host.pid,SIGSTOP));ts_mandala_close();
    started=SDL_GetTicks();
    while(host.shared && SDL_GetTicks()-started<3000) {
        measured_callback(NULL,(Uint8*)out,sizeof(out));ts_mandala_poll(&a.sister.mandala,&prefs);SDL_Delay(10);
    }
    assert(!host.shared); /* Timed retirement must reap a suspended child. */
#endif
    for(int repeat=0;repeat<3;++repeat) {
        assert(ts_mandala_open(&a.sister.mandala,&prefs,error,sizeof(error)));
        ts_mandala_close();
        started=SDL_GetTicks();while(host.shared && SDL_GetTicks()-started<5000){ts_mandala_poll(&a.sister.mandala,&prefs);SDL_Delay(10);}
        assert(!host.shared);
    }
    /* Actual transport UI: visual controls preserve musical state, and project
       controls participate in the application's saved-state fingerprint. */
    performance_show(&u,&a);
    uint64_t hash=paged_project_state_hash(&pages,&bank,NULL,&a.sister,&a.keyboard_sequence);
    SDL_Event e;SDL_zero(e);e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_LEFT;
    e.button.windowID=performance_transport.id;e.button.x=450;e.button.y=470;
    assert(performance_event(&e,window,0,&a,&u,&pages,&bank,NULL,48000));
    assert(hash!=paged_project_state_hash(&pages,&bank,NULL,&a.sister,&a.keyboard_sequence));
    assert(ts_tapehead_running() && a.mosaic->playing && a.keyboard_sequence.running);
    performance_update(0,&a);performance_close();
    /* Renderer event behavior, without relying on a real window manager. */
    VisualShared shared;shared_init(&shared,&prefs);
    Mandala *m=calloc(1,sizeof(*m));assert(m && m_init(m,&a.sister.mandala,0));
    e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_SPACE;e.key.repeat=0;m_event(m,&shared,&e);assert(atomic_load(&shared.freeze));
    e.key.keysym.sym=SDLK_q;m_event(m,&shared,&e);assert(atomic_load(&shared.quality)==1);
    e.key.keysym.sym=SDLK_f;m_event(m,&shared,&e);m_window_mode(m,&shared,0);assert(m->fullscreen);
    e.key.keysym.sym=SDLK_ESCAPE;m_event(m,&shared,&e);m_window_mode(m,&shared,0);assert(!m->fullscreen && !atomic_load(&shared.quit));
    atomic_store(&shared.fullscreen,1);m_window_mode(m,&shared,0);
    m_window_mode(m,&shared,1);assert(!m->fullscreen && m->display==0);
    e.type=SDL_RENDER_DEVICE_RESET;m_event(m,&shared,&e);assert(m->history && m->haze && m->veil && m->echo[3] && !atomic_load(&shared.quit));
    m_destroy(m);free(m);
    puts("native launch, freeze, stall, kill, close/reopen, transport independence, reseed dirty state, display fallback: PASS");
}
static void effect_snapshot_checks(void)
{
    TsSisterRoutingSnapshot s={0};
    s.visual_fx.value[TS_MANDALA_DISTORTION][0]=.75f;
    s.visual_fx.value[TS_MANDALA_DISTORTION][1]=.6f;
    s.prism.wet=1;s.prism.lens[0].level=1;s.prism.lens[0].pan=1;
    TsMandalaEffects e=visual_effects(&s);
    assert(e.wet[TS_MANDALA_DISTORTION]==0 && e.wet[TS_MANDALA_PRISM]==0);
    s.visual_fx_peak=s.visual_prism_peak=.2f;e=visual_effects(&s);
    assert(e.wet[TS_MANDALA_DISTORTION]==.75f && fabsf(e.size[TS_MANDALA_DISTORTION]-.8f)<1e-6f);
    assert(e.wet[TS_MANDALA_PRISM]==1 && e.size[TS_MANDALA_PRISM]>.5f);
    s.visual_fx.value[TS_SISTER_FX_TRANSIENT-1][0]=.5f;
    s.visual_fx.value[TS_SISTER_FX_TRANSIENT-1][1]=.4f;
    s.visual_fx.value[TS_SISTER_FX_TRANSIENT-1][2]=.1f;
    e=visual_effects(&s);
    assert(e.wet[TS_MANDALA_TRANSIENT]==.5f && e.wet[TS_MANDALA_PRISM]==1);
    assert(fabsf(e.size[TS_MANDALA_TRANSIENT]-.8f)<1e-6f);
    atomic_store(&a.sister.snapshot.visual_enabled,1);
    ts_sister_runtime_publish_router(&a.sister);
    assert(ts_sister_runtime_get_snapshot(&a.sister,&s));
    TsSisterFxView expected=ts_sister_post_fx_view(&a.sister.post_fx);
    assert(!memcmp(&expected,&s.visual_fx,sizeof(expected)));
    TsSisterFxView previous=s.visual_fx;
    atomic_store(&a.sister.snapshot.visual_enabled,0);
    float saved=a.sister.post_fx.master_engage.current;a.sister.post_fx.master_engage.current=0;
    ts_sister_runtime_publish_router(&a.sister);assert(ts_sister_runtime_get_snapshot(&a.sister,&s));
    assert(!memcmp(&previous,&s.visual_fx,sizeof(previous)));a.sister.post_fx.master_engage.current=saved;
    performance_transport.visible=0;performance_update(0,&a);
    assert(!atomic_load(&a.sister.snapshot.visual_enabled));
    puts("effect snapshot ownership, disabled publication and signal-activity gate: PASS");
}
static void editor_click(int x,int y,int button)
{
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=mandala_editor.id;e.button.button=(Uint8)button;e.button.x=x;e.button.y=y;
    assert(mandala_editor_event(&e,&u,&a.sister.mandala));
    e.type=SDL_MOUSEBUTTONUP;assert(mandala_editor_event(&e,&u,&a.sister.mandala));
}
static void editor_key(SDL_Keycode key,Uint16 mod)
{
    SDL_Event e={0};e.type=SDL_KEYDOWN;e.key.windowID=mandala_editor.id;e.key.keysym.sym=key;e.key.keysym.mod=mod;
    assert(mandala_editor_event(&e,&u,&a.sister.mandala));
}
static void wide_render_wait(TsMandalaPreferences *prefs,unsigned revision)
{
    float wide[512*26];
    assert(host.shared);
    unsigned frames=atomic_load(&host.shared->frames),audio=atomic_load(&host.shared->drawn_audio);
    Uint32 start=SDL_GetTicks();
    while(host.shared && SDL_GetTicks()-start<5000) {
        measured_callback(NULL,(Uint8*)wide,sizeof(wide));
        ts_mandala_follow_effects(&a.sister);ts_mandala_poll(&a.sister.mandala,prefs);
        if(host.shared && atomic_load(&host.shared->frames)>=frames+3 &&
           atomic_load(&host.shared->drawn_audio)>audio &&
           atomic_load(&host.shared->drawn_config)==revision &&
           atomic_load(&host.shared->level_milli)>0)break;
        SDL_Delay(10);
    }
    assert(host.shared && atomic_load(&host.shared->ready));
    assert(atomic_load(&host.shared->frames)>=frames+3);
    assert(atomic_load(&host.shared->drawn_audio)>audio);
    assert(atomic_load(&host.shared->drawn_config)==revision);
    assert(atomic_load(&host.shared->level_milli)>0);
    assert(host.last_audio.channels==26 && host.last_audio.count==39);
}
static void wide_live_editor_checks(void)
{
    TsMandalaConfig original=a.sister.mandala;TsMandalaPreferences prefs={0};
    unsigned channels=a.output_device_channels;double time=a.mosaic->time;
    a.output_device_channels=26;
    for(int reopen=0;reopen<2;++reopen) {
        assert(ts_mandala_open(&a.sister.mandala,&prefs,error,sizeof(error)));
        wide_render_wait(&prefs,1);
        if(!reopen) {
            mandala_editor_show(&u,&a.sister.mandala);
            unsigned revision=host.revision;
            editor_click(40,95,SDL_BUTTON_LEFT);assert(a.sister.mandala.palette==3);
            wide_render_wait(&prefs,++revision);
            editor_click(165,338,SDL_BUTTON_LEFT);assert(a.sister.mandala.shape==1);
            wide_render_wait(&prefs,++revision);
            uint64_t seed=a.sister.mandala.seed;
            editor_click(430,558,SDL_BUTTON_LEFT);assert(a.sister.mandala.seed!=seed);
            wide_render_wait(&prefs,++revision);
            mw_hide(&a.sister.mandala);mandala_editor_close();
        }
        ts_mandala_close();Uint32 start=SDL_GetTicks();
        while(host.shared && SDL_GetTicks()-start<5000) {
            ts_mandala_poll(&a.sister.mandala,&prefs);SDL_Delay(10);
        }
        assert(!host.shared);
    }
    assert(a.mosaic->time>time && ts_tapehead_running() && a.keyboard_sequence.running);
    a.output_device_channels=channels;a.sister.mandala=original;
    puts("26-channel callback, live child frames/audio, palette/shape/seed edits and reopen: PASS");
}
static void editor_checks(SDL_Window *window)
{
    TsMandalaConfig original=a.sister.mandala;TsMatrixControls routing=a.sister.matrix.controls;
    performance_show(&u,&a);
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=performance_transport.id;e.button.button=SDL_BUTTON_LEFT;e.button.x=60;e.button.y=505;
    assert(performance_event(&e,window,0,&a,&u,&pages,&bank,NULL,48000));assert(mandala_editor.visible);
    uint64_t before=paged_project_state_hash(&pages,&bank,NULL,&a.sister,&a.keyboard_sequence);
    editor_click(40,95,SDL_BUTTON_LEFT);assert(a.sister.mandala.palette==3);
    assert(before!=paged_project_state_hash(&pages,&bank,NULL,&a.sister,&a.keyboard_sequence));
    editor_click(260,95,SDL_BUTTON_LEFT);assert(a.sister.mandala.palette==4);
    editor_click(165,338,SDL_BUTTON_LEFT);assert(a.sister.mandala.shape==1);
    editor_click(170,415,SDL_BUTTON_LEFT);assert(a.sister.mandala.density==55);
    editor_key(SDLK_z,KMOD_CTRL);assert(a.sister.mandala.density==100);
    editor_key(SDLK_y,KMOD_CTRL);assert(a.sister.mandala.density==55);
    editor_click(170,415,SDL_BUTTON_RIGHT);assert(a.sister.mandala.density==100);
    editor_click(190,155,SDL_BUTTON_LEFT);assert(mandala_editor.swatch==2);
    editor_click(490,234,SDL_BUTTON_LEFT);editor_key(SDLK_a,KMOD_CTRL);
    e=(SDL_Event){0};e.type=SDL_TEXTINPUT;e.text.windowID=mandala_editor.id;snprintf(e.text.text,sizeof(e.text.text),"AA33CC");
    assert(mandala_editor_event(&e,&u,&a.sister.mandala));editor_key(SDLK_RETURN,0);
    assert(a.sister.mandala.palette==TS_MANDALA_CUSTOM && ts_mandala_color(&a.sister.mandala,2)==0xAA33CC);
    editor_click(40,65,SDL_BUTTON_LEFT);editor_click(480,95,SDL_BUTTON_LEFT);
    assert(ts_mandala_color(&a.sister.mandala,2)==0xAA33CC); /* Custom survives preset visits. */
    editor_click(100,202,SDL_BUTTON_LEFT);assert((ts_mandala_color(&a.sister.mandala,2)>>16)==0);
    editor_key(SDLK_z,KMOD_CTRL);assert(ts_mandala_color(&a.sister.mandala,2)==0xAA33CC);
    editor_click(300,558,SDL_BUTTON_LEFT);assert(!memcmp(&a.sister.mandala,&original,sizeof(original)));
    editor_key(SDLK_z,KMOD_CTRL);assert(a.sister.mandala.palette==TS_MANDALA_CUSTOM);
    assert(!memcmp(&routing,&a.sister.matrix.controls,sizeof(routing)));
    assert(ts_tapehead_running() && a.mosaic->playing && a.keyboard_sequence.running);
    TsMandalaConfig replacement;ts_mandala_default(&replacement);replacement.palette=3;a.sister.mandala=replacement;
    mandala_editor_update(&a.sister.mandala);editor_key(SDLK_z,KMOD_CTRL);
    assert(!memcmp(&replacement,&a.sister.mandala,sizeof(replacement))); /* New project clears prior undo. */
    editor_click(260,95,SDL_BUTTON_LEFT);editor_key(SDLK_ESCAPE,0);assert(!mandala_editor.visible && a.sister.mandala.palette==4);
    mandala_editor_show(&u,&a.sister.mandala);editor_click(40,95,SDL_BUTTON_LEFT);editor_click(300,558,SDL_BUTTON_LEFT);
    assert(a.sister.mandala.palette==4);mw_hide(&a.sister.mandala);mandala_editor_close();performance_close();a.sister.mandala=original;

    Mandala *m=calloc(1,sizeof(*m));assert(m && m_init(m,&original,0));
    unsigned count=m->count;Uint64 rng=m->rng;
    TsMandalaConfig edited=original;edited.palette=3;edited.density=33;edited.trails=100;edited.motion=0;
    m_configure(m,&edited);assert(m->count==count && m->rng==rng);
    for(unsigned shape=0;shape<TS_MANDALA_SHAPES;++shape) {
        edited.shape=shape;m_configure(m,&edited);assert(m->count<=M_INSTANCES);
        assert(m->motifs[0].count==((int[]){5,4,5,25,7})[shape]);
        for(int k=0;k<M_MOTIFS;++k)assert(m->motifs[k].count<=M_POINTS);
        m->features=(TsMandalaFeatures){.8f,.8f,.5f,0,.1f};m_scene(m,1.0/15);
        assert(m->motion_time==0 && m->time>0);
    }
    m_destroy(m);free(m);
    puts("editor presets, custom RGB/hex, shapes, undo/revert, project replacement and bounded renderer: PASS");
}
#include "mandala_interaction_fixture.inc"
int main(int argc,char **argv)
{
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    SDL_setenv("SDL_RENDER_DRIVER","software",1);SDL_setenv("SDL_FRAMEBUFFER_ACCELERATION","0",1);
    assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_TIMER));
    SDL_Window *window=SDL_CreateWindow("Mandala host fixture",0,0,640,400,0);assert(window);
    setup(window);
    if(argc>2 && !strcmp(argv[1],"--interact")) {
        interaction_run(argv[2],argc>3?(unsigned)atoi(argv[3]):20,
            argc>4?atoi(argv[4]):10,argc>5?atoi(argv[5]):1);
    } else if(argc>2 && !strcmp(argv[1],"--editor-capture")) {
        a.sister.mandala.palette=4;a.sister.mandala.shape=1;
        mandala_editor_show(&u,&a.sister.mandala);mandala_editor_update(&a.sister.mandala);
        SDL_Surface *surface=SDL_CreateRGBSurfaceFrom(mandala_editor.pixels,MW_W,MW_H,32,MW_W*4,0xff0000,0xff00,0xff,0);
        assert(surface && !SDL_SaveBMP(surface,argv[2]));SDL_FreeSurface(surface);mandala_editor_close();
    } else if(argc>1 && !strcmp(argv[1],"--soak")) {
        unsigned seconds=argc>2?(unsigned)atoi(argv[2]):240;
        if(argc>3 && !strcmp(argv[3],"--effects")) {
            TsSisterParameters p=a.sister.parameters;
            p.fx.slot[2]=(TsSisterFxSlotControls){TS_SISTER_FX_DISTORTION,1,TS_SISTER_FX_PLACE_POST,0,.65f,.5f,0,.3f};
            p.fx.slot[3]=(TsSisterFxSlotControls){TS_SISTER_FX_GRAIN,1,TS_SISTER_FX_PLACE_POST,0,.65f,.4f,.5f,.3f};
            ts_sister_runtime_set_parameters(&a.sister,&p);
            ts_sister_post_fx_sync_controls(&a.sister.post_fx,&p.fx);
            puts("effect comparison: four settled FX slots plus Prism; same DSP in every visual mode");
        }
        assert(seconds>=2 && seconds<=300);
        SDL_AudioSpec want={0},got={0};want.freq=48000;want.format=AUDIO_F32SYS;want.channels=4;want.samples=512;want.callback=measured_callback;
        SDL_AudioDeviceID device=ts_native_open(NULL,0,&want,&got,0);assert(device && got.samples==512);
        ts_native_pause(device,0);
        TsMandalaPreferences prefs={0};
        const int modes[]={-1,0,1,1,0,-1};
        for(int trial=0;trial<6;++trial) {
            if(modes[trial]>=0) {
                prefs.quality=modes[trial];
                if(!host.shared)assert(ts_mandala_open(&a.sister.mandala,&prefs,error,sizeof(error)));
                else atomic_store(&host.shared->quality,(unsigned)modes[trial]);
            } else ts_mandala_close();
            Uint32 began=SDL_GetTicks();unsigned start=atomic_load(&timing_count);
            while(SDL_GetTicks()-began<(seconds+2)*1000u) {
                ts_mandala_follow_effects(&a.sister);ts_mandala_poll(&a.sister.mandala,&prefs);SDL_Delay(20);
                if(SDL_GetTicks()-began<2000)start=atomic_load_explicit(&timing_count,memory_order_acquire);
            }
            unsigned end=atomic_load_explicit(&timing_count,memory_order_acquire);
            report(start,end,modes[trial]<0?"OFF":modes[trial]==0?"ECONOMY":"STANDARD");
        }
        ts_native_pause(device,1);ts_native_close(device);
        printf("soak transport status: tracker=%d mosaic=%d arp=%d time=%.2f\n",ts_tapehead_running(),a.mosaic->playing,a.keyboard_sequence.running,a.mosaic->time);
    } else {editor_checks(window);wide_live_editor_checks();lifecycle(window);external_tap_checks();effect_snapshot_checks();}
    ts_mandala_shutdown();ts_tapehead_stop();ts_tapehead_close();
    ts_keyboard_sequence_source_free(ts_keyboard_sequence_source(&a.keyboard_sequence,NULL));
    ts_sister_runtime_free(&a.sister);ts_mosaic_free(a.mosaic);ts_capture_free(&a.capture);
    ts_instrument_free(&bank);ts_sample_pages_free(&pages);SDL_DestroyWindow(window);SDL_Quit();return 0;
}
