#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>
static AudioState a;
static TsUiState u;
static TsInstrument bank;
static TsSamplePages pages;
static SisterWindow sister;
static SDL_Window *window;
static void render(float last[16],unsigned channels)
{
    float out[256*16];a.output_device_channels=channels;
    for(int block=0;block<64;++block)audio_callback(&a,(Uint8*)out,256*channels*sizeof(float));
    for(unsigned ch=0;ch<channels;++ch){last[ch]=out[255*channels+ch];assert(isfinite(last[ch]) && fabsf(last[ch])<=.98001f);}
}
static void set_route(int spatial,int direct,int last)
{
    TsSourceRoute r={.matrix_enabled=1};r.matrix_gain[TS_MATRIX_AMBISONICS]=spatial;
    r.matrix_gain[TS_MATRIX_SPEAKER_BASE]=direct;r.matrix_gain[TS_MATRIX_SPEAKER_BASE+9]=500;
    r.matrix_gain[TS_MATRIX_SPEAKER_BASE+15]=last;
    bank.bank[0].output_route=r;source_route_update_voices(&a,bank.bank[0].tile_id,r);
}
static void spatial_enabled(int enabled)
{TsSpatialControls c=a.sister.spatial.controls;c.enabled=enabled;ts_spatial_set(&a.sister.spatial,&c);}
static void click_window(Uint32 id,int x,int y)
{
    SDL_Event e;SDL_zero(e);e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=id;e.button.button=SDL_BUTTON_LEFT;e.button.x=x;e.button.y=y;
    if(id==speakers_window.id)assert(speakers_event(&e,window,0,&a,&u));
    else if(id==matrix_window.id)assert(matrix_event(&e,window,0,&a,&u,&pages,&bank));
    else assert(spatial_event(&e,window,0,&a,&u,&sister));
    e.type=SDL_MOUSEBUTTONUP;
    if(id==matrix_window.id)matrix_event(&e,window,0,&a,&u,&pages,&bank);
    else if(id==spatial_window.id)spatial_event(&e,window,0,&a,&u,&sister);
}
static void shot(TsFramebuffer *fb,const char *dir,const char *name)
{
    if(!dir)return;char path[1024];snprintf(path,sizeof(path),"%s/%s.bmp",dir,name);
    SDL_Surface *s=SDL_CreateRGBSurfaceFrom(fb->pixels,640,400,32,2560,0xff0000,0xff00,0xff,0xff000000);
    assert(s && !SDL_SaveBMP(s,path));SDL_FreeSurface(s);
}
int main(int argc,char **argv)
{
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_TIMER));window=SDL_CreateWindow("fixture",0,0,640,400,0);assert(window);
    char error[256];ts_ui_init(&u);ts_instrument_init(&bank);assert(ts_sample_pages_init(&pages,error,sizeof(error)));u.tracker=&pages.tracker;
    ts_note_bank_init(&a.notes);ts_performance_init(&a.performance);ts_performance_init(&a.tile_launchers);
    ts_keyboard_sequence_init(&a.keyboard_sequence);ts_tracker_playback_init(&a.tracker);ts_audio_mixer_init(&a.mixer);
    ts_capture_init(&a.capture);ts_sister_runtime_init(&a.sister);a.output_rate=48000;a.output_device_channels=10;
    assert(ts_sister_runtime_reconfigure(&a.sister,48000,2,error,sizeof(error)));assert(ts_spatial_prepare(&a.sister.spatial,48000));
    ts_speakers_default(&a.speakers);a.speakers.count=10;u.config.speakers=a.speakers;
    TsMatrixControls controls=a.sister.matrix.controls;controls.enabled=1;assert(ts_matrix_set(&a.sister.matrix,&controls,48000));
    TsSpatialControls c=a.sister.spatial.controls;c.output[0]=0;c.output[1]=2;c.output[2]=5;c.output[3]=8;c.enabled=1;ts_spatial_set(&a.sister.spatial,&c);
    assert(ts_instrument_activate_silence(&bank,8192,48000,error,sizeof(error)));
    for(size_t i=0;i<bank.current.frames*bank.current.channels;++i)bank.current.data[i]=.12f;
    bank.has_loop=1;bank.loop_first=0;bank.loop_last=8192;assert(ts_instrument_sync_selected(&bank,error,sizeof(error)));
    set_route(1000,1000,0);assert(ts_note_bank_start(&a.notes,&bank,TS_AUDITION_CURRENT,12,0,48000)==TS_NOTE_STARTED);
    float both[16],direct[16],spatial[16];render(both,10);
    spatial_enabled(0);render(direct,10);assert(direct[0]>.01f && direct[9]>.005f);
    for(int ch=1;ch<9;++ch)assert(fabsf(direct[ch])<.000001f);
    set_route(1000,0,0);spatial_enabled(1);render(spatial,10);
    assert(a.sister.spatial.view.available);
    for(int ch=0;ch<9;++ch)assert(fabsf(both[ch]-spatial[ch]-direct[ch])<.00001f);
    assert(fabsf(both[9]-direct[9])<.00001f);assert(fabsf(spatial[2])>.001f && fabsf(spatial[5])>.001f && fabsf(spatial[8])>.001f);
    /* Missing array members: no bus audio is substituted onto the main pair. */
    render(spatial,6);assert(!a.sister.spatial.view.available);
    for(int ch=0;ch<6;++ch)assert(fabsf(spatial[ch])<.00001f);
    a.speakers.count=16;u.config.speakers=a.speakers;set_route(0,0,1000);render(spatial,16);
    assert(spatial[15]>.01f);for(int ch=0;ch<15;++ch)if(ch!=9)assert(fabsf(spatial[ch])<.00001f);
    /* Disabled physical outputs are silent even on a full-width SDL device. */
    a.output_range=(TsAudioChannelRange){3,10};render(spatial,16);
    assert(spatial[15]==0 && spatial[9]>.005f);
    set_route(1000,1000,1000);render(spatial,16);
    assert(spatial[0]==0 && spatial[15]==0 && !a.sister.spatial.view.available);
    a.output_range=(TsAudioChannelRange){1,0};
    /* A speaker mapping can move above channel 16 without changing its routes. */
    TsSpeakerSetup remap=a.speakers;remap.count=10;remap.output[9]=15;
    assert(speakers_commit(0,&a,&u,&remap));set_route(1000,1000,0);render(both,16);
    assert(both[9]==0 && both[15]>.005f);
    /* Actual controls and entry points. */
    assert(matrix_show(0,&a,&u,&pages,&bank,-1,-1));SDL_SetWindowSize(matrix_window.window,MATRIX_W,MATRIX_H);
    click_window(matrix_window.id,700,66);assert(spatial_window.visible && spatial_window.model.matrix);
    SDL_SetWindowSize(spatial_window.window,640,400);click_window(spatial_window.id,350,18);assert(spatial_window.show_routes);spatial_window.show_routes=0;
    click_window(matrix_window.id,900,66);assert(speakers_window.visible);SDL_SetWindowSize(speakers_window.window,640,400);
    click_window(speakers_window.id,250,18);assert(u.config.speakers.count==9);
    mx_capture_activity(0,&a,&pages,&bank,50);
    for(int i=0;i<matrix_window.column_count;++i)assert(mx_column(i)<TS_MATRIX_SPEAKER_BASE+9);
    assert(bank.bank[0].output_route.matrix_gain[TS_MATRIX_SPEAKER_BASE+9]==500);
    click_window(speakers_window.id,330,18);assert(u.config.speakers.count==10);
    mx_capture_activity(0,&a,&pages,&bank,50);int found=0;
    for(int i=0;i<matrix_window.column_count;++i)found|=mx_column(i)==TS_MATRIX_SPEAKER_BASE+9;
    assert(found && bank.bank[0].output_route.matrix_gain[TS_MATRIX_SPEAKER_BASE+9]==500);
    a.output_range=(TsAudioChannelRange){3,10};mx_capture_activity(0,&a,&pages,&bank,50);
    int visible_speakers=0;
    for(int i=0;i<matrix_window.column_count;++i) {
        int d=mx_column(i);assert(mx_destination_available(d));
        if(d>=TS_MATRIX_SPEAKER_BASE){++visible_speakers;assert(d>=TS_MATRIX_SPEAKER_BASE+2 && d<TS_MATRIX_SPEAKER_BASE+9);}
        if(ts_matrix_is_output(d))assert(d>=TS_MATRIX_NODES+1 && d<TS_MATRIX_NODES+5);
    }
    assert(visible_speakers==7 && bank.bank[0].output_route.matrix_gain[TS_MATRIX_SPEAKER_BASE+9]==500);
    a.output_range=(TsAudioChannelRange){1,0};a.output_device_channels=10;mx_capture_activity(0,&a,&pages,&bank,50);
    for(int i=0;i<matrix_window.column_count;++i)assert(mx_column(i)!=TS_MATRIX_SPEAKER_BASE+9); /* mapped to absent OUT16 */
    a.output_device_channels=16;mx_capture_activity(0,&a,&pages,&bank,50);found=0;
    for(int i=0;i<matrix_window.column_count;++i)found|=mx_column(i)==TS_MATRIX_SPEAKER_BASE+9;
    assert(found);
    click_window(speakers_window.id,45,76);assert(speakers_window.model.editing);
    snprintf(speakers_window.model.edit,sizeof(speakers_window.model.edit),"FRONT LEFT");
    SDL_Event e;SDL_zero(e);e.type=SDL_KEYDOWN;e.key.windowID=speakers_window.id;e.key.keysym.sym=SDLK_RETURN;
    assert(speakers_event(&e,window,0,&a,&u));assert(!strcmp(a.speakers.name[0],"FRONT LEFT"));
    assert(ts_config_save(&u.config,"speaker-config.ini",error,sizeof(error)));TsConfig loaded;assert(ts_config_load(&loaded,"speaker-config.ini",error,sizeof(error)));
    assert(!memcmp(&loaded.speakers,&u.config.speakers,sizeof(loaded.speakers)));remove("speaker-config.ini");
    click_window(speakers_window.id,430,18);assert(a.speaker_test_remaining==48000);speakers_hide(0,&a);assert(!a.speaker_test_remaining);
    /* Array count cannot fall below 3 or grow beyond the configured setup. */
    spatial_capture(&a,&u);spatial_window.model.page=1;
    for(int i=0;i<4;++i)click_window(spatial_window.id,350,90);assert(a.sister.spatial.controls.speakers==3);
    for(int i=0;i<20;++i)click_window(spatial_window.id,440,90);assert(a.sister.spatial.controls.speakers==10);
    ts_spatial_set(&a.sister.spatial,&c);spatial_capture(&a,&u);
    if(argc>1) {
        u.config_open=1;u.config.audio_backend=TS_AUDIO_BACKEND_JACK;
        u.config.audio_input_range=(TsAudioChannelRange){1,6};u.config.audio_output_range=(TsAudioChannelRange){1,10};
        ts_audio_ui_render(speakers_window.fb,&u,&bank);shot(speakers_window.fb,argv[1],"audio-channel-ranges");u.config_open=0;
        for(int page=0;page<3;++page){spatial_window.model.page=page;ts_spatial_ui_render(spatial_window.framebuffer,&spatial_window.model,&u.palette);
            char name[40];snprintf(name,sizeof(name),"matrix-ambisonics-%d",page);shot(spatial_window.framebuffer,argv[1],name);}
        speakers_show(&a,&u);speakers_window.model.setup=u.config.speakers;speakers_window.model.channels=16;
        ts_speakers_ui_render(speakers_window.fb,&speakers_window.model,&u.palette);shot(speakers_window.fb,argv[1],"speaker-setup");
        mx_capture_activity(0,&a,&pages,&bank,50);matrix_window.destination=TS_MATRIX_AMBISONICS;
        matrix_window.column=SDL_clamp(mx_column_position(TS_MATRIX_AMBISONICS),0,SDL_max(0,matrix_window.column_count-MATRIX_COLS));mx_paint(&a,&pages,&bank);
        char path[1024];snprintf(path,sizeof(path),"%s/matrix-spatial-bus.bmp",argv[1]);SDL_Surface *s=SDL_CreateRGBSurfaceFrom(matrix_window.pixels,MATRIX_W,MATRIX_H,32,MATRIX_W*4,0xff0000,0xff00,0xff,0xff000000);assert(s && !SDL_SaveBMP(s,path));SDL_FreeSurface(s);
    }
    matrix_close();spatial_window_close();speakers_close();ts_sister_runtime_free(&a.sister);ts_capture_free(&a.capture);
    ts_instrument_free(&bank);ts_sample_pages_free(&pages);SDL_DestroyWindow(window);SDL_Quit();
    puts("Matrix spatial bus: 10/16 outputs, shared direct feeds, missing channels, config, windows and naming passed");
}
