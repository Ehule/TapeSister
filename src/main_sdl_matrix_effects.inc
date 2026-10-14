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
    if(c->type==TS_MATRIX_FX_COMPRESSOR)return k<5?ts_compressor_normalized(&c->compressor,k):k==5?c->mix:c->transition;
    return k==0?c->a:k==1?c->b:k==2?c->c:k==3?c->mix:k==4?(c->gain_db+12)/24:c->transition;
}
static void mx_effect_value_set(TsMatrixEffectControls *c,int position,float value)
{
    float v=SDL_clamp(value,0,1);int k=mx_effect_control_id(c,position);
    if(c->type==TS_MATRIX_FX_COMPRESSOR) {
        if(k<5)ts_compressor_set_normalized(&c->compressor,k,v);else if(k==5)c->mix=v;else c->transition=v;
    } else if(k==0)c->a=v;else if(k==1)c->b=v;else if(k==2)c->c=v;else if(k==3)c->mix=v;else if(k==4)c->gain_db=v*24-12;else c->transition=v;
}
static void mx_effect_label(char *s,size_t n,const TsMatrixEffectControls *c,int position)
{
    static const char *names[][3]={{"","",""},{"SIZE","DECAY",""},{"TIME","FEEDBACK",""},
        {"DRIVE","TONE",""},{"SIZE","DENSITY","PITCH"},{"ATTACK","SUSTAIN","RESPONSE"}};
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
    else if(c->type==TS_SISTER_FX_TRANSIENT && k<2)snprintf(s,n,"%s %+.1f DB",names[c->type][k],((k?c->b:c->a)-.5f)*(k?48:30));
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
        for(int type=1;type<TS_MATRIX_FX_TYPES;++type)mx_button(32+(type-1)*202,736,192,ts_matrix_effect_name(type),0);
        mx_text(32,779,"NEW EFFECTS START UNPATCHED. CONNECT A SOURCE TO THEIR COLUMN, THEN THEIR ROW TO A DESTINATION.",MX_DIM,1);return;
    }
    int slot=mx_effect_slot();if(slot<0){mx_text(32,703,"INSTANCE REMOVED - SELECT ANOTHER ROW",MX_DIM,2);return;}
    const TsMatrixEffectControls *c=&w->controls.effect[slot];
    snprintf(text,sizeof(text),"%s #%u",ts_matrix_effect_name(c->type),(unsigned)c->id);mx_text(32,703,text,MX_GREEN,2);
    mx_button(272,696,126,c->enabled?"ACTIVE":"BYPASS",c->enabled);
    if(c->type==TS_MATRIX_FX_COMPRESSOR) {
        mx_button(410,696,200,c->compressor.external?"SC: EXTERNAL":"SC: INTERNAL",c->compressor.external);
        snprintf(text,sizeof(text),"REDUCTION %.1f DB",w->effect_gr);mx_text(626,704,text,MX_AMBER,1);
    } else mx_text(418,704,"INDEPENDENT INSTANCE",MX_DIM,1);
    mx_button(898,696,138,"REMOVE",0);mx_button(1048,696,192,"BACK TO ROUTES",0);
    if(w->effect_progress<1){mx_rect(630,715,240,3,MX_DIM);mx_rect(630,715,(int)(240*w->effect_progress),3,MX_AMBER);}
    int count=mx_effect_controls(c),width=1204/count;
    for(int i=0;i<count;++i) {
        int x=32+i*width;mx_effect_label(text,sizeof(text),c,i);mx_text(x,735,text,MX_TEXT,1);
        mx_rect(x,758,width-18,6,MX_DIM);mx_rect(x+(int)((width-22)*mx_effect_value(c,i)),753,4,16,MX_GREEN);
    }
    mx_text(32,779,"DRAG / WHEEL: ADJUST | SHIFT+WHEEL: FINE | RIGHT-CLICK: DEFAULT | BYPASS FADE: 10 MS TO 60 MIN",MX_DIM,1);
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
    if(left && y>=696 && y<724 && x>=1048){w->effect_menu=0;w->effect_open=0;w->drag=0;return;}
    if(!mx_editable())return;
    if(w->effect_menu) {
        if(left && y>=736 && y<764 && x>=32 && x<1244)mx_effect_add(device,audio,ui,pages,bank,1+(x-32)/202);
        return;
    }
    int slot=mx_effect_slot();if(slot<0)return;
    TsMatrixEffectControls *c=&w->controls.effect[slot];int change=0,count=mx_effect_controls(c),width=1204/count;
    if(left && y>=696 && y<724) {
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
