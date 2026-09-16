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
#include "main_sdl_prism_matrix.inc"
static void sister_prism_preserve_parameter_locks(const SisterWindow *s,const TsPrismControls *old,TsPrismControls *c)
{
#define KEEP(id,field) if(ts_sister_ui_parameter_locked(&s->model,TS_SISTER_UI_PARAM_PRISM_##id))c->field=old->field
    KEEP(LENSES,lenses);KEEP(SPREAD,spread);KEEP(DRIFT,drift);KEEP(FOCUS,focus);
    KEEP(STEREO,stereo);KEEP(BODY,body);KEEP(MIX,mix);KEEP(OUTPUT,output_db);
    KEEP(DRY,dry_level);KEEP(COLOR,color);KEEP(RATE,drift_rate);KEEP(OCTAVE,group_octave);
    KEEP(SEQ_RATE,seq_rate);KEEP(TIME,morph_seconds);
#undef KEEP
}
static void sister_prism_factory_cycle(SDL_AudioDeviceID device,AudioState *audio,
                                        SisterWindow *s,int steps)
{
    TsPrismControls c=s->model.parameters.prism,old=c;
    if(device)SDL_LockAudioDevice(device);
    int matrix_active=audio->sister.prism.matrix.active;
    if(device)SDL_UnlockAudioDevice(device);
    if(c.morph_enabled || (matrix_active && !s->model.prism_matrix_edit)) {
        snprintf(s->model.status,sizeof(s->model.status),"MORPH LOCKED / RIGHT-CLICK CAP TO EDIT");return;
    }
    int index=ts_prism_factory_find(&c);
    if(index<0)index=s->model.prism_factory_index;
    if(index<0 || index>=TS_PRISM_FACTORY_PRESETS)index=c.mode;
    index=(index+steps%TS_PRISM_FACTORY_PRESETS+TS_PRISM_FACTORY_PRESETS)%TS_PRISM_FACTORY_PRESETS;
    ts_prism_factory_apply(&c,index);
    sister_prism_preserve_parameter_locks(s,&old,&c);
    s->model.prism_factory_index=index;
    sister_prism_publish(device,audio,s,&c);
    sister_preset_model_sync(s,&audio->sister);
    snprintf(s->model.status,sizeof(s->model.status),"FACTORY %02d/%02d: %s / GROUP OCT %+.0F / CAP TO KEEP",
        index+1,TS_PRISM_FACTORY_PRESETS,ts_prism_factory_name(index),(double)c.group_octave);
}
static int sister_prism_endpoint_at(int x,int y)
{
    if(y<280 || y>=302)return -1;
    if((x>=16 && x<82) || (x>=160 && x<226))return 0;
    if((x>=88 && x<154) || (x>=232 && x<298))return 1;
    return -1;
}
static void sister_prism_browse_state(SisterWindow *s,int side,int wheel)
{
    int state=ts_sister_ui_prism_state(&s->model,side);
    int direction=wheel>0?1:-1,steps=wheel%(TS_PRISM_STATES-1);if(steps<0)steps=-steps;
    if(state<0)state=direction>0?TS_PRISM_STATES-1:0;
    while(steps--) {
        do {state=(state+direction+TS_PRISM_STATES)%TS_PRISM_STATES;}
        while(state==ts_sister_ui_prism_assigned(&s->model,1-side));
    }
    s->model.prism_browse[side]=state==ts_sister_ui_prism_assigned(&s->model,side)?0:state+1;
    snprintf(s->model.status,sizeof(s->model.status),"CHOOSE %c / %s / WHEEL DOES NOT CHANGE SOUND",'A'+state,
        s->model.parameters.prism.captured&(1<<state)?"CLICK TO MORPH; RIGHT-CLICK TO EDIT":
        s->model.parameters.prism.morph_enabled?"EMPTY: RIGHT-CLICK CAP TO EDIT":"EMPTY: CLICK CAP TO SAVE LIVE SOUND");
    s->rendered_model_valid=0;
}
/* An explicit clear/empty recall leaves performance mode and assigns an editable
   destination. Unlike wheel browsing, it must not retain a hidden locked pair. */
static void sister_prism_begin_empty_edit(SDL_AudioDeviceID device,AudioState *audio,
                                         SisterWindow *s,TsPrismControls *c,int side,int state)
{
    if(c->morph_enabled) {
        int source_side;
        if(state==c->endpoint[side])source_side=1-side;
        else if(state==c->endpoint[1-side])source_side=side;
        else {
            if(device)SDL_LockAudioDevice(device);
            float position=audio->sister.prism.morph_position;
            if(device)SDL_UnlockAudioDevice(device);
            source_side=position>=.5f;
        }
        if(c->active_pair_valid) {
            TsPrismPatch live=*ts_prism_endpoint_patch(c,source_side);
            memcpy(c,&live,sizeof(live));
        } else ts_prism_recall(c,c->endpoint[source_side]);
    }
    c->active_pair_valid=0;c->morph_enabled=0;c->morph_trigger=0;c->morph=(float)side;
    /* Both buttons may have browsed to the same letter before one was confirmed. */
    if(state==c->endpoint[1-side])c->endpoint[1-side]=c->endpoint[side];
    c->endpoint[side]=state;
    memset(s->model.prism_browse,0,sizeof(s->model.prism_browse));
    s->model.prism_edit_endpoint=0;
}
static int sister_prism_clear_capture(SDL_AudioDeviceID device,AudioState *audio,SisterWindow *s,int x,int y)
{
    if(s->model.prism_panel!=1 || x>=154)return 0;
    int side=sister_prism_endpoint_at(x,y);if(side<0)return 0;
    sister_prism_matrix_takeover(device,audio,s);
    TsPrismControls c=s->model.parameters.prism;int state=ts_sister_ui_prism_state(&s->model,side);
    if(state<0) {
        TsPrismPatch live=*ts_prism_endpoint_patch(&c,side);memcpy(&c,&live,sizeof(live));
        c.active_pair_valid=0;c.morph_enabled=0;c.morph_trigger=0;
        memset(s->model.prism_browse,0,sizeof(s->model.prism_browse));
        sister_prism_publish(device,audio,s,&c);return 1;
    }
    sister_prism_begin_empty_edit(device,audio,s,&c,side,state);
    c.captured&=~(1<<state);memset(ts_prism_state(&c,state),0,sizeof(TsPrismPatch));
    if(!c.captured)c.morph=0;
    sister_prism_publish(device,audio,s,&c);
    snprintf(s->model.status,sizeof(s->model.status),"%c EMPTY / SOUND UNLOCKED / EDIT THEN CAP %c / OTHER STATES KEPT",'A'+state,'A'+state);
    return 1;
}
/* Wheel choices remain in the UI. Only a confirmed idle end changes the DSP
   pair, so browsing or attempting a change mid-morph never replaces its sound. */
static int sister_prism_state_click(SDL_AudioDeviceID device,AudioState *audio,SisterWindow *s,
                                    int which,int right)
{
    if(s->model.prism_matrix_edit && !right && which<2 &&
       ts_sister_ui_prism_state(&s->model,which)==s->model.prism_matrix_edit-1) {
        sister_prism_matrix_save(device,audio,s);return 1;
    }
    sister_prism_matrix_takeover(device,audio,s);
    TsPrismControls c=s->model.parameters.prism;
    int side=which%2,state=ts_sister_ui_prism_state(&s->model,side),pending=state!=c.endpoint[side];
    if(state<0) {
        if(right && which<2) {
            TsPrismPatch live=*ts_prism_endpoint_patch(&c,side);memcpy(&c,&live,sizeof(live));
            c.active_pair_valid=0;c.morph_enabled=0;c.morph_trigger=0;
            sister_prism_publish(device,audio,s,&c);
            snprintf(s->model.status,sizeof(s->model.status),"LIVE SOUND / WHEEL TO A-L AND CAPTURE");
        } else snprintf(s->model.status,sizeof(s->model.status),"LIVE TRANSITION END / WHEEL TO A-L TO STORE A STATE");
        return 1;
    }
    if(state==c.endpoint[1-side] && !(right && which<2 && !pending)) {
        snprintf(s->model.status,sizeof(s->model.status),"%c IS ALREADY ON THE OTHER END / CHOOSE A DIFFERENT LETTER",'A'+state);return 1;
    }
    int occupied=(c.captured&(1<<state))!=0;
    float position=c.morph;
    if(c.morph_enabled) {
        if(device)SDL_LockAudioDevice(device);
        position=audio->sister.prism.morph_position;
        if(device)SDL_UnlockAudioDevice(device);
    }
    if(right && which<2) {
        if(!occupied) {
            sister_prism_begin_empty_edit(device,audio,s,&c,side,state);
            snprintf(s->model.status,sizeof(s->model.status),"EDIT %c (EMPTY) / SOUND UNLOCKED / CLICK CAP %c TO SAVE",'A'+state,'A'+state);
        } else {
            ts_prism_assign_endpoint(&c,side,state);ts_prism_recall(&c,state);s->model.prism_edit_endpoint=state+1;
            snprintf(s->model.status,sizeof(s->model.status),"RECALLED %c / EDIT THEN CLICK CAP %c TO SAVE",'A'+state,'A'+state);
        }
    } else if(which<2 && (!pending || !occupied)) {
        if(c.morph_enabled) {
            snprintf(s->model.status,sizeof(s->model.status),"MORPH LOCKED / RIGHT-CLICK CAP %c TO EDIT, THEN LEFT-CLICK TO SAVE",'A'+state);return 1;
        }
        c.endpoint[side]=state;ts_prism_capture(&c,state);c.morph=(float)side;
        s->model.prism_edit_endpoint=0;
        snprintf(s->model.status,sizeof(s->model.status),"CAPTURED %c / A-L BANK KEPT",'A'+state);
    } else {
        if(!occupied) {
            snprintf(s->model.status,sizeof(s->model.status),"%c IS EMPTY: CAPTURE IT BEFORE MORPHING",'A'+state);return 1;
        }
        if(pending && ts_prism_pair_ready(&c) && fabsf(position-(1-side))>.000001f) {
            snprintf(s->model.status,sizeof(s->model.status),"CHOICE %c READY / FINISH MORPH OR CHOOSE THE OTHER, SILENT END",'A'+state);return 1;
        }
        ts_prism_assign_endpoint(&c,side,state);
        if(!ts_prism_pair_ready(&c)) {
            if(which>=2) {
                snprintf(s->model.status,sizeof(s->model.status),"CAPTURE BOTH SELECTED LETTERS BEFORE MORPHING");return 1;
            }
            ts_prism_recall(&c,state);s->model.prism_edit_endpoint=state+1;
            snprintf(s->model.status,sizeof(s->model.status),"RECALLED %c / CAPTURE THE OTHER END TO MORPH",'A'+state);
        } else {
            s->model.prism_edit_endpoint=0;c.morph=position;c.morph_enabled=1;c.morph_target=side;
            ++c.morph_trigger;if(!c.morph_trigger)++c.morph_trigger;
            snprintf(s->model.status,sizeof(s->model.status),"MORPH TO %c / %.2F SECONDS",'A'+state,c.morph_seconds);
        }
    }
    s->model.prism_browse[side]=0;
    sister_prism_publish(device,audio,s,&c);return 1;
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
            return sister_prism_state_click(device,audio,s,which,right);
        } else if(y>=308 && y<330 && x>=16 && x<298) {
            if(x<82)c.seq_enabled=!c.seq_enabled;
            else if(x>=88 && x<154) { s->model.prism_seq_edit=!s->model.prism_seq_edit;s->rendered_model_valid=0;return 1; }
            else if(x>=160 && x<226)++c.seq_reset;
            else if(x>=232) { c.seq_count=0;memset(c.sequence,0,sizeof(c.sequence));++c.seq_reset; }
            else return 0;
            snprintf(s->model.status,sizeof(s->model.status),"SEQ %s: %d STEPS / OTHER LENSES SUSTAIN",c.seq_enabled?"ON":"OFF",c.seq_count);
        } else if(y>=308 && y<330 && x>=472 && x<616) {
            c.snap=(c.snap+(right?3:1))%4;
            snprintf(s->model.status,sizeof(s->model.status),"DRAG SNAP %s / ARROWS: CENT, SHIFT SEMITONE, CTRL+SHIFT OCTAVE",ts_prism_snap_name(c.snap));
        } else return 0;
    } else if(s->model.prism_panel==2) {
        if(y>=280 && y<302 && (x<306 || x>=412) &&
           (!s->model.prism_matrix_edit || ((mod&KMOD_SHIFT) && x<306))) {
            sister_prism_matrix_takeover(device,audio,s);c=s->model.parameters.prism;
        }
        if(y>=308 && y<330 && x>=16 && x<616) {
            int lock=(x-16)/101;if(lock>=6 || (x-16)%101>=95)return 0;
            c.locks^=1u<<lock;
        } else if(y>=280 && y<302) {
            if(x>=412 && x<616) {
                static uint32_t serial=0;
                uint32_t seed=(uint32_t)SDL_GetPerformanceCounter() ^ (++serial*747796405u);
                int vary=x>=516;TsPrismControls old=c;ts_prism_generate(&c,seed,vary);
                if(s->model.prism_matrix_edit) {c.enabled=old.enabled;c.matrix_run=old.matrix_run;c.matrix_restart=old.matrix_restart;}
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
                if(device)SDL_LockAudioDevice(device);
                ts_prism_matrix_export(&audio->sister.prism,&c);
                if(device)SDL_UnlockAudioDevice(device);
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
                    TsPrismControls old=c;
                    s->model.prism_preset_index=index;c=s->prism_presets.entries[index].controls;sister_prism_bank_sync(s);
                    if(mod&KMOD_SHIFT) {s->model.prism_edit_endpoint=0;memset(s->model.prism_browse,0,sizeof(s->model.prism_browse));}
                    else {
                        /* Browsing is an audition: endpoints belong to the
                           current performance, not to the next preset. */
                        ts_prism_copy_bank(&c,&old);
                        c.matrix=old.matrix;c.matrix_run=s->model.prism_matrix_edit?old.matrix_run:0;
                        c.matrix_restart=old.matrix_restart;c.active_pair_valid=0;
                        if(s->model.prism_matrix_edit)c.enabled=old.enabled;
                        c.morph_enabled=0;c.morph_trigger=0;c.morph=old.morph;
                        c.morph_seconds=old.morph_seconds;c.locks=old.locks;
                    }
                    snprintf(s->model.status,sizeof(s->model.status),"PRISM %s: %.31s",(mod&KMOD_SHIFT)?"FULL SETUP RECALLED":"SOUND / A-L RETAINED",s->model.prism_preset_name);
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
/* Solve audible pan too: narrowing/asymmetric glass changes the step size and
   inverting glass changes its sign. The offset includes automatic Stereo. */
static float sister_prism_pan_offset(const TsPrismControls *c,int lens,float target)
{
    TsPrismControls probe=*c;probe.morph_enabled=0;
    float low=-4,high=4;
    probe.pan_offset[lens]=low;float a=ts_prism_control_view(&probe).lens[lens].pan;
    probe.pan_offset[lens]=high;float b=ts_prism_control_view(&probe).lens[lens].pan;
    /* Avoid landing deep inside the hard-pan clamp, so the next fine step
       responds immediately. */
    float edge=target<=-1 ? -1.f : target>=1 ? 1.f : 0;
    target=SDL_clamp(target,-.999999f,.999999f);
    for(int j=0;j<24;++j) {
        float mid=(low+high)*.5f;probe.pan_offset[lens]=mid;
        float value=ts_prism_control_view(&probe).lens[lens].pan;
        if((value<target)==(a<b))low=mid;else high=mid;
    }
    return (low+high)*.5f+edge*(a<b?1.f:-1.f)*.000004f;
}
