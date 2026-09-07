/* Exercise the actual SDL controller without a physical device. This test
   intentionally includes the application translation unit: lifecycle and
   callback pointer ownership should not be replaced with test doubles. */
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>

static void wait_portal(AudioState *audio,TsUiState *ui,TsInstrument *instrument,
                         PortalController *c)
{
    Uint32 start=SDL_GetTicks();
    while(c->worker && SDL_GetTicks()-start<10000) {
        portal_poll(0,audio,ui,instrument,c);SDL_Delay(1);
    }
    assert(!c->worker && !ui->portal.busy);
}
int main(void)
{
    static TsInstrument instrument;
    static TsUiState ui;
    static AudioState audio;
    PortalController c;
    TsPortalUi *p=&ui.portal;
    const char *bin=getenv("TS_TEST_CDP_BIN");
    char error[160];
    if(!bin || !*bin) {puts("Portal controller test: set TS_TEST_CDP_BIN to run real-CDP checks");return 0;}
    SDL_SetMainReady();
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    assert(SDL_Init(SDL_INIT_TIMER|SDL_INIT_AUDIO)==0);
    SDL_AudioSpec desired={0};desired.freq=44100;desired.format=AUDIO_F32SYS;
    desired.channels=2;desired.samples=256;
    SDL_AudioDeviceID audition=SDL_OpenAudioDevice(NULL,0,&desired,NULL,0);
    assert(audition); /* Kept paused: test ownership/routing, never play hardware. */
    ts_real_output=audition;
    ts_instrument_init(&instrument);ts_ui_init(&ui);
    ts_sister_runtime_init(&audio.sister);ts_note_bank_init(&audio.notes);
    ts_performance_init(&audio.performance);ts_performance_init(&audio.tile_launchers);
    ts_capture_init(&audio.capture);audio.bank_slot=-1;
    assert(ts_instrument_generate(&instrument,TS_GENERATOR_METALLIC,555,error,sizeof(error)));
    portal_init(&c,p);p->open=1;
    snprintf(ui.config.cdp_bin_path,sizeof(ui.config.cdp_bin_path),"%s",bin);
    uint64_t before=ts_sample_hash(&instrument.current);
    assert(portal_source(0,&audio,&ui,&instrument,&c));
    ts_portal_recipe_default(&p->recipe,ts_portal_process_at(1));
    portal_preview(0,&audio,&ui,&c);assert(c.worker);
    wait_portal(&audio,&ui,&instrument,&c);
    assert(p->valid && p->result && p->history_count==1);
    assert(ts_sample_hash(&instrument.current)==before);
    p->listen_result=0;p->loop=1;
    p->waves[0].has_selection=1;p->waves[0].selection_first=100;
    p->waves[0].selection_last=1000;p->waves[0].playhead=200;
    portal_play(audition,&audio,&ui,&c,44100,1);
    assert(p->playing && audio.sample==p->source && audio.looping);
    assert(audio.range_start==100 && audio.range_end==1000 && audio.position==200);
    portal_ab(audition,&audio,&ui,&c,44100,1);
    assert(p->playing && audio.sample==p->result && audio.position==200);
    assert(audio.range_end==p->result->frames);
    audio.playing=0;portal_poll(audition,&audio,&ui,&instrument,&c);
    assert(!p->playing && p->waves[1].playhead==0);
    portal_play(audition,&audio,&ui,&c,44100,1);assert(audio.position==0);
    portal_stop(audition,&audio,p,&c);assert(!audio.sample && !p->playing);
    ts_real_output=0;portal_play(audition,&audio,&ui,&c,44100,1);
    assert(!p->playing && strstr(p->message,"NO AUDIO OUTPUT"));ts_real_output=audition;
    uint64_t rendered=ts_sample_hash(p->result);
    portal_apply(0,&audio,&ui,&instrument,&c,0);
    assert(ts_sample_hash(&instrument.current)==rendered);
    assert(ts_instrument_undo(&instrument,error,sizeof(error)));
    assert(ts_sample_hash(&instrument.current)==before);

    /* Selection replacement preserves both untouched outer regions and undo. */
    instrument.has_selection=1;instrument.selection_first=1000;
    instrument.selection_last=instrument.current.frames-1000;c.selection_scope=1;
    float prefix[1000],suffix[1000];size_t original_frames=instrument.current.frames;
    memcpy(prefix,instrument.current.data,sizeof(prefix));
    memcpy(suffix,instrument.current.data+original_frames-1000,sizeof(suffix));
    assert(portal_source(0,&audio,&ui,&instrument,&c));
    assert(c.source.frames==original_frames-2000);
    portal_preview(0,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);
    assert(p->valid);size_t replaced_frames=p->result->frames;
    portal_apply(0,&audio,&ui,&instrument,&c,0);
    assert(instrument.current.frames==replaced_frames+2000);
    assert(!memcmp(prefix,instrument.current.data,sizeof(prefix)));
    assert(!memcmp(suffix,instrument.current.data+instrument.current.frames-1000,sizeof(suffix)));
    assert(ts_instrument_undo(&instrument,error,sizeof(error)));
    assert(ts_sample_hash(&instrument.current)==before);
    instrument.has_selection=0;c.selection_scope=0;

    /* Old preview cannot target a changed page or tile. */
    portal_select_history(0,&audio,p,&c,0);ui.sample_page=1;
    portal_apply(0,&audio,&ui,&instrument,&c,0);
    assert(strstr(p->message,"CHANGED") && ts_sample_hash(&instrument.current)==before);
    ui.sample_page=0;instrument.current.data[10]*=0.5f;
    portal_apply(0,&audio,&ui,&instrument,&c,0);assert(strstr(p->message,"CHANGED"));

    /* Changing a control invalidates an in-flight result. */
    assert(portal_source(0,&audio,&ui,&instrument,&c));
    portal_preview(0,&audio,&ui,&c);assert(c.worker);
    portal_parameter_drag(0,&audio,p,&c,0,370);
    wait_portal(&audio,&ui,&instrument,&c);assert(!p->valid && !p->result);

    /* Close while rendering; polling must not resurrect the page/result. */
    ts_portal_recipe_default(&p->recipe,ts_portal_process_at(0));
    portal_preview(0,&audio,&ui,&c);assert(c.worker);
    portal_close(0,&audio,&ui,&c);
    wait_portal(&audio,&ui,&instrument,&c);assert(!p->open && !p->valid);
    p->open=1;

    /* History owns the result; callback references are detached on eviction. */
    for(int i=0;i<TS_PORTAL_HISTORY+2;++i) {
        p->recipe.values[0]=(double)(i+2);
        portal_preview(0,&audio,&ui,&c);
        /* A held source note started during a render is detached before
           result/history storage moves on worker completion. */
        p->listen_result=0;portal_note_on(audition,&audio,&ui,0,44100);
        assert(ts_note_bank_count(&audio.notes)==1);
        wait_portal(&audio,&ui,&instrument,&c);
        assert(ts_note_bank_count(&audio.notes)==0);
        assert(p->valid && c.history_count<=TS_PORTAL_HISTORY);
        audio.sample=p->result;audio.playing=1;p->playing=1;
    }
    assert(c.history_count==TS_PORTAL_HISTORY);
    portal_invalidate(0,&audio,p,&c);assert(audio.sample==NULL && !audio.playing);

    /* A pin is a recipe, never a pointer to the original sound. */
    portal_select_history(0,&audio,p,&c,c.history_count-1);
    snprintf(c.library_path,sizeof(c.library_path),"portal-controller-%u.recipes",SDL_GetTicks());
    p->pin_slot=2;p->exact_pin=1;portal_save(p,&c,1);
    assert(p->library.pins[2].process_id[0] && p->library.pins[2].exposed==0);
    TsPortalRecipe saved=p->library.pins[2];p->recipe.values[0]=20;
    portal_save(p,&c,1);assert(!memcmp(&saved,&p->library.pins[2],sizeof(saved)));
    assert(strstr(p->message,"OCCUPIED"));remove(c.library_path);
    c.library_writable=0;p->pin_slot=3;
    portal_save(p,&c,1);assert(strstr(p->message,"BLOCKED"));
    assert(!p->library.pins[3].process_id[0]);c.library_writable=1;

    /* New tile uses a free slot and does not mutate its source. */
    assert(portal_source(0,&audio,&ui,&instrument,&c));
    ts_portal_recipe_default(&p->recipe,ts_portal_process_at(0));
    portal_preview(0,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);
    int source=instrument.selected_slot;before=ts_sample_hash(&instrument.current);
    portal_apply(0,&audio,&ui,&instrument,&c,1);
    assert(instrument.selected_slot!=source && instrument.bank[instrument.selected_slot].occupied);
    assert(ts_sample_hash(&instrument.bank[source].sample)==before);

    /* Exercise native event routing, numeric entry, waveform selection, and
       both main-page pin actions with a headless SDL window. */
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);assert(SDL_InitSubSystem(SDL_INIT_VIDEO)==0);
    SDL_Window *window=SDL_CreateWindow("Portal test",0,0,640,400,SDL_WINDOW_HIDDEN);
    assert(window);
    static SisterWindow sister;TransformController transform;
    transform_controller_init(&transform);
    /* Manage duplicate-name entries by stable slot, using native input events. */
    TsPortalLibrary prior_library=p->library;TsPortalRecipe prior_recipe=p->recipe;
    ts_portal_recipe_default(&p->recipe,ts_portal_process_find("blur.chorus.5"));
    p->library.recipes[1]=p->library.recipes[19]=p->recipe;
    p->library.recipes[19].values[0]=3;
    p->tab=1;p->family=TS_PORTAL_SPECTRAL+1;
    snprintf(p->query,sizeof(p->query),"chorus");
    SDL_Event manage={0};manage.type=SDL_MOUSEBUTTONDOWN;manage.button.windowID=SDL_GetWindowID(window);
    manage.button.button=SDL_BUTTON_LEFT;
#define CLICK(X,Y) do { manage.button.x=(X);manage.button.y=(Y); \
    assert(portal_event(&manage,window,audition,&audio,&ui,&instrument,&c,&sister,44100,&transform)); } while(0)
    CLICK(20,148);assert(p->selected_slot==19 && p->recipe.values[0]==3);
    p->recipe.values[0]=4;p->exact_pin=1;
    CLICK(30,340);assert(p->manage_open && p->manage_slot==19 && p->manage_scroll==16);
    CLICK(440,210);assert(p->manage_action==TS_PORTAL_UPDATE);
    CLICK(440,306);assert(!p->manage_action && p->library.recipes[19].values[0]==3);
    CLICK(440,210);CLICK(320,306);
    assert(p->library.recipes[19].values[0]==4 && p->library.recipes[1].values[0]==1.5);
    CLICK(315,182);
    SDL_Event key={0};key.type=SDL_KEYDOWN;key.key.windowID=SDL_GetWindowID(window);
    key.key.keysym.sym=SDLK_a;key.key.keysym.mod=KMOD_CTRL;
    portal_event(&key,window,audition,&audio,&ui,&instrument,&c,&sister,44100,&transform);
    SDL_Event typed={0};typed.type=SDL_TEXTINPUT;typed.text.windowID=SDL_GetWindowID(window);
    snprintf(typed.text.text,sizeof(typed.text.text),"SOFT GHOST");
    portal_event(&typed,window,audition,&audio,&ui,&instrument,&c,&sister,44100,&transform);
    CLICK(320,210);CLICK(320,306);
    assert(!strcmp(p->library.recipes[19].name,"SOFT GHOST") && !strcmp(p->recipe.name,"SOFT GHOST"));
    /* Switch banks and replace an empty pin from the captured working recipe. */
    CLICK(205,92);CLICK(320,234);CLICK(320,306);
    assert(!strcmp(p->library.pins[0].process_id,"blur.chorus.5"));
    assert(p->library.pins[0].exposed==0); /* Exact-pin mode is retained. */
    TsPortalLibrary snapshot=p->library;
    CLICK(440,234);CLICK(440,306);assert(!memcmp(&snapshot,&p->library,sizeof(snapshot)));
    CLICK(440,234);c.library_writable=0;CLICK(320,306);
    assert(!memcmp(&snapshot,&p->library,sizeof(snapshot)));c.library_writable=1;
    CLICK(320,306);assert(!p->library.pins[0].process_id[0] && p->library.pins[2].process_id[0]);
    TsPortalLibrary reloaded={0};assert(ts_portal_library_load(&reloaded,c.library_path,error,sizeof(error)));
    assert(!memcmp(&reloaded,&p->library,sizeof(reloaded)));remove(c.library_path);
    CLICK(480,70);assert(!p->manage_open);
    p->library=prior_library;p->recipe=prior_recipe;p->query[0]=0;p->tab=p->family=0;
    p->selected_tab=p->selected_slot=-1;
#undef CLICK
    SDL_Event event={0};event.type=SDL_MOUSEBUTTONDOWN;
    event.button.windowID=SDL_GetWindowID(window);event.button.button=SDL_BUTTON_LEFT;
    event.button.x=395;event.button.y=267;
    assert(portal_event(&event,window,audition,&audio,&ui,&instrument,&c,&sister,44100,&transform));
    assert(p->number_focus==0);
    event.type=SDL_KEYDOWN;event.key.windowID=SDL_GetWindowID(window);
    event.key.keysym.sym=SDLK_BACKSPACE;
    for(int i=0;i<32;++i)portal_event(&event,window,audition,&audio,&ui,&instrument,&c,&sister,44100,&transform);
    event.type=SDL_TEXTINPUT;event.text.windowID=SDL_GetWindowID(window);
    snprintf(event.text.text,sizeof(event.text.text),"4");
    portal_event(&event,window,audition,&audio,&ui,&instrument,&c,&sister,44100,&transform);
    event.type=SDL_KEYDOWN;event.key.windowID=SDL_GetWindowID(window);event.key.keysym.sym=SDLK_RETURN;
    portal_event(&event,window,audition,&audio,&ui,&instrument,&c,&sister,44100,&transform);
    assert(p->number_focus==-1 && p->recipe.values[0]==4);
    memset(&event,0,sizeof(event));event.type=SDL_MOUSEBUTTONDOWN;
    event.button.windowID=SDL_GetWindowID(window);event.button.button=SDL_BUTTON_LEFT;
    event.button.x=170;event.button.y=80;
    portal_event(&event,window,audition,&audio,&ui,&instrument,&c,&sister,44100,&transform);
    event.type=SDL_MOUSEMOTION;event.motion.windowID=SDL_GetWindowID(window);
    event.motion.x=230;event.motion.y=80;
    portal_event(&event,window,audition,&audio,&ui,&instrument,&c,&sister,44100,&transform);
    assert(p->waves[0].has_selection && p->waves[0].selection_last>p->waves[0].selection_first);
    portal_close(audition,&audio,&ui,&c);ui.show_recipes=1;ui.cdp_user_pins=1;
    memset(&event,0,sizeof(event));event.type=SDL_MOUSEBUTTONDOWN;
    event.button.windowID=SDL_GetWindowID(window);event.button.button=SDL_BUTTON_MIDDLE;
    event.button.x=184;event.button.y=340; /* Existing pin 03, not factory slot 03. */
    ui.warp_gesture.active=1;
    assert(portal_event(&event,window,audition,&audio,&ui,&instrument,&c,&sister,44100,&transform));
    assert(!p->open);ui.warp_gesture.active=0;
    assert(portal_event(&event,window,audition,&audio,&ui,&instrument,&c,&sister,44100,&transform));
    assert(p->open && p->macro_view && !c.worker && p->recipe.exposed==0);
    portal_close(audition,&audio,&ui,&c);
    event.button.button=SDL_BUTTON_LEFT;before=ts_sample_hash(&instrument.current);
    assert(portal_event(&event,window,audition,&audio,&ui,&instrument,&c,&sister,44100,&transform));
    assert(p->open && c.worker && c.quick_apply);
    wait_portal(&audio,&ui,&instrument,&c);
    assert(!p->valid && strstr(p->message,"APPLIED"));
    assert(ts_instrument_undo(&instrument,error,sizeof(error)));
    assert(ts_sample_hash(&instrument.current)==before);
    SDL_DestroyWindow(window);

    /* Reopening a stereo source cannot leave a previous mono source active. */
    instrument.current.channels=2;
    assert(!portal_source(0,&audio,&ui,&instrument,&c));
    assert(!p->source && !p->result && !c.source.data);
    instrument.current.channels=1;

    portal_close(0,&audio,&ui,&c);portal_free(&c);
    ts_performance_free(&audio.performance);ts_performance_free(&audio.tile_launchers);
    ts_sister_runtime_free(&audio.sister);ts_capture_free(&audio.capture);
    ts_instrument_free(&instrument);SDL_CloseAudioDevice(audition);ts_real_output=0;SDL_Quit();
    puts("Portal controller lifecycle tests passed");return 0;
}
