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
static TsSamplePages pages;
static SisterWindow sister;
static TsFramebuffer fb;
static void action(TsSisterUiHit hit)
{ sister_apply_action(0,&audio,&ui,&bank,&sister,NULL,NULL,hit,48000,2); }
static void click(int x,int y)
{ action(ts_sister_ui_hit_test_model(&sister.model,x,y)); }
static void snapshot(const char *path,const uint32_t *pixels,int w,int h)
{
    SDL_Surface *surface=SDL_CreateRGBSurfaceFrom((void*)pixels,w,h,32,w*4,0xff0000,0xff00,0xff,0);
    assert(surface && SDL_SaveBMP(surface,path)==0);SDL_FreeSurface(surface);
}
int main(int argc,char **argv)
{
    (void)argv;SDL_SetMainReady();SDL_setenv("SDL_VIDEODRIVER","dummy",1);
    assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER));
    ts_ui_init(&ui);ts_instrument_init(&bank);ts_sister_runtime_init(&audio.sister);
    char error[200];assert(ts_sister_runtime_reconfigure(&audio.sister,48000,2,error,sizeof(error)));
    assert(ts_sample_pages_init(&pages,error,sizeof(error)));
    ts_sister_ui_model_init(&sister.model,&ui.config);sister.model.fx_page=1;
    /* Numeric IDs are persisted in MIDI files and the two lock words. */
    assert(TS_SISTER_UI_PARAM_SLOT1_GAIN==71 && TS_SISTER_UI_PARAM_SLOT4_MIX==90);
    assert(TS_SISTER_UI_PARAM_TAPEHEAD_GAIN==91 && TS_SISTER_UI_PARAM_SLOT5_GAIN==107);
    assert(ts_sister_ui_slot_offset(TS_SISTER_UI_PARAM_TAPEHEAD_GAIN)==-1);
    assert(ts_sister_ui_slot_offset(TS_SISTER_UI_PARAM_PRISM_MIX)==-1);
    for(int i=0;i<TS_SISTER_FX_TRANSIENT;++i)click(80,248);
    assert(sister.model.parameters.fx.slot[4].type==TS_SISTER_FX_TRANSIENT);
    assert(sister.model.parameters.fx.slot[4].parameter_a==.5f && sister.model.parameters.fx.slot[4].parameter_b==.5f);
    for(int field=0;field<5;++field) {
        int id=TS_SISTER_UI_SLOT_PARAMETER(4,field);
        TsSisterUiHit h=ts_sister_ui_hit_test_model(&sister.model,160+field*74,248);
        assert(h.action==TS_SISTER_UI_ACTION_PARAMETER && h.index==id);
        h.normalized=.7f;action(h);assert(fabsf(sister_parameter_normalized(&sister.model.parameters,id)-.7f)<1e-6f);
    }
    float attack=sister.model.parameters.fx.slot[4].parameter_a;
    sister_set_parameter(&sister.model.parameters,TS_SISTER_UI_PARAM_TAPEHEAD_GAIN,.3f);
    sister_set_parameter(&sister.model.parameters,TS_SISTER_UI_PARAM_PRISM_MIX,.2f);
    assert(sister.model.parameters.tapehead_gain==1.2f && sister.model.parameters.prism.mix==.2f);
    assert(sister.model.parameters.fx.slot[4].parameter_a==attack);
    assert(ts_sister_ui_parameter_lock_toggle(&sister.model,TS_SISTER_UI_PARAM_SLOT5_A));
    action((TsSisterUiHit){.action=TS_SISTER_UI_ACTION_PARAMETER,.index=TS_SISTER_UI_PARAM_SLOT5_A,.normalized=.1f});
    assert(sister.model.parameters.fx.slot[4].parameter_a==attack);
    ts_sister_ui_parameter_lock_toggle(&sister.model,TS_SISTER_UI_PARAM_SLOT5_A);
    TsSisterUiHit midi;assert(midi_sister_hit_from_target("sister.fx.slot.5.toggle",1,&midi));
    action(midi);assert(!sister.model.parameters.fx.slot[4].enabled);action(midi);
    click(550,248);assert(sister.model.parameters.fx.slot[3].type==TS_SISTER_FX_TRANSIENT);
    click(590,200);assert(sister.model.parameters.fx.slot[4].type==TS_SISTER_FX_TRANSIENT);
    click(280,270);assert(sister.model.parameters.fx.slot[4].placement==TS_SISTER_FX_PLACE_POST);
    TsSisterUiHit effect=ts_sister_ui_hit_test_model(&sister.model,230,300),master=ts_sister_ui_hit_test_model(&sister.model,500,300);
    assert(effect.index==TS_SISTER_UI_PARAM_FX_TRANSITION && master.index==TS_SISTER_UI_PARAM_MASTER_FX_TRANSITION);
    action(effect);float fx=sister.model.parameters.fx.transition;action(master);assert(sister.model.parameters.fx.transition==fx);

    /* Use the real floating matrix event path, including whole-edit history. */
    matrix_window.window=SDL_CreateWindow("Transient routing",0,0,MATRIX_W,MATRIX_H,0);assert(matrix_window.window);
    matrix_window.id=SDL_GetWindowID(matrix_window.window);matrix_window.visible=1;matrix_window.rename=-1;
    matrix_window.controls=audio.sister.matrix.controls;matrix_window.controls.enabled=1;
    assert(ts_matrix_set(&audio.sister.matrix,&matrix_window.controls,48000));
    matrix_window.outputs=2;matrix_window.processors=0xffffffff;matrix_window.palette=ui.palette;
    mx_rows(&pages,&bank);
    for(int i=0;i<matrix_window.count;++i)if(matrix_window.rows[i].kind==2 && matrix_window.rows[i].index==TS_MATRIX_BOARD)matrix_window.selected=i;
    SDL_Event event={0};event.type=SDL_MOUSEBUTTONDOWN;event.button.windowID=matrix_window.id;
    event.button.button=SDL_BUTTON_LEFT;event.button.x=1100;event.button.y=742;
    assert(matrix_event(&event,NULL,0,&audio,&ui,&pages,&bank));
    assert(audio.sister.matrix.controls.board_insert && !audio.sister.matrix.controls.serial_mask);
    mx_undo(0,&audio,&ui,&pages,&bank,0);assert(!audio.sister.matrix.controls.board_insert);
    mx_undo(0,&audio,&ui,&pages,&bank,1);assert(audio.sister.matrix.controls.board_insert);
    if(argc>1) {
        sister.model.parameters.fx.slot[4].gain_db=0;sister.model.parameters.fx.slot[4].mix=1;
        sister.model.parameters.fx.slot[4].parameter_c=.5f;
        sister.model.routing.fx_transition_active=1;
        sister.model.routing.fx_transition_source=TS_SISTER_FX_TRANSITION_SLOT_5;
        sister.model.routing.fx_transition_progress=.43f;
        sister.model.routing.fx_master_transition_active=1;
        sister.model.routing.fx_master_transition_target_enabled=1;
        sister.model.routing.fx_master_transition_progress=.68f;
        snprintf(sister.model.status,sizeof(sister.model.status),"TRANSIENT: ATTACK +6 DB / SUSTAIN +9.6 DB / STEREO LINKED");
        ts_sister_ui_render(&fb,&sister.model,&ui.palette);snapshot("transient-pedalboard.bmp",fb.pixels,640,400);
        matrix_window.pixels=calloc(MATRIX_W*MATRIX_H,4);assert(matrix_window.pixels);
        mx_paint(&audio,&pages,&bank);snapshot("transient-matrix.bmp",matrix_window.pixels,MATRIX_W,MATRIX_H);
    }
    matrix_close();ts_sample_pages_free(&pages);ts_sister_runtime_free(&audio.sister);ts_instrument_free(&bank);SDL_Quit();
    puts("Fifth-slot native controls, MIDI IDs, locks, reorder, clock hitboxes and matrix undo passed");return 0;
}
