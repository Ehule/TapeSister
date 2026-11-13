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
    assert(sister_board_event(&e,0,&audio,&ui,&sister,&pages,&bank));
    e.type=SDL_MOUSEBUTTONUP;sister_board_event(&e,0,&audio,&ui,&sister,&pages,&bank);settle();
}
int main(int argc,char **argv)
{
    SDL_SetMainReady();SDL_setenv("SDL_VIDEODRIVER","dummy",1);assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER));
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
