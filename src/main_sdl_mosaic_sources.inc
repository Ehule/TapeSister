static TsMosaicSourceKey mosaic_source_key(const TsUiState *ui,int n)
{
    return (TsMosaicSourceKey){ui->mosaic_sources[n].bank_page,
        ui->mosaic_sources[n].bank_slot,ui->mosaic_sources[n].event};
}
static int mosaic_source_key_equal(TsMosaicSourceKey a,TsMosaicSourceKey b)
{return a.event==b.event && a.page==b.page && a.slot==b.slot;}
static void mosaic_picks_sync(TsUiState *ui,const MosaicController *c)
{
    ui->mosaic_source_pick_count=c->picked_count;
    memcpy(ui->mosaic_source_picks,c->picked_keys,c->picked_count*sizeof(c->picked_keys[0]));
}
static void mosaic_picks_clear(TsUiState *ui,MosaicController *c)
{
    if(c->picked_epoch==ui->mosaic->epoch)for(int i=0;i<c->picked_count;++i)
        if(c->picked_sources[i].source->pins)--c->picked_sources[i].source->pins;
    c->picked_count=ui->mosaic_source_pick_count=0;
}
static void mosaic_pick_remove(TsUiState *ui,MosaicController *c,int at)
{
    --c->picked_sources[at].source->pins;--c->picked_count;
    memmove(c->picked_sources+at,c->picked_sources+at+1,(c->picked_count-at)*sizeof(c->picked_sources[0]));
    memmove(c->picked_keys+at,c->picked_keys+at+1,(c->picked_count-at)*sizeof(c->picked_keys[0]));
    mosaic_picks_sync(ui,c);
}
static void mosaic_pick_add(TsUiState *ui,MosaicController *c)
{
    if(c->picked_count==TS_MOSAIC_EVENTS)return;
    c->picked_sources[c->picked_count]=c->source_drag;
    c->picked_keys[c->picked_count++]=c->source_key;++c->source_drag.source->pins;
    c->picked_epoch=ui->mosaic->epoch;mosaic_picks_sync(ui,c);
}
static int mosaic_source_preview(TsUiState *ui,MosaicController *c,int x,int y)
{
    ui->mosaic_ghost_count=0;
    if(x<151 || x>=TS_MOSAIC_CANVAS_RIGHT || y<66 || y>=376)return 0;
    double start=ui->mosaic_scroll+(y-66)/ui->mosaic_scale;
    double left=ui->mosaic_xscroll+(x-151)/(ui->mosaic_hscale>0?ui->mosaic_hscale:1);
    int count=c->source_group?c->picked_count:1;
    if(!count || start>86400)return 0;
    for(int i=0;i<count;++i) {
        TsMosaicEvent *e=&ui->mosaic_ghosts[i];*e=c->source_group?c->picked_sources[i]:c->source_drag;
        if(!e->source || e->duration<=0 || e->duration>86400)return 0;
        e->start=start;e->x=left;left+=e->width+TS_MOSAIC_GUTTER;
    }
    /* Space the row as one rigid group; selection order and common start stay fixed. */
    for(int pass=0;ui->mosaic->auto_space && pass<TS_MOSAIC_EVENTS;++pass) {
        double shift=0;
        for(int i=0;i<count;++i)for(int j=0;j<TS_MOSAIC_EVENTS;++j) {
            const TsMosaicEvent *a=&ui->mosaic_ghosts[i],*b=&ui->mosaic->events[j];
            if(!b->id || a->start>=b->start+b->duration || b->start>=a->start+a->duration)continue;
            if(a->x<b->x+b->width+TS_MOSAIC_GUTTER && b->x<a->x+a->width+TS_MOSAIC_GUTTER)
                shift=fmax(shift,b->x+b->width+TS_MOSAIC_GUTTER-a->x);
        }
        if(shift<=0)break;
        for(int i=0;i<count;++i)ui->mosaic_ghosts[i].x+=shift;
    }
    for(int i=0;i<count;++i)if(ui->mosaic_ghosts[i].x>TS_MOSAIC_LAYOUT_LIMIT)return 0;
    ui->mosaic_ghost_count=count;return 1;
}
static int mosaic_sources_event(const SDL_Event *event,SDL_AudioDeviceID device,
    TsUiState *ui,const TsInstrument *instrument,MosaicController *c,int x,int y,SDL_Keymod mod)
{
    int down=event->type==SDL_MOUSEBUTTONDOWN,up=event->type==SDL_MOUSEBUTTONUP;
    int motion=event->type==SDL_MOUSEMOTION;
    int sidebar=x>=10 && x<108 && y>=74 && y<TS_MOSAIC_SOURCE_PAGE_Y;
    if(down && event->button.button==SDL_BUTTON_LEFT && sidebar) {
        int n=ui->mosaic_source_offset+((y-74)/34)*2+(x-10)/49;
        mosaic_drag_end(ui,c);mosaic_source_drag(ui,instrument,c,n);
        if(!c->source_drag.source)return 1;
        c->source_key=mosaic_source_key(ui,n);c->drag_x=x;c->drag_y=y;
        int at=0;for(;at<c->picked_count;++at)if(mosaic_source_key_equal(c->source_key,c->picked_keys[at]))break;
        if(mod&KMOD_CTRL) {
            if(at<c->picked_count)mosaic_pick_remove(ui,c,at);else mosaic_pick_add(ui,c);
            mosaic_drag_end(ui,c);
        } else {
            if(mod&KMOD_SHIFT) {
                if(at<c->picked_count)c->source_toggle=at+1;else mosaic_pick_add(ui,c);
            } else if(at==c->picked_count)mosaic_picks_clear(ui,c);
            c->source_group=c->picked_count>0;
        }
        snprintf(ui->status,sizeof(ui->status),"SOURCES: SHIFT CLICK ADD / DRAG ROW / CTRL+R SCATTER");return 1;
    }
    if(c->drag!=3)return 0;
    if(motion) {
        if(abs(x-c->drag_x)>=3 || abs(y-c->drag_y)>=3)c->drag_moved=1;
        mosaic_source_preview(ui,c,x,y);return 1;
    }
    if(up && event->button.button==SDL_BUTTON_LEFT) {
        if(mosaic_source_preview(ui,c,x,y)) {
            int free_slots=0,count=ui->mosaic_ghost_count;
            for(int i=0;i<TS_MOSAIC_EVENTS;++i)free_slots+=!ui->mosaic->events[i].id;
            if(count>free_slots)snprintf(ui->status,sizeof(ui->status),"NOT ENOUGH ROOM FOR ALL %d SOURCES / %d FREE",count,free_slots);
            else {
                if(device)SDL_LockAudioDevice(device);
                ts_mosaic_checkpoint(ui->mosaic);mosaic_select_only(ui,0);
                for(int i=0;i<count;++i) {
                    TsMosaicEvent *preview=&ui->mosaic_ghosts[i];
                    TsMosaicEvent *e=ts_mosaic_add(ui->mosaic,preview->source,preview->start,preview->x);
                    uint64_t id=e->id;*e=*preview;e->id=id;e->revision=1;mosaic_select_add(ui,id);
                }
                if(device)SDL_UnlockAudioDevice(device);
                mosaic_picks_clear(ui,c);
                snprintf(ui->status,sizeof(ui->status),"%d CARDS ADDED / FIRST IS PRIMARY / CTRL+R SCATTER",count);
            }
        } else if(!c->drag_moved && sidebar &&
            ui->mosaic_source_offset+((y-74)/34)*2+(x-10)/49==c->drag_slot) {
            if(c->source_toggle)mosaic_pick_remove(ui,c,c->source_toggle-1);
            else if(!c->source_group)mosaic_pick_add(ui,c);
        }
        mosaic_drag_end(ui,c);ts_mosaic_collect(ui->mosaic);mosaic_sources_refresh(ui,instrument,c);return 1;
    }
    return 0;
}
