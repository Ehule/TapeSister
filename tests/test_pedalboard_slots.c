#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>

static AudioState audio;
static TsUiState ui;
static TsInstrument bank;
static SisterWindow sister;
static TsFramebuffer frame;
static char error[200];

static void action(TsSisterUiAction kind)
{
    sister_apply_action(0,&audio,&ui,&bank,&sister,NULL,NULL,
        (TsSisterUiHit){.action=kind},48000,2);
}
static void hit(int x,int y)
{
    sister_apply_action(0,&audio,&ui,&bank,&sister,NULL,NULL,
        ts_sister_ui_hit_test_model(&sister.model,x,y),48000,2);
}
static int key(SDL_Keycode key)
{
    SDL_Event e={0}; e.type=SDL_KEYDOWN; e.key.keysym.sym=key;
    return sister_pedalboard_event(0,&audio,&ui,&sister,&e);
}
static int mouse(Uint32 type,int x,int y)
{
    SDL_Event e={0};e.type=type;e.button.button=SDL_BUTTON_LEFT;e.button.x=x;e.button.y=y;
    return sister_pedalboard_event(0,&audio,&ui,&sister,&e);
}
static void config_and_persistence(void)
{
    TsConfig config;ts_config_init(&config);assert(config.pedalboard_slots==10);
    FILE *f=fopen("pedalboard-test.ini","w");assert(f);fputs("pedalboard_slots=99\n",f);fclose(f);
    assert(ts_config_load(&config,"pedalboard-test.ini",error,sizeof(error)) && config.pedalboard_slots==20);
    f=fopen("pedalboard-test.ini","w");assert(f);fputs("PedalboardSlots=2\n",f);fclose(f);
    assert(ts_config_load(&config,"pedalboard-test.ini",error,sizeof(error)) && config.pedalboard_slots==5);
    assert(ts_config_save(&config,"pedalboard-test.ini",error,sizeof(error)));
    assert(ts_config_load(&config,"pedalboard-test.ini",error,sizeof(error)) && config.pedalboard_slots==5);
    remove("pedalboard-test.ini");

    static TsSisterProjectState saved,loaded;
    ts_sister_project_state_init(&saved,48000);saved.parameters.fx.slot_count=20;
    for(int i=0;i<20;++i) {
        TsSisterFxSlotControls *s=&saved.parameters.fx.slot[i];
        *s=ts_sister_fx_empty_slot();s->type=(TsSisterFxType)(1+i%5);
        s->parameter_a=i/20.f;s->parameter_b=(20-i)/20.f;s->mix=.8f;
        s->placement=1u<<(i%5);s->parameter_locks=i%32;
    }
    assert(ts_sister_project_state_save_file(&saved,"pedalboard-state.ini",error,sizeof(error)));
    int present=0;
    assert(ts_sister_project_state_load_file(&loaded,"pedalboard-state.ini",48000,&present,error,sizeof(error)) && present);
    assert(loaded.parameters.fx.slot_count==20 && config.pedalboard_slots==5);
    for(int i=0;i<20;++i) {
        assert(loaded.parameters.fx.slot[i].type==saved.parameters.fx.slot[i].type);
        assert(loaded.parameters.fx.slot[i].parameter_a==saved.parameters.fx.slot[i].parameter_a);
        assert(loaded.parameters.fx.slot[i].parameter_locks==saved.parameters.fx.slot[i].parameter_locks);
    }
    f=fopen("pedalboard-state.ini","w");assert(f);
    fputs("TapeSister Sister Project State\nVersion=42\nPageCount=1\nSlot5Type=5\nSlot5A=0.8\n",f);fclose(f);
    assert(ts_sister_project_state_load_file(&loaded,"pedalboard-state.ini",48000,&present,error,sizeof(error)));
    assert(loaded.parameters.fx.slot_count==5 && loaded.parameters.fx.slot[4].type==TS_SISTER_FX_TRANSIENT);
    remove("pedalboard-state.ini");

    static TsSisterPresetBank presets,restored;
    ts_sister_preset_bank_init(&presets,48000);
    assert(ts_sister_preset_save_new(&presets,"TWENTY SLOTS",&saved.parameters,48000,error,sizeof(error)));
    assert(ts_sister_preset_save(&presets,"pedalboard-presets.ini",error,sizeof(error)));
    assert(ts_sister_preset_load(&restored,"pedalboard-presets.ini",48000,error,sizeof(error)));
    const TsSisterFxControls *fx=&restored.entries[restored.count-1].parameters.fx;
    assert(fx->slot_count==20 && fx->slot[19].type==TS_SISTER_FX_TRANSIENT && fx->slot[19].parameter_locks==19);
    remove("pedalboard-presets.ini");
}

static void dsp_continuity(void)
{
    static TsSisterPostFxEngine engine;
    TsSisterFxControls controls;
    assert(ts_sister_post_fx_init(&engine,1000));ts_sister_fx_controls_default(&controls);
    for(int i=0;i<20;++i)controls.slot[i]=ts_sister_fx_empty_slot();
    controls.slot_count=20;controls.slot[19].type=TS_SISTER_FX_DISTORTION;controls.slot[19].mix=1;
    ts_sister_post_fx_sync_controls(&engine,&controls);
    TsStereoFrame input={.1f,.2f},out={0};
    for(int i=0;i<100;++i)out=ts_sister_post_fx_process(&engine,3,input,0);
    assert(fabsf(out.l-input.l)>.001f); /* The twentieth slot really processes. */
    controls.transition=ts_sister_fx_transition_normalized(240000);
    controls.slot_count=19;
    ts_sister_post_fx_set_controls(&engine,&controls);
    for(int i=0;i<21;++i) {
        out=ts_sister_post_fx_process(&engine,3,input,0);
        assert(isfinite(out.l) && isfinite(out.r));
    }
    assert(engine.processing_slots==19 && fabsf(out.l-input.l)<1e-6f);
    assert(engine.slot[19].active.type==TS_SISTER_FX_EMPTY);
    assert(!engine.grain[18][4].history_frames); /* Empty slots never run kernels. */
    controls.slot[0].type=TS_SISTER_FX_DELAY;controls.slot[0].mix=.6f;
    ts_sister_post_fx_set_controls(&engine,&controls);
    for(int i=0;i<10;++i)ts_sister_post_fx_process(&engine,3,input,0);
    uint32_t remaining=engine.slot[0].morph.remaining;
    float *history=engine.delay[0][4].data;size_t write=engine.delay[0][4].write_index;
    controls.slot_count=20;ts_sister_post_fx_set_controls(&engine,&controls);
    assert(engine.slot[0].morph.remaining==remaining && engine.delay[0][4].data==history);
    assert(engine.delay[0][4].write_index==write); /* Append never resets running history. */
    /* Removing a slot in mid-topology morph must not rewind that morph. */
    controls.slot[19].type=TS_SISTER_FX_DISTORTION;controls.slot[19].mix=1;
    ts_sister_post_fx_sync_controls(&engine,&controls);
    controls.slot[19].type=TS_SISTER_FX_REVERB;ts_sister_post_fx_set_controls(&engine,&controls);
    for(int i=0;i<50;++i)ts_sister_post_fx_process(&engine,3,input,0);
    float phase=engine.slot[19].morph.current;
    controls.slot_count=19;ts_sister_post_fx_set_controls(&engine,&controls);
    assert(engine.slot[19].retiring && engine.slot[19].morph.current==phase && phase>0);
    /* Fast remove/re-add queues the replacement until the old sound fades. */
    controls.slot_count=20;controls.slot[19].type=TS_SISTER_FX_TRANSIENT;
    ts_sister_post_fx_set_controls(&engine,&controls);
    for(int i=0;i<21;++i)ts_sister_post_fx_process(&engine,3,input,0);
    assert(!engine.slot[19].retiring && engine.slot[19].pending.type==TS_SISTER_FX_TRANSIENT);
    ts_sister_post_fx_free(&engine);
}

int main(int argc,char **argv)
{
    SDL_SetMainReady();SDL_setenv("SDL_VIDEODRIVER","dummy",1);
    assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER));
    config_and_persistence();dsp_continuity();
    ts_ui_init(&ui);ts_instrument_init(&bank);ts_sister_runtime_init(&audio.sister);
    assert(ts_sister_runtime_reconfigure(&audio.sister,48000,2,error,sizeof(error)));
    ts_sister_ui_model_init(&sister.model,&ui.config);sister.model.fx_page=1;
    sister.window=SDL_CreateWindow("Pedalboard",0,0,640,400,0);assert(sister.window);
    assert(sister.model.parameters.fx.slot_count==10);
    assert(TS_SISTER_UI_PARAM_SLOT1_GAIN==71 && TS_SISTER_UI_PARAM_SLOT5_GAIN==107);
    for(int i=0;i<10;++i)action(TS_SISTER_UI_ACTION_FX_ADD);
    assert(sister.model.parameters.fx.slot_count==20 && sister.model.fx_scroll==15);
    action(TS_SISTER_UI_ACTION_FX_ADD);assert(sister.model.parameters.fx.slot_count==20);
    assert(key(SDLK_UP) && sister.model.fx_scroll==14);
    assert(key(SDLK_DOWN) && sister.model.fx_scroll==15);
    sister.model.preset_manage_open=1;assert(!key(SDLK_UP) && sister.model.fx_scroll==15);sister.model.preset_manage_open=0;
    sister.model.fx_page=0;assert(!key(SDLK_UP));sister.model.fx_page=1;
    assert(mouse(SDL_MOUSEBUTTONDOWN,630,54));assert(sister.model.fx_scroll==14 && sister.fx_scroll_repeat==-1);
    assert(mouse(SDL_MOUSEBUTTONUP,630,54));assert(!sister.fx_scroll_repeat);
    sister_fx_scroll_to(&sister,0);
    assert(mouse(SDL_MOUSEBUTTONDOWN,630,75));assert(sister.fx_scroll_drag);
    SDL_Event motion={0};motion.type=SDL_MOUSEMOTION;motion.motion.x=630;motion.motion.y=260;motion.motion.state=SDL_BUTTON_LMASK;
    assert(sister_pedalboard_event(0,&audio,&ui,&sister,&motion));assert(sister.model.fx_scroll==15);
    mouse(SDL_MOUSEBUTTONUP,630,260);
    SDL_Event wheel={0};wheel.type=SDL_MOUSEWHEEL;wheel.wheel.y=-1;
    assert(!sister_pedalboard_event(0,&audio,&ui,&sister,&wheel) && sister.model.fx_scroll==15);

    hit(80,248); /* Visible fifth row is slot 20, not slot 5. */
    assert(sister.model.parameters.fx.slot[19].type==TS_SISTER_FX_REVERB);
    TsSisterUiHit h=ts_sister_ui_hit_test_model(&sister.model,252,248);
    assert(h.index==TS_SISTER_UI_SLOT_PARAMETER(19,1));
    char target[80];assert(ts_sister_ui_midi_target(h,target,sizeof(target)));
    TsSisterUiHit midi;assert(midi_sister_hit_from_target(target,.7f,&midi) && midi.index==h.index);
    float before=sister.model.parameters.fx.slot[19].parameter_a;
    sister_toggle_parameter_lock(&audio,&sister,h.index);
    h.normalized=.1f;sister_apply_action(0,&audio,&ui,&bank,&sister,NULL,NULL,h,48000,2);
    assert(sister.model.parameters.fx.slot[19].parameter_a==before);
    sister_toggle_parameter_lock(&audio,&sister,h.index);
    h.normalized=sister_parameter_wheel_normalized(&sister.model.parameters,h.index,1,0);
    sister_apply_action(0,&audio,&ui,&bank,&sister,NULL,NULL,h,48000,2);
    assert(sister.model.parameters.fx.slot[19].parameter_a>before && sister.model.fx_scroll==15);
    hit(550,248);assert(sister.model.parameters.fx.slot[18].type==TS_SISTER_FX_REVERB);
    hit(590,200);assert(sister.model.parameters.fx.slot[19].type==TS_SISTER_FX_REVERB);
    assert(ui.config.pedalboard_slots==10);
    assert(ts_sister_ui_hit_test_model(&sister.model,20,338).action==TS_SISTER_UI_ACTION_FX_REMOVE);
    assert(ts_sister_ui_hit_test_model(&sister.model,85,338).action==TS_SISTER_UI_ACTION_FX_ADD);
    assert(ts_sister_ui_hit_test_model(&sister.model,160,300).index==TS_SISTER_UI_PARAM_FX_TRANSITION);
    if(argc>1) {
        TsSisterParameters original=sister.model.parameters;
        for(int i=15;i<20;++i) {
            sister_fx_slot_select_type(&sister.model.parameters.fx.slot[i],(TsSisterFxType)(i-14));
            sister.model.parameters.fx.slot[i].mix=.7f;
        }
        ts_sister_ui_render(&frame,&sister.model,&ui.palette);
        assert(ts_ui_write_ppm(&frame,argv[1]));
        sister.model.fx_remove_confirmation=1;
        ts_sister_ui_render(&frame,&sister.model,&ui.palette);
        assert(ts_ui_write_ppm(&frame,"/tmp/pedalboard-confirm.ppm"));
        sister.model.fx_remove_confirmation=0;
        sister.model.parameters=original;
    }
    action(TS_SISTER_UI_ACTION_FX_REMOVE);assert(sister.model.fx_remove_confirmation);
    assert(key(SDLK_ESCAPE) && sister.model.parameters.fx.slot_count==20);
    action(TS_SISTER_UI_ACTION_FX_REMOVE);assert(key(SDLK_RETURN));
    assert(sister.model.parameters.fx.slot_count==19 && sister.model.fx_scroll==14);
    for(int i=19;i>5;--i)action(TS_SISTER_UI_ACTION_FX_REMOVE);
    assert(sister.model.parameters.fx.slot_count==5 && sister.model.fx_scroll==0);
    action(TS_SISTER_UI_ACTION_FX_REMOVE);assert(sister.model.parameters.fx.slot_count==5);
    assert(ui.config.pedalboard_slots==10 && audio.sister.parameters.fx.slot_count==5);
    SDL_DestroyWindow(sister.window);ts_sister_runtime_free(&audio.sister);ts_instrument_free(&bank);SDL_Quit();
    puts("Pedalboard: 5..20 counts, INI defaults, project/preset/legacy restore, live DSP continuity, navigation and MIDI locks passed");
    return 0;
}
