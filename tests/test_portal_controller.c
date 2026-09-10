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
static void selection_screenshot(const char *variable,const TsUiState *ui,const TsInstrument *instrument)
{
    const char *path=getenv(variable);if(!path || !*path)return;
    static TsFramebuffer fb;ts_ui_render(&fb,ui,instrument);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",TS_UI_WIDTH,TS_UI_HEIGHT);
    for(size_t i=0;i<TS_UI_WIDTH*TS_UI_HEIGHT;++i) {
        unsigned char rgb[3]={(unsigned char)(fb.pixels[i]>>16),(unsigned char)(fb.pixels[i]>>8),(unsigned char)fb.pixels[i]};
        assert(fwrite(rgb,1,3,f)==3);
    }
    fclose(f);
}

static void test_selection_workflow(SDL_Window *window,SDL_AudioDeviceID device)
{
    static TsInstrument instrument;static TsUiState ui;static AudioState audio;
    static SisterWindow sister;PortalController c;TransformController transform;
    TsSamplePages pages;char error[160];TsSample original;ts_sample_init(&original);
    ts_instrument_init(&instrument);ts_ui_init(&ui);
    ts_sister_runtime_init(&audio.sister);ts_note_bank_init(&audio.notes);
    ts_performance_init(&audio.performance);ts_performance_init(&audio.tile_launchers);
    ts_capture_init(&audio.capture);audio.bank_slot=-1;
    assert(ts_sample_pages_init(&pages,error,sizeof(error)));
    transform_controller_init(&transform);
    assert(ts_instrument_generate(&instrument,TS_GENERATOR_METALLIC,777,error,sizeof(error)));
    assert(ts_sample_clone(&original,&instrument.current,error,sizeof(error)));
    portal_init(&c,&ui.portal);c.pages=&pages;ui.portal.open=1;
    snprintf(ui.config.cdp_bin_path,sizeof(ui.config.cdp_bin_path),"%s",getenv("TS_TEST_CDP_BIN"));
    TsPortalUi *p=&ui.portal;
    instrument.has_selection=1;instrument.selection_first=1000;
    instrument.selection_last=instrument.current.frames-1000;c.selection_scope=1;
    assert(portal_source(device,&audio,&ui,&instrument,&c) && p->load_selection);
    p->process_selection=1;
    portal_preview(device,&audio,&ui,&c);
    assert(!c.worker && strstr(p->message,"DRAW A SOURCE SELECTION"));
    p->waves[0].has_selection=1;p->waves[0].selection_first=2000;p->waves[0].selection_last=20000;
    ts_portal_recipe_default(&p->recipe,ts_portal_process_find("modify.speed.1"));p->recipe.values[0]=.5;
    portal_preview(device,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);
    assert(p->valid && p->rendered_region.selection);
    size_t end=p->rendered_region.result_last;
    assert(end>37000 && end<39000 && p->rendered_region.last==20000);
    assert(!memcmp(p->result->data,c.source.data,2000*sizeof(float)));
    assert(!memcmp(p->result->data+end,c.source.data+20000,(c.source.frames-20000)*sizeof(float)));
    assert(p->waves[1].selection_first==2000 && p->waves[1].selection_last==end);
    uint64_t initial=ts_sample_hash(p->source);
    /* Result audition edits leave the render valid. Source processing edits
       invalidate it while preserving the running loop and audible old result. */
    SDL_Event wheel={0};wheel.type=SDL_MOUSEWHEEL;wheel.wheel.windowID=SDL_GetWindowID(window);wheel.wheel.y=-1;
    SDL_SetModState(KMOD_ALT);SDL_WarpMouseInWindow(window,180,170);
    uint64_t generation=c.generation;
    portal_event(&wheel,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform);
    assert(p->valid && c.generation==generation && p->waves[0].selection_first==2000);
    p->listen_result=0;p->loop=1;portal_play(device,&audio,&ui,&c,44100,1);
    const TsSample *audition_result=p->result;
    assert(audio.playing && audio.sample==p->source);
    SDL_WarpMouseInWindow(window,180,80);
    portal_event(&wheel,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform);
    SDL_SetModState(KMOD_NONE);
    assert(!p->valid && p->result==audition_result && audio.playing && audio.looping);
    assert(c.generation!=generation && ts_sample_hash(p->source)==initial);
    assert(!portal_apply(device,&audio,&ui,&instrument,&c,0));
    portal_select_history(device,&audio,p,&c,0);
    assert(p->valid && p->process_selection && p->waves[0].selection_first==2000);
    assert(p->waves[0].selection_last==20000 && p->waves[1].selection_last==end);
    /* An in-flight selection render cannot be accepted after its range changes. */
    portal_preview(device,&audio,&ui,&c);assert(c.worker);
    p->waves[0].selection_first=3000;portal_selection_changed(p,&c);
    wait_portal(&audio,&ui,&instrument,&c);assert(!p->valid);
    portal_select_history(device,&audio,p,&c,0);
    float outside=c.source.data[0];c.source.data[0]=NAN;
    portal_preview(device,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);
    assert(!p->valid && strstr(p->message,"NONFINITE"));c.source.data[0]=outside;
    portal_select_history(device,&audio,p,&c,0);
    selection_screenshot("TS_TEST_PORTAL_SELECTION_SCREENSHOT",&ui,&instrument);
    portal_manage_open(p);
    selection_screenshot("TS_TEST_PORTAL_MANAGER_SCREENSHOT",&ui,&instrument);
    p->manage_open=0;
    uint64_t rendered=ts_sample_hash(p->result);
    assert(portal_apply(device,&audio,&ui,&instrument,&c,0));
    assert(ts_sample_hash(p->source)==rendered && c.source_first==1000);
    assert(p->waves[0].selection_first==2000 && p->waves[0].selection_last==end);
    assert(!memcmp(original.data,instrument.current.data,3000*sizeof(float)));
    assert(!memcmp(original.data+21000,instrument.current.data+1000+end,(original.frames-21000)*sizeof(float)));
    assert(ts_instrument_undo(&instrument,error,sizeof(error)));
    assert(ts_sample_hash(&instrument.current)==ts_sample_hash(&original));
    assert(ts_instrument_redo(&instrument,error,sizeof(error)));
    uint64_t first_generation=ts_sample_hash(&instrument.current);
    int first_slot=instrument.selected_slot;
    /* Two preserved generations, with no Reload between them. */
    ts_portal_recipe_default(&p->recipe,ts_portal_process_find("modify.loudness.6"));
    for(int pass=0;pass<2;++pass) {
        portal_preview(device,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);assert(p->valid);
        uint64_t result_hash=ts_sample_hash(p->result);int prior=instrument.selected_slot;
        uint64_t prior_hash=ts_sample_hash(&instrument.current);
        assert(portal_apply(device,&audio,&ui,&instrument,&c,2));
        assert(instrument.selected_slot!=prior && ts_sample_hash(&instrument.bank[prior].sample)==prior_hash);
        assert(ts_sample_hash(p->source)==result_hash && ts_sample_hash(&instrument.current)==result_hash);
        assert(c.source_first==0 && c.source_last==instrument.current.frames && c.source_slot==instrument.selected_slot);
        assert(p->process_selection && p->waves[0].selection_first==2000 && p->waves[0].selection_last==end);
    }
    assert(ts_sample_hash(&instrument.bank[first_slot].sample)==first_generation);
    /* New Tile keeps the Portal source, even for a selected-region result. */
    portal_preview(device,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);
    const float *source_data=p->source->data;uint64_t source_hash=c.source_hash;int source_slot=c.source_slot;
    assert(portal_apply(device,&audio,&ui,&instrument,&c,1));
    assert(p->source->data==source_data && c.source_hash==source_hash && c.source_slot==source_slot);
    /* A full page preserves everything until the user chooses New Page. */
    for(int i=0;i<TS_BANK_SLOT_COUNT;++i)if(!instrument.bank[i].occupied)
        assert(ts_instrument_bank_capture(&instrument,i,TS_BANK_CAPTURE_CURRENT,error,sizeof(error)));
    portal_preview(device,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);assert(p->valid);
    const TsSample *result=p->result;uint64_t full_hash=ts_sample_hash(&instrument.current);
    int full_slot=instrument.selected_slot;
    assert(!portal_apply(device,&audio,&ui,&instrument,&c,2));assert(p->full_action==2 && pages.page_count==1);
    selection_screenshot("TS_TEST_PORTAL_FULL_SCREENSHOT",&ui,&instrument);
    SDL_Event click={0};click.type=SDL_MOUSEBUTTONDOWN;click.button.windowID=SDL_GetWindowID(window);click.button.button=SDL_BUTTON_LEFT;
    click.button.x=380;click.button.y=225;
    portal_event(&click,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform);
    assert(!p->full_action && p->valid && p->result==result && p->source->data==source_data);
    assert(!portal_apply(device,&audio,&ui,&instrument,&c,2));
    /* Capture guard and page-limit failure retain the pending choice/result. */
    ui.sister_capture_active=1;click.button.x=230;
    portal_event(&click,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform);
    assert(pages.page_count==1 && p->full_action==2 && p->valid);ui.sister_capture_active=0;
    pages.page_count=1024;
    portal_event(&click,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform);
    assert(pages.page_count==1024 && p->full_action==2 && p->valid);pages.page_count=1;
    /* Failure after creating the page rolls back the empty page and keeps the
       old source, result, and every occupied tile. */
    TsSample *bad=&c.history[p->history_selected].output;uint32_t rate=bad->sample_rate;
    bad->sample_rate=0;
    portal_event(&click,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform);
    assert(pages.page_count==1 && ui.sample_page==0 && p->full_action==2 && p->valid);
    assert(p->source->data==source_data && ts_sample_hash(&instrument.current)==full_hash);
    bad->sample_rate=rate;
    uint64_t final_result=ts_sample_hash(p->result);
    portal_event(&click,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform);
    if(pages.page_count!=2)fprintf(stderr,"NEW PAGE FAILED: %s / %s\n",p->message,ui.status);
    assert(pages.page_count==2 && ui.sample_page==1 && c.source_page==1 && !p->full_action);
    assert(ts_sample_hash(p->source)==final_result && c.source_hash==final_result);
    const TsInstrument *previous=ts_sample_pages_page(&pages,&instrument,0);
    assert(ts_sample_hash(&previous->bank[full_slot].sample)==full_hash);
    for(int i=0;i<TS_BANK_SLOT_COUNT;++i)assert(previous->bank[i].occupied);
    portal_close(device,&audio,&ui,&c);portal_free(&c);ts_sample_free(&original);
    ts_sample_pages_free(&pages);ts_instrument_free(&instrument);
    ts_sister_runtime_free(&audio.sister);ts_performance_free(&audio.performance);
    ts_performance_free(&audio.tile_launchers);ts_capture_free(&audio.capture);
}


#include "test_portal_chains.inc"
#include "test_portal_factory_controller.inc"
#include "test_portal_workflow_ui.inc"
#include "test_portal_instrument_controller.inc"
#include "test_portal_stereo_controller.inc"
#include "test_portal_usability.inc"
#include "test_portal_browsing.inc"

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
    p->listen_result=1;portal_note_on(audition,&audio,&ui,0,44100);
    assert(ts_note_bank_count(&audio.notes)==1);
    portal_apply(0,&audio,&ui,&instrument,&c,0);
    assert(ts_sample_hash(&instrument.current)==rendered);
    assert(ts_sample_hash(p->source)==rendered && c.source_hash==rendered);
    assert(!p->valid && !p->result && !p->history_count && !c.history_count && !p->listen_result);
    assert(ts_note_bank_count(&audio.notes)==0 && !audio.playing && !portal_owns_sample(&c,audio.sample));
    for(int i=0;i<TS_NOTE_BANK_VOICE_CAPACITY;++i)assert(!audio.notes.voices[i].sample);
    assert(c.source_first==0 && c.source_last==instrument.current.frames);
    /* The next preview uses the promoted sound immediately, without Reload. */
    ts_portal_recipe_default(&p->recipe,ts_portal_process_find("modify.loudness.6"));
    portal_preview(0,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);
    uint64_t second=ts_sample_hash(p->result);
    portal_apply(0,&audio,&ui,&instrument,&c,0);
    assert(ts_sample_hash(&instrument.current)==second && ts_sample_hash(p->source)==second);
    assert(ts_instrument_undo(&instrument,error,sizeof(error)));
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
    ts_portal_recipe_default(&p->recipe,ts_portal_process_at(1));
    portal_preview(0,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);
    assert(p->valid);size_t replaced_frames=p->result->frames;
    portal_apply(0,&audio,&ui,&instrument,&c,0);
    assert(instrument.current.frames==replaced_frames+2000);
    assert(!memcmp(prefix,instrument.current.data,sizeof(prefix)));
    assert(!memcmp(suffix,instrument.current.data+instrument.current.frames-1000,sizeof(suffix)));
    assert(c.source_first==1000 && c.source_last==1000+replaced_frames && c.source.frames==replaced_frames);
    assert(!memcmp(c.source.data,instrument.current.data+1000,replaced_frames*sizeof(float)));
    ts_portal_recipe_default(&p->recipe,ts_portal_process_find("modify.loudness.6"));
    portal_preview(0,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);
    portal_apply(0,&audio,&ui,&instrument,&c,0);
    assert(instrument.current.frames==replaced_frames+2000);
    assert(!memcmp(prefix,instrument.current.data,sizeof(prefix)));
    assert(!memcmp(suffix,instrument.current.data+instrument.current.frames-1000,sizeof(suffix)));
    assert(ts_instrument_undo(&instrument,error,sizeof(error)));
    assert(ts_instrument_undo(&instrument,error,sizeof(error)));
    assert(ts_sample_hash(&instrument.current)==before);
    instrument.has_selection=0;c.selection_scope=0;

    /* Old preview cannot target a changed page or tile. */
    assert(portal_source(0,&audio,&ui,&instrument,&c));
    portal_preview(0,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);
    ui.sample_page=1;
    portal_apply(0,&audio,&ui,&instrument,&c,0);
    assert(strstr(p->message,"CHANGED") && ts_sample_hash(&instrument.current)==before);
    ui.sample_page=0;instrument.current.data[10]*=0.5f;
    portal_apply(0,&audio,&ui,&instrument,&c,0);assert(strstr(p->message,"CHANGED"));

    /* Changing a control invalidates an in-flight result. */
    assert(portal_source(0,&audio,&ui,&instrument,&c));
    ts_portal_recipe_default(&p->recipe,ts_portal_process_at(0));
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
    uint64_t snapshot_hash=ts_sample_hash(p->source),snapshot_target=c.source_hash;
    size_t snapshot_first=c.source_first,snapshot_last=c.source_last;
    const float *snapshot_data=p->source->data;
    portal_apply(0,&audio,&ui,&instrument,&c,1);
    assert(instrument.selected_slot!=source && instrument.bank[instrument.selected_slot].occupied);
    assert(ts_sample_hash(&instrument.bank[source].sample)==before);
    assert(ts_sample_hash(p->source)==snapshot_hash && p->source->data==snapshot_data);
    assert(c.source_hash==snapshot_target && c.source_slot==source);
    assert(c.source_first==snapshot_first && c.source_last==snapshot_last);

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
    CLICK(20,TS_PORTAL_LIST_Y+TS_PORTAL_LIST_ROW_H+5);assert(p->selected_slot==19 && p->recipe.values[0]==3);
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
    /* The first seven-control process must expose its offscreen tail/phase
       through native scrolling, exact entry, macro mapping, and rendering. */
    p->tab=0;p->family=TS_PORTAL_TIME+1;p->query[0]=0;
    CLICK(25,112);assert(p->family==TS_PORTAL_FILTER+1);
    CLICK(25,TS_PORTAL_LIST_Y+4*TS_PORTAL_LIST_ROW_H+4);assert(!strcmp(p->recipe.process_id,"filter.sweeping.2"));
    assert(p->parameter_scroll==0 && portal_parameter_at(p,2)==2);
    SDL_Event wheel={0};wheel.type=SDL_MOUSEWHEEL;wheel.wheel.windowID=SDL_GetWindowID(window);
    wheel.wheel.y=-100;
    SDL_WarpMouseInWindow(window,180,280);
    assert(portal_event(&wheel,window,audition,&audio,&ui,&instrument,&c,&sister,44100,&transform));
    assert(p->parameter_scroll==4 && portal_parameter_at(p,0)==4 && portal_parameter_at(p,2)==6);
    CLICK(395,305);assert(p->number_focus==6);
    key.key.keysym.sym=SDLK_a;key.key.keysym.mod=KMOD_CTRL;
    portal_event(&key,window,audition,&audio,&ui,&instrument,&c,&sister,44100,&transform);
    snprintf(typed.text.text,sizeof(typed.text.text),"0.5");
    portal_event(&typed,window,audition,&audio,&ui,&instrument,&c,&sister,44100,&transform);
    key.key.keysym.sym=SDLK_RETURN;key.key.keysym.mod=KMOD_NONE;
    portal_event(&key,window,audition,&audio,&ui,&instrument,&c,&sister,44100,&transform);
    assert(p->number_focus==-1 && p->recipe.values[6]==.5);
    CLICK(445,305);assert(!(p->recipe.exposed&(1u<<6)));
    CLICK(445,305);assert(p->recipe.exposed&(1u<<6));
    portal_preview(audition,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);
    assert(p->valid && p->result->frames==c.source.frames+(size_t)round(.25*c.source.sample_rate));
    p->recipe.exposed=(1u<<4)|(1u<<6);p->macro_view=1;p->parameter_scroll=0;
    assert(portal_parameter_at(p,0)==4 && portal_parameter_at(p,1)==6 && portal_parameter_at(p,2)==-1);
    p->macro_view=0;p->parameter_scroll=0;
    CLICK(25,112);assert(p->family==TS_PORTAL_GRAIN+1);
    CLICK(25,TS_PORTAL_LIST_Y+3*TS_PORTAL_LIST_ROW_H+4);assert(!strcmp(p->recipe.process_id,"modify.brassage.5"));
    uint64_t grain_source_hash=ts_sample_hash(&instrument.current);
    p->recipe.values[0]=.25;
    portal_preview(audition,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);
    assert(p->valid && p->result->frames>0 && p->result->channels==1);
    assert(ts_sample_hash(&instrument.current)==grain_source_hash);
    p->listen_result=1;portal_note_on(audition,&audio,&ui,0,44100);
    assert(ts_note_bank_count(&audio.notes)==1);
    p->pin_slot=31;p->exact_pin=1;portal_save(p,&c,1);
    assert(ts_portal_library_load(&reloaded,c.library_path,error,sizeof(error)));
    assert(!strcmp(reloaded.pins[31].process_id,"modify.brassage.5") && reloaded.pins[31].values[0]==.25 && reloaded.pins[31].exposed==0);
    remove(c.library_path);
    portal_invalidate(audition,&audio,p,&c);assert(ts_note_bank_count(&audio.notes)==0);
    CLICK(25,112);assert(p->family==TS_PORTAL_LOFI+1);
    CLICK(25,112);assert(p->family==TS_PORTAL_LEVEL+1);
    CLICK(25,112);assert(p->family==TS_PORTAL_DELAY+1);
    CLICK(25,TS_PORTAL_LIST_Y+4);assert(!strcmp(p->recipe.process_id,"modify.revecho.1"));
    wheel.wheel.y=-100;SDL_WarpMouseInWindow(window,180,280);
    portal_event(&wheel,window,audition,&audio,&ui,&instrument,&c,&sister,44100,&transform);
    assert(p->parameter_scroll==3 && portal_parameter_at(p,2)==5);
    CLICK(395,305);assert(p->number_focus==5);
    snprintf(p->number_text,sizeof(p->number_text),"1");
    portal_event(&key,window,audition,&audio,&ui,&instrument,&c,&sister,44100,&transform);
    assert(p->number_focus==-1 && p->recipe.values[5]==1);
    portal_preview(audition,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);
    assert(p->valid && p->result->frames>c.source.frames);
    CLICK(25,112);assert(p->family==TS_PORTAL_ENVELOPE+1);
    CLICK(25,TS_PORTAL_LIST_Y+4);assert(!strcmp(p->recipe.process_id,"envel.warp.2"));
    portal_preview(audition,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);
    assert(p->valid && p->result && p->result->frames==c.source.frames);
    CLICK(25,112);assert(p->family==TS_PORTAL_STRUCTURE+1);
    CLICK(25,TS_PORTAL_LIST_Y+4);assert(!strcmp(p->recipe.process_id,"sfedit.cut.1"));
    portal_preview(audition,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);
    assert(p->valid && llabs((long long)p->result->frames-llround(.3*c.source.sample_rate))<=1);
    CLICK(25,TS_PORTAL_LIST_Y+3*TS_PORTAL_LIST_ROW_H+4);assert(!strcmp(p->recipe.process_id,"extend.doublets"));
    portal_preview(audition,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);
    assert(p->valid && p->result->frames>c.source.frames);
    selection_screenshot("TS_TEST_PORTAL_STRUCTURE_SCREENSHOT",&ui,&instrument);
    CLICK(25,112);assert(p->family==TS_PORTAL_FACTORY+1);
    CLICK(25,112);assert(p->family==TS_PORTAL_INSTRUMENTS+1);
    CLICK(25,112);assert(p->family==0); /* Family cycle returns to All. */
    /* Odd-only spectral averaging remains valid through drag, wheel, and typing. */
    portal_invalidate(audition,&audio,p,&c);
    ts_portal_recipe_default(&p->recipe,ts_portal_process_find("blur.avrg"));p->parameter_scroll=0;
    CLICK(300,270);assert(fmod(p->recipe.values[0],2)==1);
    SDL_Event release={0};release.type=SDL_MOUSEBUTTONUP;release.button.windowID=SDL_GetWindowID(window);
    release.button.button=SDL_BUTTON_LEFT;
    portal_event(&release,window,audition,&audio,&ui,&instrument,&c,&sister,44100,&transform);
    double odd=p->recipe.values[0];
    wheel.wheel.y=1;SDL_WarpMouseInWindow(window,395,270);
    portal_event(&wheel,window,audition,&audio,&ui,&instrument,&c,&sister,44100,&transform);
    assert(p->recipe.values[0]==odd+2);
    CLICK(395,270);assert(p->number_focus==0);
    snprintf(p->number_text,sizeof(p->number_text),"12");
    portal_event(&key,window,audition,&audio,&ui,&instrument,&c,&sister,44100,&transform);
    assert(p->number_focus==0 && p->recipe.values[0]==odd+2 && strstr(p->message,"ODD INTEGER"));
    snprintf(p->number_text,sizeof(p->number_text),"13");
    portal_event(&key,window,audition,&audio,&ui,&instrument,&c,&sister,44100,&transform);
    assert(p->number_focus==-1 && p->recipe.values[0]==13);
    p->family=TS_PORTAL_SPECTRAL+1;p->scroll=0;
    snprintf(p->query,sizeof(p->query),"SPECTRAL WAVER");
    CLICK(25,TS_PORTAL_LIST_Y+4);assert(!strcmp(p->recipe.process_id,"strange.waver.1"));
    portal_preview(audition,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);
    assert(p->valid && p->result && p->result->frames>0);
    selection_screenshot("TS_TEST_PORTAL_SPECTRAL_SCREENSHOT",&ui,&instrument);
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
    test_selection_workflow(window,audition);
    test_chain_workflow(window,audition);
    test_factory_workflow(window,audition);
    test_portal_workflow_ui(window,audition);
    test_instrument_controls(window,audition);
    test_stereo_workflow(window,audition);
    test_portal_usability(window,audition);
    test_portal_browsing(window,audition);
    SDL_DestroyWindow(window);

    /* Reopening an unsupported channel layout clears the previous source. */
    instrument.current.channels=3;
    assert(!portal_source(0,&audio,&ui,&instrument,&c));
    assert(!p->source && !p->result && !c.source.data);
    instrument.current.channels=1;

    portal_close(0,&audio,&ui,&c);portal_free(&c);
    ts_performance_free(&audio.performance);ts_performance_free(&audio.tile_launchers);
    ts_sister_runtime_free(&audio.sister);ts_capture_free(&audio.capture);
    ts_instrument_free(&instrument);SDL_CloseAudioDevice(audition);ts_real_output=0;SDL_Quit();
    puts("Portal controller lifecycle tests passed");return 0;
}
