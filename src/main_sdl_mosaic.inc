static void mosaic_poll(SDL_AudioDeviceID device,TsUiState *ui,
                          TsInstrument *instrument,MosaicController *c)
{
    if(!ui->mosaic)return;
    if(!ui->mosaic_open)c->pattern_ready=0;
    if(ui->tracker_open)c->was_tracker=1;
    if(ui->mosaic_open && c->was_tracker){c->was_tracker=0;c->pattern_ready=0;}

    if(!ui->mosaic_open || c->active)c->mix_drag=c->mix_changed=c->volume_drag=c->pan_active=ui->mosaic_loop_drag=ui->mosaic_marker_drag=0;
    if(device)SDL_LockAudioDevice(device);
    ts_mosaic_pattern_retire(ui->mosaic);
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)ui->mosaic_pattern_pending[i]=ui->mosaic->pattern_retired[i]!=NULL;
    int release_preview=!ui->mosaic->audition_id && ui->mosaic->audition_source.source!=NULL;
    if(!ui->mosaic_open || c->active || !ui->mosaic->audition_id)ts_mosaic_audition(ui->mosaic,0);
    memcpy(ui->mosaic_live,ui->mosaic->live,sizeof(ui->mosaic_live));
    const TsMosaicSequence *seq=&ui->mosaic->sequence;
    ui->mosaic_sequence_running=seq->running;ui->mosaic_sequence_finishing=seq->finishing;
    ui->mosaic_sequence_index=seq->index;ui->mosaic_sequence_count=seq->count;
    ui->mosaic_sequence_id=seq->running?seq->ids[seq->index]:0;ui->mosaic_sequence_duration=seq->duration;
    TsMosaicEvent *sequence_selected=ts_mosaic_find(ui->mosaic,ui->mosaic_selected);
    ui->mosaic_sequence_selection_owned=sequence_selected && ts_mosaic_sequence_owns(ui->mosaic,(int)(sequence_selected-ui->mosaic->events));
    ui->mosaic_live_loops=ts_mosaic_live_count(ui->mosaic);
    ui->mosaic_live_master=ui->mosaic->live_master;ui->mosaic_live_paused=ui->mosaic->live_paused;
    ui->mosaic_live_quarter=ui->mosaic->live_quarter;
    ui->mosaic_time=ui->mosaic->time;ui->mosaic_playing=ui->mosaic->playing;
    if(device)SDL_UnlockAudioDevice(device);
    if(release_preview)ts_mosaic_collect(ui->mosaic);
    if(ui->mosaic_open && !c->active) {
        mosaic_sources_refresh(ui,instrument,c);
        mosaic_pattern_notices(ui,instrument,c,0);
        int recording=ui->mosaic_recording && ui->capture_state==TS_CAPTURE_RECORDING &&
            ui->input_sample_rate && ui->mosaic_record_preview.epoch==ui->mosaic->epoch;
        if(ui->mosaic_follow && !ui_dialog_open(ui) && (ui->mosaic_playing || recording) && !c->drag && !c->volume_drag && !c->pan_active && !ui->mosaic_marker_drag && !ui->mosaic_loop_drag) {
            double target=recording?ui->mosaic_record_preview.start+
                (double)ui->capture_recorded_frames/ui->input_sample_rate:ui->mosaic_time;
            double offset=(target-ui->mosaic_scroll)*ui->mosaic_scale;
            if(offset>220 || offset<0)ui->mosaic_scroll=fmax(0,target-220/ui->mosaic_scale);
            if(recording) {
                TsMosaicEvent placement={.start=ui->mosaic_record_preview.start,
                    .duration=target-ui->mosaic_record_preview.start,.x=ui->mosaic_record_preview.x,.width=86};
                ts_mosaic_space(ui->mosaic,&placement);
                double horizontal=ui->mosaic_hscale>0?ui->mosaic_hscale:1;
                double visible=(TS_MOSAIC_CANVAS_RIGHT-151)/horizontal;
                if(placement.x+86>ui->mosaic_xscroll+visible)
                    ui->mosaic_xscroll=fmax(0,placement.x+86-visible);
                if(placement.x<ui->mosaic_xscroll)ui->mosaic_xscroll=placement.x;
            }
        }
    }
    if(c->active) {
        if(!ui->show_recipes && !ui->show_ingredients)ui->show_keyboard=1;
        TsMosaicEvent *e=ts_mosaic_find(ui->mosaic,c->active);ui->staged_notes=0;
        if(e)for(int n=0;n<e->note_count;++n) {
            int k=e->notes[n]-ts_ui_keyboard_base_note(ui);if(k>=0 && k<TS_KEYBOARD_NOTE_COUNT)ui->staged_notes|=UINT64_C(1)<<k;
        }
        (void)instrument;
    }
}
static void mosaic_transport(SDL_AudioDeviceID device,TsUiState *ui,int stop)
{
    if(device)SDL_LockAudioDevice(device);
    if(stop){ts_mosaic_audition(ui->mosaic,0);ui->mosaic->playing=0;ts_mosaic_seek(ui->mosaic,0);ui->mosaic_scroll=0;}
    else {
        TsMosaic *m=ui->mosaic;
        if(!m->playing && m->repeat && m->loop_range.end>m->loop_range.start) {
            if(m->time<m->loop_range.start || m->time>=m->loop_range.end)ts_mosaic_seek(m,m->loop_range.start);
        } else if(!m->playing && m->time>=ts_mosaic_end(m))ts_mosaic_seek(m,0);
        ui->mosaic->playing=!ui->mosaic->playing;
    }
    if(device)SDL_UnlockAudioDevice(device);
}
/* Duration edges have priority over visual width, as on a single card. */
static int mosaic_card_drag_part(const TsUiState *ui,const TsMosaicEvent *e,double time,double left)
{
    double h=ui->mosaic_hscale>0?ui->mosaic_hscale:1;
    return e->start+e->duration-time<6/ui->mosaic_scale?2:
        time-e->start<6/ui->mosaic_scale?6:e->x+e->width-left<5/h?4:1;
}
/* Rigid horizontal spacing against cards outside the selection. Optional
   geometry arrays let resize share the move planner without changing ghosts. */
static double mosaic_group_spacing(const TsUiState *ui,double dx,double dt,int copying,
                                   const double *starts,const double *durations)
{
    if(!ui->mosaic->auto_space)return dx;
    const TsMosaicEvent *obstacles[TS_MOSAIC_EVENTS];int obstacle_count=0;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)
        if(ui->mosaic->events[i].id && (copying || !mosaic_chosen(ui,i)))obstacles[obstacle_count++]=&ui->mosaic->events[i];
    for(int pass=0;ui->mosaic->auto_space && pass<TS_MOSAIC_EVENTS;++pass) {
        double shift=0;
        for(int j=0;j<ui->mosaic_ghost_count;++j) {
            const TsMosaicEvent *o=&ui->mosaic_ghosts[j];double t=(starts?starts[j]:o->start)+dt,x=o->x+dx;
            double duration=durations?durations[j]:o->duration;
            for(int i=0;i<obstacle_count;++i) {
                const TsMosaicEvent *b=obstacles[i];
                if(t>=b->start+b->duration-1e-9 || b->start>=t+duration-1e-9)continue;
                if(x<b->x+b->width+TS_MOSAIC_GUTTER && b->x<x+o->width+TS_MOSAIC_GUTTER)
                    shift=fmax(shift,b->x+b->width+TS_MOSAIC_GUTTER-x);
            }
        }
        if(shift<=0)break;dx+=shift;
    }
    return dx;
}
/* Geometry is written only by the UI thread. Plan without excluding audio,
   then publish the final rigid move under its lock. On the first copy motion,
   originals are obstacles and the unpublished copies are not in the model. */
static void mosaic_plan_group(TsUiState *ui,double *move_x,double *move_t,int snap,int copying)
{
    double dx=*move_x,dt=*move_t;
    TsMosaic *m=ui->mosaic;double minx=1e30,mint=1e30,maxt=0;
    const TsMosaicEvent *obstacles[TS_MOSAIC_EVENTS];int obstacle_count=0;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)
        if(m->events[i].id && (copying || !mosaic_chosen(ui,i)))obstacles[obstacle_count++]=&m->events[i];
    for(int i=0;i<ui->mosaic_ghost_count;++i) {
        const TsMosaicEvent *o=&ui->mosaic_ghosts[i];
        minx=fmin(minx,o->x);mint=fmin(mint,o->start);maxt=fmax(maxt,o->start);
    }
    dx=fmax(dx,-minx);dt=fmin(86400-maxt,fmax(dt,-mint));
    if(snap) {
        double best=7/ui->mosaic_scale,adjust=0;
        for(int j=0;j<ui->mosaic_ghost_count;++j)for(int k=0;k<3;++k) {
            const TsMosaicEvent *o=&ui->mosaic_ghosts[j];double t=o->start+dt+o->duration*.5*k;
            if(fabs(t)<best){best=fabs(t);adjust=-t;}
            for(int i=0;i<obstacle_count;++i) {
                const TsMosaicEvent *e=obstacles[i];
                for(int a=0;a<3;++a) {
                    double d=e->start+e->duration*.5*a-t;
                    if(fabs(d)<best){best=fabs(d);adjust=d;}
                }
            }
        }
        dt=fmin(86400-maxt,fmax(-mint,dt+adjust));
    }
    dx=mosaic_group_spacing(ui,dx,dt,copying,NULL,NULL);
    *move_x=dx;*move_t=dt;
}
/* Caller holds the audio lock. This is the only publication of a group move. */
static void mosaic_move_group(TsUiState *ui,double dx,double dt)
{
    TsMosaic *m=ui->mosaic;
    for(int j=0;j<ui->mosaic_ghost_count;++j) {
        const TsMosaicEvent *o=&ui->mosaic_ghosts[j];TsMosaicEvent *e=ts_mosaic_find(m,o->id);
        if(e){e->start=o->start+dt;e->x=o->x+dx;}
    }
}
/* Copy the whole selection atomically on the first real movement. A click
   alone creates nothing, and capacity failure never creates a partial group. */
static int mosaic_begin_drag_change(TsUiState *ui,MosaicController *c)
{
    TsMosaic *m=ui->mosaic;
    if(c->drag_copy) {
        int free_slots=0;
        for(int i=0;i<TS_MOSAIC_EVENTS;++i)free_slots+=!m->events[i].id;
        if(free_slots<ui->mosaic_ghost_count) {
            snprintf(ui->status,sizeof(ui->status),"COPY NEEDS %d FREE / %d AVAILABLE (%d CARD LIMIT)",
                ui->mosaic_ghost_count,free_slots,TS_MOSAIC_EVENTS);return 0;
        }
    }
    for(int i=0;i<ui->mosaic_ghost_count;++i)
        if(!ts_mosaic_find(m,ui->mosaic_ghosts[i].id))return 0;
    ts_mosaic_checkpoint(m);
    if(c->drag_copy) {
        uint64_t selected=ui->mosaic_selected;mosaic_select_only(ui,0);
        for(int i=0;i<ui->mosaic_ghost_count;++i) {
            TsMosaicEvent *o=&ui->mosaic_ghosts[i];uint64_t old=o->id;
            /* The following rigid group move computes final spacing. Running
               per-card spacing here only to overwrite it is cubic for a dense
               reverse-ordered row, and unnecessarily excludes playback. */
            TsMosaicEvent *copy=ts_mosaic_copy_unspaced(m,old,o->start,o->x);
            /* Capacity and source IDs are stable under the caller's audio lock. */
            if(!copy)return 0;
            copy->start=o->start;copy->x=o->x;copy->revision=1;o->id=copy->id;
            mosaic_select_add(ui,copy->id);
            if(old==selected)ui->mosaic_selected=copy->id;
        }
        snprintf(ui->status,sizeof(ui->status),"COPYING %d EVENT%s - ORIGINALS STAY IN PLACE",ui->mosaic_ghost_count,ui->mosaic_ghost_count==1?"":"S");
    }
    c->drag_moved=1;return 1;
}
/* Opening geometry is immutable for the whole gesture. Alt can be pressed
   and released without accumulating error or changing which card is the parent. */
static void mosaic_resize_group(SDL_AudioDeviceID device,TsUiState *ui,MosaicController *c,double delta,int match)
{
    int top=c->drag==6,count=ui->mosaic_ghost_count;
    double amount=top?-delta:delta,low=-86400,high=86400,length=0;
    for(int i=0;i<count;++i) {
        const TsMosaicEvent *o=&ui->mosaic_ghosts[i];
        low=fmax(low,.005-o->duration);high=fmin(high,86400-o->duration);
        if(top)high=fmin(high,o->start);
    }
    if(match) {
        double limit=86400;
        if(top)for(int i=0;i<count;++i)limit=fmin(limit,ui->mosaic_ghosts[i].start+ui->mosaic_ghosts[i].duration);
        length=fmax(.005,fmin(limit,c->drag_duration+amount));
    } else amount=fmax(low,fmin(high,amount));
    double starts[TS_MOSAIC_EVENTS],durations[TS_MOSAIC_EVENTS];
    for(int i=0;i<count;++i) {
        const TsMosaicEvent *o=&ui->mosaic_ghosts[i];
        durations[i]=match?length:o->duration+amount;
        starts[i]=top?fmax(0,o->start+o->duration-durations[i]):o->start;
    }
    double dx=mosaic_group_spacing(ui,0,0,0,starts,durations);int changed=0;
    for(int i=0;i<count;++i) {
        const TsMosaicEvent *o=&ui->mosaic_ghosts[i];TsMosaicEvent *e=ts_mosaic_find(ui->mosaic,o->id);
        if(!e)return;
        changed|=fabs(e->start-starts[i])>1e-9 || fabs(e->duration-durations[i])>1e-9 || fabs(e->x-o->x-dx)>1e-9;
    }
    if(changed) {
        if(device)SDL_LockAudioDevice(device);
        if(!c->drag_moved && !mosaic_begin_drag_change(ui,c)) {
            if(device)SDL_UnlockAudioDevice(device);mosaic_drag_end(ui,c);return;
        }
        for(int i=0;i<count;++i) {
            const TsMosaicEvent *o=&ui->mosaic_ghosts[i];TsMosaicEvent *e=ts_mosaic_find(ui->mosaic,o->id);
            e->start=starts[i];e->duration=durations[i];e->x=o->x+dx;
        }
        ++ui->mosaic->revision;
        if(device)SDL_UnlockAudioDevice(device);
    }
    if(match)snprintf(ui->status,sizeof(ui->status),"RESIZE %d / MATCH LENGTH %.3FS / RELEASE ALT: RELATIVE",count,length);
    else snprintf(ui->status,sizeof(ui->status),"RESIZE %d / CHANGE %+.3FS / ALT MATCH LENGTH / ESC CANCEL",count,amount);
}
/* Event movement and held-edge scrolling share one undoable group gesture. */
static void mosaic_drag_motion(SDL_AudioDeviceID device,TsUiState *ui,MosaicController *c,
                               int x,int y,SDL_Keymod mod)
{
    TsMosaicEvent *e=ts_mosaic_find(ui->mosaic,(c->drag==2 || c->drag==6)?c->drag_event:ui->mosaic_selected);if(!e)return;
    double horizontal=ui->mosaic_hscale>0?ui->mosaic_hscale:1;
    double delta=(y-c->drag_y)/ui->mosaic_scale;
    double dx=(x-c->drag_x)/horizontal+ui->mosaic_xscroll-c->drag_xscroll,dt=delta;
    if(!c->drag_moved && abs(x-c->drag_x)<3 && abs(y-c->drag_y)<3)return;
    if((c->drag==2 || c->drag==6) && ui->mosaic_ghost_count>1) {
        mosaic_resize_group(device,ui,c,delta,(mod&KMOD_ALT)!=0);return;
    }
    if(c->drag==1)mosaic_plan_group(ui,&dx,&dt,(mod&KMOD_ALT)!=0,c->drag_copy && !c->drag_moved);
    if(device)SDL_LockAudioDevice(device);
    if(!c->drag_moved) {
        if(!mosaic_begin_drag_change(ui,c)) {
            if(device)SDL_UnlockAudioDevice(device);mosaic_drag_end(ui,c);return;
        }
        e=ts_mosaic_find(ui->mosaic,ui->mosaic_selected);
    }
    if(c->drag==1)mosaic_move_group(ui,dx,dt);
    else {
        if(c->drag==2) {
            double end=c->drag_start+c->drag_duration+delta;
            if(mod&KMOD_ALT)end=ts_mosaic_snap(ui->mosaic,e->id,end,7/ui->mosaic_scale);
            e->duration=fmin(86400,fmax(.005,end-e->start));
            double cycle=ts_mosaic_live_cycle(e);
            if(e->live && cycle>0 && cycle<=86400 && !(mod&KMOD_SHIFT))
                e->duration=fmin(86400,fmax(cycle,round(e->duration/cycle)*cycle));
            if(e->live)snprintf(ui->status,sizeof(ui->status),"STEP %.3G BASE CYCLES / SHIFT: FREE LENGTH / RUNNING STEP KEEPS ITS LENGTH",e->duration/fmax(.000001,cycle));
        } else if(c->drag==6) {
            double end=c->drag_start+c->drag_duration,start=c->drag_start+delta;
            if(mod&KMOD_ALT)start=ts_mosaic_snap(ui->mosaic,e->id,start,7/ui->mosaic_scale);
            e->start=fmax(fmax(0,end-86400),fmin(end-.005,start));e->duration=end-e->start;
        } else e->width=fmin(460,fmax(36,c->drag_width+(x-c->drag_x)/horizontal));
        ts_mosaic_space(ui->mosaic,e);
    }
    if(device)SDL_UnlockAudioDevice(device);
}

static void mosaic_drag_autoscroll(SDL_AudioDeviceID device,TsUiState *ui,MosaicController *c,Uint32 now)
{
    Uint32 elapsed=now-c->drag_scroll_ms;c->drag_scroll_ms=now;
    if(c->drag!=1 || !c->drag_moved || !ui->mosaic_open || c->active ||
       c->ui_epoch!=ui->mosaic->epoch || ui_dialog_open(ui) || ui->router_open || ui->master_eq_open)return;
    int x=c->drag_pointer_x,y=c->drag_pointer_y;
    if(y<66 || y>=TS_MOSAIC_CANVAS_BOTTOM)return;
    double speed=0,edge=24;
    if(x<151+edge)speed=-fmin(1,(151+edge-x)/edge);
    else if(x>TS_MOSAIC_CANVAS_RIGHT-edge)speed=fmin(1,(x-(TS_MOSAIC_CANVAS_RIGHT-edge))/edge);
    if(!speed || !elapsed)return;
    double horizontal=ui->mosaic_hscale>0?ui->mosaic_hscale:1;
    double scroll=fmax(0,fmin(TS_MOSAIC_LAYOUT_LIMIT,ui->mosaic_xscroll+
        speed*240*fmin(.05,elapsed/1000.)/horizontal));
    if(scroll==ui->mosaic_xscroll)return;
    ui->mosaic_xscroll=scroll;ui->mosaic_follow=0;c->zoom_saved=0;
    mosaic_drag_motion(device,ui,c,x,y,SDL_GetModState());
}

static int mosaic_toggle_key(const SDL_Event *event)
{
    if(event->type!=SDL_KEYDOWN)return 0;
    SDL_Keymod mod=(SDL_Keymod)event->key.keysym.mod;
    return (event->key.keysym.sym==SDLK_m && (mod&KMOD_CTRL) && !(mod&(KMOD_SHIFT|KMOD_ALT|KMOD_GUI))) ||
        (event->key.keysym.sym==SDLK_BACKQUOTE && !(mod&(KMOD_CTRL|KMOD_SHIFT|KMOD_ALT|KMOD_GUI)));
}
static void mosaic_source_drag(TsUiState *ui,const TsInstrument *instrument,MosaicController *c,int n)
{
    if(n<0 || n>=ui->mosaic_source_count)return;
    char error[160];TsMosaicEvent *e=ts_mosaic_find(ui->mosaic,ui->mosaic_sources[n].event);
    if(e)c->source_drag=*e;
    else if(ui->mosaic_sources[n].source && ui->mosaic_sources[n].source->pattern){
        TsMosaicSource *source=ui->mosaic_sources[n].source;
        c->source_drag=(TsMosaicEvent){.source=source,.width=100,.looping=1,.note_count=1,.notes={60},.gain=1,.last=2,.tuning={60,0}};
        snprintf(c->source_drag.name,sizeof(c->source_drag.name),"%.63s",source->sample.name);
    }
    else {
        int slot=ui->mosaic_sources[n].bank_slot;
        const TsSample *sample=ui->mosaic_sources[n].sample;
        if(!sample || !sample->data)return;
        const TsInstrument *bank=c->pages?ts_sample_pages_page(c->pages,instrument,(size_t)ui->mosaic_sources[n].bank_page):instrument;
        if(!bank || slot<0 || slot>=TS_BANK_SLOT_COUNT)return;
        TsMosaicSource *source=NULL;TsMasterEqControls flat;ts_master_eq_default(&flat);
        const TsMasterEqControls *eq=slot==bank->selected_slot?&bank->tile_eq:&bank->bank[slot].edit.tile_eq;
        if(eq->band[0].frequency && memcmp(eq,&flat,sizeof(flat))) {
            if(slot==bank->selected_slot)source=ts_mosaic_source_document(ui->mosaic,bank,error,sizeof(error));
            else {
                TsInstrument *doc=malloc(sizeof(*doc));if(!doc)return;ts_instrument_init(doc);
                if(ts_instrument_clone(doc,bank,error,sizeof(error)) && ts_instrument_select_bank(doc,slot,error,sizeof(error)))
                    source=ts_mosaic_source_document(ui->mosaic,doc,error,sizeof(error));
                ts_instrument_free(doc);free(doc);
            }
        } else source=ts_mosaic_source(ui->mosaic,sample,error,sizeof(error));
        if(!source)return;
        const TsBankSlot *b=&bank->bank[slot];int current=slot==bank->selected_slot;
        TsMosaicEvent *d=&c->source_drag;*d=(TsMosaicEvent){.source=source,.width=86,.looping=1,.note_count=1,.notes={60},.gain=1};
        d->tuning=current?bank->tuning:b->tuning;
        int loop=current?bank->has_loop:b->has_loop;
        d->first=loop?(current?bank->loop_first:b->loop_first):0;
        d->last=loop?(current?bank->loop_last:b->loop_last):sample->frames;
        d->mode=current?bank->loop_mode:b->loop_mode;
        float fade=current?bank->loop_crossfade_ms:b->loop_crossfade_ms;
        d->crossfade=loop?(size_t)llround(fade*sample->sample_rate/1000.0):0;
        if(d->crossfade>(d->last-d->first)/2)d->crossfade=(d->last-d->first)/2;
        snprintf(d->name,sizeof(d->name),"%.63s",sample->name);
    }
    c->source_drag.duration=c->source_drag.source->pattern?c->source_drag.source->pattern->duration:(double)(c->source_drag.last-c->source_drag.first)/c->source_drag.source->sample.sample_rate;
    c->source_drag.muted=c->source_drag.solo=0;
    ++c->source_drag.source->pins;c->drag_epoch=ui->mosaic->epoch;c->drag=3;c->drag_slot=n;
    ui->mosaic_source_selected=n;
}

/* Selecting a performance source is separate from selecting/placing a card.
   Copy on the UI thread once per choice, keep old notes in their immutable
   snapshot, and preserve the chosen tile/card's route and loop settings. */
static TsMosaicSourceKey mosaic_source_key(const TsUiState *,int);
static int mosaic_source_key_equal(TsMosaicSourceKey,TsMosaicSourceKey);
static void mosaic_keyboard_select(SDL_AudioDeviceID device,AudioState *a,TsUiState *ui,
    const TsInstrument *instrument,MosaicController *c,int n)
{
    if(!ui->mosaic_live_open || n<0 || n>=ui->mosaic_source_count || !c->source_drag.source ||
       c->source_drag.source->pattern)return;
    TsMosaicSourceKey key=mosaic_source_key(ui,n);
    int reuse=a->mosaic_keyboard_sample && a->mosaic_keyboard_epoch==ui->mosaic->epoch &&
        a->mosaic_keyboard_hash==c->source_drag.source->hash;
    runtime_note_collect_snapshots(device,a);
    int slot=reuse?(int)(a->mosaic_keyboard_sample-a->note_snapshots):0;
    while(!reuse && slot<TS_NOTE_BANK_VOICE_CAPACITY && a->note_snapshots[slot].data)++slot;
    char error[160];TsSample sample={0};
    if(slot==TS_NOTE_BANK_VOICE_CAPACITY || (!reuse && !ts_sample_clone(&sample,&c->source_drag.source->sample,error,sizeof(error)))) {
        snprintf(ui->mosaic_live_status,sizeof(ui->mosaic_live_status),"KEY SOURCE KEPT / NOT ENOUGH MEMORY FOR THE NEW SOURCE");return;
    }
    TsMosaicEvent *event=&c->source_drag;
    TsSourceRoute route=event->output_route;TsTileId tile=0;int loop=event->looping;
    if(key.slot>=0 && key.page>=0) {
        const TsInstrument *bank=c->pages?ts_sample_pages_page(c->pages,instrument,(size_t)key.page):instrument;
        const TsBankSlot *b=&bank->bank[key.slot];
        route=b->output_route;tile=b->tile_id;loop=key.slot==bank->selected_slot?bank->has_loop:b->has_loop;
    }
    if(device)SDL_LockAudioDevice(device);
    if(!reuse)a->note_snapshots[slot]=sample;
    a->mosaic_keyboard_sample=&a->note_snapshots[slot];
    a->mosaic_keyboard_tuning=event->tuning;a->mosaic_keyboard_first=event->first;a->mosaic_keyboard_last=event->last;
    a->mosaic_keyboard_mode=event->mode;a->mosaic_keyboard_looping=loop;
    a->mosaic_keyboard_crossfade=event->crossfade;a->mosaic_keyboard_route=route;a->mosaic_keyboard_tile=tile;
    a->mosaic_keyboard_epoch=ui->mosaic->epoch;a->mosaic_keyboard_key=key;
    a->mosaic_keyboard_hash=c->source_drag.source->hash;
    if(device)SDL_UnlockAudioDevice(device);
    if(key.page>=0)snprintf(a->mosaic_keyboard_label,sizeof(a->mosaic_keyboard_label),"BANK %02d / TILE %02d",key.page+1,key.slot+1);
    else snprintf(a->mosaic_keyboard_label,sizeof(a->mosaic_keyboard_label),"CARD #%llu / %.20s",(unsigned long long)key.event,event->name);
    snprintf(ui->mosaic_live_status,sizeof(ui->mosaic_live_status),"QWERTY: %.45s / NEW NOTES USE THIS SOURCE",a->mosaic_keyboard_label);
    ui->mosaic_live_key=key;ui->mosaic_live_key_valid=1;
    snprintf(ui->mosaic_live_keyboard,sizeof(ui->mosaic_live_keyboard),"%s",a->mosaic_keyboard_label);
    runtime_note_collect_snapshots(device,a);
}

#include "main_sdl_mosaic_sources.inc"
#include "main_sdl_mosaic_arrange.inc"
#include "main_sdl_mosaic_loop.inc"
#include "main_sdl_mosaic_navigation.inc"
#include "main_sdl_mosaic_markers.inc"
#include "main_sdl_mosaic_controls.inc"
#include "main_sdl_mosaic_fades.inc"
#include "main_sdl_mosaic_volume.inc"

static int mosaic_event(const SDL_Event *event,SDL_Window *window,SDL_AudioDeviceID device,
    AudioState *audio,TsUiState *ui,TsInstrument *instrument,MosaicController *c,
    PortalController *portal,SisterWindow *sister,TransformController *transform,int rate)
{
    if(!ui->mosaic)return 0;
    /* Notes started in another workspace still own their key/pointer releases.
       Handle these before Mosaic's panel and edit dialog consume input. */
    if(ui->mosaic_open || ui->mosaic_edit_choice) {
        if(event->type==SDL_KEYUP && note_for_key(event->key.keysym.sym)>=0) {
            release_note(device,audio,ui,note_for_key(event->key.keysym.sym));
            return 1;
        }
        if(event->type==SDL_MOUSEBUTTONUP && event->button.button==SDL_BUTTON_LEFT && ui->mouse_note>=0) {
            release_note(device,audio,ui,ui->mouse_note);ui->mouse_note=-1;
            return 1;
        }
    }
    if(ui->midi_learn_active)return 0; /* Do not edit/play beneath the global learner. */
    double horizontal=ui->mosaic_hscale>0?ui->mosaic_hscale:1;
    int down=event->type==SDL_MOUSEBUTTONDOWN,up=event->type==SDL_MOUSEBUTTONUP;
    int motion=event->type==SDL_MOUSEMOTION,key=event->type==SDL_KEYDOWN;
    int x=0,y=0;
    if(down || up)logical_mouse(window,event->button.x,event->button.y,&x,&y);
    if(motion)logical_mouse(window,event->motion.x,event->motion.y,&x,&y);
    SDL_Keymod mod=key?(SDL_Keymod)event->key.keysym.mod:SDL_GetModState();
    if(ui->mosaic_open && (ui->mosaic_marker_open || ui->mosaic_views_open))
        return mosaic_marker_event(event,device,ui,c,x,y,mod);
    if(ui->mosaic_pattern_choice) {
        int choice=-1;
        if(key && !event->key.repeat){
            if(event->key.keysym.sym==SDLK_ESCAPE || event->key.keysym.sym==SDLK_k)choice=0;
            if(event->key.keysym.sym==SDLK_u)choice=1;
            if(event->key.keysym.sym==SDLK_a)choice=2;
        }
        if(down && event->button.button==SDL_BUTTON_LEFT && y>=232 && y<256){
            if(x>=128 && x<242)choice=1;else if(x>=250 && x<364)choice=2;else if(x>=372 && x<486)choice=0;
        }
        if(choice>=0)mosaic_pattern_resolve(device,ui,c,choice);
        return down || up || motion || key || event->type==SDL_KEYUP || event->type==SDL_MOUSEWHEEL;
    }
    if(ui->mosaic_edit_choice) {
        int choice=-1;
        if(key && !event->key.repeat) {
            if(event->key.keysym.sym==SDLK_ESCAPE)choice=0;
            if(event->key.keysym.sym==SDLK_RETURN || event->key.keysym.sym==SDLK_n)choice=1;
            if(event->key.keysym.sym==SDLK_u)choice=2;
            if(event->key.keysym.sym==SDLK_a)choice=3;
            if(event->key.keysym.sym==SDLK_c)choice=4;
        }
        if(down && event->button.button==SDL_BUTTON_LEFT) {
            if(y>=220 && y<244) {
                if(x>=128 && x<242)choice=2;
                else if(x>=250 && x<364)choice=3;
                else if(x>=372 && x<486)choice=1;
            }
            if(y>=254 && y<278) {
                if(x>=154 && x<298)choice=4;
                else if(x>=316 && x<460)choice=0;
            }
        }
        if(choice>=0) {
            uint64_t owner=c->pending_id,revision=c->pending_revision,epoch=c->pending_epoch;
            int resolved=mosaic_resolve_edit(device,audio,ui,instrument,c,choice);
            if(resolved && (choice==2 || choice==3) && portal->source_event==owner && portal->source_event_epoch==epoch && portal->source_event_revision==revision) {
                TsMosaicEvent *e=ts_mosaic_find(ui->mosaic,owner);
                TsInstrument *doc=mosaic_document(c,ui,instrument,owner);
                /* Publishing the exact worker input on editor exit is not a
                   conflicting edit. Keep that job attached to its owner. */
                if(e && doc && portal->source_hash==ts_sample_hash(&doc->current))portal->source_event_revision=e->revision;
            }
            if(resolved && c->resume_exit) {
                c->resume_exit=0;
                if(choice && !c->active) {
                    ui->mosaic_open=0;SDL_PushEvent(&c->exit_event);
                }
            }
        }
        return down || up || motion || key || event->type==SDL_KEYUP || event->type==SDL_MOUSEWHEEL;
    }
    if(ui->mosaic_open && ui->mosaic_loop_open)
        return mosaic_loop_event(event,device,ui,c,x,y,mod);
    if(ui->mosaic_open && (ui->mosaic_scatter_open || ui->mosaic_spacing_confirm || ui->mosaic_align_open))
        return mosaic_arrange_event(event,window,device,ui,instrument,c,x,y,mod);
    /* Workspace keys are dispatched before panel input. */
    if(mosaic_toggle_key(event))return 0;
    if(ui->router_open || ui_dialog_open(ui) || ui->master_output_dragging)return 0;
    if(c->active) {
        if(!ui->show_recipes && !ui->show_ingredients)ui->show_keyboard=1;
        if((key && event->key.keysym.sym==SDLK_ESCAPE) ||
           (key && (mod&KMOD_CTRL) && (event->key.keysym.sym==SDLK_s || event->key.keysym.sym==SDLK_o)) ||
           (down && y>=4 && y<28 && ((x>=360 && x<398) ||
            (x>=TS_UI_NEW_X && x<TS_UI_NEW_X+TS_UI_NEW_W)))) {
            if(transform->worker){snprintf(ui->status,sizeof(ui->status),"WAIT FOR THE CURRENT EDIT TO FINISH");return 1;}
            if(!mosaic_leave(device,audio,ui,instrument,c)) {
                if(ui->mosaic_edit_choice && !(key && event->key.keysym.sym==SDLK_ESCAPE)) {
                    c->resume_exit=1;c->exit_event=*event;
                }
                return 1;
            }
            if(key && event->key.keysym.sym==SDLK_ESCAPE)return 1;
            ui->mosaic_open=0;return 0;
        }
        if(key && event->key.keysym.sym==SDLK_1 && !(mod&KMOD_CTRL))return 1;
        TsMosaicEvent *e=ts_mosaic_find(ui->mosaic,c->active);
        if(e && down && ui->show_keyboard && y>=313 && y<330 && x>=540 && x<630) {
            if(device)SDL_LockAudioDevice(device);
            ts_mosaic_checkpoint(ui->mosaic);e->looping=!e->looping;++e->revision;
            if(device)SDL_UnlockAudioDevice(device);return 1;
        }
        int note=down && event->button.button==SDL_BUTTON_LEFT && ui->show_keyboard?
            ts_ui_key_from_point_for_base(x,y,ts_ui_keyboard_base_note(ui)):-1;
        if(note>=0 && e) {
            note+=ts_ui_keyboard_base_note(ui);int at=0;
            for(;at<e->note_count && e->notes[at]!=note;++at);
            if(at==e->note_count && e->note_count==TS_MOSAIC_NOTES) {
                snprintf(ui->status,sizeof(ui->status),"FIVE NOTES PER EVENT - CLICK A SELECTED KEY TO REMOVE IT");return 1;
            }
            if(at<e->note_count && e->note_count==1)return 1;
            if(device)SDL_LockAudioDevice(device);
            ts_mosaic_checkpoint(ui->mosaic);
            if(at<e->note_count) {for(int n=at+1;n<e->note_count;++n)e->notes[n-1]=e->notes[n];--e->note_count;e->notes[e->note_count]=0;}
            else e->notes[e->note_count++]=note;
            ++e->revision;
            if(device)SDL_UnlockAudioDevice(device);
            snprintf(ui->status,sizeof(ui->status),"EVENT %llu: %d NOTE%s - EACH VOICE HAS ITS OWN LOOP CLOCK",
                (unsigned long long)e->id,e->note_count,e->note_count==1?"":"S");return 1;
        }
        return 0;
    }
    if(!ui->mosaic_open)return 0;
    /* The performance panel gives QWERTY an explicit owner. Use the same
       current/parent instrument, tuning, groups and routes as the Canvas;
       browser source selection continues to belong to card placement. */
    if(key && ui->mosaic_live_open && ui->mosaic_live_keys &&
       !(mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI))) {
        if(event->key.keysym.sym==SDLK_s && (mod&KMOD_SHIFT))return 0; /* Global sustain. */
        if(note_for_key(event->key.keysym.sym)>=0)
            return canvas_qwerty_event(event,device,audio,ui,instrument,rate);
        if(event->key.keysym.sym>=SDLK_F1 && event->key.keysym.sym<=SDLK_F8) {
            if(!event->key.repeat)set_keyboard_octave(device,audio,ui,(int)(event->key.keysym.sym-SDLK_F1));
            return 1;
        }
    }
    if(main_file_capture_event(event,window,audio,ui,sister,(uint32_t)rate))return 1;
    if(event->type==SDL_WINDOWEVENT && (event->window.event==SDL_WINDOWEVENT_FOCUS_LOST ||
       event->window.event==SDL_WINDOWEVENT_HIDDEN || event->window.event==SDL_WINDOWEVENT_MINIMIZED))mosaic_drag_end(ui,c);
    mosaic_sources_refresh(ui,instrument,c);
    if((key || event->type==SDL_KEYUP) && (event->key.keysym.sym==SDLK_LALT || event->key.keysym.sym==SDLK_RALT) &&
       (c->drag==2 || c->drag==6) && ui->mosaic_ghost_count>1) {
        if(c->drag_moved)mosaic_drag_motion(device,ui,c,c->drag_pointer_x,c->drag_pointer_y,(SDL_Keymod)event->key.keysym.mod);
        return 1;
    }
    if(mosaic_marker_event(event,device,ui,c,x,y,mod))return 1;
    if(mosaic_fades_event(event,device,ui,c,x,y,mod))return 1;
    if(mosaic_loop_event(event,device,ui,c,x,y,mod))return 1;
    if(mosaic_navigation_event(event,device,ui,c,x,y,mod))return 1;
    if(mosaic_sources_event(event,device,audio,ui,instrument,c,transform,x,y,mod))return 1;
    if(mosaic_arrange_event(event,window,device,ui,instrument,c,x,y,mod))return 1;
    if(mosaic_volume_event(event,device,ui,c,x,y))return 1;
    if(mosaic_controls_event(event,window,device,ui,c,x,y,mod))return 1;
    if(down && y<32) {
        if(x>=402 && x<494 && ui->mosaic_selected)
            (void)mosaic_enter(device,audio,ui,instrument,c,ui->mosaic_selected);
        if((x>=TS_UI_NEW_X && x<TS_UI_FT2_X+TS_UI_FT2_W) || (x>=398 && x<494))ui->mosaic_open=0;
        return 0; /* Existing global output, Sister, project and configuration controls. */
    }
    if(key) {
        SDL_Keycode modifier=event->key.keysym.sym;
        if(modifier==SDLK_LSHIFT || modifier==SDLK_RSHIFT || modifier==SDLK_LALT || modifier==SDLK_RALT ||
           modifier==SDLK_LCTRL || modifier==SDLK_RCTRL || modifier==SDLK_LGUI || modifier==SDLK_RGUI)return 1;
        if(event->key.repeat && event->key.keysym.sym!=SDLK_UP && event->key.keysym.sym!=SDLK_DOWN && event->key.keysym.sym!=SDLK_LEFT && event->key.keysym.sym!=SDLK_RIGHT)return 1;
        if(c->drag_moved && (c->drag==1 || c->drag==2 || c->drag==4 || c->drag==6) && event->key.keysym.sym==SDLK_ESCAPE) {
            if(device)SDL_LockAudioDevice(device);ts_mosaic_undo(ui->mosaic,0);if(device)SDL_UnlockAudioDevice(device);
            int resizing=c->drag==2 || c->drag==6;
            mosaic_drag_end(ui,c);if(!resizing)mosaic_select_only(ui,0);return 1;
        }
        mosaic_drag_end(ui,c);
        SDL_Keycode k=event->key.keysym.sym;
        if(k==SDLK_F11 || k==SDLK_TAB || k==SDLK_BACKQUOTE ||
           (k==SDLK_p && (mod&(KMOD_CTRL|KMOD_SHIFT))==(KMOD_CTRL|KMOD_SHIFT)))return 0;
        if(k==SDLK_u && !(mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI))){mosaic_pattern_notices(ui,instrument,c,1);return 1;}
        if(k==SDLK_SPACE){mosaic_transport(device,ui,0);return 1;}
        if(k==SDLK_ESCAPE){if(device)SDL_LockAudioDevice(device);ts_mosaic_audition(ui->mosaic,0);if(device)SDL_UnlockAudioDevice(device);mosaic_transport(device,ui,1);return 1;}
        if(k==SDLK_s && (mod&KMOD_CTRL))return 0; /* Save overlays the current workspace. */
        if(k==SDLK_f && (mod&(KMOD_CTRL|KMOD_SHIFT))==(KMOD_CTRL|KMOD_SHIFT)) {
            main_file_capture_toggle(audio,ui,sister,(uint32_t)rate);return 1;
        }
        if((k==SDLK_UP || k==SDLK_DOWN || k==SDLK_LEFT || k==SDLK_RIGHT) &&
           !(mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI))) {
            mosaic_nudge(device,ui,k,mod);return 1;
        }
        if(k==SDLK_a && !(mod&(KMOD_CTRL|KMOD_SHIFT|KMOD_ALT|KMOD_GUI)) &&
           !ts_mosaic_find(ui->mosaic,ui->mosaic_selected)) {
            mosaic_source_audition(device,ui,instrument,c);return 1;
        }
        if(device)SDL_LockAudioDevice(device);
        if(k==SDLK_a && !(mod&(KMOD_ALT|KMOD_GUI))) {
            if(mod&KMOD_CTRL) {
                for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(ui->mosaic->events[i].id) {
                    mosaic_select_add(ui,ui->mosaic->events[i].id);
                }
            } else if(mod&KMOD_SHIFT) {
                TsMosaicEvent *e=ts_mosaic_find(ui->mosaic,ui->mosaic_selected);
                if(e) {mosaic_select_only(ui,e->id);for(int i=0;i<TS_MOSAIC_EVENTS;++i)
                    if(ui->mosaic->events[i].id && ui->mosaic->events[i].source==e->source)mosaic_select_add(ui,ui->mosaic->events[i].id);}
            } else ts_mosaic_audition(ui->mosaic,ui->mosaic_selected);
        } else if(k==SDLK_z && !(mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI)))mosaic_zoom_selection(ui,c);
        else if((k==SDLK_UP || k==SDLK_DOWN || k==SDLK_LEFT || k==SDLK_RIGHT) && !(mod&(KMOD_CTRL|KMOD_GUI))) {
            if(mod&KMOD_ALT){if(!event->key.repeat)mosaic_align(ui,k);}
        } else if(k==SDLK_t && !(mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI)))mosaic_tidy(ui,0);
        else if(k==SDLK_c && (mod&KMOD_CTRL)) {
            TsMosaicEvent *e=ts_mosaic_find(ui->mosaic,ui->mosaic_selected);
            if(e) {
                if(c->clipboard_epoch==ui->mosaic->epoch && c->clipboard.source && c->clipboard.source->pins)--c->clipboard.source->pins;
                c->clipboard=*e;c->clipboard_epoch=ui->mosaic->epoch;++c->clipboard.source->pins;
            }
        }
        else if(k==SDLK_v && (mod&KMOD_CTRL)) {
            ts_mosaic_checkpoint(ui->mosaic);
            TsMosaicEvent *e=c->clipboard_epoch==ui->mosaic->epoch && c->clipboard.id?ts_mosaic_add(ui->mosaic,c->clipboard.source,ui->mosaic_time,ui->mosaic_xscroll):NULL;
            if(e) {
                uint64_t id=e->id;*e=c->clipboard;e->id=id;e->start=ui->mosaic_time;e->x=ui->mosaic_xscroll;
                ts_mosaic_space(ui->mosaic,e);mosaic_select_only(ui,e->id);
            }
        } else if(k==SDLK_DELETE || k==SDLK_BACKSPACE) {
            ts_mosaic_checkpoint(ui->mosaic);
            for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(mosaic_chosen(ui,i))ts_mosaic_delete(ui->mosaic,ui->mosaic->events[i].id);
            mosaic_select_only(ui,0);
        } else if((k==SDLK_m || k==SDLK_s) && !(mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI))) {
            int count=0,all=1;unsigned char chosen[TS_MOSAIC_EVENTS]={0};
            for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(mosaic_chosen(ui,i)) {
                chosen[i]=1;
                if(mod&KMOD_SHIFT)for(int j=0;j<TS_MOSAIC_EVENTS;++j)
                    if(ui->mosaic->events[j].id && ui->mosaic->events[j].source==ui->mosaic->events[i].source)chosen[j]=1;
            }
            for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(chosen[i]) {
                ++count;all&=k==SDLK_m?ui->mosaic->events[i].muted:ui->mosaic->events[i].solo;
            }
            if(count) {
                ts_mosaic_checkpoint(ui->mosaic);
                for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(chosen[i]) {
                    TsMosaicEvent *e=&ui->mosaic->events[i];if(k==SDLK_m)e->muted=!all;else e->solo=!all;
                }
                snprintf(ui->status,sizeof(ui->status),"%d EVENT%s: %s %s",count,count==1?"":"S",k==SDLK_m?"MUTE":"SOLO",all?"OFF":"ON");
            }
        } else if((k==SDLK_z || k==SDLK_y) && (mod&KMOD_CTRL))
            ts_mosaic_undo(ui->mosaic,k==SDLK_y || (mod&KMOD_SHIFT));
        else if(k==SDLK_HOME){ts_mosaic_seek(ui->mosaic,0);ui->mosaic_scroll=0;}
        if(device)SDL_UnlockAudioDevice(device);return 1;
    }
    if(event->type==SDL_KEYUP)return 1;
    if(event->type==SDL_MOUSEWHEEL) {
        if(c->drag)return 1;
        int mx,my;wheel_event_mouse(event,&mx,&my);logical_mouse(window,mx,my,&x,&y);
        if(x>=TS_MOSAIC_ENV_X && x<=TS_MOSAIC_ENV_RIGHT && y>=TS_MOSAIC_ENV_TOP && y<=TS_MOSAIC_ENV_BOTTOM)
            y=66+(int)lrint((ts_ui_mosaic_volume_time(ui,y)-ui->mosaic_scroll)*ui->mosaic_scale);
        double amount=wheel_event_delta(event,0);
        if(mod&KMOD_CTRL) {
            double anchor=ui->mosaic_scroll+fmax(0,y-66)/ui->mosaic_scale;
            double xanchor=ui->mosaic_xscroll+fmax(0,x-151)/horizontal;
            ui->mosaic_hscale=fmin(4,fmax(.02,horizontal*pow(1.15,amount)));
            ui->mosaic_xscroll=fmax(0,xanchor-fmax(0,x-151)/ui->mosaic_hscale);
            ui->mosaic_scale=fmin(1200,fmax(.003,ui->mosaic_scale*pow(1.15,amount)));
            ui->mosaic_scroll=fmax(0,anchor-fmax(0,y-66)/ui->mosaic_scale);
        } else if(mod&KMOD_SHIFT)ui->mosaic_xscroll=fmax(0,ui->mosaic_xscroll-amount*30/horizontal);
        else ui->mosaic_scroll=fmax(0,ui->mosaic_scroll-amount*36/ui->mosaic_scale);
        return 1;
    }
    if(down && event->button.button==SDL_BUTTON_LEFT) {
        if(x>=444 && x<538 && y>=381 && y<399) {
            int solos=0;for(int i=0;i<TS_MOSAIC_EVENTS;++i)solos+=ui->mosaic->events[i].id && ui->mosaic->events[i].solo;
            if(solos) {
                if(device)SDL_LockAudioDevice(device);ts_mosaic_checkpoint(ui->mosaic);
                for(int i=0;i<TS_MOSAIC_EVENTS;++i)ui->mosaic->events[i].solo=0;
                if(device)SDL_UnlockAudioDevice(device);
                snprintf(ui->status,sizeof(ui->status),"ALL SOLO SWITCHES CLEARED - MUTE SETTINGS RETAINED");
            }
            return 1;
        }
        /* Source tabs share this vertical band; only transport owns x >= 118. */
        if(x>=118 && y>=39 && y<58) {
            if(x>=118 && x<160)mosaic_transport(device,ui,0);
            else if(x>=164 && x<202)mosaic_transport(device,ui,1);
            else if(x>=206 && x<270)main_file_capture_toggle(audio,ui,sister,(uint32_t)rate);
            else if(x>=274 && x<318)mosaic_loop_toggle(device,ui);
            else if(x>=TS_MOSAIC_FOLLOW_X && x<TS_MOSAIC_FOLLOW_X+TS_MOSAIC_FOLLOW_W)ui->mosaic_follow=!ui->mosaic_follow;
            else if(x>=TS_MOSAIC_FIT_X && x<TS_MOSAIC_FIT_X+TS_MOSAIC_FIT_W)mosaic_fit_all(ui,c);
            else if(x>=TS_MOSAIC_ZOOM_OUT_X && x<TS_MOSAIC_ZOOM_OUT_X+TS_MOSAIC_ZOOM_W){ui->mosaic_scale=fmax(.003,ui->mosaic_scale/1.4);ui->mosaic_hscale=fmax(.02,horizontal/1.4);}
            else if(x>=TS_MOSAIC_ZOOM_IN_X && x<TS_MOSAIC_ZOOM_IN_X+TS_MOSAIC_ZOOM_W){ui->mosaic_scale=fmin(1200,ui->mosaic_scale*1.4);ui->mosaic_hscale=fmin(4,horizontal*1.4);}
            return 1;
        }
        if(x>=10 && x<108 && y>=37 && y<52){
            mosaic_drag_end(ui,c);mosaic_picks_clear(ui,c);ui->mosaic_pattern_tab=x>=55;c->pattern_ready=0;ui->mosaic_source_page=0;ui->mosaic_source_selected=-1;
            mosaic_sources_refresh(ui,instrument,c);
            mosaic_pattern_notices(ui,instrument,c,0);return 1;
        }
        if(x>=10 && x<108 && y>=TS_MOSAIC_SOURCE_PAGE_Y && y<TS_MOSAIC_SOURCE_PAGE_Y+18) {
            int pages=ui->mosaic_source_pages;
            if(x<35 && ui->mosaic_source_page>0)--ui->mosaic_source_page;
            if(x>=84 && ui->mosaic_source_page+1<pages)++ui->mosaic_source_page;
            mosaic_sources_refresh(ui,instrument,c);
            mosaic_pattern_notices(ui,instrument,c,0);return 1;
        }
        if(x>=113 && x<151 && y>=66 && y<TS_MOSAIC_CANVAS_BOTTOM) {
            if(device)SDL_LockAudioDevice(device);ts_mosaic_seek(ui->mosaic,ui->mosaic_scroll+(y-66)/ui->mosaic_scale);
            if(device)SDL_UnlockAudioDevice(device);return 1;
        }
        if(x>=151 && x<TS_MOSAIC_CANVAS_RIGHT && y>=66 && y<TS_MOSAIC_CANVAS_BOTTOM) {
            double time=ui->mosaic_scroll+(y-66)/ui->mosaic_scale,left=(x-151)/horizontal+ui->mosaic_xscroll;
            TsMosaicEvent *primary=ts_mosaic_find(ui->mosaic,ui->mosaic_selected);
            for(int pass=TS_MOSAIC_EVENTS;pass>=0;--pass) {
                int i=pass==TS_MOSAIC_EVENTS?(primary?(int)(primary-ui->mosaic->events):-1):pass;
                if(i<0 || (pass!=TS_MOSAIC_EVENTS && primary==&ui->mosaic->events[i]))continue;
                TsMosaicEvent *e=&ui->mosaic->events[i];
                if(!e->id || time<e->start || time>=e->start+e->duration || left<e->x || left>=e->x+e->width)continue;
                int chosen=mosaic_chosen(ui,i);
                if(mod&KMOD_SHIFT)mosaic_select_add(ui,e->id);
                else if(!chosen)mosaic_select_only(ui,e->id);
                mosaic_picks_clear(ui,c);
                mosaic_source_select_event(ui,e);
                /* SDL counts rapid modified presses as double/triple clicks
                   too. Selection and copy gestures must keep owning them. */
                if(event->button.clicks>=2 && !(mod&(KMOD_SHIFT|KMOD_CTRL|KMOD_ALT|KMOD_GUI))) {
                    mosaic_enter(device,audio,ui,instrument,c,e->id);return 1;
                }
                mosaic_drag_end(ui,c);ui->mosaic_ghost_count=0;
                c->shift_toggle=(mod&KMOD_SHIFT) && chosen?e->id:0;
                ui->mosaic_ghost_count=mosaic_selected_events(ui,ui->mosaic_ghosts);
                c->drag_copy=(mod&KMOD_SHIFT)!=0;
                c->drag=c->drag_copy?1:mosaic_card_drag_part(ui,e,time,left);
                if(c->drag==4 && ui->mosaic_ghost_count>1)c->drag=1;
                c->drag_event=e->id;
                c->drag_x=x;c->drag_y=y;c->drag_start=e->start;c->drag_duration=e->duration;c->drag_left=e->x;c->drag_width=e->width;
                c->drag_xscroll=ui->mosaic_xscroll;c->drag_pointer_x=x;c->drag_pointer_y=y;
                c->drag_scroll_ms=SDL_GetTicks();
                return 1;
            }
            mosaic_picks_clear(ui,c);
            if(mod&KMOD_SHIFT) {
                c->box_primary=ui->mosaic_selected;
                memcpy(c->box_selection,ui->mosaic_selection,sizeof(c->box_selection));
                memcpy(c->box_order,ui->mosaic_selection_order,sizeof(c->box_order));
                c->drag=5;ui->mosaic_box=1;ui->mosaic_box_x=ui->mosaic_box_end_x=x;
                ui->mosaic_box_y=ui->mosaic_box_end_y=y;
            } else mosaic_select_only(ui,0);
        }
        return 1;
    }
    if(motion && c->drag) {
        if(c->drag==5) {
            ui->mosaic_box_end_x=x<151?151:x>=TS_MOSAIC_CANVAS_RIGHT?TS_MOSAIC_CANVAS_RIGHT-1:x;ui->mosaic_box_end_y=y<66?66:y>(TS_MOSAIC_CANVAS_BOTTOM-1)?(TS_MOSAIC_CANVAS_BOTTOM-1):y;
            double left=ui->mosaic_xscroll+(fmin(ui->mosaic_box_x,ui->mosaic_box_end_x)-151)/horizontal;
            double right=ui->mosaic_xscroll+(fmax(ui->mosaic_box_x,ui->mosaic_box_end_x)-151)/horizontal;
            double top=ui->mosaic_scroll+(fmin(ui->mosaic_box_y,ui->mosaic_box_end_y)-66)/ui->mosaic_scale;
            double bottom=ui->mosaic_scroll+(fmax(ui->mosaic_box_y,ui->mosaic_box_end_y)-66)/ui->mosaic_scale;
            uint64_t previous=ui->mosaic_selected;
            ui->mosaic_selected=c->box_primary;
            memcpy(ui->mosaic_selection,c->box_selection,sizeof(ui->mosaic_selection));
            memcpy(ui->mosaic_selection_order,c->box_order,sizeof(ui->mosaic_selection_order));
            for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
                TsMosaicEvent *e=&ui->mosaic->events[i];
                if(e->id && e->x<right && e->x+e->width>left && e->start<bottom && e->start+e->duration>top) {
                    mosaic_select_add(ui,e->id);
                }
            }
            TsMosaicEvent *prior=ts_mosaic_find(ui->mosaic,previous);
            if(!c->box_primary && prior && mosaic_chosen(ui,(int)(prior-ui->mosaic->events)))ui->mosaic_selected=previous;
            return 1;
        }
        c->drag_pointer_x=x;c->drag_pointer_y=y;
        mosaic_drag_motion(device,ui,c,x,y,mod);return 1;
    }
    if(up && event->button.button==SDL_BUTTON_LEFT) {
        if(c->shift_toggle && !c->drag_moved)mosaic_select_remove(ui,c->shift_toggle);
        mosaic_drag_end(ui,c);ts_mosaic_collect(ui->mosaic);mosaic_sources_refresh(ui,instrument,c);return 1;
    }
    if(motion && !c->drag){
        const TsMosaicSource *source=NULL;
        if(x>=10 && x<108 && y>=74 && y<346){int col=(x-10)/49,row=(y-74)/34;
            int n=ui->mosaic_source_offset+row*2+col;if(col<2 && n<ui->mosaic_source_count)source=ui->mosaic_sources[n].source;
        }else if(x>=151 && x<TS_MOSAIC_CANVAS_RIGHT && y>=66 && y<TS_MOSAIC_CANVAS_BOTTOM){
            double t=ui->mosaic_scroll+(y-66)/ui->mosaic_scale,left=ui->mosaic_xscroll+(x-151)/horizontal;
            for(int i=TS_MOSAIC_EVENTS-1;i>=0;--i){const TsMosaicEvent *e=&ui->mosaic->events[i];
                if(e->id && t>=e->start && t<e->start+e->duration && left>=e->x && left<e->x+e->width){source=e->source;break;}}
        }
        if(source && source->pattern){const TsPatternCard *p=source->pattern;
            int i=mosaic_pattern_index(c,p->id);const TsMosaicSource *latest=i>=0?c->pattern_sources[i]:NULL;
            if(latest && mosaic_pattern_matches(p,latest->pattern))snprintf(ui->status,sizeof(ui->status),"P%03u V%u / SOURCE V%u / U: UPDATE / DOUBLE-CLICK: LATEST SOURCE",p->id,p->version,latest->pattern->version);
            else snprintf(ui->status,sizeof(ui->status),"P%03u V%u / SOURCE UNAVAILABLE / SNAPSHOT STILL PLAYS",p->id,p->version);
        }
    }
    return motion || down;
}
