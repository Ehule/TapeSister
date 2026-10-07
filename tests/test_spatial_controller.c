#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>

static AudioState test_audio;
static TsUiState test_ui;
static SisterWindow test_sister;

static void mouse(SDL_Window *main,int x,int y,int button)
{
    SDL_Event e;SDL_zero(e);e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=spatial_window.id;
    e.button.x=x;e.button.y=y;e.button.button=button;
    assert(spatial_event(&e,main,0,&test_audio,&test_ui,&test_sister));
    e.type=SDL_MOUSEBUTTONUP;assert(spatial_event(&e,main,0,&test_audio,&test_ui,&test_sister));
}
static void screenshot(const char *path)
{
    spatial_capture(&test_audio,&test_ui);
    ts_spatial_ui_render(spatial_window.framebuffer,&spatial_window.model,&test_ui.palette);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n640 400\n255\n");
    for(int i=0;i<640*400;++i){uint32_t p=spatial_window.framebuffer->pixels[i];
        unsigned char rgb[3]={(unsigned char)(p>>16),(unsigned char)(p>>8),(unsigned char)p};assert(fwrite(rgb,1,3,f)==3);}
    fclose(f);
}
int main(int argc,char **argv)
{
    assert(!SDL_setenv("SDL_VIDEODRIVER","dummy",1));assert(!SDL_setenv("SDL_AUDIODRIVER","dummy",1));
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_TIMER)==0);
    SDL_Window *main=SDL_CreateWindow("test",0,0,640,400,0);assert(main);
    ts_ui_init(&test_ui);ts_sister_runtime_init(&test_audio.sister);ts_note_bank_init(&test_audio.notes);
    ts_keyboard_sequence_init(&test_audio.keyboard_sequence);ts_tracker_playback_init(&test_audio.tracker);
    ts_performance_init(&test_audio.performance);ts_performance_init(&test_audio.tile_launchers);
    ts_audio_mixer_init(&test_audio.mixer);ts_capture_init(&test_audio.capture);
    test_audio.output_rate=48000;test_audio.output_device_channels=4;
    assert(ts_sister_runtime_reconfigure(&test_audio.sister,48000,2,NULL,0));
    assert(ts_spatial_prepare(&test_audio.sister.spatial,48000));
    assert(spatial_show(0,&test_audio,&test_ui));SDL_SetWindowSize(spatial_window.window,640,400);
    mouse(main,240,18,SDL_BUTTON_LEFT);assert(test_audio.sister.spatial.controls.enabled);
    mouse(main,290,311,SDL_BUTTON_LEFT);assert(test_audio.sister.spatial.controls.value[2]>.1f);
    mouse(main,156,176,SDL_BUTTON_LEFT);assert(test_audio.sister.spatial.controls.value[3]>.01f);
    float field_before[5];memcpy(field_before,test_audio.sister.spatial.controls.value,sizeof(field_before));
    mouse(main,150,249,SDL_BUTTON_LEFT);assert(test_audio.sister.spatial.controls.transform==TS_SPATIAL_PRESS);
    assert(!memcmp(field_before,test_audio.sister.spatial.controls.value,sizeof(field_before)));
    mouse(main,150,249,SDL_BUTTON_RIGHT);assert(test_audio.sister.spatial.controls.transform==TS_SPATIAL_FOCUS);
    mouse(main,350,18,SDL_BUTTON_LEFT);assert(test_audio.sister.spatial.controls.stereo_pair==1);
    mouse(main,240,50,SDL_BUTTON_LEFT);assert(spatial_window.model.page==2);
    mouse(main,70,153,SDL_BUTTON_LEFT);assert(test_audio.sister.spatial.controls.lfo_targets&1);
    mouse(main,430,125,SDL_BUTTON_LEFT);assert(test_audio.sister.spatial.controls.lfo_depth>.3f);
    mouse(main,50,332,SDL_BUTTON_LEFT);assert(test_audio.sister.spatial.controls.captured&1);
    mouse(main,215,262,SDL_BUTTON_LEFT);assert(test_audio.sister.spatial.controls.rise_targets&2);
    mouse(main,150,294,SDL_BUTTON_LEFT);assert(test_audio.sister.spatial.rise_active);
    mouse(main,150,50,SDL_BUTTON_LEFT);assert(spatial_window.model.page==1);
    mouse(main,350,338,SDL_BUTTON_LEFT);assert(test_audio.sister.spatial.test_remaining==48000);
    mouse(main,500,338,SDL_BUTTON_LEFT);assert(!test_audio.sister.spatial.test_remaining);
    assert(spatial_midi(0,&test_audio,&test_ui,"main.spatial.param.0",.75f));
    assert(test_audio.sister.spatial.controls.value[0]==.75f);
    assert(ts_midi_target_is_continuous("main.spatial.param.0"));
    /* Actual main callback produces independent four-channel output. */
    test_audio.tune_reference_target=.2f;test_audio.tune_reference_frequency=440;
    float samples[512*4];for(int i=0;i<40;++i)audio_callback(&test_audio,(Uint8 *)samples,sizeof(samples));
    double energy[4]={0};for(int i=0;i<512;++i)for(int ch=0;ch<4;++ch){
        float v=samples[4*i+ch];assert(isfinite(v)&&fabsf(v)<=1);energy[ch]+=v*v;}
    assert(energy[0]>0&&energy[1]>0&&energy[2]>0&&energy[3]>0);
    assert(fabs(energy[0]-energy[2])>.00001);
    if(argc>1){char path[1024];
        for(int page=0;page<3;++page){spatial_window.model.page=page;
            snprintf(path,sizeof(path),"%s/spatial-%d.ppm",argv[1],page);screenshot(path);}
    }
    /* Closing cancels the temporary test but keeps spatial audio enabled. */
    spatial_hide(0,&test_audio);assert(test_audio.sister.spatial.controls.enabled);
    assert(spatial_show(0,&test_audio,&test_ui));
    SDL_Event e;SDL_zero(e);e.type=SDL_KEYUP;e.key.windowID=spatial_window.id;e.key.keysym.sym=SDLK_z;
    assert(!spatial_event(&e,main,0,&test_audio,&test_ui,&test_sister));assert(e.key.windowID==SDL_GetWindowID(main));
    spatial_window_close();ts_sister_runtime_free(&test_audio.sister);ts_capture_free(&test_audio.capture);
    SDL_DestroyWindow(main);SDL_Quit();puts("Ambisonics native window, gestures, MIDI, callback and lifecycle passed");return 0;
}
