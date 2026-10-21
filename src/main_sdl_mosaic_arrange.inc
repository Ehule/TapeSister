/* Arrangement gestures and scatter stage changes on the UI thread. Nothing in
   the scatter preview edits the live arrangement or advances its PRNG. */
static void mosaic_zoom_selection(TsUiState *ui,MosaicController *c)
{
    if(c->zoom_saved) {
        ui->mosaic_scroll=c->zoom_scroll;ui->mosaic_xscroll=c->zoom_xscroll;
        ui->mosaic_scale=c->zoom_scale;ui->mosaic_hscale=c->zoom_hscale;c->zoom_saved=0;return;
    }
    double first=1e30,last=0,left=1e30,right=0;int count=0;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(mosaic_chosen(ui,i)) {
        const TsMosaicEvent *e=&ui->mosaic->events[i];++count;
        first=fmin(first,e->start);last=fmax(last,e->start+e->duration);
        left=fmin(left,e->x);right=fmax(right,e->x+e->width);
    }
    if(!count)return;
    c->zoom_scroll=ui->mosaic_scroll;c->zoom_xscroll=ui->mosaic_xscroll;
    c->zoom_scale=ui->mosaic_scale;c->zoom_hscale=ui->mosaic_hscale;c->zoom_saved=1;
    ui->mosaic_scale=fmin(1200,280/fmax(.005,last-first));
    ui->mosaic_hscale=fmin(4,(TS_MOSAIC_CANVAS_RIGHT-175)/fmax(1,right-left));
    ui->mosaic_scroll=fmax(0,first-15/ui->mosaic_scale);
    ui->mosaic_xscroll=fmax(0,left-12/ui->mosaic_hscale);ui->mosaic_follow=0;
}
static void mosaic_tidy(TsUiState *ui,int all)
{
    TsMosaic *m=ui->mosaic;int previous=m->auto_space;
    if(!all) {int count=0;for(int i=0;i<TS_MOSAIC_EVENTS;++i)count+=mosaic_chosen(ui,i);if(!count)return;}
    ts_mosaic_checkpoint(m);m->auto_space=1;
    /* One pass per card in storage order gives a deterministic, horizontal
       re-layout. No card's start, duration or audio parameters are modified. */
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)
        if(m->events[i].id && (all || mosaic_chosen(ui,i)))ts_mosaic_space(m,&m->events[i]);
    if(!all)m->auto_space=previous;
}
/* Caller holds the audio lock. Validate the whole selection before editing. */
static void mosaic_align(TsUiState *ui,SDL_Keycode key)
{
    TsMosaic *m=ui->mosaic;TsMosaicEvent *primary=ts_mosaic_find(m,ui->mosaic_selected);
    int parent=key==SDLK_UP || key==SDLK_DOWN,count=0,changed=0;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)count+=mosaic_chosen(ui,i);
    if(parent && count<2) {ui->mosaic_align_open=1;return;}
    if(!primary || !count)return;
    int bottom=key==SDLK_DOWN || key==SDLK_RIGHT;
    double anchor=parent?primary->start+(bottom?primary->duration:0):m->time;
    double starts[TS_MOSAIC_EVENTS];
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(mosaic_chosen(ui,i)) {
        TsMosaicEvent *e=&m->events[i];starts[i]=parent && e==primary?e->start:anchor-(bottom?e->duration:0);
        if(starts[i]<-1e-9 || starts[i]>86400 || !isfinite(starts[i])) {
            snprintf(ui->status,sizeof(ui->status),"ALIGNMENT OUTSIDE TIMELINE / NO CARDS MOVED");return;
        }
        starts[i]=fmax(0,starts[i]);changed|=fabs(starts[i]-e->start)>1e-9;
    }
    if(!changed)return;
    ts_mosaic_checkpoint(m);
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(mosaic_chosen(ui,i))m->events[i].start=starts[i];
    /* Auto-spacing may move secondary cards horizontally, but never the primary
       or unselected cards. Time and duration alignment stay exact. */
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(mosaic_chosen(ui,i) && &m->events[i]!=primary)ts_mosaic_space(m,&m->events[i]);
    snprintf(ui->status,sizeof(ui->status),"ALIGNED %s TO %s / CTRL+Z UNDO",bottom?"ENDS":"STARTS",parent?"PRIMARY":"PLAYHEAD");
}
static void mosaic_nudge(TsUiState *ui,SDL_Keycode key,SDL_Keymod mod)
{
    ui->mosaic_ghost_count=mosaic_selected_events(ui,ui->mosaic_ghosts);
    if(!ui->mosaic_ghost_count)return;
    int horizontal=key==SDLK_LEFT || key==SDLK_RIGHT;
    double delta=(key==SDLK_UP || key==SDLK_LEFT?-1:1)*(mod&KMOD_SHIFT?10:1);
    if(horizontal) {
        double left=100000,right=0;
        for(int i=0;i<ui->mosaic_ghost_count;++i){left=fmin(left,ui->mosaic_ghosts[i].x);right=fmax(right,ui->mosaic_ghosts[i].x);}
        delta=fmax(-left,fmin(100000-right,delta));
        if(delta!=0) {
            ts_mosaic_checkpoint(ui->mosaic);
            mosaic_move_group(ui,delta,0,0);
        }
    } else {ts_mosaic_checkpoint(ui->mosaic);mosaic_move_group(ui,0,delta*.1,0);}
    ui->mosaic_ghost_count=0;
}
static double mosaic_scatter_random(uint32_t *seed)
{
    *seed=*seed*1664525u+1013904223u;return (double)(*seed>>8)/16777216.0;
}
static void mosaic_scatter_preview(TsUiState *ui,MosaicController *c)
{
    ui->mosaic_ghost_count=0;ui->mosaic_scatter_valid=0;
    if(!c->scatter_pool_count)return;
    int count=ui->mosaic_scatter_copies?ui->mosaic_scatter_count:c->scatter_pool_count,free_count=0;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)free_count+=!ui->mosaic->events[i].id;
    if(count<1 || count>TS_MOSAIC_EVENTS || (ui->mosaic_scatter_copies && count>free_count)) {
        snprintf(ui->mosaic_scatter_status,sizeof(ui->mosaic_scatter_status),"NOT ENOUGH SPACE: %d FREE CARDS",free_count);return;
    }
    uint32_t seed=c->scatter_seed;
    double clusters[3];for(int i=0;i<3;++i)clusters[i]=mosaic_scatter_random(&seed);
    double span=ui->mosaic_scatter_end-ui->mosaic_scatter_start;
    for(int i=0;i<count;++i) {
        TsMosaicEvent *e=&ui->mosaic_ghosts[i];*e=c->scatter_pool[i%c->scatter_pool_count];
        double p=mosaic_scatter_random(&seed);
        if(ui->mosaic_scatter_cluster)p=fmax(0,fmin(1,clusters[i%3]+(p-.5)*.18));
        e->start=ui->mosaic_scatter_start+p*span;
        e->x=ui->mosaic_xscroll+mosaic_scatter_random(&seed)*fmax(0,(TS_MOSAIC_CANVAS_RIGHT-151)/(ui->mosaic_hscale>0?ui->mosaic_hscale:1)-e->width);
        if(!ui->mosaic_scatter_overlap)for(int pass=0;pass<TS_MOSAIC_EVENTS*2;++pass) {
            double shift=0;
            for(int j=0;j<TS_MOSAIC_EVENTS+i;++j) {
                const TsMosaicEvent *b=j<TS_MOSAIC_EVENTS?&ui->mosaic->events[j]:&ui->mosaic_ghosts[j-TS_MOSAIC_EVENTS];
                if(j<TS_MOSAIC_EVENTS && (!b->id || (!ui->mosaic_scatter_copies && mosaic_chosen(ui,j))))continue;
                if(e->start>=b->start+b->duration || b->start>=e->start+e->duration)continue;
                if(e->x<b->x+b->width+TS_MOSAIC_GUTTER && b->x<e->x+e->width+TS_MOSAIC_GUTTER)
                    shift=fmax(shift,b->x+b->width+TS_MOSAIC_GUTTER-e->x);
            }
            if(shift<=0)break;e->x+=shift;
        }
    }
    ui->mosaic_ghost_count=count;ui->mosaic_scatter_valid=1;
    snprintf(ui->mosaic_scatter_status,sizeof(ui->mosaic_scatter_status),"%d CARDS / %d %s / PREVIEW",count,c->scatter_pool_count,c->scatter_from_sources?"SOURCES":"SELECTED");
}
static void mosaic_scatter_open(TsUiState *ui,MosaicController *c)
{
    mosaic_drag_end(ui,c);c->scatter_pool_count=0;c->scatter_from_sources=c->picked_count>0;
    if(c->scatter_from_sources) {
        c->scatter_pool_count=c->picked_count;
        memcpy(c->scatter_pool,c->picked_sources,c->picked_count*sizeof(c->scatter_pool[0]));
    } else c->scatter_pool_count=mosaic_selected_events(ui,c->scatter_pool);
    if(!c->scatter_pool_count) {
        snprintf(ui->status,sizeof(ui->status),"SELECT CARDS OR CTRL-CLICK SOURCES, THEN CTRL+R");return;
    }
    c->scatter_epoch=ui->mosaic->epoch;
    c->scatter_seed=(uint32_t)SDL_GetTicks()^0x9e3779b9u;
    ui->mosaic_follow=0;
    ui->mosaic_scatter_open=1;ui->mosaic_scatter_copies=c->scatter_from_sources;
    ui->mosaic_scatter_start=ui->mosaic_scroll;
    ui->mosaic_scatter_end=fmin(86400,ui->mosaic_scroll+310/ui->mosaic_scale);
    ui->mosaic_scatter_count=c->scatter_pool_count;
    ui->mosaic_scatter_overlap=!ui->mosaic->auto_space;ui->mosaic_scatter_field=0;
    mosaic_scatter_preview(ui,c);
}
static int mosaic_scatter_number(TsUiState *ui,MosaicController *c)
{
    if(!ui->mosaic_scatter_field)return 1;
    char *end=NULL;double n=strtod(ui->mosaic_scatter_text,&end);
    int field=ui->mosaic_scatter_field;
    if(end==ui->mosaic_scatter_text || *end || !isfinite(n) || n<0 || n>86400 ||
       (field==1 && n>=ui->mosaic_scatter_end) || (field==2 && n<=ui->mosaic_scatter_start) ||
       (field==3 && (n<1 || n>TS_MOSAIC_EVENTS || floor(n)!=n))) {
        snprintf(ui->mosaic_scatter_status,sizeof(ui->mosaic_scatter_status),"ENTER A VALID RANGE OR COUNT (1-%d)",TS_MOSAIC_EVENTS);return 0;
    }
    if(field==1)ui->mosaic_scatter_start=n;else if(field==2)ui->mosaic_scatter_end=n;else ui->mosaic_scatter_count=(int)n;
    ui->mosaic_scatter_field=0;SDL_StopTextInput();mosaic_scatter_preview(ui,c);return 1;
}
static void mosaic_scatter_close(TsUiState *ui)
{ui->mosaic_scatter_open=ui->mosaic_scatter_field=ui->mosaic_ghost_count=0;SDL_StopTextInput();}
static void mosaic_scatter_apply(SDL_AudioDeviceID device,TsUiState *ui,MosaicController *c)
{
    if(!mosaic_scatter_number(ui,c))return;
    mosaic_scatter_preview(ui,c);if(!ui->mosaic_scatter_valid)return;
    if(device)SDL_LockAudioDevice(device);
    ts_mosaic_checkpoint(ui->mosaic);
    /* Explicit overlap permission also disables auto-spacing for subsequent
       gestures. Undo restores both the layout and the previous preference. */
    if(ui->mosaic_scatter_overlap)ui->mosaic->auto_space=0;
    mosaic_select_only(ui,0);
    for(int i=0;i<ui->mosaic_ghost_count;++i) {
        TsMosaicEvent *preview=&ui->mosaic_ghosts[i],*e=NULL;
        if(ui->mosaic_scatter_copies) {
            for(int j=0;j<TS_MOSAIC_EVENTS;++j)if(!ui->mosaic->events[j].id) {
                e=&ui->mosaic->events[j];*e=*preview;e->id=ui->mosaic->next_id++;e->revision=1;break;
            }
        } else {e=ts_mosaic_find(ui->mosaic,preview->id);if(e){e->start=preview->start;e->x=preview->x;}}
        if(e)mosaic_select_add(ui,e->id);
    }
    if(device)SDL_UnlockAudioDevice(device);
    snprintf(ui->status,sizeof(ui->status),"SCATTER APPLIED / CTRL+Z UNDO / Z FIT SELECTION");
    mosaic_scatter_close(ui);mosaic_picks_clear(ui,c);
}
static int mosaic_arrange_event(const SDL_Event *event,SDL_Window *window,SDL_AudioDeviceID device,
    TsUiState *ui,TsInstrument *instrument,MosaicController *c,int x,int y,SDL_Keymod mod)
{
    (void)window;
    int key=event->type==SDL_KEYDOWN,down=event->type==SDL_MOUSEBUTTONDOWN;
    SDL_Keycode k=key?event->key.keysym.sym:0;
    if(ui->mosaic_align_open) {
        if((key && (k==SDLK_RETURN || k==SDLK_KP_ENTER || k==SDLK_ESCAPE || k==SDLK_SPACE)) ||
           (down && event->button.button==SDL_BUTTON_LEFT && x>=250 && x<390 && y>=220 && y<244))ui->mosaic_align_open=0;
        return key || down || event->type==SDL_MOUSEBUTTONUP || event->type==SDL_KEYUP ||
            event->type==SDL_TEXTINPUT || event->type==SDL_MOUSEWHEEL || event->type==SDL_MOUSEMOTION;
    }
    if(ui->mosaic_spacing_confirm) {
        int yes=(key && !event->key.repeat && k==SDLK_RETURN) || (down && event->button.button==SDL_BUTTON_LEFT && y>=220 && y<244 && x>=180 && x<306);
        int no=(key && k==SDLK_ESCAPE) || (down && event->button.button==SDL_BUTTON_LEFT && y>=220 && y<244 && x>=330 && x<456);
        if(yes){if(device)SDL_LockAudioDevice(device);mosaic_tidy(ui,1);if(device)SDL_UnlockAudioDevice(device);}
        if(yes || no)ui->mosaic_spacing_confirm=0;
        return key || down || event->type==SDL_TEXTINPUT || event->type==SDL_MOUSEWHEEL || event->type==SDL_MOUSEMOTION;
    }
    if(ui->mosaic_scatter_open) {
        if(c->scatter_epoch!=ui->mosaic->epoch){mosaic_scatter_close(ui);return 1;}
        if(key && k==SDLK_ESCAPE){mosaic_scatter_close(ui);return 1;}
        if(event->type==SDL_TEXTINPUT && ui->mosaic_scatter_field) {
            size_t n=strlen(ui->mosaic_scatter_text),more=strlen(event->text.text);
            if(n+more<sizeof(ui->mosaic_scatter_text))memcpy(ui->mosaic_scatter_text+n,event->text.text,more+1);return 1;
        }
        if(key && ui->mosaic_scatter_field) {
            size_t n=strlen(ui->mosaic_scatter_text);
            if(k==SDLK_BACKSPACE && n)ui->mosaic_scatter_text[n-1]=0;
            if(k==SDLK_a && (mod&KMOD_CTRL))ui->mosaic_scatter_text[0]=0;
            if(k==SDLK_RETURN || k==SDLK_TAB)mosaic_scatter_number(ui,c);
            return 1;
        }
        if(key && !event->key.repeat && k==SDLK_RETURN)mosaic_scatter_apply(device,ui,c);
        if(down && event->button.button==SDL_BUTTON_LEFT) {
            if(y>=272 && y<296 && x>=370 && x<490){mosaic_scatter_close(ui);return 1;}
            if(!mosaic_scatter_number(ui,c))return 1;
            int field=0;
            if(y>=115 && y<139){if(x>=130 && x<290)field=1;else if(x>=310 && x<490)field=2;}
            if(y>=151 && y<175 && x>=130 && x<240)field=3;
            if(field) {
                ui->mosaic_scatter_field=field;
                snprintf(ui->mosaic_scatter_text,sizeof(ui->mosaic_scatter_text),"%.3f",field==1?ui->mosaic_scatter_start:field==2?ui->mosaic_scatter_end:(double)ui->mosaic_scatter_count);
                if(field==3)snprintf(ui->mosaic_scatter_text,sizeof(ui->mosaic_scatter_text),"%d",ui->mosaic_scatter_count);
                SDL_StartTextInput();return 1;
            }
            if(y>=151 && y<175 && x>=260 && x<490 && !c->scatter_from_sources)ui->mosaic_scatter_copies=!ui->mosaic_scatter_copies;
            if(y>=186 && y<210){if(x>=130 && x<300)ui->mosaic_scatter_cluster=!ui->mosaic_scatter_cluster;else if(x>=320 && x<490)ui->mosaic_scatter_overlap=!ui->mosaic_scatter_overlap;}
            if(y>=272 && y<296) {
                if(x>=130 && x<230)c->scatter_seed+=0x9e3779b9u;
                else if(x>=250 && x<350){mosaic_scatter_apply(device,ui,c);return 1;}
            }
            mosaic_scatter_preview(ui,c);
        }
        return key || down || event->type==SDL_TEXTINPUT || event->type==SDL_MOUSEWHEEL || event->type==SDL_MOUSEMOTION;
    }
    if(key && !event->key.repeat && k==SDLK_r && (mod&KMOD_CTRL)) {mosaic_scatter_open(ui,c);return 1;}
    if(down && event->button.button==SDL_BUTTON_LEFT && x>=118 && x<200 && y>=56 && y<66) {
        if(ui->mosaic->auto_space) {if(device)SDL_LockAudioDevice(device);ts_mosaic_checkpoint(ui->mosaic);ui->mosaic->auto_space=0;if(device)SDL_UnlockAudioDevice(device);}
        else ui->mosaic_spacing_confirm=1;return 1;
    }
    if(down && event->button.button==SDL_BUTTON_LEFT && (mod&KMOD_ALT) && x>=151 && x<TS_MOSAIC_CANVAS_RIGHT && y>=66 && y<376) {
        double time=ui->mosaic_scroll+(y-66)/ui->mosaic_scale,left=ui->mosaic_xscroll+(x-151)/(ui->mosaic_hscale>0?ui->mosaic_hscale:1);
        uint64_t ids[TS_MOSAIC_EVENTS];int count=0,current=-1;
        for(int i=TS_MOSAIC_EVENTS-1;i>=0;--i) {
            const TsMosaicEvent *e=&ui->mosaic->events[i];
            if(e->id && time>=e->start && time<e->start+e->duration && left>=e->x && left<e->x+e->width) {
                if(e->id==ui->mosaic_selected)current=count;ids[count++]=e->id;
            }
        }
        if(count>1){mosaic_select_only(ui,ids[(current+1)%count]);return 1;}
        return 0;
    }
    return 0;
}
