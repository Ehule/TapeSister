/* One paged inspector for added effects and fixed processor controls. */
enum { FX_INSPECT_ROWS=7, FX_INSPECT_MAX=1024 };
enum { FX_CONTROL_EFFECT, FX_CONTROL_MACRO, FX_CONTROL_SUNVOX, FX_CONTROL_FILTER,
    FX_CONTROL_POWER, FX_CONTROL_EXTERNAL, FX_CONTROL_EQ, FX_CONTROL_EQ_SOLO,
    FX_CONTROL_LIMITER, FX_CONTROL_COMPRESSOR, FX_CONTROL_PARAMETER,
    FX_CONTROL_INTEGER, FX_CONTROL_BYTE, FX_CONTROL_HEADROOM, FX_CONTROL_BOARD_MODE };
typedef struct {
    int kind,index,field,steps;size_t offset;
    float value,reset;char label[192];
} FxInspectorControl;
static struct { FxInspectorControl control[FX_INSPECT_MAX];int count,node,slot; } fx_inspector;
static FxInspectorControl *fx_control(int kind,int index,int field,float value,float reset,int steps,const char *label)
{
    if(fx_inspector.count==FX_INSPECT_MAX)return NULL;
    FxInspectorControl *c=&fx_inspector.control[fx_inspector.count++];
    *c=(FxInspectorControl){.kind=kind,.index=index,.field=field,.value=SDL_clamp(value,0,1),.reset=reset,.steps=steps};
    snprintf(c->label,sizeof(c->label),"%s",label);return c;
}
static void fx_integer(const TsSisterParameters *p,const TsSisterParameters *defaults,size_t offset,int maximum,const char *name)
{
    int value=0,reset=0;char label[160];memcpy(&value,(const char *)p+offset,sizeof(value));memcpy(&reset,(const char *)defaults+offset,sizeof(reset));
    if(offset==offsetof(TsSisterParameters,prism.mode))snprintf(label,sizeof(label),"%s %s",name,ts_prism_mode_name(value));
    else if(offset==offsetof(TsSisterParameters,prism.input_shape) || offset==offsetof(TsSisterParameters,prism.output_shape))snprintf(label,sizeof(label),"%s %s",name,ts_prism_shape_name(value));
    else if(offset==offsetof(TsSisterParameters,fx.fallout.noise_type))snprintf(label,sizeof(label),"%s %s",name,ts_sister_fallout_noise_type_name(value));
    else if(maximum==1)snprintf(label,sizeof(label),"%s %s",name,value?"ON":"OFF");
    else snprintf(label,sizeof(label),"%s %d",name,value);
    FxInspectorControl *c=fx_control(FX_CONTROL_INTEGER,0,0,value/(float)maximum,reset/(float)maximum,maximum,label);
    if(c)c->offset=offset;
}
#define FX_INT(member,maximum,label) fx_integer(p,&defaults,offsetof(TsSisterParameters,member),maximum,label)
static void fx_parameter(const TsSisterParameters *p,const TsSisterParameters *defaults,int parameter,const char *name)
{
    float value=sister_parameter_normalized(p,parameter);char label[192];
    snprintf(label,sizeof(label),"%s %.1f%%",name?name:sister_parameter_name(parameter),value*100);
    if(parameter==TS_DARK_PARAM_MODE)snprintf(label,sizeof(label),"MEMORY %s",p->memory_mode==TS_SISTER_MEMORY_DARK?"DARK MATTER":"TAPE");
    else if(parameter==TS_DARK_PARAM_ALGORITHM)snprintf(label,sizeof(label),"ALGORITHM %s",ts_dark_matter_algorithm_name(p->dark.algorithm));
    else if(parameter==TS_DARK_PARAM_SIZE)snprintf(label,sizeof(label),"SIZE %s",ts_dark_matter_size_name(p->dark.size));
    else if(parameter==TS_SISTER_UI_PARAM_FILTER_TYPE)snprintf(label,sizeof(label),"FILTER %s",ts_sister_filter_type_name(p->filter_type));
    else if(parameter==TS_SISTER_UI_PARAM_PRISM_LENSES)snprintf(label,sizeof(label),"LENSES %d",p->prism.lenses);
    else if(parameter==TS_SISTER_UI_PARAM_PRISM_OCTAVE)snprintf(label,sizeof(label),"OCTAVE %+d",p->prism.group_octave);
    else if(parameter==TS_SISTER_UI_PARAM_BUFFER_SECONDS)snprintf(label,sizeof(label),"BUFFER %.0f S",p->buffer_seconds);
    fx_control(FX_CONTROL_PARAMETER,0,parameter,value,sister_parameter_normalized(defaults,parameter),0,label);
}
static float fx_sunvox_value(const TsSunVoxControls *c,int at,const TsSunVoxParameter *p)
{return c->value[at]>=0?c->value[at]/32768.f:p->maximum==p->minimum?0:(p->initial-p->minimum)/(float)(p->maximum-p->minimum);}
static void fx_sunvox_label(char *label,size_t size,const TsSunVoxParameter *p,float value)
{
    int raw=p->minimum+(int)lroundf(value*(p->maximum-p->minimum));
    if(p->discrete && strchr(p->unit,';')) {
        const char *option=p->unit;int n=raw-p->minimum;
        while(n-->0 && strchr(option,';'))option=strchr(option,';')+1;
        const char *end=strchr(option,';');
        snprintf(label,size,"%s: %.*s",p->name,(int)(end?end-option:strlen(option)),option);
    } else snprintf(label,size,"%s: %d %s",p->name,raw+p->offset,p->unit);
}
static int fx_inspector_build(void)
{
    MatrixWindow *w=&matrix_window;int slot=mx_effect_slot(),node=-1;char label[192];
    if(slot<0 && w->selection_count==1 && w->selection[0].kind==2)node=w->selection[0].index;
    uint64_t key=slot>=0?(UINT64_C(1)<<32)|w->controls.effect[slot].id:(uint64_t)(node+1);
    if(key!=fx_canvas.inspector_key){fx_canvas.inspector_key=key;fx_canvas.inspector_page=0;}
    fx_inspector.count=0;fx_inspector.slot=slot;fx_inspector.node=node;
    if(slot>=0) {
        const TsMatrixEffectControls *c=&w->controls.effect[slot];TsMatrixEffectControls defaults;
        ts_matrix_effect_default(&defaults,c->type,c->id);
        if(c->type==TS_MATRIX_FX_SUNVOX) {
            for(int i=0;i<ts_sunvox_parameter_count(&c->sunvox);++i) {
                TsSunVoxParameter p;if(!ts_sunvox_parameter(&c->sunvox,i,&p))continue;
                float v=fx_sunvox_value(&c->sunvox,i,&p);fx_sunvox_label(label,sizeof(label),&p,v);
                fx_control(FX_CONTROL_SUNVOX,slot,i,v,0,p.discrete?p.maximum-p.minimum:0,label);
            }
        } else if(c->type==TS_MATRIX_FX_META) {
            for(int i=0;i<4;++i) {
                float v=c->meta.scene_mode && c->meta.captured==3?c->meta.scene[0][i]+(c->meta.scene[1][i]-c->meta.scene[0][i])*c->meta.morph:c->meta.macro[i];
                snprintf(label,sizeof(label),"%s %.0f%%",c->meta.label[i],v*100);
                fx_control(FX_CONTROL_MACRO,slot,i,v,defaults.meta.macro[i],0,label);
            }
        } else if(c->type==TS_MATRIX_FX_FILTER) {
            for(int i=0;i<TS_CF_COUNT;++i) {
                ts_character_filter_label(&c->filter,i,label,sizeof(label));
                int steps=i==TS_CF_ROUTE?2:i==TS_CF_L_MODEL || i==TS_CF_R_MODEL || i==TS_CF_L_MODE || i==TS_CF_R_MODE || i==TS_CF_FOCUS?1:0;
                fx_control(FX_CONTROL_FILTER,slot,i,c->filter.value[i],defaults.filter.value[i],steps,label);
            }
        }
        int first=c->type==TS_MATRIX_FX_META || c->type==TS_MATRIX_FX_FILTER?3:0;
        for(int i=first;i<mx_effect_controls(c);++i) {
            mx_effect_label(label,sizeof(label),c,i);
            fx_control(FX_CONTROL_EFFECT,slot,i,mx_effect_value(c,i),mx_effect_value(&defaults,i),0,label);
        }
        if(c->type==TS_MATRIX_FX_COMPRESSOR)fx_control(FX_CONTROL_EXTERNAL,slot,0,c->compressor.external,0,1,c->compressor.external?"SIDECHAIN EXTERNAL":"SIDECHAIN INTERNAL");
        if(c->type==TS_SISTER_FX_TRANSIENT) {
            snprintf(label,sizeof(label),"POWER %uX",1+c->transient_power);
            fx_control(FX_CONTROL_POWER,slot,0,c->transient_power/3.f,0,3,label);
        }
    } else {
        const FxProcessorState *f=&w->fixed;const TsSisterParameters *p=&f->parameters;
        static TsSisterParameters defaults;static unsigned defaults_rate;
        if(defaults_rate!=w->sample_rate){ts_sister_parameters_default(&defaults,w->sample_rate);defaults_rate=w->sample_rate;}
        if(node==TS_MATRIX_EQ) {
            TsMasterEqControls flat;ts_master_eq_default(&flat);

            for(int b=0;b<TS_MASTER_EQ_BANDS;++b)for(int k=0;k<5;++k) {
                const TsEqBand *q=&w->live_eq.band[b];float v=k==0?q->enabled:k==1?q->type/(float)(TS_EQ_TYPE_COUNT-1):ts_master_eq_normalized(q,k-2,w->sample_rate);
                float reset=k==0?flat.band[b].enabled:k==1?flat.band[b].type/(float)(TS_EQ_TYPE_COUNT-1):ts_master_eq_normalized(&flat.band[b],k-2,w->sample_rate);
                if(k==0)snprintf(label,sizeof(label),"BAND %d %s",b+1,q->enabled?"ON":"OFF");
                else if(k==1)snprintf(label,sizeof(label),"BAND %d %s",b+1,ts_master_eq_type_name(q->type));
                else if(k==2)snprintf(label,sizeof(label),"BAND %d FREQ %.0f HZ",b+1,q->frequency);
                else if(k==3)snprintf(label,sizeof(label),"BAND %d GAIN %+.1f DB",b+1,q->gain_db);
                else snprintf(label,sizeof(label),"BAND %d Q %.2f",b+1,q->q);
                fx_control(FX_CONTROL_EQ,b,k,v,reset,k==0?1:k==1?TS_EQ_TYPE_COUNT-1:0,label);
            }
            snprintf(label,sizeof(label),"SOLO BAND %d (0 = ALL)",w->live_eq.solo_band);
            fx_control(FX_CONTROL_EQ_SOLO,0,0,w->live_eq.solo_band/(float)TS_MASTER_EQ_BANDS,0,TS_MASTER_EQ_BANDS,label);
        } else if(node==TS_MATRIX_LIMITER) {
            snprintf(label,sizeof(label),"CEILING %+.1f DB",f->ceiling);fx_control(FX_CONTROL_LIMITER,0,0,(f->ceiling+12)/12,11.f/12,0,label);
            snprintf(label,sizeof(label),"LOOKAHEAD %.2f MS",f->lookahead);fx_control(FX_CONTROL_LIMITER,0,1,logf(f->lookahead/.1f)/logf(100),.5f,0,label);
            snprintf(label,sizeof(label),"RELEASE %.1f MS",f->release);fx_control(FX_CONTROL_LIMITER,0,2,logf(f->release/10)/logf(200),logf(12)/logf(200),0,label);
        } else if(node==TS_MATRIX_COMPRESSOR) {
            TsMatrixEffectControls c={.type=TS_MATRIX_FX_COMPRESSOR,.compressor=w->compressor};TsCompressorControls d;ts_compressor_default(&d);
            for(int k=0;k<5;++k){mx_effect_label(label,sizeof(label),&c,k);fx_control(FX_CONTROL_COMPRESSOR,0,k,ts_compressor_normalized(&w->compressor,k),ts_compressor_normalized(&d,k),0,label);}
            fx_control(FX_CONTROL_EXTERNAL,-1,0,w->compressor.external,0,1,w->compressor.external?"SIDECHAIN EXTERNAL":"SIDECHAIN INTERNAL");
        } else if(node==TS_MATRIX_PRISM) {
            FX_INT(prism.mode,TS_PRISM_MODE_COUNT-1,"MODE");
            for(int i=TS_SISTER_UI_PARAM_PRISM_LENSES;i<=TS_SISTER_UI_PARAM_PRISM_SEQ_RATE;++i)fx_parameter(p,&defaults,i,NULL);
            FX_INT(prism.input_shape,TS_PRISM_SHAPE_COUNT-1,"INPUT SHAPE");FX_INT(prism.output_shape,TS_PRISM_SHAPE_COUNT-1,"OUTPUT SHAPE");
            FX_INT(prism.seq_enabled,1,"LENS SEQUENCE");
        } else if(node==TS_MATRIX_SISTER) {
            for(int i=TS_DARK_PARAM_MODE;i<TS_DARK_PARAM_END;++i)fx_parameter(p,&defaults,i,NULL);
            fx_parameter(p,&defaults,TS_MEMORY_PARAM_BLEND,"TAPE / DARK BLEND");fx_parameter(p,&defaults,TS_MEMORY_PARAM_FADE,"MODE FADE");
            for(int i=TS_SISTER_UI_PARAM_H1_LEVEL;i<=TS_SISTER_UI_PARAM_INPUT_GAIN;++i)fx_parameter(p,&defaults,i,NULL);
            for(int i=TS_SISTER_UI_PARAM_MONITOR_DRY;i<=TS_SISTER_UI_PARAM_BLEED;++i)fx_parameter(p,&defaults,i,NULL);
            fx_parameter(p,&defaults,TS_SISTER_UI_PARAM_BUFFER_SECONDS,NULL);
        } else if(node==TS_MATRIX_FALLOUT) {
            FX_INT(fx.fallout.noise_type,TS_SISTER_FALLOUT_NOISE_COUNT-1,"NOISE TYPE");
            FX_INT(fx.fallout.drop_enabled,1,"DROP");FX_INT(fx.fallout.pan_enabled,1,"PAN");FX_INT(fx.fallout.skip_enabled,1,"SKIP");
            FX_INT(fx.fallout.bit_enabled,1,"BITS");FX_INT(fx.fallout.pitch_enabled,1,"PITCH");
            for(int i=TS_SISTER_UI_PARAM_FALLOUT_MIX;i<=TS_SISTER_UI_PARAM_FALLOUT_RISE_INTENSITY;++i)fx_parameter(p,&defaults,i,NULL);
            fx_parameter(p,&defaults,TS_SISTER_UI_PARAM_FALLOUT_MASTER_TRANSITION,NULL);
        } else if(node==TS_MATRIX_BOARD) {
            fx_control(FX_CONTROL_BOARD_MODE,0,0,w->controls.board_insert,0,1,w->controls.board_insert?"MODE INSERT":"MODE SEND");
            fx_parameter(p,&defaults,TS_SISTER_UI_PARAM_FX_TRANSITION,NULL);fx_parameter(p,&defaults,TS_SISTER_UI_PARAM_MASTER_FX_TRANSITION,NULL);
            fx_parameter(p,&defaults,TS_SISTER_UI_PARAM_MASTER_FX_FEEDBACK,NULL);
            for(int i=0;i<p->fx.slot_count;++i) {
                const TsSisterFxSlotControls *s=&p->fx.slot[i];if(!s->type)continue;
                snprintf(label,sizeof(label),"SLOT %d %s",i+1,ts_sister_fx_type_name(s->type));
                fx_integer(p,&defaults,offsetof(TsSisterParameters,fx.slot)+i*sizeof(*s)+offsetof(TsSisterFxSlotControls,enabled),1,label);
                for(int k=0;k<5;++k) {
                    if(s->type==TS_SISTER_FX_FILTER && k>=1 && k<=3)continue;
                    if(s->type<=TS_SISTER_FX_DISTORTION && k==3)continue;
                    snprintf(label,sizeof(label),"SLOT %d %s",i+1,k==0?"OUTPUT":k==4?"MIX":ts_meta_parameter_name(s->type,k-1));fx_parameter(p,&defaults,TS_SISTER_UI_SLOT_PARAMETER(i,k),label);
                }
                if(s->type==TS_SISTER_FX_FILTER)for(int k=0;k<TS_CF_COUNT;++k) {
                    ts_character_filter_label(&s->filter,k,label,sizeof(label));fx_parameter(p,&defaults,TS_CF_MIDI_BASE+i*32+k,label);
                }
            }
        } else if(node==TS_MATRIX_MAIN) {
            const char *names[]={"HEADROOM AUTO","HEADROOM FLOAT","HEADROOM CLIP"};
            fx_control(FX_CONTROL_HEADROOM,0,0,w->controls.main_headroom/2.f,0,2,names[SDL_clamp(w->controls.main_headroom,0,2)]);
        }
    }
    fx_canvas.inspector_page=SDL_clamp(fx_canvas.inspector_page,0,SDL_max(0,(fx_inspector.count-1)/FX_INSPECT_ROWS));
    return fx_inspector.count;
}
#undef FX_INT
static int fx_inspector_enabled(void)
{
    const MatrixWindow *w=&matrix_window;
    if(fx_inspector.slot>=0)return w->controls.effect[fx_inspector.slot].enabled;
    switch(fx_inspector.node) {
    case TS_MATRIX_EQ:return w->live_eq.enabled;
    case TS_MATRIX_LIMITER:return w->fixed.limiter_enabled;
    case TS_MATRIX_COMPRESSOR:return w->compressor.enabled;
    case TS_MATRIX_PRISM:return w->fixed.parameters.prism.enabled;
    case TS_MATRIX_BOARD:return w->fixed.parameters.fx.enabled;
    case TS_MATRIX_FALLOUT:return w->fixed.parameters.fx.fallout.enabled;
    default:return -1;
    }
}
static void fx_inspector_controls_paint(void)
{
    MatrixWindow *w=&matrix_window;char label[192];int slot=fx_inspector.slot;
    int x=fx_inspector_x(),width=fx_inspector_width();
    const TsMatrixEffectControls *c=slot>=0?&w->controls.effect[slot]:NULL;
    const char *title=c?(c->type==TS_MATRIX_FX_SUNVOX?ts_sunvox_name(&c->sunvox):c->type==TS_MATRIX_FX_META?c->meta.name:ts_matrix_effect_name(c->type)):ts_matrix_node_name(fx_inspector.node);
    uint32_t color=c?fx_color(c->type):FX_CABLE;
    fx_panel_text(x,125,width+5,47,title,FX_FOCUS,2,2);
    int enabled=fx_inspector_enabled();
    if(fx_inspector.node==TS_MATRIX_EQ && w->eq_morph_owned)fx_button(x,178,width,"STOP MORPH",1);
    else if(enabled>=0)fx_button(x,178,width,enabled?"ACTIVE":"BYPASS",enabled);
    else fx_panel_text(x,178,width,36,"PARAMETERS",FX_DIM,2,1);
    int start=fx_canvas.inspector_page*FX_INSPECT_ROWS;
    for(int row=0;row<FX_INSPECT_ROWS && start+row<fx_inspector.count;++row) {
        const FxInspectorControl *p=&fx_inspector.control[start+row];int y=230+row*53;
        fx_panel_text(x,y,width,38,p->label,FX_TEXT,2,2);
        mx_rect(x,y+39,width,7,FX_EDGE);mx_rect(x,y+39,(int)(width*p->value),7,color);mx_rect(x+(int)((width-4)*p->value),y+36,4,13,FX_CABLE);
    }
    if(fx_inspector.count>FX_INSPECT_ROWS) {
        fx_button(x,610,52,"<",0);fx_button(1184,610,52,">",0);
        snprintf(label,sizeof(label),"%d / %d",fx_canvas.inspector_page+1,(fx_inspector.count+FX_INSPECT_ROWS-1)/FX_INSPECT_ROWS);
        fx_panel_text(x+62,617,width-122,24,label,FX_DIM,1,1);
    }
    if(c && c->type==TS_MATRIX_FX_SUNVOX)fx_button(x,648,width,"BROWSE SUNVOX",0);
    else if(c && c->type==TS_MATRIX_FX_META)fx_button(x,648,width,"OPEN CHAIN",0);
    else fx_panel_text(x,648,width,32,"Wheel: edit / Shift: fine / RMB: reset",FX_DIM,1,2);
}
static void fx_inspector_set(const FxInspectorControl *control,float value,int reset,int wheel,
    SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    MatrixWindow *w=&matrix_window;if(!mx_editable())return;
    float v=SDL_clamp(reset?control->reset:value,0,1);
    if(control->steps)v=roundf(v*control->steps)/control->steps;
    int kind=control->kind,k=control->field,at=control->index;
    if(kind==FX_CONTROL_SUNVOX) {
        TsSunVoxParameter p;
        if(!ts_sunvox_parameter(&w->controls.effect[at].sunvox,k,&p) || p.maximum==p.minimum)return;
    }
    if(kind==FX_CONTROL_HEADROOM || kind==FX_CONTROL_BOARD_MODE) {
        if(kind==FX_CONTROL_HEADROOM)w->controls.main_headroom=(int)lroundf(v*2);else w->controls.board_insert=v>=.5f;
        mx_commit_controls(device,audio,ui);return;
    }
    if(kind<=FX_CONTROL_EXTERNAL && at>=0) {
        TsMatrixEffectControls *c=&w->controls.effect[at];
        if(kind==FX_CONTROL_EFFECT)mx_effect_value_set(c,k,v);
        else if(kind==FX_CONTROL_MACRO)ts_meta_macro_set(&c->meta,k,v);
        else if(kind==FX_CONTROL_SUNVOX)c->sunvox.value[k]=reset?-1:(int)lroundf(v*32768);
        else if(kind==FX_CONTROL_FILTER)ts_character_filter_set(&c->filter,k,v);
        else if(kind==FX_CONTROL_POWER)c->transient_power=(uint8_t)lroundf(v*3);
        else if(kind==FX_CONTROL_EXTERNAL)c->compressor.external=v>=.5f;
        mx_commit_controls(device,audio,ui);return;
    }
    if(device)SDL_LockAudioDevice(device);
    FxProcessorState next;mx_processor_read(audio,&next);
    if((kind==FX_CONTROL_EQ || kind==FX_CONTROL_EQ_SOLO) && w->eq_morph_owned) {
        if(device)SDL_UnlockAudioDevice(device);
        snprintf(w->status,sizeof(w->status),"EQ MORPH OWNS THIS CURVE / CLICK STOP MORPH TO HOLD IT AND EDIT.");return;
    }
    if(kind==FX_CONTROL_EQ) {
        TsEqBand *b=&next.eq.band[at];
        if(k==0)b->enabled=v>=.5f;else if(k==1)b->type=(int)lroundf(v*(TS_EQ_TYPE_COUNT-1));
        else ts_master_eq_set_normalized(b,k-2,v,w->sample_rate);
    } else if(kind==FX_CONTROL_EQ_SOLO)next.eq.solo_band=(int)lroundf(v*TS_MASTER_EQ_BANDS);
    else if(kind==FX_CONTROL_LIMITER) {
        if(k==0)next.ceiling=-12+12*v;else if(k==1)next.lookahead=.1f*powf(100,v);else next.release=10*powf(200,v);
    } else if(kind==FX_CONTROL_COMPRESSOR || kind==FX_CONTROL_EXTERNAL) {
        TsCompressorControls c=audio->sister.compressor.controls;
        if(kind==FX_CONTROL_EXTERNAL)c.external=v>=.5f;else ts_compressor_set_normalized(&c,k,v);
        ts_compressor_set(&audio->sister.compressor,&c);w->compressor=ui->config.compressor=audio->sister.compressor.controls;
    } else if(kind==FX_CONTROL_PARAMETER || kind==FX_CONTROL_INTEGER || kind==FX_CONTROL_BYTE) {
        int allowed=1;
        if(fx_inspector.node==TS_MATRIX_PRISM && audio->sister.prism.matrix.active) {
            snprintf(w->status,sizeof(w->status),"PRISM MATRIX ACTIVE: EDIT ITS BANK LETTER OR STOP THE MATRIX TO EDIT LIVE CONTROLS.");allowed=0;
        }
        if(kind==FX_CONTROL_PARAMETER) {
            static TsSisterUiModel model;
            model.parameters=next.parameters;model.parameter_locks=audio->sister.parameter_locks;model.parameter_locks_high=audio->sister.parameter_locks_high;
            if(ts_sister_ui_parameter_locked(&model,k)) {
                snprintf(w->status,sizeof(w->status),"%s IS LOCKED IN ITS PROCESSOR EDITOR.",sister_parameter_name(k));allowed=0;
            }
            if(next.parameters.prism.morph_enabled &&
               ((k>=TS_SISTER_UI_PARAM_PRISM_LENSES && k<=TS_SISTER_UI_PARAM_PRISM_COLOR) ||
                k==TS_SISTER_UI_PARAM_PRISM_RATE || k==TS_SISTER_UI_PARAM_PRISM_OCTAVE)) {
                snprintf(w->status,sizeof(w->status),"PRISM MORPH ACTIVE: EDIT ITS CAPTURED STATE IN THE PRISM EDITOR.");allowed=0;
            }
            if(allowed) {
                if(wheel && !reset)v=sister_parameter_wheel_normalized(&next.parameters,k,wheel,(SDL_GetModState()&KMOD_SHIFT)!=0);
                sister_set_parameter(&next.parameters,k,v);
            }
        } else if(allowed) {
            int n=(int)lroundf(v*control->steps);
            if(kind==FX_CONTROL_BYTE)*((uint8_t *)&next.parameters+control->offset)=(uint8_t)n;
            else memcpy((char *)&next.parameters+control->offset,&n,sizeof(n));
        }
    }
    mx_processor_apply(audio,ui,&next);
    if(device)SDL_UnlockAudioDevice(device);
    w->dirty=1;
}
static void fx_inspector_toggle(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    MatrixWindow *w=&matrix_window;if(!mx_editable())return;
    if(fx_inspector.slot>=0){w->controls.effect[fx_inspector.slot].enabled=!w->controls.effect[fx_inspector.slot].enabled;mx_commit_controls(device,audio,ui);return;}
    if(device)SDL_LockAudioDevice(device);
    FxProcessorState next;mx_processor_read(audio,&next);
    switch(fx_inspector.node) {
    case TS_MATRIX_EQ:
        if(w->eq_morph_owned){ts_master_eq_morph_stop(&audio->sister.master_eq);mx_processor_read(audio,&next);}
        else next.eq.enabled=!next.eq.enabled;
        break;
    case TS_MATRIX_LIMITER:next.limiter_enabled=!next.limiter_enabled;break;
    case TS_MATRIX_PRISM:next.parameters.prism.enabled=!next.parameters.prism.enabled;break;
    case TS_MATRIX_BOARD:next.parameters.fx.enabled=!next.parameters.fx.enabled;break;
    case TS_MATRIX_FALLOUT:next.parameters.fx.fallout.enabled=!next.parameters.fx.fallout.enabled;break;
    case TS_MATRIX_COMPRESSOR: {
        TsCompressorControls c=audio->sister.compressor.controls;c.enabled=!c.enabled;
        ts_compressor_set(&audio->sister.compressor,&c);w->compressor=ui->config.compressor=audio->sister.compressor.controls;break;
    }
    }
    mx_processor_apply(audio,ui,&next);if(device)SDL_UnlockAudioDevice(device);
}
static int fx_inspector_event(SDL_Event *e,int x,int y,int wheel,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    MatrixWindow *w=&matrix_window;int dragging=w->drag>=FX_DRAG_CONTROL && w->drag<FX_DRAG_CONTROL+FX_INSPECT_ROWS;
    int left_x=fx_inspector_x(),width=fx_inspector_width();
    if(!dragging && (x<fx_split()+8 || y<FX_TOP || y>=FX_BOTTOM))return 0;
    int count=fx_inspector_build();if(!count)return 1;
    int click=e->type==SDL_MOUSEBUTTONDOWN,left=click && e->button.button==SDL_BUTTON_LEFT;
    if(dragging) {
        if(e->type==SDL_MOUSEMOTION && fx_canvas.inspector_drag<count)
            fx_inspector_set(&fx_inspector.control[fx_canvas.inspector_drag],(x-left_x)/(float)width,0,0,device,audio,ui);
        return 1;
    }
    if(left && y>=178 && y<206 && x>=left_x && x<1236){fx_inspector_toggle(device,audio,ui);return 1;}
    if(y>=610 && y<638 && count>FX_INSPECT_ROWS) {
        if(wheel)fx_canvas.inspector_page-=wheel;
        else if(left && x>=left_x && x<left_x+52)--fx_canvas.inspector_page;
        else if(left && x>=1184 && x<1236)++fx_canvas.inspector_page;
        fx_canvas.inspector_page=SDL_clamp(fx_canvas.inspector_page,0,(count-1)/FX_INSPECT_ROWS);return 1;
    }
    if(left && y>=648 && y<676 && fx_inspector.slot>=0) {
        TsMatrixEffectControls *c=&w->controls.effect[fx_inspector.slot];
        if(c->type==TS_MATRIX_FX_SUNVOX)svfx_open(c->id);else if(c->type==TS_MATRIX_FX_META)fx_canvas.meta=c->id;
        return 1;
    }
    int row=(y-230)/53,at=fx_canvas.inspector_page*FX_INSPECT_ROWS+row;
    if(x>=left_x && x<1236 && y>=230 && row<FX_INSPECT_ROWS && at<count && (y-230)%53<49) {
        FxInspectorControl c=fx_inspector.control[at];
        if(wheel) {
            float step=c.steps?1.f/c.steps:(SDL_GetModState()&KMOD_SHIFT)?.002f:.02f;
            fx_inspector_set(&c,c.value+wheel*step,0,wheel,device,audio,ui);
        } else if(click && e->button.button==SDL_BUTTON_RIGHT)fx_inspector_set(&c,0,1,0,device,audio,ui);
        else if(left) {
            fx_canvas.inspector_drag=at;w->drag=FX_DRAG_CONTROL+row;
            fx_inspector_set(&c,(x-left_x)/(float)width,0,0,device,audio,ui);SDL_CaptureMouse(SDL_TRUE);
        }
    }
    return 1;
}
