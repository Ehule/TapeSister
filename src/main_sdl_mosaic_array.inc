/* Array starts use the full snap interval, never the thinned display grid.
   Prepare the entire edit before checkpointing so failures cannot add a prefix. */
typedef struct {double left,right;} MosaicArrayObstacle;
static int mosaic_array_obstacle_order(const void *a,const void *b)
{
    double x=((const MosaicArrayObstacle *)a)->left,y=((const MosaicArrayObstacle *)b)->left;
    return (x>y)-(x<y);
}
static void mosaic_array_space(const TsMosaic *m,TsMosaicEvent *stage,int count,TsMosaicEvent *e)
{
    MosaicArrayObstacle obstacles[TS_MOSAIC_EVENTS];int n=0;
    /* Existing and staged cards together cannot exceed the checked capacity. */
    for(int i=0;i<TS_MOSAIC_EVENTS+count;++i) {
        const TsMosaicEvent *b=i<TS_MOSAIC_EVENTS?&m->events[i]:&stage[i-TS_MOSAIC_EVENTS];
        if((i<TS_MOSAIC_EVENTS && !b->id) || e->start>=b->start+b->duration-1e-9 ||
           b->start>=e->start+e->duration-1e-9)continue;
        obstacles[n++]=(MosaicArrayObstacle){b->x,b->x+b->width};
    }
    qsort(obstacles,(size_t)n,sizeof(*obstacles),mosaic_array_obstacle_order);
    /* Sweep left to right once: moving past one obstacle cannot hit an earlier
       one. This also avoids repeated full-canvas scans for dense arrays. */
    for(int i=0;i<n;++i) {
        if(e->x+e->width+TS_MOSAIC_GUTTER<=obstacles[i].left)break;
        if(e->x<obstacles[i].right+TS_MOSAIC_GUTTER)e->x=obstacles[i].right+TS_MOSAIC_GUTTER;
    }
}
static int mosaic_array_sources(SDL_AudioDeviceID device,TsUiState *ui,
    const TsInstrument *instrument,MosaicController *c)
{
    TsMosaic *m=ui->mosaic;TsMosaicRange range=ui->mosaic_array_range;
    int mode=ui->mosaic_array_mode;
    if(mode && (!isfinite(range.start) || !isfinite(range.end) || range.start<0 || range.end<=range.start)) {
        snprintf(ui->status,sizeof(ui->status),"SELECT A TIME RANGE FIRST / SHIFT+DRAG THE RULER");return 0;
    }
    char *tail;long quantity=strtol(ui->mosaic_array_count,&tail,10);
    if(mode!=1 && (!ui->mosaic_array_count[0] || *tail || quantity<1 || quantity>TS_MOSAIC_EVENTS)) {
        snprintf(ui->status,sizeof(ui->status),"COUNT MUST BE 1 TO %d",TS_MOSAIC_EVENTS);return 0;
    }
    long steps=strtol(ui->mosaic_array_steps,&tail,10);
    if(mode!=2 && (!ui->mosaic_array_steps[0] || *tail || steps<1 || steps>1000000)) {
        snprintf(ui->status,sizeof(ui->status),"GRID STEPS MUST BE 1 TO 1000000");return 0;
    }
    double grid=ts_mosaic_grid_step(ui->mosaic_array_grid,ui->mosaic_grid_quarter);
    double first=mode==0?ui->mosaic_array_time:mode==2?range.start:ceil(range.start/grid-1e-9)*grid;
    double step=mode==2?(range.end-range.start)/quantity:grid*steps;
    double rows=mode==1?ceil((range.end-first)/step-1e-9):(double)quantity;
    if(rows<1) {snprintf(ui->status,sizeof(ui->status),"NO GRID STARTS INSIDE THE TIME RANGE");return 0;}
    int sources=c->picked_epoch==m->epoch?c->picked_count:0;
    int fallback=!sources;
    if(fallback && (ui->mosaic_source_selected<0 || ui->mosaic_source_selected>=ui->mosaic_source_count)) {
        snprintf(ui->status,sizeof(ui->status),"SELECT A SOURCE IN THE SIDEBAR FIRST");return 0;
    }
    if(fallback)sources=1;
    int free_slots=0;for(int i=0;i<TS_MOSAIC_EVENTS;++i)free_slots+=!m->events[i].id;
    if(rows>free_slots/sources) {
        snprintf(ui->status,sizeof(ui->status),"NOTHING ARRAYED / %.0F CARDS NEEDED, %d FREE / %s",rows*sources,free_slots,
            mode==1?"WIDEN GRID OR SHORTEN RANGE":"REDUCE COUNT OR SOURCE SELECTION");return 0;
    }
    int count=(int)rows*sources;
    if(!m->next_id || m->next_id>=UINT64_MAX-(uint64_t)count || first+(rows-1)*step>86400) {
        snprintf(ui->status,sizeof(ui->status),"NOTHING ARRAYED / ARRAY EXCEEDS CANVAS BOUNDS");return 0;
    }
    double left=ui->mosaic_array_x;
    TsMosaicEvent *stage=calloc((size_t)count,sizeof(*stage));
    if(!stage){snprintf(ui->status,sizeof(ui->status),"NOTHING ARRAYED / NOT ENOUGH MEMORY");return 0;}
    if(fallback)mosaic_source_drag(ui,instrument,c,ui->mosaic_source_selected);
    const TsMosaicEvent *templates=fallback?&c->source_drag:c->picked_sources;
    int patterns=0,ok=0;
    for(int row=0,n=0;row<(int)rows;++row) {
        double x=fmax(0,left);
        for(int j=0;j<sources;++j,++n) {
            TsMosaicEvent *e=&stage[n];*e=templates[j];
            if(!e->source || !isfinite(e->duration) || e->duration<=0 || e->duration>86400 ||
               !isfinite(e->width) || e->width<=0) {
                snprintf(ui->status,sizeof(ui->status),"NOTHING ARRAYED / SELECT A VALID SOURCE");goto done;
            }
            e->start=first+row*step;e->x=x;
            if(m->auto_space)mosaic_array_space(m,stage,n,e);
            if(!isfinite(e->x) || e->x>TS_MOSAIC_LAYOUT_LIMIT) {
                snprintf(ui->status,sizeof(ui->status),"NOTHING ARRAYED / NO ROOM FOR SPACED CARDS");goto done;
            }
            x=e->x+e->width+TS_MOSAIC_GUTTER;patterns+=e->source->pattern!=NULL;
        }
    }
    if(device)SDL_LockAudioDevice(device);
    if(!ts_mosaic_reserve_patterns(m,patterns)) {
        if(device)SDL_UnlockAudioDevice(device);
        snprintf(ui->status,sizeof(ui->status),"NOTHING ARRAYED / PATTERN VOICE RESERVATION FAILED");goto done;
    }
    ts_mosaic_checkpoint(m);mosaic_select_only(ui,0);
    for(int i=0,n=0;i<TS_MOSAIC_EVENTS && n<count;++i)if(!m->events[i].id) {
        TsMosaicEvent *e=&m->events[i];*e=stage[n++];e->id=m->next_id++;e->revision=1;
        mosaic_select_add(ui,e->id);
    }
    ++m->revision;m->route_discontinuity=1;
    if(device)SDL_UnlockAudioDevice(device);
    ui->project_modified=1;ok=1;
    snprintf(ui->status,sizeof(ui->status),"ARRAYED %d CARDS / %.4GS APART / FULL LENGTHS KEPT / CTRL+Z TO UNDO",count,step);
done:
    if(fallback)mosaic_drag_end(ui,c);
    free(stage);return ok;
}

static void mosaic_array_close(TsUiState *ui)
{ui->mosaic_array_open=0;SDL_StopTextInput();}
static void mosaic_array_open(SDL_AudioDeviceID device,TsUiState *ui,const MosaicController *c)
{
    if(device)SDL_LockAudioDevice(device);
    ui->mosaic_array_epoch=ui->mosaic->epoch;ui->mosaic_array_range=ui->mosaic->loop_range;
    ui->mosaic_array_time=ui->mosaic->time;
    if(device)SDL_UnlockAudioDevice(device);
    ui->mosaic_array_x=ui->mosaic_xscroll+(c->hover_x-151)/(ui->mosaic_hscale>0?ui->mosaic_hscale:1);
    ui->mosaic_array_grid=ui->mosaic->grid;ui->mosaic_array_grid.enabled=1;
    ui->mosaic_array_mode=ui->mosaic_array_range.end>ui->mosaic_array_range.start?1:0;
    if(!ui->mosaic_array_count[0])snprintf(ui->mosaic_array_count,sizeof(ui->mosaic_array_count),"4");
    snprintf(ui->mosaic_array_steps,sizeof(ui->mosaic_array_steps),"1");
    ui->mosaic_array_sources=c->picked_count?c->picked_count:ui->mosaic_source_selected>=0?1:0;
    ui->mosaic_array_status[0]=0;ui->mosaic_array_field=ui->mosaic_array_mode==1;ui->mosaic_array_open=1;SDL_StartTextInput();
}
static int mosaic_array_event(const SDL_Event *event,SDL_AudioDeviceID device,TsUiState *ui,
    const TsInstrument *instrument,MosaicController *c,int x,int y)
{
    if(!ui->mosaic_array_open)return 0;
    if(ui->mosaic_array_epoch!=ui->mosaic->epoch){mosaic_array_close(ui);return 1;}
    int key=event->type==SDL_KEYDOWN,down=event->type==SDL_MOUSEBUTTONDOWN;
    SDL_Keycode k=key?event->key.keysym.sym:0;
    int apply=key && !event->key.repeat && (k==SDLK_RETURN || k==SDLK_KP_ENTER);
    int cancel=key && k==SDLK_ESCAPE;
    int mode=ui->mosaic_array_mode;
    if(down && event->button.button==SDL_BUTTON_LEFT) {
        for(int i=0;i<3;++i)if(x>=110+i*140 && x<240+i*140 && y>=82 && y<106) {
            ui->mosaic_array_mode=mode=i;ui->mosaic_array_status[0]=0;
            ui->mosaic_array_field=mode==1?1:0;
        }
        if(y>=120 && y<144) {
            if(x>=174 && x<268 && mode!=1)ui->mosaic_array_field=0;
            if(x>=440 && x<530 && mode!=2)ui->mosaic_array_field=1;
        }
        if(mode!=2) {
            for(int i=0;i<3;++i)if(x>=110+i*140 && x<240+i*140 && y>=166 && y<190)ui->mosaic_array_grid.unit=i;
            static const int divisions[]={1,2,3,4,6,8,12,16,24,32,48,64};
            for(int i=0;i<12;++i)if(x>=110+(i%6)*70 && x<172+(i%6)*70 &&
                y>=208+(i/6)*28 && y<232+(i/6)*28)ui->mosaic_array_grid.division=divisions[i];
        }
        if(y>=326 && y<350){apply=x>=110 && x<310;cancel=x>=330 && x<530;}
    }
    if(cancel){mosaic_array_close(ui);return 1;}
    char *field=ui->mosaic_array_field?ui->mosaic_array_steps:ui->mosaic_array_count;
    int editable=ui->mosaic_array_field?mode!=2:mode!=1;
    if(editable && event->type==SDL_TEXTINPUT) {
        size_t n=strlen(field);
        for(const char *p=event->text.text;*p && n<sizeof(ui->mosaic_array_count)-1;++p)
            if(*p>='0' && *p<='9')field[n++]=*p;
        field[n]=0;ui->mosaic_array_status[0]=0;
    }
    if(key) {
        size_t n=strlen(field);
        if(editable && k==SDLK_BACKSPACE && n)field[n-1]=0;
        if(editable && k==SDLK_a && (event->key.keysym.mod&KMOD_CTRL))field[0]=0;
        if(k==SDLK_TAB)ui->mosaic_array_field=mode==1?1:mode==2?0:!ui->mosaic_array_field;
    }
    if(event->type==SDL_MOUSEWHEEL && y>=120 && y<144) {
        int which=x>=174 && x<268?0:x>=440 && x<530?1:-1;
        if(which>=0 && (which?mode!=2:mode!=1)) {
            char *value=which?ui->mosaic_array_steps:ui->mosaic_array_count;
            long n=strtol(value,NULL,10),limit=which?1000000:TS_MOSAIC_EVENTS;
            n=SDL_max(1,SDL_min(limit,n));
            n=SDL_max(1,SDL_min(limit,n+SDL_clamp(wheel_event_detents(event,0),-1000000,1000000)));
            snprintf(value,sizeof(ui->mosaic_array_count),"%ld",n);ui->mosaic_array_field=which;
        }
    }
    if(apply) {
        if(mosaic_array_sources(device,ui,instrument,c))mosaic_array_close(ui);
        else snprintf(ui->mosaic_array_status,sizeof(ui->mosaic_array_status),"%.159s",ui->status);
    }
    return key || down || event->type==SDL_KEYUP || event->type==SDL_MOUSEBUTTONUP ||
        event->type==SDL_MOUSEMOTION || event->type==SDL_MOUSEWHEEL || event->type==SDL_TEXTINPUT;
}
