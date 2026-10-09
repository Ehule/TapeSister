#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#define TAPEHEAD_EMBEDDED
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>
#include "../third_party/tapehead/application/src/ft2_replayer.h"
#include "../third_party/tapehead/application/src/ft2_structs.h"
#include "../third_party/tapehead/application/src/ft2_pattern_draw.h"
#include "../third_party/tapehead/application/src/ft2_tables.h"
#include "../third_party/tapehead/application/src/ft2_config.h"
static AudioState a;
static TsUiState u;
static TsSamplePages pages;
static TsInstrument bank;
static SDL_Window *window;
static char error[256];
static void click(int x,int y,int button)
{
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=source_route_window.id;
    e.button.x=x;e.button.y=y;e.button.button=button;
    assert(source_route_event(&e,window,0,&a,&u,&pages,&bank));
    e.type=SDL_MOUSEBUTTONUP;assert(source_route_event(&e,window,0,&a,&u,&pages,&bank));
}
static void shot(const char *directory,const char *name)
{
    if(!directory)return;
    source_route_capture(&a,&pages,&bank);
    ts_source_route_ui_render(source_route_window.framebuffer,&source_route_window.model,&u.palette);
    char path[1024];snprintf(path,sizeof(path),"%s/%s.bmp",directory,name);
    SDL_Surface *surface=SDL_CreateRGBSurfaceFrom(source_route_window.framebuffer->pixels,640,400,32,640*4,0xff0000,0xff00,0xff,0xff000000);
    assert(surface&&!SDL_SaveBMP(surface,path));SDL_FreeSurface(surface);
}
static void render(double energy[4])
{
    float out[512*4];memset(energy,0,4*sizeof(*energy));
    for(int block=0;block<4;++block) {
        audio_callback(&a,(Uint8*)out,sizeof(out));
        if(block==3)for(int f=0;f<512;++f)for(int ch=0;ch<4;++ch) {
            assert(isfinite(out[f*4+ch])&&fabsf(out[f*4+ch])<=1);energy[ch]+=fabsf(out[f*4+ch]);
        }
    }
}
static void shared_send_controls(const char *directory)
{
    ts_tapehead_stop();ts_note_bank_clear(&a.notes);a.output_device_channels=4;
    pages.tracker.lanes[0].output_route=(TsSourceRoute){.mode=TS_SOURCE_INHERIT};
    bank.bank[0].output_route=(TsSourceRoute){.mode=TS_SOURCE_PAIR,.speaker=2,.second=3};
    ts_tapehead_routes_changed(bank.bank[0].tile_id,bank.bank[0].output_route);
    assert(source_route_show(0,&a,&u,&pages,&bank,-1,0));
    click(399,20,SDL_BUTTON_LEFT);assert(source_route_window.model.page==1);
    click(100,89,SDL_BUTTON_LEFT);click(226,152,SDL_BUTTON_LEFT);click(546,191,SDL_BUTTON_LEFT);
    assert(bank.bank[0].output_route.mix_enabled && bank.bank[0].output_route.clean_level==0);
    assert(bank.bank[0].output_route.send_level[TS_SEND_PRISM]==100);
    TsSisterParameters parameters=a.sister.parameters;
    parameters.prism.enabled=1;parameters.prism.lenses=1;parameters.prism.body=1;
    parameters.prism.mix=.25f;parameters.prism.morph_enabled=0;
    ts_sister_runtime_set_parameters(&a.sister,&parameters);
    click(520,20,SDL_BUTTON_LEFT);assert(source_route_window.model.page==2);
    click(500,48,SDL_BUTTON_LEFT);assert(a.sister.router.controls.send_mask==1);
    assert(ts_note_bank_start(&a.notes,&bank,TS_AUDITION_CURRENT,12,0,48000)==TS_NOTE_STARTED);
    double energy[4];render(energy);render(energy);
    assert(energy[0]>1 && energy[1]>energy[0] && energy[2]==0 && energy[3]==0);
    uint64_t clock=a.sister.prism.clock;render(energy);assert(a.sister.prism.clock-clock==2048);
    click(32,224,SDL_BUTTON_LEFT);render(energy);assert(energy[0]>1 && energy[1]==0 && energy[2]==0 && energy[3]==0);
    click(188,224,SDL_BUTTON_RIGHT);click(502,347,SDL_BUTTON_LEFT);
    assert(a.sister.router.controls.return_level[0]==50 && a.sister.router.controls.return_route[0].width==0);
    shot(directory,"shared-prism-return");
    click(399,20,SDL_BUTTON_LEFT);shot(directory,"tile-shared-sends");
    /* Source gain edits affect held voices, and muting clean cannot leak Main. */
    click(226,191,SDL_BUTTON_LEFT);render(energy);render(energy);
    assert(energy[0]==0 && energy[1]==0 && energy[2]==0 && energy[3]==0);
    click(546,191,SDL_BUTTON_LEFT);render(energy);assert(energy[0]>1 && energy[1]>1);
    /* Keyboard DRY capture keeps the source even when its audible clean level is zero. */
    assert(a.keyboard_dry.l>0 && a.keyboard_dry.r>0);
    ts_note_bank_clear(&a.notes);render(energy);
    assert(embedded_open(window,0,&a,&u,&pages,&bank,48000));u.tracker_open=1;
    startPlaying(PLAYMODE_SONG,0);render(energy);render(energy);
    assert(energy[0]>1 && energy[1]>1 && energy[2]==0 && energy[3]==0);
    assert(source_route_show(0,&a,&u,&pages,&bank,0,0));
    click(399,20,SDL_BUTTON_LEFT);click(100,89,SDL_BUTTON_LEFT);click(226,152,SDL_BUTTON_LEFT);
    render(energy);render(energy);assert(energy[0]==0 && energy[1]==0 && energy[2]==0 && energy[3]==0);
    assert(pages.tracker.lanes[0].output_route.mix_enabled); /* Whole-track override replaces tile sends. */
    click(466,191,SDL_BUTTON_LEFT);click(546,230,SDL_BUTTON_LEFT);
    assert(pages.tracker.lanes[0].output_route.send_level[0]==75 && pages.tracker.lanes[0].output_route.send_level[1]==100);
    /* A second shared processor gets a separate input sum and output pair. */
    parameters=a.sister.parameters;parameters.fx.enabled=1;
    for(int i=0;i<4;++i)parameters.fx.slot[i]=(TsSisterFxSlotControls){0};
    parameters.fx.slot[0]=(TsSisterFxSlotControls){.type=TS_SISTER_FX_DELAY,.enabled=1,
        .placement=TS_SISTER_FX_PLACE_POST,.mix=.5f,.parameter_a=0,.parameter_b=.1f};
    ts_sister_runtime_set_parameters(&a.sister,&parameters);
    click(520,20,SDL_BUTTON_LEFT);click(300,89,SDL_BUTTON_LEFT);click(500,48,SDL_BUTTON_LEFT);
    click(80,153,SDL_BUTTON_LEFT);click(230,153,SDL_BUTTON_LEFT);
    assert(a.sister.router.controls.send_mask==3);
    render(energy);render(energy);render(energy);
    assert(energy[0]>1 && energy[1]>1 && energy[2]>1 && energy[3]>1);
    shot(directory,"shared-pedalboard-return");
    click(399,20,SDL_BUTTON_LEFT);
    shot(directory,"track-shared-sends");
    assert(ts_tapehead_export(&pages.tracker,error,sizeof(error)));
    assert(ts_sister_tracker_save_file(&pages.tracker,"send-score.tst",error,sizeof(error)));
    ts_tapehead_stop();ts_tapehead_close();
    assert(ts_sister_tracker_load_file(&pages.tracker,"send-score.tst",error,sizeof(error)));
    assert(pages.tracker.lanes[0].output_route.clean_level==0 && pages.tracker.lanes[0].output_route.send_level[1]==100);
    assert(embedded_open(window,0,&a,&u,&pages,&bank,48000));startPlaying(PLAYMODE_SONG,0);
    render(energy);assert(energy[0]>1 && energy[1]>1 && energy[2]>1 && energy[3]>1);
    ts_tapehead_stop();remove("send-score.tst");
    /* Global Router offers the same returns without needing a selected tile. */
    /* Match the application's static window storage: this contains the large
       framebuffer/preset banks and cannot fit on MinGW's default 2 MiB stack. */
    static SisterWindow sister;
    source_route_hide();u.router_open=1;
    SDL_Event event={0};event.type=SDL_MOUSEBUTTONDOWN;event.button.windowID=SDL_GetWindowID(window);
    event.button.button=SDL_BUTTON_LEFT;event.button.x=70;event.button.y=84;
    assert(router_event(&event,window,0,&a,&u,&sister));
    assert(source_route_window.visible && source_route_window.track==-2 && source_route_window.model.page==2);
    assert(!source_route_window.model.can_rename);
    /* Enabling Sister still uses one shared Prism pass per sample. */
    assert(ts_sister_runtime_enable(&a.sister,48000,2,2,5,error,sizeof(error)));
    clock=a.sister.prism.clock;render(energy);assert(a.sister.prism.clock-clock==2048);
    ts_sister_runtime_disable(&a.sister);
}

int main(int argc,char **argv)
{
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_TIMER));
    window=SDL_CreateWindow("Source routing fixture",0,0,640,400,0);assert(window);
    ts_ui_init(&u);ts_instrument_init(&bank);assert(ts_sample_pages_init(&pages,error,sizeof(error)));
    u.tracker=&pages.tracker;ts_note_bank_init(&a.notes);ts_performance_init(&a.performance);ts_performance_init(&a.tile_launchers);
    ts_keyboard_sequence_init(&a.keyboard_sequence);ts_tracker_playback_init(&a.tracker);ts_audio_mixer_init(&a.mixer);
    ts_capture_init(&a.capture);ts_sister_runtime_init(&a.sister);a.output_rate=48000;a.output_device_channels=4;
    a.sister.spatial.controls.output[2]=2;a.sister.spatial.controls.output[3]=3;
    assert(ts_sister_runtime_reconfigure(&a.sister,48000,2,error,sizeof(error)));assert(ts_spatial_prepare(&a.sister.spatial,48000));
    assert(ts_instrument_activate_silence(&bank,8192,48000,error,sizeof(error)));
    free(bank.current.data);bank.current.channels=2;bank.current.data=calloc(8192*2,sizeof(float));assert(bank.current.data);
    for(int f=0;f<8192;++f){bank.current.data[f*2]=.12f;bank.current.data[f*2+1]=.24f;}
    bank.has_loop=1;bank.loop_first=0;bank.loop_last=8192;
    assert(ts_instrument_sync_selected(&bank,error,sizeof(error)));
    assert(ts_instrument_bank_rename(&bank,0,"GLASS RHYTHM",error,sizeof(error)));
    assert(ts_ui_bank_action(1,0)==TS_UI_BANK_ACTION_ROUTING);
    assert(ts_ui_bank_action(1,TS_UI_BANK_MOD_SHIFT)==TS_UI_BANK_ACTION_CLEAR);
    assert(source_route_show(0,&a,&u,&pages,&bank,-1,0));SDL_SetWindowSize(source_route_window.window,640,400);
    click(300,89,SDL_BUTTON_LEFT);click(80,153,SDL_BUTTON_LEFT);click(230,153,SDL_BUTTON_LEFT);
    assert(bank.bank[0].output_route.mode==TS_SOURCE_PAIR&&bank.bank[0].output_route.speaker==2&&bank.bank[0].output_route.second==3);
    assert(ts_note_bank_start(&a.notes,&bank,TS_AUDITION_CURRENT,12,0,48000)==TS_NOTE_STARTED);
    double energy[4];render(energy);assert(energy[0]==0&&energy[1]==0&&energy[2]>1&&energy[3]>energy[2]);
    assert(a.keyboard_dry.l>0&&a.keyboard_dry.r>0);
    ts_sister_runtime_set_master_output_gain(&a.sister,0);render(energy);render(energy);
    assert(energy[0]==0&&energy[1]==0&&energy[2]==0&&energy[3]==0);
    ts_sister_runtime_set_master_output_gain(&a.sister,1);render(energy);render(energy);
    assert(energy[2]>1&&energy[3]>1);
    /* Live UI edit reaches the already sounding voice, then returns to Main. */
    click(100,89,SDL_BUTTON_LEFT);render(energy);assert(energy[0]>1&&energy[1]>1&&energy[2]==0&&energy[3]==0);
    ts_note_bank_clear(&a.notes);for(int i=0;i<4;++i)render(energy);
    click(300,89,SDL_BUTTON_LEFT);click(149,224,SDL_BUTTON_LEFT);click(250,285,SDL_BUTTON_LEFT);
    shot(argc>1?argv[1]:NULL,"tile-routing");
    click(188,224,SDL_BUTTON_RIGHT);click(188,285,SDL_BUTTON_RIGHT);
    /* The native tracker renders the same tile and honors explicit Main as an override. */
    assert(embedded_open(window,0,&a,&u,&pages,&bank,48000));u.tracker_open=1;
    pattern[editor.editPattern][0]=(note_t){.note=49,.instr=1};song.songLength=1;song.orders[0]=editor.editPattern;
    startPlaying(PLAYMODE_SONG,0);render(energy);assert(energy[0]==0&&energy[1]==0&&energy[2]>1&&energy[3]>1);
    const pattCoord2_t *header=&pattCoord2Table[config.ptnStretch][ui.pattChanScrollShown][getPatternEditorView()];
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_RIGHT;
    assert(ts_tapehead_event(&e,32,header->upperRowsY+3));
    tracker_refresh(0,&a,&u,&pages,&bank,48000,NULL);
    assert(source_route_window.track==0&&source_route_window.model.route.mode==TS_SOURCE_INHERIT);
    shot(argc>1?argv[1]:NULL,"track-routing-inherit");
    click(100,89,SDL_BUTTON_LEFT);assert(ts_tapehead_running());render(energy);
    assert(energy[0]>1&&energy[1]>1&&energy[2]==0&&energy[3]==0);
    click(500,89,SDL_BUTTON_LEFT);click(80,153,SDL_BUTTON_LEFT);
    assert(pages.tracker.lanes[0].output_route.mode==TS_SOURCE_SPEAKER);
    assert(ts_tapehead_running());render(energy);assert(energy[1]>1&&energy[0]==0&&energy[2]==0&&energy[3]==0);
    shot(argc>1?argv[1]:NULL,"track-routing-override");
    /* Export/re-import exercises STH3 restore as well as the outer v4 lane codec. */
    assert(ts_tapehead_export(&pages.tracker,error,sizeof(error)));
    assert(ts_sister_tracker_save_file(&pages.tracker,"route-score.tst",error,sizeof(error)));
    ts_tapehead_stop();pages.tracker.lanes[0].output_route.mode=TS_SOURCE_INHERIT;
    assert(ts_sister_tracker_load_file(&pages.tracker,"route-score.tst",error,sizeof(error)));
    ts_tapehead_close();assert(embedded_open(window,0,&a,&u,&pages,&bank,48000));
    startPlaying(PLAYMODE_SONG,0);render(energy);assert(energy[1]>1&&energy[0]==0&&energy[2]==0&&energy[3]==0);
    click(500,48,SDL_BUTTON_LEFT);assert(pages.tracker.lanes[0].output_route.mode==TS_SOURCE_INHERIT);
    render(energy);assert(energy[0]==0&&energy[1]==0&&energy[2]>1&&energy[3]>1);
    /* Changing devices keeps the same route and folds only its missing pair. */
    a.output_device_channels=2;float stereo[1024];audio_callback(&a,(Uint8*)stereo,sizeof(stereo));
    assert(a.clean_output.missing==12&&fabsf(stereo[1000])>0&&fabsf(stereo[1001])>0);
    assert(source_route_show(0,&a,&u,&pages,&bank,-1,0));shot(argc>1?argv[1]:NULL,"routing-stereo-fallback");
    e=(SDL_Event){0};e.type=SDL_KEYUP;e.key.windowID=source_route_window.id;e.key.keysym.sym=SDLK_z;
    assert(!source_route_event(&e,window,0,&a,&u,&pages,&bank));assert(e.key.windowID==SDL_GetWindowID(window));
    shared_send_controls(argc>1?argv[1]:NULL);
    ts_tapehead_stop();ts_tapehead_close();source_route_close();remove("route-score.tst");
    ts_sister_runtime_free(&a.sister);ts_capture_free(&a.capture);ts_instrument_free(&bank);ts_sample_pages_free(&pages);
    SDL_DestroyWindow(window);SDL_Quit();puts("Source routing UI, native audio, track precedence, live edits, saved score and stereo fallback passed");return 0;
}
