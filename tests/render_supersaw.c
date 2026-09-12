/* Reproduce the audition pack with the unchanged FM renderer, actual Portal
   chains and Mosaic mixer. Usage: render_supersaw CDP_BIN NEW_OUTPUT_DIRECTORY */
#include "tapesister/cdp_portal.h"
#include "tapesister/mosaic.h"
#include "tapesister/sample_pages.h"
#include "tapesister/ui.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#define MKDIR(path) mkdir(path,0700)
#endif

static char error[2048];
#define CHECK(call) do {if(!(call)){fprintf(stderr,"%s: %s\n",#call,error);exit(1);}}while(0)
static void path_for(char *path,size_t size,const char *directory,const char *name)
{
    int count=snprintf(path,size,"%s/%s",directory,name);
    if(count<0 || (size_t)count>=size){fputs("Output path is too long\n",stderr);exit(1);}
}
static TsMosaicEvent *event(TsMosaic *m,TsMosaicSource *s,const char *name,
                           double start,double duration,double x,float gain,float pan,
                           double in,double out,int note,int third,int fifth)
{
    TsMosaicEvent *e=ts_mosaic_add(m,s,start,x);CHECK(e);
    snprintf(e->name,sizeof(e->name),"%s",name);e->duration=duration;e->gain=gain;e->pan=pan;
    e->fade_in=in;e->fade_out=out;e->first=s->sample.sample_rate/2;
    e->last=7*s->sample.sample_rate;e->crossfade=s->sample.sample_rate/20;
    e->looping=1;e->notes[0]=note;e->note_count=1;
    if(third>=0)e->notes[e->note_count++]=third;
    if(fifth>=0)e->notes[e->note_count++]=fifth;
    return e;
}
static void render_scene(TsMosaic *scene,double seconds,const char *path)
{
    TsSample audio={0};audio.sample_rate=44100;audio.channels=2;
    audio.frames=(size_t)(seconds*audio.sample_rate);audio.data=calloc(audio.frames*2,sizeof(float));CHECK(audio.data);
    ts_mosaic_seek(scene,0);scene->playing=1;
    for(size_t i=0;i<audio.frames;++i) {
        TsStereoFrame frame=ts_mosaic_read(scene,(int)audio.sample_rate);
        CHECK(isfinite(frame.l) && isfinite(frame.r));
        audio.data[2*i]=frame.l;audio.data[2*i+1]=frame.r;
    }
    CHECK(ts_sample_peak(&audio)<.98f);
    CHECK(ts_sample_save_wav16(&audio,path,error,sizeof(error)));
    printf("%s: %.2f seconds, peak %.4f\n",path,seconds,ts_sample_peak(&audio));
    ts_sample_free(&audio);scene->playing=0;ts_mosaic_seek(scene,0);
}
int main(int argc,char **argv)
{
    if(argc!=3){fputs("Usage: render_supersaw CDP_BIN NEW_OUTPUT_DIRECTORY\n",stderr);return 1;}
    /* Refuse an existing directory so regeneration cannot overwrite a project. */
    if(MKDIR(argv[2])!=0){fprintf(stderr,"Create a new output directory: %s\n",strerror(errno));return 1;}
    TsInstrument *bank=calloc(1,sizeof(*bank)),*record=calloc(1,sizeof(*record));CHECK(bank && record);
    ts_instrument_init(bank);ts_instrument_init(record);
    TsSamplePages pages;CHECK(ts_sample_pages_init(&pages,error,sizeof(error)));
    TsMosaic *scene=ts_mosaic_create();CHECK(scene);pages.mosaic=scene;
    TsCdpRuntime runtime;TsCdpRunOptions options;TsCdpRunResult result;
    ts_cdp_runtime_init(&runtime);ts_cdp_run_options_init(&options);ts_cdp_run_result_init(&result);
    CHECK(ts_cdp_runtime_discover(&runtime,argv[1],NULL,error,sizeof(error)));
    TsGeneratorRecipe seed={0};seed.kind=TS_GENERATOR_FM;seed.seed=0x53415731u;
    TsFmPatch patch;ts_fm_patch_from_recipe(&seed,&patch);
    patch.structure=0;patch.active_mask=1;patch.ratios[0]=1;patch.waveforms[0]=TS_FM_WAVE_SAW;
    patch.drone_mode=1;patch.extreme_mode=0;patch.pitch_lock=1;patch.mutation_mask=TS_FM_MUTATE_FILTER;
    patch.feedback=0;patch.interaction_mix=0;patch.transient_mix=0;patch.depth=.15f;
    /* A moderate cutoff keeps this patch in the existing filter's stable
       range, leaving a saw spectrum for CDP rather than a near-Nyquist burst. */
    patch.filter_mode=TS_FILTER_LOWPASS;patch.filter_cutoff_hz=6000;
    patch.filter_resonance=.19f;patch.filter_envelope_amount=0;
    for(int i=0;i<TS_FM_OPERATOR_COUNT;++i){patch.lfo_types[i]=TS_FM_LFO_OFF;patch.lfo_depths[i]=0;}
    bank->generator=seed;
    CHECK(ts_instrument_apply_fm_patch(bank,&patch,error,sizeof(error)));
    snprintf(bank->current.name,sizeof(bank->current.name),"SAW SOURCE C4");
    ts_instrument_set_selection(bank,bank->current.sample_rate/2,7*bank->current.sample_rate);
    CHECK(ts_instrument_set_loop_from_selection(bank,error,sizeof(error)));
    ts_instrument_clear_selection(bank);
    CHECK(ts_instrument_set_loop_crossfade(bank,50,error,sizeof(error)));
    TsSample clean={0};CHECK(ts_sample_clone(&clean,&bank->current,error,sizeof(error)));
    TsMosaicSource *sources[4];sources[0]=ts_mosaic_source(scene,&clean,error,sizeof(error));CHECK(sources[0]);
    char path[1024];path_for(path,sizeof(path),argv[2],"01-Saw-Source-C4.wav");
    CHECK(ts_sample_save_wav32f(&clean,path,error,sizeof(error)));
    static TsPortalLibrary library;
    const char *filenames[]={"02-Supersaw-Tight-C4.wav","03-Supersaw-C4.wav","04-Witch-Saw-C4.wav"};
    for(unsigned i=0;i<3;++i) {
        CHECK(ts_portal_instrument_recipe(8+i,&library.recipes[i]));
        CHECK(ts_cdp_run_portal(&runtime,&library.recipes[i],&clean,&options,&result,error,sizeof(error)));
        snprintf(result.output.name,sizeof(result.output.name),"%s C4",library.recipes[i].name);
        path_for(path,sizeof(path),argv[2],filenames[i]);CHECK(ts_sample_save_wav32f(&result.output,path,error,sizeof(error)));
        CHECK(ts_instrument_select_bank(bank,(int)i+1,error,sizeof(error)));
        CHECK(ts_instrument_import_sample(bank,&result.output,1,result.output.sample_rate/2,
                                          7*result.output.sample_rate,TS_LOOP_FORWARD,error,sizeof(error)));
        CHECK(ts_instrument_set_loop_crossfade(bank,50,error,sizeof(error)));
        sources[i+1]=ts_mosaic_source(scene,&bank->current,error,sizeof(error));CHECK(sources[i+1]);
        printf("%s: %zu frames, %.4f peak\n",library.recipes[i].name,result.output.frames,result.peak);
    }
    path_for(path,sizeof(path),argv[2],"Supersaw-Presets.recipes");CHECK(ts_portal_library_save(&library,path,error,sizeof(error)));
    /* A listening comparison at one octave below the C4 source, equal event
       gains, two-second gap, then the separate evolving Mosaic arrangement. */
    TsMosaic *comparison=ts_mosaic_create();CHECK(comparison);comparison->gain=.7f;
    for(unsigned i=0;i<4;++i) {
        TsMosaicSource *s=ts_mosaic_source(comparison,&sources[i]->sample,error,sizeof(error));CHECK(s);
        event(comparison,s,i?library.recipes[i-1].name:"SAW SOURCE",i*5,4,100*i,.65f,0,.2,.5,48,-1,-1);
    }
    path_for(path,sizeof(path),argv[2],"05-Dry-Comparison.wav");render_scene(comparison,20,path);ts_mosaic_free(comparison);
    scene->gain=1;
    event(scene,sources[0],"ROOT",0,26,0,.22f,0,.25,6,36,-1,-1);
    event(scene,sources[1],"TIGHT / LOW FIFTH",0,16,100,.33f,-.4f,2,5,48,55,-1);
    event(scene,sources[2],"SUPERSAW / C MINOR",4,22,200,.30f,.35f,4,7,60,63,67);
    TsMosaicEvent *wide=event(scene,sources[3],"WITCH / HIGH FIFTH",10,16,300,.34f,-.2f,5,6,72,79,-1);
    wide->first=44100;wide->last=6*44100;
    path_for(path,sizeof(path),argv[2],"06-Mosaic-Performance.wav");render_scene(scene,28,path);
    CHECK(ts_instrument_select_bank(bank,0,error,sizeof(error)));
    path_for(path,sizeof(path),argv[2],"Supersaw-Mosaic.tsr");
    CHECK(ts_sample_pages_bundle_project_path(path,path,sizeof(path),error,sizeof(error)));
    CHECK(ts_sample_pages_save_project(&pages,bank,record,NULL,path,error,sizeof(error)));
    uint64_t arrangement=ts_mosaic_hash(scene),source=ts_sample_hash(&bank->current);
    CHECK(ts_sample_pages_load_project(&pages,bank,record,path,error,sizeof(error)));
    CHECK(ts_mosaic_hash(scene)==arrangement && ts_sample_hash(&bank->current)==source);
    CHECK(bank->bank[0].has_generator && bank->bank[0].generator.fm_patch.waveforms[0]==TS_FM_WAVE_SAW);
    CHECK(!strcmp(bank->bank[0].sample.name,"SAW SOURCE C4"));
    for(int i=0;i<4;++i)CHECK(bank->bank[i].has_loop && bank->bank[i].loop_crossfade_ms==50);
    /* Native UI screenshot of the exact recipe and rendered waveform. */
    TsUiState *ui=malloc(sizeof(*ui));TsFramebuffer *fb=malloc(sizeof(*fb));CHECK(ui && fb);ts_ui_init(ui);
    ui->portal.open=1;ui->portal.chain_active=1;ui->portal.chain_stage=0;ui->portal.macro_view=1;
    ui->portal.chain=library.recipes[1];ts_portal_step_get(&ui->portal.chain.stages[0],&ui->portal.recipe);
    ui->portal.family=TS_PORTAL_INSTRUMENTS+1;snprintf(ui->portal.query,sizeof(ui->portal.query),"saw");
    ui->portal.source=&clean;ui->portal.result=&bank->bank[2].sample;ui->portal.valid=1;ui->portal.listen_result=1;
    ui->portal.peak=.7f;snprintf(ui->portal.source_name,sizeof(ui->portal.source_name),"SAW SOURCE C4");
    ts_portal_wave_reset(&ui->portal.waves[0],ui->portal.source);ts_portal_wave_reset(&ui->portal.waves[1],ui->portal.result);
    snprintf(ui->status,sizeof(ui->status),"SUPERSAW / NINE CENTERED LAYERS / COLOUR AND OUTPUT");ts_ui_render(fb,ui,bank);
    path_for(path,sizeof(path),argv[2],"Supersaw-Portal.ppm");CHECK(ts_ui_write_ppm(fb,path));
    free(fb);free(ui);ts_sample_free(&clean);ts_cdp_run_result_free(&result);
    ts_mosaic_free(scene);ts_sample_pages_free(&pages);ts_instrument_free(bank);ts_instrument_free(record);free(bank);free(record);
    puts("Saved and reopened the Mosaic project, FM source and rendered bank tiles.");return 0;
}
