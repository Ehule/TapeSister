/* Prism bank I/O stays on the event thread. Audio sees only a bounded state copy. */
static int prism_presets_file_path(char *path,size_t size)
{
    char directory[TS_RUNTIME_PATH_MAX];
    if(runtime_path_parent(config_file_path(),directory,sizeof(directory)) &&
       runtime_join_path(path,size,directory,"prism-presets.ini"))return 1;
    return snprintf(path,size,"prism-presets.ini")>=0 && strlen("prism-presets.ini")<size;
}
static void sister_prism_bank_sync(SisterWindow *s)
{
    unsigned count=s->prism_presets.count;
    int index=s->model.prism_preset_index;
    if(index<0 || index>=(int)count)index=0;
    s->model.prism_preset_index=index;s->model.prism_preset_count=(int)count;
    snprintf(s->model.prism_preset_name,sizeof(s->model.prism_preset_name),"%.31s",
        count ? s->prism_presets.entries[index].name : "EMPTY");
}
static void sister_prism_publish(SDL_AudioDeviceID device,AudioState *audio,SisterWindow *s,const TsPrismControls *c)
{
    if(device)SDL_LockAudioDevice(device);
    TsSisterParameters p=audio->sister.parameters;p.prism=*c;
    ts_sister_runtime_set_parameters(&audio->sister,&p);
    ts_sister_runtime_mark_selected_preset_modified(&audio->sister);
    s->model.parameters=audio->sister.parameters;
    if(device)SDL_UnlockAudioDevice(device);
    s->rendered_model_valid=0;
}
static void sister_prism_preserve_parameter_locks(const SisterWindow *s,const TsPrismControls *old,TsPrismControls *c)
{
#define KEEP(id,field) if(ts_sister_ui_parameter_locked(&s->model,TS_SISTER_UI_PARAM_PRISM_##id))c->field=old->field
    KEEP(LENSES,lenses);KEEP(SPREAD,spread);KEEP(DRIFT,drift);KEEP(FOCUS,focus);
    KEEP(STEREO,stereo);KEEP(BODY,body);KEEP(MIX,mix);KEEP(OUTPUT,output_db);
    KEEP(DRY,dry_level);KEEP(COLOR,color);KEEP(RATE,drift_rate);KEEP(OCTAVE,group_octave);
    KEEP(SEQ_RATE,seq_rate);KEEP(TIME,morph_seconds);
#undef KEEP
}
static int sister_prism_performance_click(SDL_AudioDeviceID device,AudioState *audio,SisterWindow *s,int x,int y,int right,SDL_Keymod mod)
{
    if(y>=250 && y<272) {
        int panel=x>=16 && x<94 ? 0 : x>=102 && x<194 ? 1 : x>=202 && x<288 ? 2 : -1;
        if(panel>=0) { s->model.prism_panel=panel;s->rendered_model_valid=0;return 1; }
    }
    TsPrismControls c=s->model.parameters.prism;
    if(s->model.prism_panel==1) {
        if(y>=280 && y<302 && x>=16 && x<298) {
            int which=x<82 ? 0 : x>=88 && x<154 ? 1 : x>=160 && x<226 ? 2 : x>=232 ? 3 : -1;
            if(which<0)return 0;
            if(which<2) {
                if(right)ts_prism_recall(&c,which);
                else if(c.morph_enabled) {
                    snprintf(s->model.status,sizeof(s->model.status),"RIGHT CAP A/B TO RECALL BEFORE EDITING OR CAPTURING");return 1;
                } else ts_prism_capture(&c,which);
            } else if(c.captured==3) {
                c.morph_enabled=1;c.morph_target=which-2;++c.morph_trigger;
                if(!c.morph_trigger)++c.morph_trigger;
            } else {
                snprintf(s->model.status,sizeof(s->model.status),"CAPTURE A AND B BEFORE MORPHING");return 1;
            }
            snprintf(s->model.status,sizeof(s->model.status),"%s %c",which<2 ? (right?"RECALLED":"CAPTURED") : "MORPH TO",'A'+which%2);
        } else if(y>=308 && y<330 && x>=16 && x<298) {
            if(x<82)c.seq_enabled=!c.seq_enabled;
            else if(x>=88 && x<154) { s->model.prism_seq_edit=!s->model.prism_seq_edit;s->rendered_model_valid=0;return 1; }
            else if(x>=160 && x<226)++c.seq_reset;
            else if(x>=232) { c.seq_count=0;memset(c.sequence,0,sizeof(c.sequence));++c.seq_reset; }
            else return 0;
            snprintf(s->model.status,sizeof(s->model.status),"SEQ %s: %d STEPS / OTHER LENSES SUSTAIN",c.seq_enabled?"ON":"OFF",c.seq_count);
        } else if(y>=308 && y<330 && x>=472 && x<616) {
            c.snap=(c.snap+(right?3:1))%4;
            snprintf(s->model.status,sizeof(s->model.status),"DRAG SNAP %s / ARROWS ALWAYS FINE",ts_prism_snap_name(c.snap));
        } else return 0;
    } else if(s->model.prism_panel==2) {
        if(y>=308 && y<330 && x>=16 && x<616) {
            int lock=(x-16)/101;if(lock>=6 || (x-16)%101>=95)return 0;
            c.locks^=1u<<lock;
        } else if(y>=280 && y<302) {
            if(x>=412 && x<616) {
                static uint32_t serial=0;
                uint32_t seed=(uint32_t)SDL_GetPerformanceCounter() ^ (++serial*747796405u);
                int vary=x>=516;TsPrismControls old=c;ts_prism_generate(&c,seed,vary);
                sister_prism_preserve_parameter_locks(s,&old,&c);
                snprintf(s->model.status,sizeof(s->model.status),"PRISM %s / LOCKED GROUPS PRESERVED",vary?"VARIED":"NEW");
            } else if(x>=314 && x<404) {
                TsPrismBank *next=malloc(sizeof(*next));
                if(!next) { snprintf(s->model.status,sizeof(s->model.status),"PRISM SAVE: OUT OF MEMORY");return 1; }
                *next=s->prism_presets;
                int index=(mod&KMOD_SHIFT) && next->count ? s->model.prism_preset_index : (int)next->count;
                if(index>=TS_PRISM_PRESETS) { free(next);snprintf(s->model.status,sizeof(s->model.status),"PRISM BANK FULL / SHIFT SAVE OVERWRITES SELECTED");return 1; }
                if(index==(int)next->count)++next->count;
                if(c.morph_enabled && s->model.routing.prism.valid) { c.morph=s->model.routing.prism.morph;c.morph_trigger=0; }
                next->entries[index].controls=c;
                snprintf(next->entries[index].name,32,"%02d %s %02d",index+1,ts_prism_mode_name(c.mode),c.lenses);
                char path[TS_RUNTIME_PATH_MAX],error[160];
                if(prism_presets_file_path(path,sizeof(path)) && ts_prism_bank_save(next,path,error,sizeof(error))) {
                    s->prism_presets=*next;s->model.prism_preset_index=index;sister_prism_bank_sync(s);
                    snprintf(s->model.status,sizeof(s->model.status),"PRISM SAVED: %.31s",s->model.prism_preset_name);
                } else snprintf(s->model.status,sizeof(s->model.status),"COULD NOT SAVE PRISM BANK");
                free(next);s->rendered_model_valid=0;return 1;
            } else if(x>=16 && x<306) {
                if(right && x>=52 && x<270)ts_prism_reset_lenses(&c);
                else if(s->prism_presets.count) {
                    int index=s->model.prism_preset_index;
                    if(x<46)index=(index+(int)s->prism_presets.count-1)%(int)s->prism_presets.count;
                    else if(x>=276)index=(index+1)%(int)s->prism_presets.count;
                    else if(x<52 || x>=270)return 0;
                    s->model.prism_preset_index=index;c=s->prism_presets.entries[index].controls;sister_prism_bank_sync(s);
                    snprintf(s->model.status,sizeof(s->model.status),"PRISM RECALLED: %.31s",s->model.prism_preset_name);
                } else return 1;
            } else return 0;
        } else return 0;
    } else return 0;
    sister_prism_publish(device,audio,s,&c);return 1;
}
/* Solve against audible cents so inverted glass still makes Up move up.
   Drift is excluded; the user tunes the center of its wandering trajectory. */
static float sister_prism_pitch_offset(const TsPrismControls *c,int lens,float target)
{
    TsPrismControls probe=*c;probe.drift=0;probe.morph_enabled=0;
    float low=-1200,high=1200;
    probe.pitch_offset[lens]=low;float a=ts_prism_control_view(&probe).lens[lens].cents;
    probe.pitch_offset[lens]=high;float b=ts_prism_control_view(&probe).lens[lens].cents;
    for(int j=0;j<20;++j) {
        float mid=(low+high)*.5f;probe.pitch_offset[lens]=mid;
        float value=ts_prism_control_view(&probe).lens[lens].cents;
        if((value<target)==(a<b))low=mid;else high=mid;
    }
    return (low+high)*.5f;
}
