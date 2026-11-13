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
static TsSamplePages pages;
static TsInstrument bank;
static SisterWindow sister;
static void settle(void)
{
    for(int i=0;i<700;++i)ts_matrix_effects_tick(&audio.sister.matrix_effects);
    mx_collect_effects(0,&audio);sister_board_sync(&audio,&ui,&sister);
}
static void click(int x,int y)
{
    int w,h;SDL_GetWindowSize(sister.window,&w,&h);
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=sister.window_id;
    e.button.button=SDL_BUTTON_LEFT;e.button.x=x*w/640;e.button.y=y*h/400;
    int handled=sister_board_event(&e,0,&audio,&ui,&sister,&pages,&bank);
    if(!handled)fprintf(stderr,"unhandled click %d,%d: window %d,%d page %d preset %d board %u scroll %d\n",x,y,w,h,sister.model.fx_page,sister.model.preset_manage_open,sister.board_id,sister.model.fx_scroll);
    assert(handled);
    e.type=SDL_MOUSEBUTTONUP;sister_board_event(&e,0,&audio,&ui,&sister,&pages,&bank);settle();
}
static void live_insertion_mix(void)
{
    for(int type=TS_SISTER_FX_REVERB;type<TS_SISTER_FX_TYPE_COUNT;++type) {
        TsSisterFxSlotControls slot=ts_sister_fx_empty_slot();
        slot.mix=1; /* Existing projects may have empty rows at full mix. */
        sister_fx_slot_select_type(&slot,(TsSisterFxType)type);assert(slot.mix==0);
        slot.mix=.37f;
        for(int next=TS_SISTER_FX_REVERB;next<TS_SISTER_FX_TYPE_COUNT;++next) {
            sister_fx_slot_select_type(&slot,(TsSisterFxType)next);assert(slot.mix==.37f);
        }
        sister_fx_slot_select_type(&slot,TS_SISTER_FX_EMPTY);
        for(int next=TS_SISTER_FX_REVERB;next<TS_SISTER_FX_TYPE_COUNT;++next) {
            sister_fx_slot_select_type(&slot,(TsSisterFxType)next);assert(slot.mix==0);
        }
    }
}
int main(int argc,char **argv)
{
    SDL_SetMainReady();SDL_setenv("SDL_VIDEODRIVER","dummy",1);assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER));
    live_insertion_mix();
    ts_ui_init(&ui);ts_instrument_init(&bank);char error[256];assert(ts_sample_pages_init(&pages,error,sizeof(error)));
    ts_sister_runtime_init(&audio.sister);audio.output_rate=48000;audio.output_device_channels=2;
    ts_sister_ui_model_init(&sister.model,&ui.config);sister.model.parameters=audio.sister.parameters;
    static TsMatrixControls c;c=audio.sister.matrix.controls;c.enabled=1;
    for(int i=0;i<10;++i){assert(ts_matrix_effect_add(&c,TS_MATRIX_FX_PEDALBOARD)==i);c.effect[i].board.slot[0].type=TS_SISTER_FX_TRANSIENT;}
    assert(ts_matrix_effects_restore(&audio.sister.matrix_effects,&c,48000));assert(ts_matrix_set(&audio.sister.matrix,&c,48000));
    matrix_window.controls=ui.config.matrix=c;matrix_window.layout=&audio.sister.matrix_layout;mx_bind_mosaic(&ui,&pages);
    TsSisterFxControls shared=audio.sister.parameters.fx;
    sister_board_open(c.effect[0].id);sister_board_sync(&audio,&ui,&sister);
    assert(sister.board_id==c.effect[0].id && sister.model.board_selected==1 && sister.model.board_count==11);
    click(190,55);
    float edited=audio.sister.matrix.controls.effect[0].board.slot[0].gain_db;
    assert(edited!=0 && audio.sister.matrix.controls.effect[1].board.slot[0].gain_db==0);
    assert(!memcmp(&shared,&audio.sister.parameters.fx,sizeof(shared)));
    assert(!memcmp(&shared,&sister.model.parameters.fx,sizeof(shared)));
    int cursor=matrix_window.history_cursor;assert(cursor>0);
    click(145,18);assert(sister.board_id==c.effect[1].id && matrix_window.history_cursor==cursor);
    click(100,18);assert(sister.board_id==c.effect[0].id);
    assert(sister_board_view(&sister)->parameters.fx.slot[0].gain_db==edited);
    SDL_Event key={0};key.type=SDL_KEYDOWN;key.key.windowID=sister.window_id;key.key.keysym.sym=SDLK_z;key.key.keysym.mod=KMOD_CTRL;
    assert(sister_board_event(&key,0,&audio,&ui,&sister,&pages,&bank));settle();
    assert(audio.sister.matrix.controls.effect[0].board.slot[0].gain_db==0);
    key.key.keysym.mod=KMOD_CTRL|KMOD_SHIFT;assert(sister_board_event(&key,0,&audio,&ui,&sister,&pages,&bank));settle();
    assert(audio.sister.matrix.controls.effect[0].board.slot[0].gain_db==edited);
    for(int i=10;i<20;++i)click(85,342);
    assert(audio.sister.matrix.controls.effect[0].board.count==20);
    assert(sister_board_view(&sister)->parameters.fx.slot_count==20 && sister.model.fx_scroll==15);
    /* Populate the last slot, require confirmation, then cancel it. */
    click(100,244);assert(audio.sister.matrix.controls.effect[0].board.slot[19].type==TS_SISTER_FX_REVERB);
    assert(audio.sister.matrix.controls.effect[0].board.slot[19].mix==0);
    click(22,342);assert(sister.model.fx_remove_confirmation);
    click(370,219);assert(!sister.model.fx_remove_confirmation && audio.sister.matrix.controls.effect[0].board.count==20);
    /* Keyboard scrolling uses this board's 20 rows, not P1's ten. Enter must
       confirm removal here without reaching the original-board handler. */
    key.key.keysym.mod=0;key.key.keysym.sym=SDLK_UP;
    assert(sister_board_event(&key,0,&audio,&ui,&sister,&pages,&bank));assert(sister.model.fx_scroll==14);
    key.key.keysym.sym=SDLK_DOWN;
    assert(sister_board_event(&key,0,&audio,&ui,&sister,&pages,&bank));assert(sister.model.fx_scroll==15);
    click(22,342);assert(sister.model.fx_remove_confirmation);
    key.key.keysym.sym=SDLK_RETURN;
    assert(sister_board_event(&key,0,&audio,&ui,&sister,&pages,&bank));settle();
    assert(!sister.model.fx_remove_confirmation && audio.sister.matrix.controls.effect[0].board.count==19);
    assert(!memcmp(&shared,&audio.sister.parameters.fx,sizeof(shared)));
    click(85,342);assert(audio.sister.matrix.controls.effect[0].board.count==20);
    click(420,18);assert(sister.model.board_tab_first==8);
    click(100,18);assert(sister.board_id==c.effect[8].id && sister.model.board_selected==9);
    sister_board_open(c.effect[0].id);sister_board_sync(&audio,&ui,&sister);
    const int sizes[][2]={{640,400},{960,600},{1280,800},{1366,768}};
    for(int i=0;i<4;++i){SDL_SetWindowSize(sister.window,sizes[i][0],sizes[i][1]);click(145,18);assert(sister.board_id==c.effect[1].id);click(100,18);assert(sister.board_id==c.effect[0].id);}
    if(argc>1) {
        sister.model.fx_scroll=0;
        ts_sister_ui_render(&sister.framebuffer,sister_board_view(&sister),&ui.palette);
        SDL_Surface *surface=SDL_CreateRGBSurfaceFrom(sister.framebuffer.pixels,640,400,32,2560,0xff0000,0xff00,0xff,0);
        assert(surface && !SDL_SaveBMP(surface,argv[1]));SDL_FreeSurface(surface);
    }
    /* MIDI uses a saved child ID and continues to address that processor
       after reordering, tab changes, and project-owned board edits. */
    sister.model.fx_scroll=0;
    uint32_t child=audio.sister.matrix.controls.effect[0].board.slot[0].id;
    char target[80];snprintf(target,sizeof(target),"board.param.%u.%u.0",sister.board_id,child);
    assert(ts_midi_target_is_continuous(target));
    matrix_window.controls=audio.sister.matrix.controls;
    TsSisterFxSlotControls tmp=matrix_window.controls.effect[0].board.slot[0];
    matrix_window.controls.effect[0].board.slot[0]=matrix_window.controls.effect[0].board.slot[3];matrix_window.controls.effect[0].board.slot[3]=tmp;
    mx_commit_controls(0,&audio,&ui);settle();click(145,18);
    assert(sister_board_midi(0,&audio,&ui,&sister,target,.9f));settle();
    assert(fabsf(audio.sister.matrix.controls.effect[0].board.slot[3].gain_db-9.6f)<1e-5f);
    assert(audio.sister.matrix.controls.effect[1].board.slot[3].gain_db==0);
    float pickup;assert(sister_board_midi_value(target,&ui,&sister,&pickup) && fabsf(pickup-.9f)<1e-5f);
    sister_board_open(c.effect[0].id);sister_board_sync(&audio,&ui,&sister);
    /* The native picker cycle reaches SunVox, opens the shared editor, and
       edits only this child. P1 and neighboring boards retain their controls. */
    matrix_window.controls=audio.sister.matrix.controls;
    matrix_window.controls.effect[0].board.slot[0].type=TS_SISTER_FX_RESONATOR;
    mx_commit_controls(0,&audio,&ui);settle();click(100,52);
    assert(sunvox_editor.child==audio.sister.matrix.controls.effect[0].board.slot[0].id && sunvox_editor.node==c.effect[0].id);
    assert(svfx_controls() && audio.sister.matrix.controls.effect[0].board.slot[0].type==TS_SISTER_FX_SUNVOX);
    svfx_controls()->value[0]=12000;svfx_commit(0,&audio,&ui);settle();
    assert(audio.sister.matrix.controls.effect[0].board.slot[0].sunvox.value[0]==12000);
    snprintf(target,sizeof(target),"board.param.%u.%u.100",sunvox_editor.node,sunvox_editor.child);
    assert(sister_board_midi(0,&audio,&ui,&sister,target,.25f));
    assert(audio.sister.matrix.controls.effect[0].board.slot[0].sunvox.value[0]==8192);
    assert(!memcmp(&shared,&audio.sister.parameters.fx,sizeof(shared)));svfx_back();sister_board_sync(&audio,&ui,&sister);
    /* Board preset recall carries nested assets without changing P1, matrix
       routes, or an adjacent board. Existing row identities remain local. */
    TsSisterParameters stored=sister_board_view(&sister)->parameters;
    size_t preset=pedalboard_presets.count;
    assert(ts_sister_preset_save_new(&pedalboard_presets,"Nested board",&stored,48000,error,sizeof(error)));
    assert(ts_sister_preset_save(&pedalboard_presets,"board-controller-presets.ini",error,sizeof(error)));
    assert(ts_sister_preset_load(&pedalboard_presets,"board-controller-presets.ini",48000,error,sizeof(error)));remove("board-controller-presets.ini");
    matrix_window.controls=audio.sister.matrix.controls;matrix_window.controls.effect[0].board.slot[0].sunvox.value[0]=22000;mx_commit_controls(0,&audio,&ui);settle();
    assert(sister_board_preset_recall(0,&audio,&ui,&sister,preset));settle();
    assert(audio.sister.matrix.controls.effect[0].board.slot[0].sunvox.value[0]==8192);
    assert(!memcmp(&shared,&audio.sister.parameters.fx,sizeof(shared)));
    /* P1 uses the same child picker, including its stable MIDI target. */
    sister_board_open(0);sister_board_sync(&audio,&ui,&sister);
    sister.model.fx_scroll=5;click(100,244);
    assert(audio.sister.parameters.fx.slot[9].type==TS_SISTER_FX_REVERB && audio.sister.parameters.fx.slot[9].mix==0);
    /* Refreshing shared P1 must retain scrolling and its removal dialog. */
    sister.model.fx_scroll=3;sister.model.fx_remove_confirmation=1;
    sister_board_sync(&audio,&ui,&sister);
    assert(sister.model.fx_scroll==3 && sister.model.fx_remove_confirmation);
    sister.model.fx_scroll=0;sister.model.fx_remove_confirmation=0;
    sister.model.parameters.fx.slot[0].type=TS_SISTER_FX_RESONATOR;
    assert(sister_board_publish(0,&audio,&ui,&sister));click(100,52);
    assert(!sunvox_editor.node && sunvox_editor.child && svfx_controls());
    svfx_controls()->value[0]=10000;svfx_commit(0,&audio,&ui);assert(audio.sister.parameters.fx.slot[0].sunvox.value[0]==10000);
    svfx_back();sister_board_sync(&audio,&ui,&sister);
    sister.model.parameters.fx=shared;assert(sister_board_publish(0,&audio,&ui,&sister));
    sister_board_open(c.effect[0].id);sister_board_sync(&audio,&ui,&sister);
    if(argc>1) {
        sister.model.fx_scroll=0;ts_sister_ui_render(&sister.framebuffer,sister_board_view(&sister),&ui.palette);
        SDL_Surface *surface=SDL_CreateRGBSurfaceFrom(sister.framebuffer.pixels,640,400,32,2560,0xff0000,0xff00,0xff,0);
        assert(surface && !SDL_SaveBMP(surface,argv[1]));SDL_FreeSurface(surface);
    }
    /* Stale tabs never attach to a replacement occupying the old port. */
    uint32_t old=sister.board_id;matrix_window.controls=audio.sister.matrix.controls;
    ts_matrix_effect_remove(&matrix_window.controls,0);
    assert(ts_matrix_effect_add(&matrix_window.controls,TS_MATRIX_FX_PEDALBOARD)==0);
    assert(matrix_window.controls.effect[0].id!=old);mx_commit_controls(0,&audio,&ui);settle();
    assert(sister.board_id==0 && sister.model.board_selected==0);
    assert(!memcmp(&shared,&audio.sister.parameters.fx,sizeof(shared)));
    sister_window_hide(&sister);matrix_history_clear();ts_sister_runtime_disable(&audio.sister);
    ts_sample_pages_free(&pages);ts_instrument_free(&bank);SDL_Quit();
    puts("pedalboard tabs: isolation, undo/redo, 20 slots, confirmation, paging, scaling and stale IDs passed");return 0;
}
