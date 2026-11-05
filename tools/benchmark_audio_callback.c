/* Explicit, headless Mosaic project benchmark through the canonical callback.
   Timing excludes loading, validation, hashing and optional raw float output.
   Usage: PROJECT [SECONDS [WARMUP [RATE [BLOCK [CHANNELS [PCM_DUMP]]]]]] */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#define SDL_MAIN_HANDLED
#define TAPEHEAD_EMBEDDED
#define main tapesister_application_main
static int tapesister_application_main(int,char **);
#ifndef TS_BENCH_APP_SOURCE
#define TS_BENCH_APP_SOURCE "../src/main_sdl.c"
#endif
#include TS_BENCH_APP_SOURCE
#undef main
#include <inttypes.h>
#include <time.h>

static AudioState audio;
static TsUiState ui;
static TsInstrument bank,record;
static TsSamplePages pages;
static TsSisterProjectState state;
static char error[2048];
#define REQUIRE(x) do {if(!(x)){fprintf(stderr,"Failed: %s: %s\n",#x,error);exit(2);}} while(0)
static double thread_seconds(void)
{
#ifdef _WIN32
    FILETIME created,exited,kernel,user;
    REQUIRE(GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user));
    ULARGE_INTEGER k,u;k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
    u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;
    return (k.QuadPart+u.QuadPart)*1e-7;
#else
    struct timespec t;REQUIRE(!clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t));
    return t.tv_sec+t.tv_nsec*1e-9;
#endif
}
static int sort_double(const void *a,const void *b)
{double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);}
static uint64_t hash_bytes(uint64_t hash,const void *buffer,size_t size)
{const unsigned char *p=buffer;for(size_t i=0;i<size;++i)hash=(hash^p[i])*UINT64_C(1099511628211);return hash;}
int main(int argc,char **argv)
{
    if(argc<2 || argc>8){fprintf(stderr,"%s PROJECT [SECONDS [WARMUP [RATE [BLOCK [CHANNELS [PCM_DUMP]]]]]]\n",argv[0]);return 2;}
    double seconds=argc>2?strtod(argv[2],NULL):120,warmup=argc>3?strtod(argv[3],NULL):30;
    unsigned rate=argc>4?(unsigned)strtoul(argv[4],NULL,10):48000;
    unsigned block=argc>5?(unsigned)strtoul(argv[5],NULL,10):512;
    unsigned channels=argc>6?(unsigned)strtoul(argv[6],NULL,10):2;
    REQUIRE(isfinite(seconds) && seconds>0 && seconds<=86400 && isfinite(warmup) && warmup>=0 && warmup<seconds);
    REQUIRE(rate>=8000 && rate<=384000 && block>=16 && block<=8192 && channels>=2 && channels<=32);
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);
    REQUIRE(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER));
    SDL_Window *window=SDL_CreateWindow("Audio benchmark",0,0,640,400,SDL_WINDOW_HIDDEN);REQUIRE(window);
    ts_ui_init(&ui);ts_instrument_init(&bank);ts_instrument_init(&record);
    ts_note_bank_init(&audio.notes);ts_performance_init(&audio.performance);ts_performance_init(&audio.tile_launchers);
    ts_keyboard_sequence_init(&audio.keyboard_sequence);ts_tracker_playback_init(&audio.tracker);
    ts_audio_mixer_init(&audio.mixer);ts_capture_init(&audio.capture);ts_sister_runtime_init(&audio.sister);
    tapeLinkReaderInit(&audio.live_link);audio.output_rate=rate;audio.output_device_channels=channels;
    ts_speakers_default(&audio.speakers);audio.mixer.program_gain=audio.mixer.master_gain=1;
    REQUIRE(ts_sample_pages_init(&pages,error,sizeof(error)));
    TsTapeHeadHost host={.window=window};REQUIRE(ts_tapehead_init(&host,rate,error,sizeof(error)));
    pages.mosaic=ts_mosaic_create();REQUIRE(pages.mosaic);audio.mosaic=pages.mosaic;
    REQUIRE(ts_sample_pages_load_project(&pages,&bank,&record,argv[1],error,sizeof(error)));
    audio.gallery=pages.gallery;audio.mosaic=pages.mosaic;
    int present=0;REQUIRE(ts_sister_project_state_load(&state,argv[1],rate,&present,error,sizeof(error)));REQUIRE(present);
    REQUIRE(ts_sister_runtime_reconfigure(&audio.sister,rate,2,error,sizeof(error)));
    REQUIRE(ts_spatial_prepare(&audio.sister.spatial,rate));
    REQUIRE(ts_sister_project_state_apply(&state,&audio.sister,&bank));
    REQUIRE(ts_sister_runtime_set_page(&audio.sister,pages.active_page,&bank));
    ts_sister_runtime_configure_limiter(&audio.sister,1,-.2f,3,120);
    audio.sister.master_output_gain=(TsSisterRamp){.current=1,.target=1};
    memcpy(audio.sister.performance_gain_current,state.performance_mixer.gain,sizeof(audio.sister.performance_gain_current));
    ts_sister_post_fx_sync_controls(&audio.sister.post_fx,&state.parameters.fx);
    ts_mosaic_seek(audio.mosaic,0);audio.mosaic->playing=1;
    uint64_t total=(uint64_t)llround(seconds*rate),skip=(uint64_t)llround(warmup*rate);
    size_t capacity=(size_t)((total+block-1)/block),count=0;
    double *wall=calloc(capacity,sizeof(double));float *out=calloc((size_t)block*channels,sizeof(float));
    float *stereo=calloc((size_t)block*2,sizeof(float));REQUIRE(wall && out && stereo);audio.offline_stereo=stereo;
    FILE *dump=argc>7?fopen(argv[7],"wb"):NULL;if(argc>7)REQUIRE(dump);
    uint64_t frames=0,hash=UINT64_C(14695981039346656037),hardware_hash=hash;
    double sum_cpu=0,sum_wall=0,energy=0,peak=0,frequency=(double)SDL_GetPerformanceFrequency();
    for(uint64_t at=0;at<total;) {
        unsigned n=(unsigned)fmin(block,total-at);
        double cpu=thread_seconds();Uint64 start=SDL_GetPerformanceCounter();
        audio_callback(&audio,(Uint8 *)out,(int)(n*channels*sizeof(float)));
        double elapsed=(SDL_GetPerformanceCounter()-start)/frequency,cost=thread_seconds()-cpu;
        /* Hash the complete render, including startup and warmup. */
        for(unsigned i=0;i<n*channels;++i)REQUIRE(isfinite(out[i]));
        for(unsigned i=0;i<n*2;++i)REQUIRE(isfinite(stereo[i]));
        hash=hash_bytes(hash,stereo,n*2*sizeof(float));
        hardware_hash=hash_bytes(hardware_hash,out,n*channels*sizeof(float));
        if(dump)REQUIRE(fwrite(out,channels*sizeof(float),n,dump)==n);
        if(at>=skip) {
            sum_cpu+=cost;sum_wall+=elapsed;wall[count++]=elapsed*1000;frames+=n;
            for(unsigned i=0;i<n*2;++i){energy+=(double)stereo[i]*stereo[i];peak=fmax(peak,fabs(stereo[i]));}
        }
        at+=n;
    }
    REQUIRE(count && frames);qsort(wall,count,sizeof(double),sort_double);
    printf("{\"rate\":%u,\"block\":%u,\"channels\":%u,\"audio_seconds\":%.6f,\"warmup_seconds\":%.3f,"
        "\"thread_cpu_percent\":%.6f,\"mean_callback_cpu_ms\":%.6f,\"mean_callback_wall_ms\":%.6f,\"p95_callback_wall_ms\":%.6f,"
        "\"rms\":%.9g,\"peak\":%.9g,\"stereo_hash\":\"%016" PRIx64 "\",\"hardware_hash\":\"%016" PRIx64 "\"}\n",
        rate,block,channels,frames/(double)rate,warmup,sum_cpu/(frames/(double)rate)*100,sum_cpu*1000/count,
        sum_wall*1000/count,wall[(size_t)(count*.95)],sqrt(energy/(frames*2)),peak,hash,hardware_hash);
    if(dump)REQUIRE(!fclose(dump));
    ts_tapehead_close();ts_sister_runtime_free(&audio.sister);
    ts_mosaic_free(ts_sample_pages_mosaic_root(&pages));pages.mosaic=NULL;ts_sample_pages_free(&pages);
    ts_instrument_free(&bank);ts_instrument_free(&record);SDL_DestroyWindow(window);SDL_Quit();
    free(wall);free(out);free(stereo);return 0;
}
