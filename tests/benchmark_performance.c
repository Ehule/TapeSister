/* Explicit Release benchmark, not a timing-sensitive CI test. No device needed.
   Measures the actual callback and software paint with a deterministic sine. */
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
static TsInstrument instrument;
static TsFramebuffer framebuffer;
int main(void)
{
    SDL_SetMainReady(); assert(!SDL_Init(SDL_INIT_TIMER));
    ts_profile_init(SDL_GetPerformanceCounter,SDL_GetPerformanceFrequency());
    ts_ui_init(&u); ts_instrument_init(&instrument);
    ts_note_bank_init(&a.notes); ts_performance_init(&a.performance);
    ts_performance_init(&a.tile_launchers); ts_keyboard_sequence_init(&a.keyboard_sequence);
    ts_tracker_playback_init(&a.tracker); ts_capture_init(&a.capture);
    tapeLinkReaderInit(&a.live_link); ts_audio_mixer_init(&a.mixer);
    ts_sister_runtime_init(&a.sister); a.output_rate=48000; a.output_device_channels=2;
    a.fm_output_gain=a.fm_output_target=1; a.step=1; a.loop_direction=1;
    char error[160];
    assert(ts_sister_runtime_reconfigure(&a.sister,48000,2,error,sizeof(error)));
    instrument.current.frames=48000; instrument.current.channels=2; instrument.current.sample_rate=48000;
    instrument.current.data=calloc(96000,sizeof(float)); assert(instrument.current.data);
    for(unsigned i=0;i<48000;i++)for(int c=0;c<2;c++)
        instrument.current.data[i*2+c]=.2f*sinf((float)(2*M_PI*(c?330:220)*i/48000));
    instrument.view_last=48000; a.sample=&instrument.current; a.range_end=48000; a.looping=1;
    float output[1024];
    for(int playing=0;playing<2;playing++) {
        a.playing=playing;
        for(int i=0;i<100;i++)audio_callback(&a,(Uint8 *)output,sizeof(output));
        ts_profile_enable(1);ts_profile_reset();
        uint64_t begin=SDL_GetPerformanceCounter();
        for(int i=0;i<1500;i++)audio_callback(&a,(Uint8 *)output,sizeof(output));
        double seconds=(double)(SDL_GetPerformanceCounter()-begin)/SDL_GetPerformanceFrequency();
        printf("%s: %.3f ms/callback, %.3f%% of one core at 48000/512\n",playing?"loop":"idle",seconds*1000/1500,seconds/(1500*512./48000)*100);
        char report[8192];ts_profile_report(report,sizeof(report));puts(report);
        ts_profile_enable(0);
    }
    ts_profile_enable(1);ts_profile_reset();
    for(int i=0;i<180;i++) {
        ts_profile_lane_begin(0);uint64_t t=ts_profile_begin(TS_PROF_UI),p=ts_profile_begin(TS_PROF_RENDER);
        ts_ui_render(&framebuffer,&u,&instrument);ts_profile_end(TS_PROF_RENDER,p);
        ts_profile_end(TS_PROF_UI,t);ts_profile_lane_end(0);
    }
    char report[8192];ts_profile_report(report,sizeof(report));puts(report);
    ts_sister_runtime_free(&a.sister);ts_instrument_free(&instrument);SDL_Quit();return 0;
}
