#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>

static void silence(void *unused, Uint8 *data, int bytes)
{ (void)unused; memset(data, 0, bytes); }

static void named_edit_for_health(AudioState *audio)
{
    lock_edit(TS_AUDIO_LOGICAL_OUTPUT, audio);
    SDL_Delay(1);
    ts_audio_unlock_device(TS_AUDIO_LOGICAL_OUTPUT);
}

static int publish(void *data)
{
    TsRealtimeDiagnostics *d = data;
    for (unsigned i = 0; i < 100000; ++i) {
        unsigned rate = (i / 25) % 2 ? 48000 : 96000;
        ts_realtime_diagnostics_record_timed(d, (uint64_t)i * 10000,
            2500, 1000000, rate, rate / 100, 0);
    }
    return 0;
}

static atomic_int profile_done;
static int publish_effect_profiles(void *unused)
{
    (void)unused;
    for(uint32_t i=1;i<=100000;++i) {
        ts_profile_lane_begin(1);
        TsProfileEffectIdentity id={.generation=i,.id=i,.patch=i*3,.type=3,.quantum=i*2};
        uint64_t start=ts_profile_effect_begin(15);
        ts_profile_effect_end(15,&id,start,1);ts_profile_lane_end(1);
    }
    atomic_store(&profile_done,1);return 0;
}
static void check_concurrent_profiles(void)
{
    ts_profile_enable(1);atomic_store(&profile_done,0);
    SDL_Thread *thread=SDL_CreateThread(publish_effect_profiles,"effect profiles",NULL);assert(thread);
    unsigned reads=0;
    do {
        TsProfileSnapshot s;ts_profile_snapshot(&s);const TsProfileEffectValue *v=&s.effect[15];
        if(v->samples) {
            assert(v->identity.id==v->identity.generation);
            assert(v->identity.patch==v->identity.generation*3);
            assert(v->identity.quantum==v->identity.generation*2);
            assert(v->samples==1 && v->transitions==1 && v->blocks.calls==32);
            assert(v->blocks.ticks==v->blocks.peak*32);
        }
        if(++reads%200==0)ts_profile_reset();
    } while(!atomic_load(&profile_done));
    SDL_WaitThread(thread,NULL);ts_profile_enable(0);
}

static void check_cpu_readout(AudioState *a,TsUiState *ui,SDL_Window *window)
{
    TsRealtimeDiagnosticsSnapshot s={.completed_ticks=1000,.counter_frequency=1000,
        .recent_callback_count=25,.recent_load_percent=26.5};
    assert(ts_audio_cpu_load(&s,1100,1,1)==26.5);
    assert(ts_audio_cpu_load(&s,2000,1,1)<0);
    assert(ts_audio_cpu_load(&s,1100,0,1)<0);
    assert(ts_audio_cpu_load(&s,1100,1,0)<0);
    s.recent_callback_count=0;assert(ts_audio_cpu_load(&s,1100,1,1)<0);
    char label[24];ts_ui_cpu_label(label,sizeof(label),-1);assert(!strcmp(label,"CPU --"));
    ts_ui_cpu_label(label,sizeof(label),26.5);assert(!strcmp(label,"CPU 26.5%"));
    ts_ui_cpu_label(label,sizeof(label),125);assert(!strcmp(label,"CPU 125.0%"));
    ts_ui_cpu_label(label,sizeof(label),1200);assert(!strcmp(label,"CPU >999%"));

    TsProfileSnapshot profile={.frequency=1000,.seconds=10,.epoch=1};
    profile.effect[0].identity.generation=1;profile.effect[0].blocks.ticks=100;
    ts_audio_profile_recent(&profile);assert(!ts_health.recent_profile_ready);
    profile.seconds=11;profile.effect[0].blocks.ticks=335;
    ts_audio_profile_recent(&profile);assert(fabs(ts_health.effect_cpu[0]-23.5)<.001);
    profile.seconds=12;ts_audio_profile_recent(&profile);assert(ts_health.effect_cpu[0]==0);
    TsProfileSnapshot missed=profile;missed.seconds=13;memset(missed.effect,0,sizeof(missed.effect));
    ts_audio_profile_recent(&missed);assert(ts_health.previous_profile.seconds==12);
    profile.seconds=12.25;profile.effect[0].identity.generation=2;profile.effect[0].blocks.ticks=10;
    ts_audio_profile_recent(&profile);assert(ts_health.effect_cpu[0]<0);
    profile.seconds=13;profile.effect[0].blocks.ticks=120;
    ts_audio_profile_recent(&profile);assert(ts_health.effect_cpu[0]==12);
    ++profile.epoch;profile.seconds=20;ts_audio_profile_recent(&profile);
    assert(!ts_health.recent_profile_ready); /* Reset even if elapsed time is larger. */

    SDL_Event click;SDL_zero(click);click.type=SDL_MOUSEBUTTONDOWN;
    click.button.windowID=SDL_GetWindowID(window);click.button.button=SDL_BUTTON_LEFT;
    click.button.x=40;click.button.y=390;
    assert(ts_audio_health_event(&click,window,a,ui));
    assert(ts_health.visible && ts_health.profile_effects && ts_profile_enabled() && ts_health.owns_profile);
    ts_audio_health_hide();assert(!ts_profile_enabled());
    SDL_SetWindowSize(window,1280,800);click.button.x=80;click.button.y=780;ui->mosaic_open=1;
    assert(ts_audio_health_event(&click,window,a,ui));assert(ts_health.profile_effects);
    ts_audio_health_hide();ui->mosaic_open=0;
    ui->tracker_open=1;assert(!ts_audio_health_event(&click,window,a,ui));ui->tracker_open=0;
    ui->config_open=1;assert(!ts_audio_health_event(&click,window,a,ui));ui->config_open=0;
    ts_profile_enable(1);TsProfileSnapshot before;ts_profile_snapshot(&before);
    assert(ts_audio_health_event(&click,window,a,ui));assert(!ts_health.owns_profile);
    TsProfileSnapshot after;ts_profile_snapshot(&after);assert(before.epoch==after.epoch);
    ts_audio_health_hide();assert(ts_profile_enabled());ts_profile_enable(0);
    matrix_window.layout=&matrix_window.full_layout;
    assert(fx_event(&click,window,0,a,ui,NULL,NULL,1100,820,0));
    matrix_window.layout=NULL;
    assert(ts_health.visible && ts_health.profile_effects && ts_health.owns_profile);
    ts_audio_health_close();assert(!ts_profile_enabled());
    SDL_SetWindowSize(window,640,400);
    if(SDL_getenv("TAPESISTER_CPU_SCREENSHOTS")) {
        TsInstrument *bank=calloc(1,sizeof(*bank));TsFramebuffer *fb=calloc(1,sizeof(*fb));
        assert(bank && fb);ts_instrument_init(bank);ui->audio_cpu_percent=26.5;
        ts_ui_render(fb,ui,bank);ts_ui_render_file_recording(fb,ui);
        assert(ts_ui_write_ppm(fb,"cpu-main.ppm"));
        ui->mosaic=ts_mosaic_create();assert(ui->mosaic);ui->mosaic_open=1;
        ts_ui_render(fb,ui,bank);ts_ui_render_file_recording(fb,ui);
        assert(ts_ui_write_ppm(fb,"cpu-mosaic.ppm"));
        ui->file_record_state=TS_PERFORMANCE_FILE_RECORDING;ui->file_record_rate=48000;
        ts_ui_render(fb,ui,bank);ts_ui_render_file_recording(fb,ui);
        assert(ts_ui_write_ppm(fb,"cpu-recording.ppm"));
        ui->file_record_state=TS_PERFORMANCE_FILE_IDLE;
        ts_mosaic_free(ui->mosaic);ui->mosaic=NULL;ui->mosaic_open=0;
        ts_instrument_free(bank);free(bank);free(fb);
    }
}


#include "test_audio_health_layout.inc"

int main(int argc, char **argv)
{
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    /* Dummy windows have no native GPU surface. Keep both the renderer and its
       backing framebuffer in software, independent of the CI host's drivers. */
    assert(SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "software", SDL_HINT_OVERRIDE));
    assert(SDL_SetHintWithPriority(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0", SDL_HINT_OVERRIDE));
    assert(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER));
    AudioState *a = calloc(1, sizeof(*a));
    TsUiState *ui = calloc(1, sizeof(*ui)); assert(a && ui);
    ts_ui_init(ui);
    check_health_recent_events(ui,a);
    ts_realtime_diagnostics_init(&a->realtime_diagnostics);
    a->realtime_diagnostics_enabled = 1;
    a->realtime_counter_frequency = SDL_GetPerformanceFrequency();
    a->audio_health_started = SDL_GetTicks();
    SDL_Window *main_window = SDL_CreateWindow("main", 0, 0, 640, 400, 0);
    assert(main_window);
    assert(window_drawable(main_window));SDL_HideWindow(main_window);
    assert(!window_drawable(main_window));SDL_ShowWindow(main_window);
    assert(window_drawable(main_window));
    SDL_Renderer *damage_renderer=SDL_CreateRenderer(main_window,-1,SDL_RENDERER_SOFTWARE);assert(damage_renderer);
    SDL_Texture *damage_texture=SDL_CreateTexture(damage_renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,640,400);assert(damage_texture);
    TsFramebuffer *damage_frame=calloc(1,sizeof(*damage_frame));uint32_t *damage_previous=calloc(640*400,sizeof(uint32_t));assert(damage_frame && damage_previous);
    int damage_valid=0;
    assert(update_texture_damage(damage_texture,damage_frame,damage_previous,&damage_valid)==1);
    assert(update_texture_damage(damage_texture,damage_frame,damage_previous,&damage_valid)==2);
    damage_frame->pixels[20]=0xffffffffu;
    assert(update_texture_damage(damage_texture,damage_frame,damage_previous,&damage_valid)==1);
    damage_valid=0;assert(update_texture_damage(damage_texture,damage_frame,damage_previous,&damage_valid)==1);
    SDL_DestroyTexture(damage_texture);SDL_DestroyRenderer(damage_renderer);free(damage_frame);free(damage_previous);
    ts_profile_init(SDL_GetPerformanceCounter,SDL_GetPerformanceFrequency());
    SDL_AudioSpec want = {0}, got = {0};
    want.freq = 48000; want.format = AUDIO_F32SYS; want.channels = 2;
    want.samples = 256; want.callback = silence;
    ts_real_output = ts_native_open(NULL, 0, &want, &got, 0); assert(ts_real_output);
    ts_output_userdata = a;
    /* A freshly opened report must retain the age of the actual callback,
       instead of treating an old count as new progress. No sleeps needed. */
    char progress[512];
    ts_audio_health_sample(a,SDL_GetTicks());
    ts_audio_health_progress_report(a,progress,sizeof(progress));
    assert(strstr(progress,"no timed completion observed"));
    uint64_t frequency=SDL_GetPerformanceFrequency(),now=SDL_GetPerformanceCounter();
    ts_realtime_diagnostics_record_timed(&a->realtime_diagnostics,now,1,frequency,48000,256,0);
    ts_audio_health_sample(a,SDL_GetTicks());
    assert(fabs(ts_audio_health_callback_age(&ts_health.snapshot,now+2*frequency)-2)<.001);
    ts_audio_health_progress_report(a,progress,sizeof(progress));
    assert(strstr(progress,"Callback progress: recent"));
    ts_audio_lock_device_at(TS_AUDIO_LOGICAL_OUTPUT,"unfinished Mosaic edit");
    ts_audio_lock_device_at(TS_AUDIO_LOGICAL_OUTPUT,"nested edit");
    ts_audio_health_progress_report(a,progress,sizeof(progress));
    assert(strstr(progress,"Current UI audio lock: HELD; depth 2") && strstr(progress,"unfinished Mosaic edit"));
    ts_audio_unlock_device(TS_AUDIO_LOGICAL_OUTPUT);ts_audio_unlock_device(TS_AUDIO_LOGICAL_OUTPUT);
    ts_audio_health_progress_report(a,progress,sizeof(progress));
    assert(strstr(progress,"not held by this UI thread"));
    ts_realtime_diagnostics_reset(&a->realtime_diagnostics);
    ts_audio_endpoints_init();
    ts_audio_endpoint_configure(&ts_output_endpoint, "");
    ts_audio_endpoint_begin_open(&ts_output_endpoint, 0);
    ts_audio_endpoint_opened(&ts_output_endpoint, ts_real_output, "Test output", 0,
        got.freq, got.channels, got.samples, "F32");
    ui->config.audio_buffer_frames = 2048; /* Display must use the obtained size. */
    /* Idle Matrix maintenance must not contend with audio once per UI tick. */
    ts_matrix_effects_init(&a->sister.matrix_effects);
    for(int i=0;i<100;++i)mx_collect_effects(TS_AUDIO_LOGICAL_OUTPUT,a);
    assert(atomic_load(&a->realtime_diagnostics.control_count)==0);
    TsMatrixControls lifecycle;ts_matrix_default(&lifecycle);
    assert(ts_matrix_effect_add(&lifecycle,TS_MATRIX_FX_COMPRESSOR)==0);
    assert(ts_matrix_effects_restore(&a->sister.matrix_effects,&lifecycle,48000));
    ts_matrix_effect_remove(&lifecycle,0);
    TsMatrixEffectPlan retirement;
    assert(ts_matrix_effects_stage(&a->sister.matrix_effects,&lifecycle,48000,&retirement));
    ts_matrix_effects_commit(&a->sister.matrix_effects,&lifecycle,48000,&retirement);
    assert(a->sister.matrix_effects.previous[0]);
    mx_collect_effects(TS_AUDIO_LOGICAL_OUTPUT,a);
    assert(a->sister.matrix_effects.previous[0]);
    assert(atomic_load(&a->realtime_diagnostics.control_count)==0);
    for(int i=0;i<576;++i)ts_matrix_effects_tick(&a->sister.matrix_effects);
    mx_collect_effects(TS_AUDIO_LOGICAL_OUTPUT,a);
    assert(!a->sister.matrix_effects.previous[0]);
    assert(atomic_load(&a->realtime_diagnostics.control_count)==1);
    ts_realtime_diagnostics_reset(&a->realtime_diagnostics);
    ts_audio_lock_device_at(TS_AUDIO_LOGICAL_OUTPUT, "outer edit");
    ts_audio_lock_device_at(TS_AUDIO_LOGICAL_OUTPUT, "nested edit");
    ts_audio_unlock_device(TS_AUDIO_LOGICAL_OUTPUT);
    assert(atomic_load(&a->realtime_diagnostics.control_count) == 0);
    SDL_Delay(1); /* Ensure a nonzero hold on low-resolution platform clocks. */
    ts_audio_unlock_device(TS_AUDIO_LOGICAL_OUTPUT);
    TsRealtimeDiagnosticsSnapshot s;
    ts_realtime_diagnostics_get(&a->realtime_diagnostics, &s);
    assert(s.control_count == 1 && !strcmp(s.control_worst_site, "outer edit"));
    ts_realtime_diagnostics_reset(&a->realtime_diagnostics);
    named_edit_for_health(a);
    ts_realtime_diagnostics_get(&a->realtime_diagnostics, &s);
    assert(s.control_count == 1 && !strcmp(s.control_worst_site, "named_edit_for_health"));

    check_cpu_readout(a,ui,main_window);
    SDL_Event e;SDL_zero(e); e.type = SDL_KEYDOWN;
    e.key.windowID = SDL_GetWindowID(main_window); e.key.keysym.sym = SDLK_F12;
    int handled = ts_audio_health_event(&e, main_window, a, ui);
    if (!ts_health.visible)
        fprintf(stderr, "Audio Health open failed: %s; SDL: %s\n", ui->status, SDL_GetError());
    assert(handled && ts_health.visible);
    SDL_RendererInfo renderer_info;
    assert(!SDL_GetRendererInfo(ts_health.renderer, &renderer_info));
    assert(renderer_info.flags & SDL_RENDERER_SOFTWARE);
    SDL_Event profile_event;SDL_zero(profile_event);profile_event.type=SDL_MOUSEBUTTONDOWN;
    profile_event.button.windowID=ts_health.window_id;profile_event.button.button=SDL_BUTTON_LEFT;
    health_button_position(&profile_event,TS_HEALTH_BUTTON_PROFILE);
    check_health_button_gaps(main_window,a,ui);
    assert(ts_audio_health_event(&profile_event,main_window,a,ui));assert(ts_profile_enabled());
    ts_audio_health_update(a,ui);
    if (SDL_getenv("TAPESISTER_PROFILE_SCREENSHOT")) {
        SDL_Surface *surface=SDL_CreateRGBSurfaceFrom(ts_health.framebuffer->pixels,640,400,32,640*4,0x00ff0000u,0x0000ff00u,0x000000ffu,0xff000000u);
        assert(surface);assert(!SDL_SaveBMP(surface,SDL_getenv("TAPESISTER_PROFILE_SCREENSHOT")));SDL_FreeSurface(surface);
    }
    /* The secondary view uses the same logical window coordinates and never
       alters the instrument. Exercise every slot over complete sampling cycles:
       effect timings now have staggered phases, not one shared callback. */
    TsProfileEffectIdentity identities[TS_MATRIX_EFFECT_CAPACITY];
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i) {
        TsSunVoxControls c;ts_sunvox_default(&c);
        if(i<3)assert(ts_sunvox_select(&c,i));
        identities[i]=(TsProfileEffectIdentity){.generation=(uint64_t)i+1,.id=(uint32_t)i+1,
            .type=i<3?TS_MATRIX_FX_SUNVOX:TS_SISTER_FX_GRAIN,.patch=c.patch,.quantum=256};
    }
    for(int block=0;block<64;++block) {
        ts_profile_lane_begin(1);
        for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i) {
            uint64_t start=ts_profile_effect_begin(i);
            ts_profile_effect_end(i,&identities[i],start,0);
        }
        ts_profile_lane_end(1);
    }
    TsProfileSnapshot effect_snapshot;ts_profile_snapshot(&effect_snapshot);
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i) {
        assert(effect_snapshot.effect[i].samples>0);
        assert(effect_snapshot.effect[i].identity.id==(uint32_t)i+1);
    }
    SDL_Event effects_event=profile_event;health_button_position(&effects_event,TS_HEALTH_BUTTON_EFFECTS);
    assert(ts_audio_health_event(&effects_event,main_window,a,ui));assert(ts_health.profile_effects);
    ts_audio_health_update(a,ui);
    if(SDL_getenv("TAPESISTER_MATRIX_PROFILE_SCREENSHOT")) {
        SDL_Surface *surface=SDL_CreateRGBSurfaceFrom(ts_health.framebuffer->pixels,640,400,32,640*4,0x00ff0000u,0x0000ff00u,0x000000ffu,0xff000000u);
        assert(surface);assert(!SDL_SaveBMP(surface,SDL_getenv("TAPESISTER_MATRIX_PROFILE_SCREENSHOT")));SDL_FreeSurface(surface);
    }
    check_health_profile_scroll(main_window,a,ui);
    SDL_Event profile_copy=profile_event;health_button_position(&profile_copy,TS_HEALTH_BUTTON_COPY);
    assert(ts_audio_health_event(&profile_copy,main_window,a,ui));
    char *profile_report=SDL_GetClipboardText();
    assert(strstr(profile_report,"#16 GRAIN") && strstr(profile_report,"peak ms/block"));SDL_free(profile_report);
    assert(ts_audio_health_event(&effects_event,main_window,a,ui));assert(!ts_health.profile_effects);
    assert(ts_audio_health_event(&profile_event,main_window,a,ui));assert(!ts_profile_enabled());
    for (unsigned i = 0; i < 25; ++i)
        ts_realtime_diagnostics_record_timed(&a->realtime_diagnostics, i * 10000u,
            2500, 1000000, 48000, 480, 0);
    ts_audio_health_update(a, ui);
    char report[32768]; ts_audio_health_report(a, report, sizeof(report));
    assert(strstr(report, "48000 Hz, 256 frames, 2 channels"));
    assert(strstr(report, "Build: " TAPESISTER_BUILD_MARKER));
    assert(strstr(report, "Longest hold call site: named_edit_for_health"));
    assert(strstr(report, "not exposed by this backend"));
    assert(strstr(report, "Recent processing: 25.00%"));
    assert(strstr(report, "not round-trip latency"));
    assert(strstr(report, "Active setup: awaiting UI snapshot"));
    /* Copy uses the same scaled coordinate mapping as the resizable window. */
    e.type = SDL_MOUSEBUTTONDOWN; e.button.windowID = ts_health.window_id;
    e.button.button = SDL_BUTTON_LEFT; health_button_position(&e,TS_HEALTH_BUTTON_COPY);
    assert(ts_audio_health_event(&e, main_window, a, ui));
    char *copied = SDL_GetClipboardText(); assert(strstr(copied, "TapeSister Audio Health")); SDL_free(copied);
    ts_audio_health_update(a,ui); /* Include action feedback in the optional screenshot. */
    e.type = SDL_MOUSEWHEEL; e.wheel.windowID = ts_health.window_id; e.wheel.y = 1;
    assert(ts_audio_health_event(&e, main_window, a, ui));
    e.type = SDL_KEYDOWN; e.key.windowID = ts_health.window_id; e.key.keysym.sym = SDLK_z;
    assert(ts_audio_health_event(&e, main_window, a, ui));
    ts_note_bank_init(&a->notes);
    TsSample sample; ts_sample_init(&sample);
    sample.frames = 128; sample.channels = 1; sample.sample_rate = 48000;
    sample.data = calloc(sample.frames, sizeof(float)); assert(sample.data);
    TsTuning tuning = {60, 0}; TsNoteEvent note;
    assert(ts_note_event_qwerty(&note, note_for_key(SDLK_z), 60));
    assert(ts_note_bank_start_sample_event(&a->notes, &sample, &tuning, &note, 0, 48000) == TS_NOTE_STARTED);
    e.type = SDL_KEYUP;
    assert(ts_audio_health_event(&e, main_window, a, ui));
    assert(ts_note_bank_count(&a->notes) == 0); /* No stuck key across focus. */
    ts_note_bank_set_sustain(&a->notes, 1);
    assert(ts_note_bank_start_sample_event(&a->notes, &sample, &tuning, &note, 0, 48000) == TS_NOTE_STARTED);
    assert(ts_audio_health_event(&e, main_window, a, ui));
    assert(ts_note_bank_count(&a->notes) == 1); /* HOLD remains intentional. */
    /* Reports describe simultaneous workload and effective Router bypass,
       including processors missing from the old callback bitfield. */
    ui->keyboard_hold = 1;
    a->keyboard_sequence.running = a->keyboard_sequence.slot_running = 1;
    a->keyboard_sequence.active_slot = 4;
    a->sister.enabled = a->sister.rolling = a->sister.monitor_enabled = 1;
    a->sister.source_switches = TS_SISTER_SOURCE_TILES | TS_SISTER_SOURCE_FM;
    ts_router_init(&a->sister.router);
    for (int i = 0; i < TS_ROUTER_COUNT; ++i) a->sister.router.wet[i] = 1;
    ts_sister_fx_controls_default(&a->sister.parameters.fx);
    a->sister.parameters.fx.enabled = a->sister.parameters.fx.fallout.enabled = 1;
    a->sister.parameters.fx.slot[0] = (TsSisterFxSlotControls){
        .type = TS_SISTER_FX_DELAY, .enabled = 1,
        .placement = TS_SISTER_FX_PLACE_H1 | TS_SISTER_FX_PLACE_POST, .mix = .4f};
    a->sister.parameters.fx.slot[1] = (TsSisterFxSlotControls){
        .type = TS_SISTER_FX_REVERB, .enabled = 0, .placement = TS_SISTER_FX_PLACE_POST};
    a->sister.parameters.prism.enabled = 1;
    a->sister.parameters.prism.lenses = 12;
    a->sister.prism.matrix.running = 1;
    a->tracker.lanes[0].voice.active=1;
    a->mosaic=ts_mosaic_create();assert(a->mosaic);
    char mosaic_error[160];
    TsMosaicSource *source=ts_mosaic_source(a->mosaic,&sample,mosaic_error,sizeof(mosaic_error));assert(source);
    for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        TsMosaicEvent *card=ts_mosaic_add(a->mosaic,source,i<3?0:10,0);assert(card);
        card->duration=2;card->note_count=i<3?i+1:1;
    }
    a->mosaic->events[1].muted=1;a->mosaic->events[2].looping=0;
    a->mosaic->playing=1;a->mosaic->time=1; /* One-shot audio has already ended. */
    ts_audio_health_capture_setup(a, ui, 1000);
    assert(ts_health.setup.tracker_voices==1);
    char setup[3072]; ts_audio_health_setup_report(setup, sizeof(setup), 1020);
    assert(strstr(setup, "Keyboard HOLD: ON; keyboard voice instances: 1; latched/sustained: 1"));
    assert(strstr(setup, "ARP: RUNNING; outer ARP: RUNNING; active slot: 5"));
    assert(strstr(setup,"Mosaic cards: 1024 total; 3 at playhead; 6 assigned notes"));
    assert(strstr(setup,"Mosaic position: 1.000000 s; repeat: OFF"));
    assert(strstr(setup,"Mosaic switches: 1 muted; 0 solo"));
    assert(strstr(setup, "Sister Machine: ON, routed"));
    assert(strstr(setup, "Sister source switches: Tiles, FM"));
    assert(strstr(setup, "Fallout: ON, routed; FX master: ON"));
    assert(strstr(setup, "Prism: ON, routed") && strstr(setup, "base lenses: 12"));
    assert(strstr(setup, "Matrix: RUNNING"));
    assert(strstr(setup, "mix 40.0%; placement H1+POST"));
    assert(strstr(setup, "Slot 2: REVERB; OFF"));
    assert(strstr(setup, "send assigned: OFF; send available: OFF"));
    a->sister.router.controls.bypass_mask = 1u << TS_ROUTER_PRISM;
    a->sister.router.wet[TS_ROUTER_PRISM] = 0;
    a->sister.parameters.fx.enabled = 0;
    a->mosaic->playing=0;
    ts_audio_health_capture_setup(a, ui, 1100); /* Bounded refresh; no live-state reads in report. */
    ts_audio_health_setup_report(setup, sizeof(setup), 1100);
    assert(strstr(setup, "Prism: ON, routed"));
    assert(strstr(setup,"3 at playhead; 6 assigned notes"));
    ts_audio_health_capture_setup(a, ui, 1250);
    assert(ts_health.setup.tracker_voices==1);
    ts_audio_health_setup_report(setup, sizeof(setup), 1250);
    assert(strstr(setup, "Prism: ON, bypassed"));
    assert(strstr(setup, "Fallout: ON, routed; FX master: OFF"));
    assert(strstr(setup, "Pedalboard master: OFF"));
    assert(strstr(setup,"1024 total; 0 at playhead; 0 assigned notes"));
    ts_audio_health_report(a, report, sizeof(report));
    assert(strstr(report, "Slot 4:") && strstr(report, "Native counters cover"));
    a->keyboard_sequence.running = a->keyboard_sequence.slot_running = 0;
    a->sister.enabled = a->sister.parameters.prism.enabled = 0;
    a->sister.prism.matrix.running = 0;
    ts_audio_health_capture_setup(a, ui, 1500);
    ts_audio_health_setup_report(setup, sizeof(setup), 1500);
    assert(strstr(setup, "ARP: STOPPED; outer ARP: STOPPED; active slot: 0"));
    assert(strstr(setup, "Sister Machine: OFF") && strstr(setup, "Prism: OFF"));
    assert(strstr(setup, "Matrix: STOPPED"));
    TsMatrixControls *matrix=&a->sister.matrix.controls;
    ts_matrix_default(matrix);matrix->enabled=1;
    int grain=ts_matrix_effect_add(matrix,TS_SISTER_FX_GRAIN);
    int comp=ts_matrix_effect_add(matrix,TS_MATRIX_FX_COMPRESSOR);
    assert(grain==0 && comp==1);
    matrix->row[TS_MATRIX_EFFECT_BASE+grain].matrix_gain[TS_MATRIX_EFFECT_BASE+comp]=500;
    matrix->row[TS_MATRIX_MOSAIC].matrix_gain[TS_MATRIX_EFFECT_SC_BASE+comp]=1000;
    ts_audio_health_capture_setup(a,ui,1750);
    char matrix_report[8192];ts_audio_health_setup_report(matrix_report,sizeof(matrix_report),1750);
    assert(strstr(matrix_report,"GRAIN #1 -> COMPRESSOR #2"));
    assert(strstr(matrix_report,"MOSAIC -> COMPRESSOR #2 SIDECHAIN"));
    assert(strstr(matrix_report,"Matrix effect #1: GRAIN; ON; mix 100.0%"));
    assert(!strstr(matrix_report,"OUTPUT -> OUTPUT"));
    assert(strstr(matrix_report,"Matrix MAIN MIX headroom: FLOAT"));
    matrix->serial_mask|=1u<<TS_MATRIX_MAIN;
    ts_audio_health_capture_setup(a,ui,2050);ts_audio_health_setup_report(matrix_report,sizeof(matrix_report),2050);
    assert(strstr(matrix_report,"Matrix MAIN MIX headroom: LEGACY CLIP"));
    matrix->main_headroom=TS_MATRIX_HEADROOM_FLOAT;
    ts_audio_health_capture_setup(a,ui,2350);ts_audio_health_setup_report(matrix_report,sizeof(matrix_report),2350);
    assert(strstr(matrix_report,"Matrix MAIN MIX headroom: FLOAT"));
    char short_report[17]; memset(short_report, 'x', sizeof(short_report));
    ts_audio_health_setup_report(short_report, sizeof(short_report) - 1, 1250);
    assert(short_report[15] == '\0' && short_report[16] == 'x');
    ts_note_bank_clear(&a->notes); ts_sample_free(&sample);
    e.type = SDL_DROPFILE; e.drop.windowID = ts_health.window_id; e.drop.file = SDL_strdup("test.wav");
    assert(ts_audio_health_event(&e, main_window, a, ui));
    if (argc > 1) {
        SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(ts_health.framebuffer->pixels,
            640, 400, 32, 640 * 4, SDL_PIXELFORMAT_ARGB8888); assert(surface);
        assert(!SDL_SaveBMP(surface, argv[1])); SDL_FreeSurface(surface);
    }
    /* Session recording works with Health hidden; resets retain the timeline. */
    char folder_url[6160];
    assert(ts_audio_session_folder_url("C:\\A B\\\xc3\xa9\\logs\\session.jsonl",folder_url,sizeof(folder_url)));
    assert(!strcmp(folder_url,"file:///C:/A%20B/%C3%A9/logs/"));
    assert(ts_audio_session_folder_url("\\\\server\\share\\logs\\session.jsonl",folder_url,sizeof(folder_url)));
    assert(!strcmp(folder_url,"file://server/share/logs/"));
    assert(ts_audio_session_folder_url("relative/logs/session.jsonl",folder_url,sizeof(folder_url)));
    assert(!strncmp(folder_url,"file:///",8) && strstr(folder_url,"/relative/logs/"));
    assert(!ts_audio_session_folder_url("",folder_url,sizeof(folder_url)));
    char *session_dir=SDL_GetPrefPath("TapeSisterTests","Health");assert(session_dir);
    assert(ts_session_log_start(session_dir,0));SDL_free(session_dir);
    ts_health.visible=0;ts_health.setup.valid=0;
    ts_audio_health_capture_setup(a,ui,SDL_GetTicks());assert(ts_health.setup.valid);
    ts_profile_enable(1);
    TsProfileEffectIdentity log_effect={.generation=777,.id=16,.type=TS_SISTER_FX_GRAIN};
    for(int block=0;block<64;++block) {
        ts_profile_lane_begin(1);
        uint64_t log_start=ts_profile_effect_begin(15);
        ts_profile_effect_end(15,&log_effect,log_start,0);ts_profile_lane_end(1);
    }
    ts_audio_session_update(a,ui);
    ts_profile_enable(0);
    e.type=SDL_KEYDOWN;e.key.windowID=SDL_GetWindowID(main_window);e.key.keysym.sym=SDLK_F12;
    e.key.keysym.mod=KMOD_LCTRL|KMOD_LSHIFT;e.key.repeat=0;
    assert(ts_audio_health_event(&e,main_window,a,ui));assert(!ts_health.visible);
    ts_health.visible=1;
    e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=ts_health.window_id;e.button.button=SDL_BUTTON_LEFT;
    health_button_position(&e,TS_HEALTH_BUTTON_SAVE);assert(ts_audio_health_event(&e,main_window,a,ui));
    TsSessionLogStatus session;
    for(int i=0;i<500;++i){ts_audio_session_update(a,ui);ts_session_log_status(&session);if(session.export_result!=1)break;SDL_Delay(10);}
    assert(session.export_result==2);char *saved_path=SDL_GetClipboardText();assert(!strcmp(saved_path,session.export_path));SDL_free(saved_path);
    remove(session.export_path);
    char *project_dir=SDL_GetPrefPath("TapeSisterTests","Health Project");assert(project_dir);
    snprintf(ui->project_path,sizeof(ui->project_path),"%sShow.tsr",project_dir);SDL_free(project_dir);
    /* A normal project save must not overwrite the user's clipboard. */
    assert(!SDL_SetClipboardText("keep my clipboard"));
    assert(ts_session_log_export_project(ui->project_path,0));
    for(int i=0;i<500;++i){ts_audio_session_update(a,ui);ts_session_log_status(&session);if(session.export_result!=1)break;SDL_Delay(10);}
    assert(session.export_result==2 && strstr(session.export_path,"logs/health-session-"));
    saved_path=SDL_GetClipboardText();assert(!strcmp(saved_path,"keep my clipboard"));SDL_free(saved_path);
    /* SAVE SESSION follows the active project even after a previous export;
       completion must be noticed when the writer finishes before UI polling. */
    assert(ts_audio_health_event(&e,main_window,a,ui));
    for(int i=0;i<500;++i){ts_session_log_status(&session);if(session.export_result!=1)break;SDL_Delay(10);}
    ts_audio_session_update(a,ui);
    saved_path=SDL_GetClipboardText();assert(!strcmp(saved_path,session.export_path));SDL_free(saved_path);
    ts_audio_health_report(a,report,sizeof(report));assert(strstr(report,"Session export: SAVED"));
    ts_audio_health_reset(a);
    ts_realtime_diagnostics_get(&a->realtime_diagnostics, &s);
    assert(!s.callback_count && !s.control_count && !s.recent_callback_count);
    e.type = SDL_WINDOWEVENT; e.window.windowID = ts_health.window_id; e.window.event = SDL_WINDOWEVENT_CLOSE;
    assert(ts_audio_health_event(&e, main_window, a, ui) && !ts_health.visible);
    ui->config_open = 1;
    e.type = SDL_MOUSEBUTTONDOWN; e.button.windowID = SDL_GetWindowID(main_window);
    e.button.button = SDL_BUTTON_LEFT; e.button.x = 200; e.button.y = 48;
    assert(ts_audio_health_event(&e, main_window, a, ui) && ts_health.visible);
    e.type = SDL_KEYDOWN; e.key.windowID = SDL_GetWindowID(main_window);
    e.key.keysym.sym = SDLK_F12; e.key.keysym.mod = 0; e.key.repeat = 0;
    assert(ts_audio_health_event(&e, main_window, a, ui) && !ts_health.visible);

    /* Concurrent rate changes must never mix the recent numerator/denominator. */
    SDL_Thread *thread = SDL_CreateThread(publish, "health publisher", &a->realtime_diagnostics);
    assert(thread);
    for (unsigned i = 0; i < 100000; ++i) {
        ts_realtime_diagnostics_get(&a->realtime_diagnostics, &s);
        assert(s.recent_load_percent == 0 || fabs(s.recent_load_percent - 25) < .00001);
    }
    SDL_WaitThread(thread, NULL);
    check_concurrent_profiles();
    assert(spatial_show(0,a,ui));
    SDL_HideWindow(spatial_window.window);spatial_window.presented=0;
    spatial_window.framebuffer->pixels[0]=0x12345678u;
    spatial_window_update(ui);assert(!spatial_window.presented);
    assert(spatial_window.framebuffer->pixels[0]==0x12345678u);
    spatial_window_close();
    ts_audio_health_close(); ts_native_close(ts_real_output);
    ts_real_output = 0; ts_output_userdata = NULL;
    ts_session_log_stop();ts_session_log_status(&session);
    SDL_RWops *log=SDL_RWFromFile(session.path,"rb");assert(log);size_t log_size=(size_t)SDL_RWsize(log);
    char *history=calloc(log_size+1,1);assert(history);assert(SDL_RWread(log,history,1,log_size)==log_size);SDL_RWclose(log);
    assert(strstr(history,"heard_click") && strstr(history,"health_reset") && strstr(history,"setup_snapshot") && strstr(history,"audio_device"));
    assert(strstr(history,"mosaic_cards=1024 mosaic_at_playhead=0 mosaic_notes=0"));
    assert(strstr(history,"profile_snapshot") && strstr(history,"#16 GRAIN") && strstr(history,"peak ms/block"));
    free(history);remove(session.path);remove(session.export_path);
    SDL_DestroyWindow(main_window);ts_mosaic_free(a->mosaic); free(a); free(ui); SDL_Quit();
    puts("Audio Health: counters, nested locks, concurrent snapshots, actual device, window isolation, reset and clipboard passed");
    return 0;
}
