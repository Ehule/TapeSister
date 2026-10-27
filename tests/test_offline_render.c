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
static AudioState live;
static TsUiState view;
static TsSamplePages pages;
static TsInstrument bank,record;
static SDL_Window *window;
static char error[256],root[1024];

static void wait_render(int expected)
{
    Uint32 start=SDL_GetTicks();
    while(offline_job.active && SDL_GetTicks()-start<30000){offline_job_poll();SDL_Delay(5);}
    if(offline_job.active || offline_job.progress.state!=expected)fprintf(stderr,"Render result: %s (state %d)\n",offline_job.progress.message,offline_job.progress.state);
    assert(!offline_job.active && offline_job.progress.state==expected);
}
static double energy(const char *path,unsigned frames,unsigned bits)
{
    TsSample s;ts_sample_init(&s);assert(ts_sample_load_wav(&s,path,error,sizeof(error)));
    assert(s.sample_rate==48000 && s.channels==2 && s.frames==frames);
    double sum=0;for(size_t i=0;i<s.frames*2;++i){assert(isfinite(s.data[i]));sum+=fabs(s.data[i]);}
    ts_sample_free(&s);
    FILE *f=fopen(path,"rb");assert(f);unsigned char h[36];assert(fread(h,1,36,f)==36);fclose(f);
    assert(h[34]==bits && h[20]==(bits==32?3:1));return sum;
}
static void start(const char *name,unsigned sources,int bits)
{
    char path[1024];assert(offline_join(path,sizeof(path),root,name));
    offline_ui.sources=sources;offline_ui.bits=bits;
    uint64_t hash=ts_mosaic_hash(live.mosaic);double time=live.mosaic->time;int playing=ts_tapehead_running();
    int row=song.row,pos=song.songPos;char project[1024];strcpy(project,view.project_path);
    if(!offline_start(0,&live,&view,&bank,&pages,&record,0,path,0))fprintf(stderr,"start: %s\n",view.status);
    assert(offline_job.active);
    assert(hash==ts_mosaic_hash(live.mosaic) && live.mosaic->time==time);
    assert(ts_tapehead_running()==playing && song.row==row && song.songPos==pos);
    assert(!strcmp(project,view.project_path) && view.project_modified);
}
static void shot(SDL_Renderer *renderer,const char *name,int w,int h)
{
    char path[1024];assert(offline_join(path,sizeof(path),root,name));
    SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);assert(surface);
    assert(!SDL_RenderReadPixels(renderer,NULL,SDL_PIXELFORMAT_ARGB8888,surface->pixels,surface->pitch));
    assert(!SDL_SaveBMP(surface,path));SDL_FreeSurface(surface);
}
int main(int argc,char **argv)
{
    int dispatch=offline_dispatch(argc,argv);if(dispatch>=0)return dispatch;
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);
    /* A worker opening an audio device would fail with this driver. */
    SDL_setenv("SDL_AUDIODRIVER","no-such-audio-driver",1);
    assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER));
    window=SDL_CreateWindow("Offline test",0,0,640,400,0);assert(window);
    if(argc>1)snprintf(root,sizeof(root),"%s",argv[1]);
    else snprintf(root,sizeof(root),"offline-render-test-%llu",(unsigned long long)SDL_GetPerformanceCounter());
#ifdef _WIN32
    _mkdir(root);
#else
    mkdir(root,0700);
#endif
    ts_ui_init(&view);ts_instrument_init(&bank);ts_instrument_init(&record);
    assert(ts_sample_pages_init(&pages,error,sizeof(error)));view.tracker=&pages.tracker;
    ts_note_bank_init(&live.notes);ts_performance_init(&live.performance);ts_performance_init(&live.tile_launchers);
    ts_keyboard_sequence_init(&live.keyboard_sequence);ts_tracker_playback_init(&live.tracker);
    ts_audio_mixer_init(&live.mixer);ts_capture_init(&live.capture);ts_sister_runtime_init(&live.sister);
    live.output_rate=48000;live.output_device_channels=2;live.speakers=view.config.speakers;
    assert(ts_sister_runtime_reconfigure(&live.sister,48000,2,error,sizeof(error)));
    assert(ts_spatial_prepare(&live.sister.spatial,48000));
    assert(ts_instrument_activate_silence(&bank,4800,48000,error,sizeof(error)));
    for(int f=0;f<4800;++f)bank.current.data[f]=.04f*sinf((float)(2*M_PI*220*f/48000));
    bank.has_loop=1;bank.loop_first=0;bank.loop_last=4800;
    assert(ts_instrument_sync_selected(&bank,error,sizeof(error)));
    pages.mosaic=live.mosaic=view.mosaic=ts_mosaic_create();assert(live.mosaic);
    TsSample source={.data=calloc(4800,sizeof(float)),.frames=4800,.sample_rate=48000,.channels=1};assert(source.data);
    for(int f=0;f<4800;++f)source.data[f]=.03f*sinf((float)(2*M_PI*440*f/48000));
    TsMosaicSource *ms=ts_mosaic_source(live.mosaic,&source,error,sizeof(error));assert(ms);ts_sample_free(&source);
    TsMosaicEvent *card=ts_mosaic_add(live.mosaic,ms,0,0);assert(card);card->duration=2;card->looping=1;
    assert(embedded_open(window,0,&live,&view,&pages,&bank,48000));
    pattern[editor.editPattern][0]=(note_t){.note=49,.instr=1};song.songLength=1;song.orders[0]=editor.editPattern;
    /* Tempo changes and a loop: no fixed-duration score arithmetic is allowed. */
    pattern[editor.editPattern][MAX_CHANNELS]=(note_t){.efx=0x0f,.efxData=150};
    startPlaying(PLAYMODE_SONG,0);live.mosaic->playing=1;live.mosaic->time=.37;
    live.sister.performance_transport=6;view.project_modified=1;
    performance_show(&view,&live);performance_transport.painted=SDL_GetTicks()-100;performance_update(0,&live);shot(performance_transport.renderer,"f11-offline.bmp",PERF_W,PERF_H);
    SDL_Event click={0};click.type=SDL_MOUSEBUTTONDOWN;click.button.button=SDL_BUTTON_LEFT;
    click.button.windowID=performance_transport.id;click.button.x=450;click.button.y=618;
    assert(performance_event(&click,window,0,&live,&view,&pages,&bank,NULL,48000));
    assert(offline_ui.visible && offline_ui.sources==6);
    offline_update();shot(offline_ui.renderer,"offline-render.bmp",OFFLINE_W,OFFLINE_H);
    double seconds;assert(offline_time("1:02:03.5",&seconds) && seconds==3723.5);
    assert(!offline_time("1:99",&seconds) && !offline_time("nan",&seconds) && !offline_time("12junk",&seconds));
    assert(ts_browser_mode_edits_filename(TS_BROWSER_RENDER_WAV));
    strcpy(offline_ui.duration,"0.6");strcpy(offline_ui.tail,"0.2");
    start("mosaic.wav",4,24);wait_render(2);char path[1024];offline_join(path,sizeof(path),root,"mosaic.wav");double mosaic=energy(path,38400,24);assert(mosaic>1);
    start("tracker.wav",2,24);wait_render(2);offline_join(path,sizeof(path),root,"tracker.wav");double tracker=energy(path,38400,24);assert(tracker>1);
    start("both.wav",6,32);wait_render(2);offline_join(path,sizeof(path),root,"both.wav");assert(energy(path,38400,32)>fmax(mosaic,tracker));
    /* Zero gain must silence both native sources in the exported mix. */
    live.sister.performance_mixer.gain[1]=live.sister.performance_mixer.gain[2]=0;
    start("muted.wav",6,24);wait_render(2);offline_join(path,sizeof(path),root,"muted.wav");assert(energy(path,38400,24)<.1);
    live.sister.performance_mixer.gain[1]=live.sister.performance_mixer.gain[2]=1;
    /* The native final OUT fader is included (it is not in the TSR sidecar). */
    ts_sister_runtime_set_master_output_gain(&live.sister,0);
    start("out-muted.wav",6,24);wait_render(2);offline_join(path,sizeof(path),root,"out-muted.wav");assert(energy(path,38400,24)==0);
    ts_sister_runtime_set_master_output_gain(&live.sister,1);
    /* A delay tail survives the precise source stop, without restarting loops. */
    TsSisterParameters params=live.sister.parameters;params.fx.enabled=1;params.fx.master_transition=0;
    params.fx.slot[0]=(TsSisterFxSlotControls){.type=TS_SISTER_FX_DELAY,.enabled=1,.placement=TS_SISTER_FX_PLACE_POST,
        .mix=.7f,.parameter_a=.5f,.parameter_b=.5f};
    ts_sister_runtime_set_parameters(&live.sister,&params);ts_sister_post_fx_sync_controls(&live.sister.post_fx,&params.fx);
    strcpy(offline_ui.tail,"1");start("tail.wav",4,32);wait_render(2);
    offline_join(path,sizeof(path),root,"tail.wav");assert(energy(path,76800,32)>1);
    TsSample tail;ts_sample_init(&tail);assert(ts_sample_load_wav(&tail,path,error,sizeof(error)));
    double late=0;for(size_t i=48000*2;i<tail.frames*2;++i)late+=fabs(tail.data[i]);assert(late>.01);ts_sample_free(&tail);
    params.fx.enabled=0;ts_sister_runtime_set_parameters(&live.sister,&params);ts_sister_post_fx_sync_controls(&live.sister.post_fx,&params.fx);
    strcpy(offline_ui.tail,"0.2");
    /* Existing output is preserved on collision, cancellation and write failure. */
    start("mosaic.wav",4,24);wait_render(-1);offline_join(path,sizeof(path),root,"mosaic.wav");assert(energy(path,38400,24)==mosaic);
    strcpy(offline_ui.duration,"120");start("cancel.wav",6,24);offline_job_cancel();wait_render(3);
    offline_join(path,sizeof(path),root,"cancel.wav");assert(!ts_browser_path_exists(path));
    strcpy(offline_ui.duration,"0.1");start("missing/output.wav",4,24);wait_render(-1);
    /* Active EQ timer restarts in the child; live automation remains untouched. */
    TsEqMorph morph;ts_eq_morph_default(&morph);morph.captured=3;morph.seconds=1;
    for(int k=0;k<2;++k){morph.state[k].enabled=1;for(int b=0;b<TS_MASTER_EQ_BANDS;++b)morph.state[k].band[b].enabled=0;}
    morph.state[0].band[0]=(TsEqBand){.enabled=1,.type=TS_EQ_LOW_PASS,.frequency=20000,.q=.707f};
    morph.state[1].band[0]=(TsEqBand){.enabled=1,.type=TS_EQ_LOW_PASS,.frequency=20,.q=.707f};
    ts_master_eq_morph_set(&live.sister.master_eq,&morph);assert(ts_master_eq_morph_start(&live.sister.master_eq));
    strcpy(offline_ui.duration,"1.5");strcpy(offline_ui.tail,"0");
    start("automation.wav",4,32);assert(offline_job.options.eq_run);wait_render(2);assert(live.sister.master_eq.morph_running);
    offline_join(path,sizeof(path),root,"automation.wav");TsSample sweep;ts_sample_init(&sweep);assert(ts_sample_load_wav(&sweep,path,error,sizeof(error)));
    double early=0,late_eq=0;for(int i=4800*2;i<9600*2;++i)early+=fabs(sweep.data[i]);
    for(int i=60000*2;i<64800*2;++i)late_eq+=fabs(sweep.data[i]);assert(early>1 && late_eq<early*.05);ts_sample_free(&sweep);
    offline_close();performance_close();ts_tapehead_close();ts_sister_runtime_free(&live.sister);
    ts_mosaic_free(live.mosaic);pages.mosaic=NULL;ts_sample_pages_free(&pages);ts_instrument_free(&bank);ts_instrument_free(&record);
    SDL_DestroyWindow(window);SDL_Quit();puts("Offline render integration passed");return 0;
}
