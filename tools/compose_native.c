/* Offline composition fixture. No alternate synth, tracker, mixer or effects:
   use public project APIs and the same application callback as FILE OUT.
   Usage: create PROJECT SCORE_CSV CDP_BIN | render PROJECT WAV SECONDS [START]
   START trims a rendered stream; playback always begins at song order zero.
   This deliberately remains an optional tool, not a new application mode. */
#define SDL_MAIN_HANDLED
#define TAPEHEAD_EMBEDDED
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include "../third_party/tapehead/application/src/ft2_replayer.h"
#include "../third_party/tapehead/application/src/ft2_structs.h"
#include "../third_party/tapehead/application/src/scopes/ft2_scopes.h"
#include <errno.h>

static AudioState studio;
static TsUiState view;
static TsInstrument bank, recording;
static TsSamplePages project;
static TsSisterProjectState state;
static TsCdpRuntime cdp;
static TsPortalLibrary portal_presets;
static SDL_Window *window;
static char message[2048];
static unsigned portal_count;
static int performance;
static int asset_count=19;
#define REQUIRE(call) do { if (!(call)) { fprintf(stderr,"%s: %s\n",#call,message); exit(1); } } while(0)

static void setup(void)
{
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_TIMER)) {
        fprintf(stderr,"SDL: %s\n",SDL_GetError());exit(1);
    }
    window=SDL_CreateWindow("Native composition",0,0,640,400,SDL_WINDOW_HIDDEN);
    REQUIRE(window);
    ts_ui_init(&view);ts_instrument_init(&bank);ts_instrument_init(&recording);
    REQUIRE(ts_sample_pages_init(&project,message,sizeof(message)));
    view.tracker=&project.tracker;
    ts_note_bank_init(&studio.notes);ts_performance_init(&studio.performance);
    ts_performance_init(&studio.tile_launchers);ts_keyboard_sequence_init(&studio.keyboard_sequence);
    ts_tracker_playback_init(&studio.tracker);ts_audio_mixer_init(&studio.mixer);
    ts_capture_init(&studio.capture);ts_sister_runtime_init(&studio.sister);
    studio.output_rate=48000;studio.output_device_channels=2;
    REQUIRE(ts_sister_runtime_reconfigure(&studio.sister,48000,2,message,sizeof(message)));
    REQUIRE(ts_spatial_prepare(&studio.sister.spatial,48000));
    ts_sister_project_state_init(&state,48000);
}

static TsFmPatch basic(int wave,unsigned seed)
{
    TsFmPatch p;ts_fm_patch_basic(&p,(TsFmWaveform)wave);
    p.sound_seed=seed;p.directions=TS_FM_DIRECTION_MELODIC;p.drone_mode=0;
    p.interaction_mix=1;
    p.attack_seconds=.005f;p.decay_seconds=.65f;
    p.pitch_sweep=0;p.transient_mix=0;p.shape=.3f;
    return p;
}

static void select_asset(int alias)
{
    size_t page=(size_t)(alias-1)/16;
    while(project.page_count<=page)REQUIRE(ts_sample_pages_append(&project,message,sizeof(message)));
    REQUIRE(ts_sample_pages_switch(&project,&bank,page,message,sizeof(message)));
    REQUIRE(ts_instrument_select_bank(&bank,(alias-1)%16,message,sizeof(message)));
}

static void fm_asset(int alias,const char *name,TsFmPatch p,float seconds,int root)
{
    select_asset(alias);ts_fm_patch_sanitize(&p);
    bank.generator=(TsGeneratorRecipe){.seed=p.sound_seed,.kind=TS_GENERATOR_FM,
        .seconds=seconds,.frequency=261.625565f,.fm_patch=p,.has_fm_patch=1};
    REQUIRE(ts_instrument_create_selected(&bank,p.sound_seed,message,sizeof(message)));
    if(bank.tuning.root_note!=root)REQUIRE(ts_instrument_set_tuning(&bank,root,0,message,sizeof(message)));
    if(bank.audible_tuning.root_note!=root)REQUIRE(ts_instrument_set_audible_tuning(&bank,root,0,message,sizeof(message)));
    REQUIRE(ts_instrument_bank_rename(&bank,bank.selected_slot,name,message,sizeof(message)));
    snprintf(bank.current.name,sizeof(bank.current.name),"%s",name);
    snprintf(bank.parent.name,sizeof(bank.parent.name),"%s",name);
    REQUIRE(ts_instrument_sync_selected(&bank,message,sizeof(message)));
    project.tracker.aliases[alias]=bank.bank[bank.selected_slot].tile_id;
    printf("FM %02d %-24s %.3fs root %d\n",alias,name,(double)bank.current.frames/bank.current.sample_rate,root);
}

static void transformed_asset(int alias,int source,const char *name,const char *process,double value)
{
    const TsBankSlot *original=ts_sample_pages_find_tile(&project,&bank,project.tracker.aliases[source],NULL);
    REQUIRE(original);
    TsSample input;ts_sample_init(&input);
    REQUIRE(ts_sample_clone(&input,&original->sample,message,sizeof(message)));
    const TsPortalProcess *process_def=ts_portal_process_find(process);REQUIRE(process_def);
    TsPortalRecipe recipe;ts_portal_recipe_default(&recipe,process_def);
    if(process_def->parameter_count)recipe.values[0]=value;
    TsCdpRunOptions opts;ts_cdp_run_options_init(&opts);opts.timeout_ms=120000;
    TsCdpRunResult result;ts_cdp_run_result_init(&result);
    if(!ts_cdp_run_portal(&cdp,&recipe,&input,&opts,&result,message,sizeof(message))) {
        fprintf(stderr,"CDP %s: %s\n%s\n",process,message,result.diagnostic);exit(1);
    }
    REQUIRE(result.finite && result.output.frames>0);
    select_asset(alias);
    REQUIRE(ts_instrument_import_sample(&bank,&result.output,0,0,0,TS_LOOP_FORWARD,message,sizeof(message)));
    REQUIRE(ts_instrument_bank_rename(&bank,bank.selected_slot,name,message,sizeof(message)));
    snprintf(bank.current.name,sizeof(bank.current.name),"%s",name);
    snprintf(bank.parent.name,sizeof(bank.parent.name),"%s",name);
    REQUIRE(ts_instrument_sync_selected(&bank,message,sizeof(message)));
    project.tracker.aliases[alias]=bank.bank[bank.selected_slot].tile_id;
    if(portal_count<TS_PORTAL_SLOTS)portal_presets.recipes[portal_count++]=recipe;
    printf("CDP %02d %-24s %s %.3f -> %.3fs peak %.5f\n",alias,name,process,value,
        (double)result.output.frames/result.output.sample_rate,result.peak);
    ts_cdp_run_result_free(&result);ts_sample_free(&input);
}

static void instruments(void)
{
    TsFmPatch p=basic(TS_FM_WAVE_SINE,1001);
    p.directions=TS_FM_DIRECTION_PERC;p.percussion=TS_FM_PERC_KICK;
    p.ratios[0]=43.65353f/261.625565f;p.pitch_sweep=3.5f;
    p.attack_seconds=.0007f;p.decay_seconds=.145f;p.transient_mix=.075f;
    fm_asset(1,"01 CINDER - kick",p,1.25f,60);
    p=basic(TS_FM_WAVE_SINE,1002);p.directions=TS_FM_DIRECTION_PERC;
    p.percussion=TS_FM_PERC_SNARE;p.structure=3;p.active_mask=7;
    p.ratios[0]=.65f;p.ratios[1]=1.05f;p.ratios[2]=1;
    p.waveforms[2]=TS_FM_WAVE_NOISE;p.transient_mix=.20f;
    p.decay_seconds=.15f;p.attack_seconds=.0008f;
    p.filter_mode=TS_FILTER_LOWPASS;p.filter_cutoff_hz=6000;p.filter_resonance=.25f;p.feedback=.15f;
    fm_asset(2,"02 IRON VEIL - snare",p,1.25f,60);
    p=basic(TS_FM_WAVE_NOISE,1003);p.directions=TS_FM_DIRECTION_PERC;
    p.percussion=TS_FM_PERC_HAT;p.structure=3;p.active_mask=7;
    p.waveforms[0]=p.waveforms[1]=TS_FM_WAVE_SQUARE;p.waveforms[2]=TS_FM_WAVE_NOISE;
    p.ratios[0]=12.7f;p.ratios[1]=17.31f;p.ratios[2]=1;
    p.decay_seconds=.028f;p.attack_seconds=.0005f;p.transient_mix=.15f;
    p.filter_mode=TS_FILTER_HIGHPASS;p.filter_cutoff_hz=5600;p.filter_resonance=.12f;
    fm_asset(3,"03 NEEDLE - closed hat",p,.24f,60);
    p.sound_seed=1004;p.decay_seconds=.20f;p.filter_cutoff_hz=4800;
    fm_asset(4,"04 ASH - open hat",p,1.6f,60);
    p=basic(TS_FM_WAVE_SINE,1005);p.ratios[0]=.125f;
    p.drone_mode=1;p.directions=TS_FM_DIRECTION_DRONE;
    fm_asset(5,"05 FOUNDATION - mono sub",p,4,24);
    p=basic(TS_FM_WAVE_SAW,1006);p.structure=TS_FM_STRUCTURE_UNISON;p.active_mask=63;
    p.drone_mode=1;p.directions=TS_FM_DIRECTION_DRONE;p.feedback=.23f;
    p.filter_mode=TS_FILTER_HIGHPASS;p.filter_cutoff_hz=115;p.filter_resonance=.08f;
    const float detune[]={-11,-5,-1,2,6,12};
    for(int v=0;v<6;++v){p.ratios[v]=.5f*exp2f(detune[v]/1200);
        p.waveforms[v]=TS_FM_WAVE_SAW;p.lfo_types[v]=TS_FM_LFO_PITCH_SINE;
        p.lfo_rates[v]=.065f+.017f*v;p.lfo_depths[v]=.0008f;}
    fm_asset(6,"06 BLACK SNOW - Reese",p,6,48);
    p.sound_seed=1007;p.feedback=.6f;p.filter_cutoff_hz=160;
    for(int v=0;v<6;++v){p.waveforms[v]=v%3==0?TS_FM_WAVE_FOLDED:TS_FM_WAVE_SAW;
        p.lfo_depths[v]=.0014f;p.lfo_rates[v]*=1.5f;}
    fm_asset(7,"07 WHITEOUT - torn Reese",p,6,48);
    p=basic(TS_FM_WAVE_SAW,1008);p.structure=TS_FM_STRUCTURE_UNISON;p.active_mask=63;
    p.attack_seconds=.38f;p.decay_seconds=2.5f;p.filter_mode=TS_FILTER_LOWPASS;
    p.filter_cutoff_hz=1800;p.filter_resonance=.2f;
    for(int v=0;v<6;++v){p.ratios[v]=exp2f(detune[v]*.65f/1200);
        p.waveforms[v]=v&1?TS_FM_WAVE_TRIANGLE:TS_FM_WAVE_SAW;
        p.lfo_types[v]=TS_FM_LFO_FILTER_SINE;p.lfo_rates[v]=.045f+.008f*v;p.lfo_depths[v]=.08f;}
    fm_asset(8,"08 FROST WINDOW - choir",p,8,60);
    p=basic(TS_FM_WAVE_SINE,1009);p.structure=0;p.active_mask=3;p.depth=.85f;
    p.ratios[0]=1;p.ratios[1]=2;p.attack_seconds=.006f;p.decay_seconds=.52f;
    p.filter_mode=TS_FILTER_LOWPASS;p.filter_cutoff_hz=4700;p.filter_resonance=.12f;
    p.lfo_types[0]=TS_FM_LFO_PITCH_SINE;p.lfo_rates[0]=4.7f;p.lfo_depths[0]=.0008f;
    fm_asset(9,"09 LANTERN - glass lead",p,3,60);
    p=basic(TS_FM_WAVE_SINE,1010);p.structure=3;p.active_mask=63;p.depth=1.3f;
    p.attack_seconds=.22f;p.decay_seconds=1.1f;
    float vowel[]={1,2,3,5,9,13};
    for(int v=0;v<6;++v){p.ratios[v]=vowel[v];p.lfo_rates[v]=.11f+.07f*v;
        p.lfo_types[v]=TS_FM_LFO_INDEX_SINE;p.lfo_depths[v]=.18f;}
    p.filter_mode=TS_FILTER_BANDPASS;p.filter_cutoff_hz=1000;p.filter_resonance=.38f;
    fm_asset(10,"10 ABSENT - FM vowel",p,5,60);
    p=basic(TS_FM_WAVE_TRIANGLE,1011);p.structure=3;p.active_mask=7;
    p.ratios[0]=.5f;p.ratios[1]=.75f;p.ratios[2]=1.002f;
    p.drone_mode=1;p.directions=TS_FM_DIRECTION_DRONE;
    p.filter_mode=TS_FILTER_BANDPASS;p.filter_cutoff_hz=360;p.filter_resonance=.3f;
    p.lfo_types[0]=TS_FM_LFO_FILTER_SINE;p.lfo_rates[0]=.09f;p.lfo_depths[0]=.22f;
    fm_asset(11,"11 BURIED - fifth drone",p,8,48);
    p=basic(TS_FM_WAVE_SINE,1012);p.structure=0;p.active_mask=3;p.depth=1.7f;
    p.ratios[0]=1;p.ratios[1]=2.713f;p.attack_seconds=.001f;p.decay_seconds=.32f;
    fm_asset(12,"12 ICE - metallic bell",p,2.5f,60);
    p=basic(TS_FM_WAVE_NOISE,1013);p.attack_seconds=1.2f;p.decay_seconds=2;
    p.filter_mode=TS_FILTER_BANDPASS;p.filter_cutoff_hz=1700;p.filter_resonance=.4f;
    p.lfo_types[0]=TS_FM_LFO_FILTER_SINE;p.lfo_rates[0]=.08f;p.lfo_depths[0]=.65f;
    fm_asset(13,"13 FLURRY - noise swell",p,6,60);
    p=basic(TS_FM_WAVE_SINE,1014);p.directions=TS_FM_DIRECTION_PERC;
    p.percussion=TS_FM_PERC_DIGITAL;p.structure=0;p.active_mask=7;
    p.ratios[0]=.38f;p.ratios[1]=1.731f;p.ratios[2]=3.137f;
    p.depth=4;p.feedback=.5f;p.pitch_sweep=1.2f;p.transient_mix=.16f;
    p.decay_seconds=.13f;p.attack_seconds=.001f;
    fm_asset(14,"14 GATE - iron strike",p,1.2f,60);
    transformed_asset(15,10,"15 ABSENT x3 - time","stretch.time.1",3);
    transformed_asset(16,9,"16 LANTERN - spectral fog","blur.blur",42);
    transformed_asset(17,8,"17 WINDOW - reverse","modify.radical.1",0);
    transformed_asset(18,15,"18 ABSENT - reverse mist","modify.radical.1",0);
}

static void tape_asset(void)
{
    const TsBankSlot *source=ts_sample_pages_find_tile(&project,&bank,project.tracker.aliases[9],NULL);
    REQUIRE(source);
    TsSample input;ts_sample_init(&input);REQUIRE(ts_sample_clone(&input,&source->sample,message,sizeof(message)));
    TsSisterMachine machine;memset(&machine,0,sizeof(machine));
    REQUIRE(ts_sister_machine_init(&machine,input.sample_rate,2,8));
    TsSisterParameters p;ts_sister_parameters_default(&p,input.sample_rate);
    p.head1_level=.7f;p.head1_time_ms=performance?600:625;p.head1_feedback=.33f;
    p.head2_level=.5f;p.head2_scrub=.3f;p.head2_rate_index=2;p.head2_feedback=.16f;
    p.head3_level=.3f;p.head3_span=.28f;p.head3_rate_index=5;
    p.wow=.13f;p.drop=.08f;p.headroom=.7f;p.duck_enabled=0;
    p.decorrelation_enabled=1;p.width=.65f;p.soak=.28f;p.bleed=.32f;
    p.soak_targets=TS_SISTER_EFFECT_TARGET_MIX;p.filter_type=TS_SISTER_FILTER_LOWPASS;
    p.filter_cutoff_hz=2900;p.fx.enabled=0;
    ts_sister_machine_set_parameters(&machine,&p);ts_sister_machine_seed(&machine,0x534e4f57);
    ts_sister_machine_reset(&machine);ts_sister_machine_set_rolling(&machine,1);
    TsSample output={.sample_rate=input.sample_rate,.channels=2,.frames=input.sample_rate*12u};
    output.data=calloc(output.frames*2,sizeof(float));REQUIRE(output.data);
    for(size_t f=0;f<output.frames;++f){
        if(f%512==0)ts_sister_machine_begin_audio_block(&machine);
        float x=f<input.frames?input.data[f]:0;
        TsSisterOutput o=ts_sister_machine_process_frame(&machine,(TsStereoFrame){x,x},(TsStereoFrame){0,0});
        output.data[f*2]=o.mix.l;output.data[f*2+1]=o.mix.r;
        if(f%512==511)ts_sister_machine_end_audio_block(&machine);
    }
    select_asset(19);
    REQUIRE(ts_instrument_import_sample(&bank,&output,0,0,0,TS_LOOP_FORWARD,message,sizeof(message)));
    REQUIRE(ts_instrument_bank_rename(&bank,bank.selected_slot,"19 MEMORY - Sister glass",message,sizeof(message)));
    snprintf(bank.current.name,sizeof(bank.current.name),"19 MEMORY - Sister glass");
    snprintf(bank.parent.name,sizeof(bank.parent.name),"19 MEMORY - Sister glass");
    REQUIRE(ts_instrument_sync_selected(&bank,message,sizeof(message)));
    project.tracker.aliases[19]=bank.bank[bank.selected_slot].tile_id;
    ts_sample_free(&input);ts_sample_free(&output);ts_sister_machine_free(&machine);
    printf("SISTER 19 MEMORY: %dms echo, reverse and half-speed heads, wow, weave\n",performance?600:625);
}

static void performance_instruments(void)
{
    TsFmPatch p=basic(TS_FM_WAVE_TRIANGLE,2020);
    p.structure=TS_FM_STRUCTURE_UNISON;p.active_mask=63;
    p.drone_mode=1;p.directions=TS_FM_DIRECTION_DRONE;
    p.filter_mode=TS_FILTER_LOWPASS;p.filter_cutoff_hz=2100;p.filter_resonance=.12f;
    const float cents[]={-8,-3,0,2,5,9};
    for(int v=0;v<6;++v){
        p.ratios[v]=exp2f(cents[v]/1200);p.waveforms[v]=v==4?TS_FM_WAVE_SAW:TS_FM_WAVE_TRIANGLE;
        p.lfo_types[v]=TS_FM_LFO_FILTER_SINE;p.lfo_rates[v]=.018f+.004f*v;p.lfo_depths[v]=.14f;
    }
    fm_asset(20,"20 HALO - sustained choir",p,8,60);
    transformed_asset(21,8,"21 FROST - long exhale","stretch.time.1",6);
    transformed_asset(22,20,"22 HALO - spectral light","blur.blur",96);
    transformed_asset(23,13,"23 FLURRY - slow dust","stretch.time.1",6);
}

static void mixer(void)
{
    TsSisterParameters *p=&state.parameters;
    p->prism.enabled=1;p->prism.mode=TS_PRISM_ENSEMBLE;p->prism.lenses=8;
    p->prism.spread=.28f;p->prism.drift=.18f;p->prism.focus=.7f;
    p->prism.stereo=.8f;p->prism.body=.8f;p->prism.mix=1;p->prism.dry_level=0;
    p->prism.output_db=-4;p->prism.morph_enabled=0;
    p->fx.enabled=1;p->fx.transition=0;p->fx.master_transition=0;
    for(int i=0;i<4;++i)p->fx.slot[i]=(TsSisterFxSlotControls){0};
    p->fx.slot[0]=(TsSisterFxSlotControls){.type=TS_SISTER_FX_DELAY,.enabled=1,
        .placement=TS_SISTER_FX_PLACE_POST,.mix=.28f,.parameter_a=.7894f,.parameter_b=.32f};
    p->fx.slot[1]=(TsSisterFxSlotControls){.type=TS_SISTER_FX_REVERB,.enabled=1,
        .placement=TS_SISTER_FX_PLACE_POST,.mix=.72f,.parameter_a=.68f,.parameter_b=.67f};
    p->fx.fallout.enabled=1;p->fx.fallout.mix=.6f;p->fx.fallout.noise=0;
    p->fx.fallout.skip_enabled=1;p->fx.fallout.skip_span=.22f;p->fx.fallout.skip_rate=.18f;
    p->fx.fallout.pan_enabled=1;p->fx.fallout.pan_rate=.17f;
    p->fx.fallout.bit_enabled=1;p->fx.fallout.bit_quality=.68f;p->fx.fallout.bit_resolution=.12f;
    p->fx.fallout.feedback=.08f;p->fx.fallout.pitch_enabled=0;p->fx.fallout.drop_enabled=0;
    p->fx.fallout.lfo_intensity=0;p->fx.fallout.rise_intensity=0;
    state.router.send_mask=(1u<<TS_SEND_PRISM)|(1u<<TS_SEND_PEDALBOARD)|(1u<<TS_SEND_FALLOUT);
    state.router.return_level[TS_SEND_PRISM]=65;state.router.return_level[TS_SEND_PEDALBOARD]=58;
    state.router.return_level[TS_SEND_FALLOUT]=22;
    for(int i=0;i<TS_SOURCE_SENDS;++i)state.router.return_route[i]=(TsSourceRoute){
        .mode=TS_SOURCE_PAIR,.speaker=0,.second=1,.width=0};
    static const char *names[]={"CINDER / KICK","IRON / SNARE","NEEDLES / HATS","ASH / METAL", "MONO FOUNDATION",
        "BLACK SNOW / REESE","CHOIR LEFT","CHOIR CENTER","CHOIR RIGHT","LANTERN MOTIF",
        "ABSENT / GHOST","BURIED DRONE","ICE / ANSWER","WHITEOUT ECHO","REVERSE / SWELL","IRON ACCENTS"};
    static const float trims[]={1.1f,.90f,.55f,.43f,.40f,.60f,.50f,.44f,.48f,.65f,.55f,.30f,.34f,.35f,.42f,.42f};
    static const int pans[]={0,0,-18,30,0,0,-60,0,60,-8,35,0,42,-35,0,-30};
    static const int prism[]={0,0,0,0,0,10,65,50,65,10,38,22,24,20,45,0};
    static const int verb[]={0,18,5,12,0,0,44,40,44,38,55,32,45,24,65,30};
    if(performance){
        p->fx.slot[0].parameter_a=logf(600.0f/8.0f)/logf(250.0f);
        p->fx.slot[0].mix=.20f;
        p->fx.slot[0].parameter_b=.26f;
    }
    for(int i=0;i<16;++i){TsTrackerLane *l=&project.tracker.lanes[i];
        snprintf(l->name,sizeof(l->name),"%s",names[i]);l->trim=trims[i]*.75f;
        l->output_route=(TsSourceRoute){.mode=TS_SOURCE_PAIR,.speaker=0,.second=1,.pan=pans[i],
            .width=(i==4?-100:0),.mix_enabled=1,.clean_level=100};
        l->output_route.send_level[TS_SEND_PRISM]=prism[i];
        l->output_route.send_level[TS_SEND_PEDALBOARD]=verb[i];
        l->output_route.send_level[TS_SEND_FALLOUT]=(i==10||i==14)?35:0;
        if(performance&&i==9){
            l->output_route.send_level[TS_SEND_PEDALBOARD]=16;
            l->output_route.send_level[TS_SEND_PRISM]=5;
        }
    }
}

/* Text score fields: pattern,row,lane,alias,MIDI,volume,ASCII-effect,value.
   MIDI -1=none,-2=off,-3=cut; volume -1=unchanged. */
static void read_score(const char *path)
{
    FILE *f=fopen(path,"r");REQUIRE(f);char line[512];
    char pattern_names[256][TS_TRACKER_NAME_SIZE]={{0}};
    TsSisterTracker *t=&project.tracker;t->bpm=performance?75:72;t->ticks_per_line=3;t->channel_count=16;t->loop=0;
    while(fgets(line,sizeof(line),f)){
        if(!strncmp(line,"#@pattern ",10)){
            int index,offset=0;
            REQUIRE(sscanf(line+10,"%d %n",&index,&offset)==1&&index>=0&&index<256);
            line[strcspn(line,"\r\n")]=0;
            snprintf(pattern_names[index],TS_TRACKER_NAME_SIZE,"%s",line+10+offset);
        }
        if(line[0]=='#'||line[0]=='\n')continue;
        int p,r,l,a,n,v,fx,x;
        REQUIRE(sscanf(line,"%d,%d,%d,%d,%d,%d,%d,%d",&p,&r,&l,&a,&n,&v,&fx,&x)==8);
        REQUIRE(p>=0&&p<256&&r>=0&&r<128&&l>=0&&l<16&&a>=0&&a<=asset_count&&n>=-3&&n<=107&&v>=-1&&v<=64);
        REQUIRE(fx==0||(fx>='0'&&fx<='9')||(fx>='A'&&fx<='Z'));REQUIRE(x>=0&&x<=255);
        while(t->pattern_count<=p){TsPatternId id;REQUIRE(ts_sister_tracker_add_pattern(t,128,&id,message,sizeof(message)));
            t->orders[t->order_count++]=id;}
        TsTrackerCell *c=&t->patterns[p]->cells[r][l];
        c->tile_id=a?t->aliases[a]:0;c->note_kind=n>=0?TS_TRACKER_NOTE_PITCH:n==-2?TS_TRACKER_NOTE_OFF:n==-3?TS_TRACKER_NOTE_CUT:TS_TRACKER_NOTE_NONE;
        c->note=n>=0?n:0;c->has_volume=v>=0;c->volume=v>=0?v:0;c->fx_command=fx;c->fx_value=x;
    }
    REQUIRE(!ferror(f));fclose(f);REQUIRE(t->pattern_count>0);t->editor_pattern=t->patterns[0]->id;
    for(int i=0;i<t->pattern_count;++i){
        const char *part=t->pattern_count<22?"STUDY":i<2?"FROST":i<6?"FIRST SNOW":i<10?"UNDER GLASS":
            i<12?"ABSENCE":i<14?"MEMORY":i<20?"WHITEOUT":i<22?"AFTERIMAGE":"SILENCE HOLD";
        snprintf(t->patterns[i]->name,TS_TRACKER_NAME_SIZE,"%02d %s",i+1,part);
        if(pattern_names[i][0])memcpy(t->patterns[i]->name,pattern_names[i],TS_TRACKER_NAME_SIZE);
    }
    REQUIRE(ts_sister_tracker_validate(t,message,sizeof(message)));
}

static void save_and_verify(const char *path)
{
    REQUIRE(ts_sample_pages_switch(&project,&bank,0,message,sizeof(message)));
    REQUIRE(ts_instrument_select_bank(&bank,5,message,sizeof(message)));
    REQUIRE(embedded_open(window,0,&studio,&view,&project,&bank,48000));
    snprintf(song.name,sizeof(song.name),"%s",performance?"THRESHOLD":"BLACK SNOW");
    REQUIRE(ts_tapehead_export(&project.tracker,message,sizeof(message)));
    ts_tapehead_close();
    state.page_count=project.page_count;
    REQUIRE(ts_sample_pages_save_project(&project,&bank,&recording,&state,path,message,sizeof(message)));
    uint64_t score=ts_sister_tracker_hash(&project.tracker),samples[24]={0};
    for(int a=1;a<=asset_count;++a){const TsBankSlot *s=ts_sample_pages_find_tile(&project,&bank,project.tracker.aliases[a],NULL);
        REQUIRE(s);samples[a]=ts_sample_hash(&s->sample);}
    TsSisterProjectState expected=state;
    REQUIRE(ts_sample_pages_load_project(&project,&bank,&recording,path,message,sizeof(message)));
    REQUIRE(score==ts_sister_tracker_hash(&project.tracker));
    for(int a=1;a<=asset_count;++a){const TsBankSlot *s=ts_sample_pages_find_tile(&project,&bank,project.tracker.aliases[a],NULL);
        REQUIRE(s&&samples[a]==ts_sample_hash(&s->sample));
        if(a<=14||a==20)REQUIRE(s->has_generator&&s->generator.has_fm_patch);}
    int present=0;REQUIRE(ts_sister_project_state_load(&state,path,48000,&present,message,sizeof(message)));REQUIRE(present);
    REQUIRE(!memcmp(&expected.router,&state.router,sizeof(state.router)));
    REQUIRE(fabsf(expected.parameters.prism.spread-state.parameters.prism.spread)<1e-6f);
    REQUIRE(fabsf(expected.parameters.fx.slot[0].parameter_a-state.parameters.fx.slot[0].parameter_a)<1e-6f);
    printf("RELOAD VERIFIED: %u patterns, %u orders, %d tiles, native FM recipes, score hash %llu\n",
        project.tracker.pattern_count,project.tracker.order_count,asset_count,(unsigned long long)score);
    char presets[1400];snprintf(presets,sizeof(presets),"%s-portal-recipes.txt",path);
    REQUIRE(ts_portal_library_save(&portal_presets,presets,message,sizeof(message)));
}

static void put_le(FILE *f,uint32_t v,int bytes){while(bytes--){REQUIRE(fputc(v&255,f)!=EOF);v>>=8;}}
static void render(const char *path,const char *wav,double seconds,double start)
{
    REQUIRE(isfinite(seconds)&&seconds>0&&seconds<=3600&&isfinite(start)&&start>=0&&start<seconds);
    REQUIRE(ts_sample_pages_load_project(&project,&bank,&recording,path,message,sizeof(message)));
    int present=0;REQUIRE(ts_sister_project_state_load(&state,path,48000,&present,message,sizeof(message)));REQUIRE(present);
    REQUIRE(ts_sister_project_state_apply(&state,&studio.sister,&bank));
    REQUIRE(embedded_open(window,0,&studio,&view,&project,&bank,48000));view.tracker_open=1;
    const char *solo=SDL_getenv("TS_COMPOSE_SOLO");
    if(solo){int lane=atoi(solo);REQUIRE(lane>=0&&lane<16);for(int i=0;i<16;++i)setChannelMute(i,i!=lane);}
    startPlaying(PLAYMODE_SONG,0);
    uint64_t total=(uint64_t)llround(seconds*48000),skip=(uint64_t)llround(start*48000);
    uint64_t score_end=(uint64_t)llround(project.tracker.order_count*128.0*project.tracker.ticks_per_line*2.5/project.tracker.bpm*48000);
    FILE *f=fopen(wav,"wb");REQUIRE(f);uint32_t bytes=(uint32_t)((total-skip)*6);
    fwrite("RIFF",1,4,f);put_le(f,36+bytes,4);fwrite("WAVEfmt ",1,8,f);put_le(f,16,4);
    put_le(f,1,2);put_le(f,2,2);put_le(f,48000,4);put_le(f,48000*6,4);put_le(f,6,2);put_le(f,24,2);
    fwrite("data",1,4,f);put_le(f,bytes,4);
    float out[512*2];double energy=0,peak=0;uint64_t clipped=0;int stopped=0;
    for(uint64_t at=0;at<total;){
        unsigned frames=(unsigned)fmin(512,total-at);
        if(at<score_end&&at+frames>score_end)frames=(unsigned)(score_end-at);
        if(at>=score_end&&!stopped){ts_tapehead_stop();stopped=1;}
        audio_callback(&studio,(Uint8*)out,(int)(frames*2*sizeof(float)));
        for(unsigned i=0;i<frames;++i)for(int ch=0;ch<2;++ch){
            float value=out[i*2+ch];REQUIRE(isfinite(value));if(at+i<skip)continue;
            energy+=(double)value*value;peak=fmax(peak,fabs(value));clipped+=fabs(value)>=.97999f;
            int32_t pcm=(int32_t)llround(fmax(-1,fmin(1,value))*8388607);put_le(f,(uint32_t)pcm,3);
        }
        at+=frames;
        if(at%480000==0){printf("render %.0f / %.0f seconds\n",at/48000.0,seconds);fflush(stdout);}
    }
    REQUIRE(!ferror(f));REQUIRE(fclose(f)==0);
    printf("WAV %.3fs peak %.6f (%.2fdBFS), RMS %.2fdBFS, guard-contact samples %llu\n",
        (total-skip)/48000.0,peak,20*log10(peak),10*log10(energy/((total-skip)*2)),(unsigned long long)clipped);
}

int main(int argc,char **argv)
{
    if(argc<5){fprintf(stderr,"create|create-performance PROJECT SCORE_CSV CDP_BIN | render PROJECT WAV END_SECONDS [START_SECONDS]\n");return 2;}
    performance=!strcmp(argv[1],"create-performance");asset_count=performance?23:19;
    setup();
    if(!strcmp(argv[1],"create")||performance){
        ts_cdp_runtime_init(&cdp);REQUIRE(ts_cdp_runtime_discover(&cdp,argv[4],NULL,message,sizeof(message)));
        instruments();tape_asset();if(performance)performance_instruments();mixer();read_score(argv[3]);save_and_verify(argv[2]);
    }else if(!strcmp(argv[1],"render"))render(argv[2],argv[3],strtod(argv[4],NULL),argc>5?strtod(argv[5],NULL):0);
    else {fprintf(stderr,"Unknown mode\n");return 2;}
    ts_tapehead_close();ts_sister_runtime_free(&studio.sister);ts_sample_pages_free(&project);
    ts_instrument_free(&bank);ts_instrument_free(&recording);SDL_DestroyWindow(window);SDL_Quit();return 0;
}
