/* Added processors use stable instance IDs in the view and fixed physical
   ports in the DSP graph. Only occupied ports are presented to the player. */
static int mx_effect_slot(void)
{
    MatrixWindow *w=&matrix_window;
    if(w->effect_open)for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)
        if(w->controls.effect[i].id==w->effect_open)return i;
    return -1;
}
static int mx_effect_controls(const TsMatrixEffectControls *c)
{return c->type==TS_MATRIX_FX_COMPRESSOR?7:c->type>=TS_SISTER_FX_GRAIN?6:5;}
static int mx_effect_control_id(const TsMatrixEffectControls *c,int position)
{return c->type!=TS_MATRIX_FX_COMPRESSOR && c->type<TS_SISTER_FX_GRAIN && position>=2?position+1:position;}
static float mx_effect_value(const TsMatrixEffectControls *c,int position)
{
    int k=mx_effect_control_id(c,position);
    if(c->type==TS_MATRIX_FX_META && k<3)return c->meta.scene_mode && c->meta.captured==3?c->meta.scene[0][k]+(c->meta.scene[1][k]-c->meta.scene[0][k])*c->meta.morph:c->meta.macro[k];
    if(c->type==TS_MATRIX_FX_COMPRESSOR)return k<5?ts_compressor_normalized(&c->compressor,k):k==5?c->mix:c->transition;
    if(c->type==TS_MATRIX_FX_FILTER && k<3){int focus=c->filter.value[TS_CF_ROUTE]>=.75f && c->filter.value[TS_CF_FOCUS]>=.5f?8:0;return c->filter.value[(int[]){TS_CF_L_CUT,TS_CF_L_RES,TS_CF_L_DRIVE}[k]+focus];}
    return k==0?c->a:k==1?c->b:k==2?c->c:k==3?c->mix:k==4?(c->gain_db+12)/24:c->transition;
}
static void mx_effect_value_set(TsMatrixEffectControls *c,int position,float value)
{
    float v=SDL_clamp(value,0,1);int k=mx_effect_control_id(c,position);
    if(c->type==TS_MATRIX_FX_META && k<3){ts_meta_macro_set(&c->meta,k,v);return;}
    if(c->type==TS_MATRIX_FX_FILTER && k<3){int focus=c->filter.value[TS_CF_ROUTE]>=.75f && c->filter.value[TS_CF_FOCUS]>=.5f?8:0;ts_character_filter_set(&c->filter,(int[]){TS_CF_L_CUT,TS_CF_L_RES,TS_CF_L_DRIVE}[k]+focus,v);return;}
    if(c->type==TS_MATRIX_FX_COMPRESSOR) {
        if(k<5)ts_compressor_set_normalized(&c->compressor,k,v);else if(k==5)c->mix=v;else c->transition=v;
    } else if(k==0)c->a=v;else if(k==1)c->b=v;else if(k==2)c->c=v;else if(k==3)c->mix=v;else if(k==4)c->gain_db=v*24-12;else c->transition=v;
}
static void mx_effect_label(char *s,size_t n,const TsMatrixEffectControls *c,int position)
{
    static const char *names[][3]={{"","",""},{"SIZE","DECAY",""},{"TIME","FEEDBACK",""},
        {"DRIVE","TONE",""},{"SIZE","DENSITY","PITCH"},{"ATTACK","SUSTAIN","RESPONSE"},
        {"","",""},{"CUTOFF","RESONANCE","DRIVE"},{"FREQUENCY","Q","SPREAD"},{"ROOT","DECAY","DAMPING"},{"MACRO 1","MACRO 2","MACRO 3"}};
    int k=mx_effect_control_id(c,position),comp=c->type==TS_MATRIX_FX_COMPRESSOR;
    if(k==(comp?6:5)) {
        float ms=ts_sister_fx_transition_ms(c->transition);
        if(ms<1000)snprintf(s,n,"BYPASS FADE %.0f MS",ms);
        else if(ms<60000)snprintf(s,n,"BYPASS FADE %.1f S",ms/1000);
        else snprintf(s,n,"BYPASS FADE %.1f MIN",ms/60000);
    } else if(k==(comp?5:3))snprintf(s,n,"MIX %.0f%%",c->mix*100);
    else if(comp) {
        const char *cn[]={"THRESHOLD","RATIO","ATTACK","RELEASE","OUTPUT"};
        const float v[]={c->compressor.threshold_db,c->compressor.ratio,c->compressor.attack_ms,c->compressor.release_ms,c->compressor.output_db};
        snprintf(s,n,k==1?"%s %.1f:1":k==2 || k==3?"%s %.1f MS":"%s %+.1f DB",cn[k],v[k]);
    } else if(k==4)snprintf(s,n,"OUTPUT %+.1f DB",c->gain_db);
    else if(c->type==TS_SISTER_FX_TRANSIENT && k<2)snprintf(s,n,"%s %+.1f DB",names[c->type][k],((k?c->b:c->a)-.5f)*(k?48:30)*(1+c->transient_power));
    else if(c->type==TS_SISTER_FX_TRANSIENT && k==2)snprintf(s,n,"RESPONSE %.2fX",.25f*powf(16,c->c));
    else snprintf(s,n,"%s %.0f%%",names[c->type][k],mx_effect_value(c,position)*100);
}
static void mx_effect_paint(void)
{
    MatrixWindow *w=&matrix_window;char text[100];
    if(w->effect_menu) {
        int used=0;for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)used+=w->controls.effect[i].id!=0;
        snprintf(text,sizeof(text),"ADD AN INDEPENDENT EFFECT (%d / %d)",used,TS_MATRIX_EFFECT_CAPACITY);mx_text(32,703,text,MX_GREEN,2);
        mx_button(1080,696,160,"CLOSE",0);
        for(int type=1;type<TS_MATRIX_FX_TYPES;++type)mx_button(32+(type-1)%7*172,736+(type-1)/7*34,162,ts_matrix_effect_name(type),0);
        mx_text(32,813,"NEW EFFECTS START UNPATCHED. CONNECT THEIR INPUT COLUMN AND OUTPUT ROW.",MX_DIM,1);return;
    }
    int slot=mx_effect_slot();if(slot<0){mx_text(32,703,"INSTANCE REMOVED - SELECT ANOTHER ROW",MX_DIM,2);return;}
    const TsMatrixEffectControls *c=&w->controls.effect[slot];
    snprintf(text,sizeof(text),"%s #%u",ts_matrix_effect_name(c->type),(unsigned)c->id);mx_text(32,703,text,MX_GREEN,2);
    mx_button(272,696,126,c->enabled?"ACTIVE":"BYPASS",c->enabled);
    if(c->type==TS_MATRIX_FX_COMPRESSOR) {
        mx_button(410,696,200,c->compressor.external?"SC: EXTERNAL":"SC: INTERNAL",c->compressor.external);
        snprintf(text,sizeof(text),"REDUCTION %.1f DB",w->effect_gr);mx_text(626,704,text,MX_AMBER,1);
    } else if(c->type==TS_SISTER_FX_TRANSIENT) {
        snprintf(text,sizeof(text),"PWR %uX",1u+c->transient_power);mx_button(410,696,94,text,c->transient_power!=0);
        TsSisterFxSlotControls control={.type=TS_SISTER_FX_TRANSIENT,.parameter_a=c->a,.parameter_b=c->b,.parameter_c=c->c,
            .transient_power=c->transient_power,.transient_states=c->transient_states};
        for(int i=0;i<TS_GRAIN_STATES;++i) {
            int selected=c->transient_states.selected==i+1,stored=(c->transient_states.captured&(1u<<i))!=0;
            snprintf(text,sizeof(text),"%d%s",i+1,selected && ts_grain_state_changed(&control)?"*":"");
            mx_button(518+i*34,696,28,text,stored);
            if(selected)mx_rect(518+i*34,722,28,2,c->transient_states.editing?MX_AMBER:MX_GREEN);
        }
        snprintf(text,sizeof(text),"GLIDE %.1F S",c->transient_states.seconds);mx_button(694,696,190,text,0);
    } else if(c->type==TS_MATRIX_FX_META)mx_button(410,696,200,"EDIT META FX",0);
    else if(c->type==TS_MATRIX_FX_FILTER)mx_button(410,696,200,"EDIT FILTER",0);
    else mx_text(418,704,"INDEPENDENT INSTANCE",MX_DIM,1);
    mx_button(898,696,138,"REMOVE",0);mx_button(1048,696,192,"BACK TO ROUTES",0);
    if(w->effect_progress<1 && c->type!=TS_SISTER_FX_TRANSIENT){mx_rect(630,715,240,3,MX_DIM);mx_rect(630,715,(int)(240*w->effect_progress),3,MX_AMBER);}
    int count=mx_effect_controls(c),width=1204/count;
    for(int i=0;i<count;++i) {
        int x=32+i*width;mx_effect_label(text,sizeof(text),c,i);mx_text(x,735,text,MX_TEXT,1);
        mx_rect(x,758,width-18,6,MX_DIM);mx_rect(x+(int)((width-22)*mx_effect_value(c,i)),753,4,16,MX_GREEN);
    }
    mx_text(32,779,c->type==TS_SISTER_FX_TRANSIENT?
        "STATES: CLICK CAPTURE/PLAY | SHIFT SAVE | RIGHT EDIT | MIDDLE CLEAR | GLIDE: DRAG/WHEEL, RIGHT INSTANT":
        "DRAG / WHEEL: ADJUST | SHIFT+WHEEL: FINE | RIGHT-CLICK: DEFAULT | BYPASS FADE: 10 MS TO 60 MIN",MX_DIM,1);
}
static void mx_append_port(TsMatrixLayout *l,int d)
{
    int at=0;while(at<TS_MATRIX_DESTINATIONS && l->column[at]!=d)++at;
    if(at<TS_MATRIX_DESTINATIONS){memmove(l->column+at,l->column+at+1,TS_MATRIX_DESTINATIONS-at-1);l->column[TS_MATRIX_DESTINATIONS-1]=(unsigned char)d;}
}
static void mx_effect_add(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank,int type)
{
    MatrixWindow *w=&matrix_window;if(!mx_editable())return;
    if(!w->controls.enabled){snprintf(w->status,sizeof(w->status),"SELECT USE MATRIX FIRST, THEN ADD AN EFFECT.");return;}
    TsMatrixControls next=w->controls;int slot=ts_matrix_effect_add(&next,type);
    if(slot<0){snprintf(w->status,sizeof(w->status),"INSTANCE LIMIT REACHED (%d). REMOVE AN EFFECT TO ADD ANOTHER.",TS_MATRIX_EFFECT_CAPACITY);return;}
    TsMatrixEffectPlan plan;if(!mx_stage_effects(device,audio,&next,&plan))return;
    if(device)SDL_LockAudioDevice(device);
    ts_matrix_effects_commit(&audio->sister.matrix_effects,&next,(unsigned)audio->output_rate,&plan);
    ts_matrix_set(&audio->sister.matrix,&next,(unsigned)audio->output_rate);
    if(device)SDL_UnlockAudioDevice(device);
    w->controls=ui->config.matrix=next;w->effect_open=next.effect[slot].id;w->effect_menu=w->compressor_open=w->drag=0;
    mx_append_port(w->layout,TS_MATRIX_EFFECT_BASE+slot);
    if(type==TS_MATRIX_FX_COMPRESSOR)mx_append_port(w->layout,TS_MATRIX_EFFECT_SC_BASE+slot);
    w->filter=4;mx_rows(pages,bank);
    for(int i=0;i<w->count;++i)if(w->rows[i].kind==4 && w->rows[i].tile==w->effect_open){mx_select(i,0);w->scroll=SDL_max(0,i-MATRIX_VISIBLE+1);}
    w->destination=TS_MATRIX_EFFECT_BASE+slot;mx_reveal_column(type==TS_MATRIX_FX_COMPRESSOR?TS_MATRIX_EFFECT_SC_BASE+slot:w->destination);
    snprintf(w->status,sizeof(w->status),"ADDED %s #%u. PATCH ITS INPUT COLUMN AND OUTPUT ROW. CTRL+Z TO UNDO.",ts_matrix_effect_name(type),(unsigned)w->effect_open);
}
static int mx_clear_effect_route(TsSourceRoute *r,int slot)
{
    int n=TS_MATRIX_EFFECT_BASE+slot,sc=TS_MATRIX_EFFECT_SC_BASE+slot;
    int changed=r->matrix_gain[n] || r->matrix_gain[sc];r->matrix_gain[n]=r->matrix_gain[sc]=0;return changed;
}
static void mx_effect_remove(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank,int slot)
{
    MatrixWindow *w=&matrix_window;if(!mx_editable())return;
    TsMatrixControls next=w->controls;uint32_t id=next.effect[slot].id;
    ts_matrix_effect_remove(&next,slot);
    TsMatrixEffectPlan plan;if(!mx_stage_effects(device,audio,&next,&plan))return;
    if(device)SDL_LockAudioDevice(device);
    for(int i=0;i<TS_TRACKER_LANES;++i)mx_clear_effect_route(&pages->tracker.lanes[i].output_route,slot);
    for(size_t p=0;p<ts_sample_pages_count(pages);++p) {
        TsInstrument *page=ts_sample_pages_page_mut(pages,bank,p);if(!page)continue;
        for(int i=0;i<TS_BANK_SLOT_COUNT;++i)if(page->bank[i].occupied && mx_clear_effect_route(&page->bank[i].output_route,slot))
            source_route_update_voices(audio,page->bank[i].tile_id,page->bank[i].output_route);
    }
    if(w->mosaic)for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(w->mosaic->events[i].id && mx_clear_effect_route(&w->mosaic->events[i].output_route,slot))++w->mosaic->revision;
    ts_matrix_effects_commit(&audio->sister.matrix_effects,&next,(unsigned)audio->output_rate,&plan);
    ts_matrix_set(&audio->sister.matrix,&next,(unsigned)audio->output_rate);
    if(device)SDL_UnlockAudioDevice(device);
    w->controls=ui->config.matrix=next;w->effect_open=0;w->drag=0;
    mx_refresh_tracker(pages,bank);mx_rows(pages,bank);
    snprintf(w->status,sizeof(w->status),"REMOVED INSTANCE #%u AND ALL ITS CONNECTIONS, INCLUDING HIDDEN SOURCES. CTRL+Z TO UNDO.",(unsigned)id);
}
static void mx_effect_event(const SDL_Event *e,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank,int x,int y,int wheel)
{
    MatrixWindow *w=&matrix_window;int click=e->type==SDL_MOUSEBUTTONDOWN,left=click && e->button.button==SDL_BUTTON_LEFT;
    if(e->type==SDL_MOUSEBUTTONUP){w->drag=0;return;}
    if(left && y>=696 && y<724 && x>=1048){w->effect_menu=0;w->effect_open=0;w->drag=0;return;}
    if(!mx_editable())return;
    if(w->effect_menu) {
        if(left && y>=736 && y<798 && (y-736)%34<28 && x>=32 && x<1244){int type=1+(x-32)/172+7*((y-736)/34);if(type<TS_MATRIX_FX_TYPES)mx_effect_add(device,audio,ui,pages,bank,type);}
        return;
    }
    int slot=mx_effect_slot();if(slot<0)return;
    TsMatrixEffectControls *c=&w->controls.effect[slot];int change=0,count=mx_effect_controls(c),width=1204/count;
    if(c->type==TS_SISTER_FX_TRANSIENT && (w->drag==40 || (y>=696 && y<724 && x>=410 && x<884))) {
        TsSisterFxSlotControls control={.type=TS_SISTER_FX_TRANSIENT,.parameter_a=c->a,.parameter_b=c->b,.parameter_c=c->c,
            .transient_power=c->transient_power,.transient_states=c->transient_states};
        TsGrainStates *bank=&control.transient_states;
        if(w->drag==40 || x>=694) {
            if(wheel){bank->seconds=SDL_clamp(bank->seconds+wheel*((SDL_GetModState()&KMOD_SHIFT)?.1f:1.f),0,600);change=1;}
            else if(click && e->button.button==SDL_BUTTON_RIGHT){bank->seconds=0;change=1;}
            else if(left || (w->drag==40 && e->type==SDL_MOUSEMOTION)) {
                bank->seconds=(powf(6001.f,SDL_clamp((x-694)/189.f,0,1))-1)*.1f;w->drag=40;change=1;
            }
        } else if(x<504) {
            if(wheel){control.transient_power=(uint8_t)SDL_clamp(control.transient_power+wheel,0,3);change=1;}
            else if(click && e->button.button==SDL_BUTTON_RIGHT){control.transient_power=0;change=1;}
            else if(left){control.transient_power=(control.transient_power+1)%4;change=1;}
        } else if(x>=518 && x<688 && (x-518)%34<28 && click) {
            int index=(x-518)/34;
            if(e->button.button==SDL_BUTTON_MIDDLE) {
                bank->captured&=(uint8_t)~(1u<<index);
                if(bank->selected==index+1)bank->selected=bank->editing=0;
            } else if(e->button.button==SDL_BUTTON_RIGHT)ts_grain_state_recall(&control,index,1);
            else if(left) {
                if((SDL_GetModState()&KMOD_SHIFT) || !(bank->captured&(1u<<index)) ||
                   (bank->selected==index+1 && (bank->editing || ts_grain_state_changed(&control))))ts_grain_state_capture(&control,index);
                else ts_grain_state_recall(&control,index,0);
            } else return;
            change=1;
        }
        if(change) {
            c->a=control.parameter_a;c->b=control.parameter_b;c->c=control.parameter_c;
            c->transient_power=control.transient_power;c->transient_states=control.transient_states;
            mx_commit_controls(device,audio,ui);
        }
        return;
    }
    if(left && y>=696 && y<724) {
        if(c->type==TS_MATRIX_FX_META && x>=410 && x<610){meta_fx_open(c->id);return;}
        if(c->type==TS_MATRIX_FX_FILTER && x>=410 && x<610){character_filter_matrix_open(c->id);return;}
        if(x>=272 && x<398){c->enabled=!c->enabled;change=1;}
        if(c->type==TS_MATRIX_FX_COMPRESSOR && x>=410 && x<610){c->compressor.external=!c->compressor.external;if(c->compressor.external)mx_reveal_column(TS_MATRIX_EFFECT_SC_BASE+slot);change=1;}
        if(x>=898 && x<1036){mx_effect_remove(device,audio,ui,pages,bank,slot);return;}
    }
    int control=x>=32 && x<1236?(x-32)/width:-1;
    if(control>=0 && control<count && x<32+control*width+width-18 && y>=730 && y<777) {
        if(wheel){mx_effect_value_set(c,control,mx_effect_value(c,control)+wheel*((SDL_GetModState()&KMOD_SHIFT)?.002f:.02f));change=1;}
        else if(left)w->drag=20+control;
        else if(click && e->button.button==SDL_BUTTON_RIGHT) {
            TsMatrixEffectControls defaults;ts_matrix_effect_default(&defaults,c->type,c->id);
            mx_effect_value_set(c,control,mx_effect_value(&defaults,control));change=1;
        }
    }
    if(w->drag>=20 && (left || e->type==SDL_MOUSEMOTION)) {
        control=w->drag-20;mx_effect_value_set(c,control,(x-32-control*width)/(float)(width-22));change=1;
    }
    if(change)mx_commit_controls(device,audio,ui);
}
