#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>
#include "tapesister/audio_assets.h"
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define TEST_MKDIR(path) _mkdir(path)
#define TEST_CHDIR(path) _chdir(path)
#else
#define TEST_MKDIR(path) mkdir(path,0700)
#define TEST_CHDIR(path) chdir(path)
#endif
static AudioState audio;
static TsUiState ui;
static TsSamplePages pages;
static TsInstrument bank;
static SisterWindow sister;
static char error[256];
static void eq_roundtrip_and_audio(void)
{
    TsSisterFxSlotControls slot=ts_sister_fx_empty_slot();sister_fx_slot_select_type(&slot,TS_SISTER_FX_EQ);
    slot.mix=1;slot.eq.band[2].gain_db=9;slot.eq.band[2].q=2;
    static TsSisterProjectState saved,loaded;ts_sister_project_state_init(&saved,48000);
    saved.parameters.fx.slot_count=1;saved.parameters.fx.slot[0]=slot;
    for(int i=1;i<TS_SISTER_FX_SLOT_COUNT;++i)saved.parameters.fx.slot[i]=ts_sister_fx_empty_slot();
    int eq=ts_matrix_effect_add(&saved.matrix,TS_MATRIX_FX_EQ),board=ts_matrix_effect_add(&saved.matrix,TS_MATRIX_FX_PEDALBOARD);
    assert(eq>=0 && board>=0);saved.matrix.effect[eq].eq=slot.eq;
    saved.matrix.effect[board].board.count=1;saved.matrix.effect[board].board.slot[0]=slot;
    assert(ts_sister_project_state_save_file(&saved,"editor-sync.tsr",error,sizeof(error)));
    int present=0;assert(ts_sister_project_state_load_file(&loaded,"editor-sync.tsr",48000,&present,error,sizeof(error)) && present);
    assert(loaded.parameters.fx.slot_count==1);
    assert(loaded.matrix.effect[board].board.count==1);
    assert(!memcmp(&slot.eq,&loaded.parameters.fx.slot[0].eq,sizeof(slot.eq)));
    assert(!memcmp(&slot.eq,&loaded.matrix.effect[eq].eq,sizeof(slot.eq)));
    assert(!memcmp(&slot.eq,&loaded.matrix.effect[board].board.slot[0].eq,sizeof(slot.eq)));remove("editor-sync.tsr");
    static TsSisterPresetBank presets,read;ts_sister_preset_bank_init(&presets,48000);
    assert(ts_sister_preset_save_new(&presets,"EQ recall",&saved.parameters,48000,error,sizeof(error)));
    assert(ts_sister_preset_save(&presets,"editor-sync.ini",error,sizeof(error)));
    assert(ts_sister_preset_load(&read,"editor-sync.ini",48000,error,sizeof(error)));
    static TsSisterParameters recalled;assert(ts_sister_preset_recall(&read,read.count-1,&recalled));
    assert(recalled.fx.slot_count==1 && !memcmp(&slot.eq,&recalled.fx.slot[0].eq,sizeof(slot.eq)));remove("editor-sync.ini");
    TsSisterSingleFx *single=ts_sister_single_fx_new(TS_SISTER_FX_EQ,48000,1);assert(single);ts_sister_single_fx_set(single,&slot);
    TsPedalboard *b=ts_pedalboard_new(&saved.matrix.effect[board].board,48000,1);assert(b);
    static TsMatrixEffects effects;ts_matrix_effects_init(&effects);assert(ts_matrix_effects_restore(&effects,&saved.matrix,48000));
    static TsSisterPostFxEngine original;assert(ts_sister_post_fx_init(&original,48000));ts_sister_post_fx_sync_controls(&original,&saved.parameters.fx);
    double in_energy=0,out_energy=0;
    for(int i=0;i<48000;++i){float x=.1f*sinf((float)(i*6.283185307179586*1000/48000));TsStereoFrame in={x,-x};
        TsStereoFrame a=ts_sister_single_fx_process(single,in,1),bb=ts_pedalboard_process(b,in);
        ts_matrix_effects_tick(&effects);TsStereoFrame c=ts_matrix_effects_process(&effects,eq,in,(TsStereoFrame){0},1);
        TsStereoFrame d=ts_sister_post_fx_process(&original,TS_SISTER_EFFECT_PROCESSOR_COUNT-1,in,0);
        assert(isfinite(a.l) && fabsf(a.l-bb.l)<.00001f && fabsf(a.l-c.l)<.00001f);
        if(i>24000){assert(fabsf(d.l-a.l)<.001f);in_energy+=x*x;out_energy+=a.l*a.l;}
    }
    assert(out_energy/in_energy>7 && out_energy/in_energy<9);
    TsStereoFrame bypass=ts_sister_single_fx_process(single,(TsStereoFrame){.2f,-.1f},0);assert(bypass.l==.2f && bypass.r==-.1f);
    ts_sister_single_fx_free(single);ts_pedalboard_free(b);ts_matrix_effects_free(&effects);ts_sister_post_fx_free(&original);
}
static void board_click(int x,int y)
{
    int width,height;SDL_GetWindowSize(sister.window,&width,&height);
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=sister.window_id;e.button.button=SDL_BUTTON_LEFT;
    e.button.x=x*width/640;e.button.y=y*height/400;
    assert(sister_board_event(&e,0,&audio,&ui,&sister,&pages,&bank));
    e.type=SDL_MOUSEBUTTONUP;sister_board_event(&e,0,&audio,&ui,&sister,&pages,&bank);
    ts_matrix_effects_finish_edits(&audio.sister.matrix_effects);mx_collect_effects(0,&audio);sister_board_sync(&audio,&ui,&sister);
}
static void board_operations(void)
{
    sister_board_open(0);sister_board_sync(&audio,&ui,&sister);
    int count=audio.sister.parameters.fx.slot_count;board_click(85,342);assert(audio.sister.parameters.fx.slot_count==count+1);
    int width,height;SDL_GetWindowSize(sister.window,&width,&height);
    SDL_Event wheel={0};wheel.type=SDL_MOUSEWHEEL;wheel.wheel.windowID=sister.window_id;wheel.wheel.y=2;
#if SDL_VERSION_ATLEAST(2,26,0)
    wheel.wheel.mouseX=54*width/640;wheel.wheel.mouseY=342*height/400;
#else
    SDL_WarpMouseInWindow(sister.window,54*width/640,342*height/400);
#endif
    assert(sister_board_event(&wheel,0,&audio,&ui,&sister,&pages,&bank));assert(sister.model.fx_add_count==3);
    board_click(85,342);assert(audio.sister.parameters.fx.slot_count==count+4);
    sister.model.fx_scroll=0;uint32_t survivor=audio.sister.parameters.fx.slot[2].id;
    board_click(606,TS_SISTER_UI_FX_TOP+TS_SISTER_UI_FX_ROW_STEP+10);
    assert(audio.sister.parameters.fx.slot_count==count+3 && audio.sister.parameters.fx.slot[1].id==survivor);
    mx_undo(0,&audio,&ui,&pages,&bank,0);sister_board_sync(&audio,&ui,&sister);
    assert(audio.sister.parameters.fx.slot_count==count+4 && audio.sister.parameters.fx.slot[2].id==survivor);
    while(audio.sister.parameters.fx.slot_count>1){sister.model.fx_scroll=0;board_click(606,TS_SISTER_UI_FX_TOP+10);}
    board_click(606,TS_SISTER_UI_FX_TOP+10);assert(audio.sister.parameters.fx.slot_count==1 && audio.sister.parameters.fx.slot[0].type==TS_SISTER_FX_EMPTY);
    assert(ts_sister_ui_fx_scroll(&sister.model)==0);
}
static void shared_cards(void)
{
    TsMosaic *m=ui.mosaic;TsSample sample={.sample_rate=48000,.channels=2,.frames=4096};sample.data=calloc(sample.frames*2,sizeof(float));strcpy(sample.name,"Mosaic original");
    TsMosaicSource *source=ts_mosaic_source(m,&sample,error,sizeof(error));assert(source);ts_sample_free(&sample);
    TsMosaicEvent *e=ts_mosaic_add(m,source,0,0);assert(e);uint64_t id=e->id;
    mx_bind_mosaic(&ui,&pages);matrix_window.layout=&audio.sister.matrix_layout;matrix_window.palette=ui.palette;matrix_window.controls=audio.sister.matrix.controls;mx_rows(&pages,&bank);
    MatrixRow row={0};for(int i=0;i<matrix_window.count;++i)if(matrix_window.rows[i].kind==3 && matrix_window.rows[i].tile==id)row=matrix_window.rows[i];assert(row.kind==3);
    TsFxCanvasPosition *p=fx_position(row,1);strcpy(p->name,"Stale FX alias");p->color=0xff010203;
    assert(fx_source_color(row)==ts_ui_mosaic_card_color(&ui.palette,e));
    FxCanvasNode node={.row=row};assert(!strcmp(fx_node_title(&node),e->name));
    matrix_window.selection_count=1;matrix_window.selection[0]=row;
    fx_properties_begin(row);assert(!strcmp(fx_canvas.properties_name,"Mosaic original") && fx_canvas.properties_color==0);
    strcpy(fx_canvas.properties_name,"Both canvases");fx_canvas.properties_color=0xff9173bd;
    SDL_Event enter={0};enter.type=SDL_KEYDOWN;enter.key.windowID=fx_properties_window.id;enter.key.keysym.sym=SDLK_RETURN;
    assert(matrix_event(&enter,NULL,0,&audio,&ui,&pages,&bank));
    assert(!strcmp(e->name,"Both canvases") && e->color==0xff9173bd);
    assert(fx_wire_color(row,TS_MATRIX_MAIN)==e->color);
    fx_wire_set_color((FxCanvasWire){.source=row,.destination=TS_MATRIX_MAIN},0xffabcabc);
    e->color=0xffbc78de;assert(fx_wire_color(row,TS_MATRIX_MAIN)==0xffabcabc);
    assert(fx_wire_color(row,TS_FX_MOSAIC_BUS)==e->color);
    assert(fx_source_toggle(0,&ui,0,0)==1 && e->muted);
    assert(fx_source_toggle(0,&ui,1,0)==1 && e->solo);
    assert(ts_mosaic_undo(m,0) && !e->solo && e->muted);assert(ts_mosaic_undo(m,0) && !e->muted);
    assert(ts_mosaic_undo(m,0) && !strcmp(e->name,"Mosaic original") && e->color==0);
    assert(ts_mosaic_undo(m,1) && !strcmp(e->name,"Both canvases"));
    e->color=0xffbc78de;
    assert(!TEST_MKDIR("editor-sync-mosaic") || errno==EEXIST);
    assert(ts_mosaic_save(m,"editor-sync-mosaic",error,sizeof(error)));
    TsMosaic *loaded=ts_mosaic_create();assert(ts_mosaic_load(loaded,"editor-sync-mosaic",error,sizeof(error)));
    assert(ts_mosaic_find(loaded,id)->color==e->color && !strcmp(ts_mosaic_find(loaded,id)->name,e->name));
    assert(!TEST_MKDIR("editor-sync-shared") || errno==EEXIST);
    assert(!TEST_MKDIR("editor-sync-shared/samples") || errno==EEXIST);
    assert(!TEST_MKDIR("editor-sync-shared/samples/audio") || errno==EEXIST);
    remove("editor-sync-shared/mosaic-000.tsa");
    TsAudioAssets *assets=ts_audio_assets_begin("editor-sync-shared",1);assert(assets);
    assert(ts_mosaic_save(m,"editor-sync-shared",error,sizeof(error)));ts_audio_assets_end(assets);
    assert(ts_mosaic_load(loaded,"editor-sync-shared",error,sizeof(error)));
    assert(ts_mosaic_find(loaded,id)->color==e->color && !strcmp(ts_mosaic_find(loaded,id)->name,e->name));
    ts_mosaic_free(loaded);
    MosaicController controller={0};controller.pages=&pages;mosaic_select_only(&ui,id);
    SDL_Event f2={0};f2.type=SDL_KEYDOWN;f2.key.keysym.sym=SDLK_F2;
    assert(mosaic_context_event(&f2,0,&audio,&ui,&bank,&controller,0,0));assert(fx_canvas.properties);
    fx_properties_close();
    SDL_Event menu={0};menu.type=SDL_MOUSEBUTTONDOWN;menu.button.button=SDL_BUTTON_RIGHT;
    assert(mosaic_context_event(&menu,0,&audio,&ui,&bank,&controller,170,67) && ui.mosaic_context_open);
    menu.button.button=SDL_BUTTON_LEFT;
    assert(mosaic_context_event(&menu,0,&audio,&ui,&bank,&controller,ui.mosaic_context_x+10,ui.mosaic_context_y+10));
    assert(fx_canvas.properties && !ui.mosaic_context_open);fx_properties_close();
    /* Same local event ID in another movement must not inherit the edit. */
    assert(ts_sample_pages_enable_gallery(&pages,error,sizeof(error)));
    TsMosaicGallery *gallery=pages.gallery;assert(ts_gallery_add(gallery,error,sizeof(error)));
    TsMosaic *second=gallery->canvas[1].scene;
    TsMosaicSource *other_source=ts_mosaic_source(second,&source->sample,error,sizeof(error));assert(other_source);
    TsMosaicEvent *other=ts_mosaic_add(second,other_source,0,0);assert(other && other->id==id);
    other->color=0xff447799;strcpy(other->name,"Second movement");
    ui.mosaic_gallery=gallery;mx_bind_mosaic(&ui,&pages);mx_rows(&pages,&bank);
    row.canvas=gallery->canvas[0].id;
    matrix_window.selection[0]=row;matrix_window.selection_count=1;
    assert(fx_source_toggle(0,&ui,0,0)==1 && e->muted && !other->muted);
    MatrixRow other_row=row;other_row.canvas=gallery->canvas[1].id;
    assert(fx_source_color(other_row)==other->color && fx_source_color(row)==e->color);
}
static void eq_window_behavior(void)
{
    SDL_Window *main_window=SDL_CreateWindow("Mosaic",0,0,640,400,SDL_WINDOW_HIDDEN);assert(main_window);
    Uint32 origin=sister.window_id;int mosaic=ui.mosaic_open;
    eq_editor_open(0,&audio,&ui,-1,0,0,origin);assert(eq_editor.visible && !ui.master_eq_open);
    assert(SDL_GetWindowFlags(eq_editor.window)&SDL_WINDOW_RESIZABLE);
    SDL_SetWindowSize(eq_editor.window,800,500);eq_editor_paint(0,&audio,&ui);
    SDL_Event level={0};level.type=SDL_MOUSEBUTTONDOWN;level.button.windowID=eq_editor.id;level.button.button=SDL_BUTTON_LEFT;
    level.button.x=(419*800+639)/640;level.button.y=350*500/400;
    assert(eq_editor_event(&level,main_window,0,&audio,&ui,&sister,&pages,&bank));
    assert(ui.config.master_output_percent==50 && fabsf(ui.master_output.gain-.5f)<.01f);
    level.type=SDL_MOUSEBUTTONUP;assert(eq_editor_event(&level,main_window,0,&audio,&ui,&sister,&pages,&bank));
    ui.midi_learn_active=1;level.type=SDL_MOUSEBUTTONDOWN;level.button.x=60*800/640;level.button.y=312*500/400;
    assert(eq_editor_event(&level,main_window,0,&audio,&ui,&sister,&pages,&bank));
    assert(!strcmp(ui.midi_learn_pending,"main.eq.band.1.0"));ui.midi_learn_active=0;ui.midi_learn_pending[0]=0;
    if(getenv("TS_TEST_EDITOR_PPM"))assert(ts_ui_write_ppm(&eq_editor.fb,getenv("TS_TEST_EDITOR_PPM")));
    SDL_Event escape={0};escape.type=SDL_KEYDOWN;escape.key.windowID=eq_editor.id;escape.key.keysym.sym=SDLK_ESCAPE;
    assert(eq_editor_event(&escape,main_window,0,&audio,&ui,&sister,&pages,&bank));assert(!eq_editor.visible && ui.mosaic_open==mosaic && sister.model.visible);
    TsSisterParameters next=audio.sister.parameters;sister_fx_slot_select_type(&next.fx.slot[0],TS_SISTER_FX_EQ);
    ts_sister_runtime_set_parameters(&audio.sister,&next);
    eq_editor_open(0,&audio,&ui,0,0,audio.sister.parameters.fx.slot[0].id,origin);assert(eq_editor.visible);
    eq_editor.model->config.master_eq.band[1].gain_db=6;eq_editor_publish(0,&audio,&ui,&sister);
    assert(audio.sister.parameters.fx.slot[0].eq.band[1].gain_db==6 && audio.sister.master_eq.live.band[1].gain_db==0);
    next=audio.sister.parameters;next.fx.slot[0]=ts_sister_fx_empty_slot();ts_sister_runtime_set_parameters(&audio.sister,&next);
    assert(eq_editor_event(&escape,main_window,0,&audio,&ui,&sister,&pages,&bank));assert(!eq_editor.visible);
    matrix_show(0,&audio,&ui,&pages,&bank,-1,-1);fx_view(1,&pages,&bank);fx_build(&pages,&bank);fx_fit();
    int fixed=-1;for(int i=0;i<fx_canvas.count;++i)if(fx_canvas.node[i].row.kind==2 && fx_canvas.node[i].row.index==TS_MATRIX_EQ)fixed=i;
    assert(fixed>=0);FxCanvasNode *n=&fx_canvas.node[fixed];
    SDL_Event click={0};click.type=SDL_MOUSEBUTTONDOWN;click.button.windowID=matrix_window.id;click.button.button=SDL_BUTTON_LEFT;click.button.clicks=2;
    assert(fx_event(&click,main_window,0,&audio,&ui,&pages,&bank,fx_x(n->x+fx_node_w(n)/2),fx_y(n->y+fx_node_h(n)/2),0));
    assert(eq_editor.visible && eq_editor.source==-1 && eq_editor.return_id==matrix_window.id);
    SDL_Event release={0};release.type=SDL_KEYUP;release.key.windowID=eq_editor.id;release.key.keysym.sym=SDLK_a;
    assert(!eq_editor_event(&release,main_window,0,&audio,&ui,&sister,&pages,&bank));assert(release.key.windowID==SDL_GetWindowID(main_window));
    escape.key.windowID=eq_editor.id;assert(eq_editor_event(&escape,main_window,0,&audio,&ui,&sister,&pages,&bank));
    assert(!eq_editor.visible && matrix_window.visible && ui.mosaic_open==mosaic);
    SDL_DestroyWindow(main_window);
}
int main(void)
{
    SDL_SetMainReady();SDL_setenv("SDL_VIDEODRIVER","dummy",1);assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER));
    /* Asset writes are transactional; each repeated test needs a fresh root. */
    char work[128];snprintf(work,sizeof(work),"editor-sync-%llu",(unsigned long long)SDL_GetPerformanceCounter());
    assert(!TEST_MKDIR(work) && !TEST_CHDIR(work));
    eq_roundtrip_and_audio();ts_ui_init(&ui);ts_instrument_init(&bank);assert(ts_sample_pages_init(&pages,error,sizeof(error)));
    ts_sister_runtime_init(&audio.sister);audio.output_rate=48000;audio.output_device_channels=2;
    pages.mosaic=ts_mosaic_create();assert(pages.mosaic);ui.mosaic=pages.mosaic;ui.mosaic_open=1;ts_sister_ui_model_init(&sister.model,&ui.config);sister.model.parameters=audio.sister.parameters;
    mx_bind_mosaic(&ui,&pages);matrix_window.layout=&audio.sister.matrix_layout;
    board_operations();shared_cards();eq_window_behavior();
    eq_editor_destroy();matrix_close();if(sister.window)SDL_DestroyWindow(sister.window);
    ts_sister_runtime_free(&audio.sister);ts_sample_pages_free(&pages);ts_instrument_free(&bank);SDL_Quit();
    puts("EQ audio/recall, slot operations, shared card styles, cable inheritance and editor windows passed.");return 0;
}
