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
    ui->mosaic_hscale=fmin(4,(ts_ui_mosaic_canvas_right(ui)-175)/fmax(1,right-left));
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
static void mosaic_nudge(SDL_AudioDeviceID device,TsUiState *ui,SDL_Keycode key,SDL_Keymod mod)
{
    ui->mosaic_ghost_count=mosaic_selected_events(ui,ui->mosaic_ghosts);
    if(!ui->mosaic_ghost_count)return;
    int horizontal=key==SDLK_LEFT || key==SDLK_RIGHT;
    double delta=(key==SDLK_UP || key==SDLK_LEFT?-1:1)*(mod&KMOD_SHIFT?10:1);
    if(horizontal) {
        double left=TS_MOSAIC_LAYOUT_LIMIT,right=0;
        for(int i=0;i<ui->mosaic_ghost_count;++i){left=fmin(left,ui->mosaic_ghosts[i].x);right=fmax(right,ui->mosaic_ghosts[i].x);}
        delta=fmax(-left,fmin(fmax(0,TS_MOSAIC_LAYOUT_LIMIT-right),delta));
    }
    if(delta!=0) {
        double dx=horizontal?delta:0,dt=horizontal?0:delta*.1;
        mosaic_plan_group(ui,&dx,&dt,0,0);
        if(device)SDL_LockAudioDevice(device);
        ts_mosaic_checkpoint(ui->mosaic);mosaic_move_group(ui,dx,dt);
        if(device)SDL_UnlockAudioDevice(device);
    }
    ui->mosaic_ghost_count=0;
}
#include "main_sdl_mosaic_scatter.inc"
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
    if(ui->mosaic_scatter_open)return mosaic_scatter_event(event,window,device,ui,c,x,y,mod);
    if(key && !event->key.repeat && k==SDLK_r && (mod&KMOD_CTRL)) {mosaic_scatter_open(ui,c);return 1;}
    if(down && event->button.button==SDL_BUTTON_LEFT && x>=118 && x<200 && y>=56 && y<66) {
        if(ui->mosaic->auto_space) {if(device)SDL_LockAudioDevice(device);ts_mosaic_checkpoint(ui->mosaic);ui->mosaic->auto_space=0;if(device)SDL_UnlockAudioDevice(device);}
        else ui->mosaic_spacing_confirm=1;return 1;
    }
    if(down && event->button.button==SDL_BUTTON_LEFT && (mod&KMOD_ALT) && x>=151 && x<ts_ui_mosaic_canvas_right(ui) && y>=66 && y<TS_MOSAIC_CANVAS_BOTTOM) {
        uint64_t ids[TS_MOSAIC_EVENTS];int count=0,current=-1;
        for(int i=TS_MOSAIC_EVENTS-1;i>=0;--i) {
            const TsMosaicEvent *e=&ui->mosaic->events[i];
            if(mosaic_card_distance(ui,e,x,y)>=0) {
                if(e->id==ui->mosaic_selected)current=count;ids[count++]=e->id;
            }
        }
        /* A selected duration edge belongs to the resize gesture even with Alt
           held at mouse-down. Interior Alt-click still cycles covered cards. */
        if(count && !(mod&KMOD_SHIFT)) {
            TsMosaicEvent *front=ts_mosaic_find(ui->mosaic,ids[current>=0?current:0]);
            int part=mosaic_card_drag_part(ui,front,x,y),selected=0;
            if((part==2 || part==6) && mosaic_chosen(ui,(int)(front-ui->mosaic->events))) {
                for(int i=0;i<TS_MOSAIC_EVENTS;++i)selected+=mosaic_chosen(ui,i)!=0;
                if(selected>1)return 0;
            }
        }
        if(count>1){mosaic_select_only(ui,ids[(current+1)%count]);return 1;}
        return 0;
    }
    return 0;
}
