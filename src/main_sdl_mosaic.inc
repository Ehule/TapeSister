static void mosaic_poll(SDL_AudioDeviceID device,TsUiState *ui,
                          TsInstrument *instrument,MosaicController *c)
{
    if(!ui->mosaic)return;
    if(device)SDL_LockAudioDevice(device);
    ui->mosaic_time=ui->mosaic->time;ui->mosaic_playing=ui->mosaic->playing;
    if(device)SDL_UnlockAudioDevice(device);
    if(ui->mosaic_open && !c->active) {
        mosaic_sources_refresh(ui,instrument,c);
        if(ui->mosaic_follow && ui->mosaic_playing && !c->drag) {
            double offset=(ui->mosaic_time-ui->mosaic_scroll)*ui->mosaic_scale;
            if(offset>220 || offset<0)ui->mosaic_scroll=fmax(0,ui->mosaic_time-220/ui->mosaic_scale);
        }
    }
    if(c->active) {
        if(!ui->show_recipes && !ui->show_ingredients)ui->show_keyboard=1;
        TsMosaicEvent *e=ts_mosaic_find(ui->mosaic,c->active);ui->staged_notes=0;
        if(e)for(int n=0;n<e->note_count;++n) {
            int k=e->notes[n]-ts_ui_keyboard_base_note(ui);if(k>=0 && k<24)ui->staged_notes|=1u<<k;
        }
        (void)instrument;
    }
}
static void mosaic_transport(SDL_AudioDeviceID device,TsUiState *ui,int stop)
{
    if(device)SDL_LockAudioDevice(device);
    if(stop){ui->mosaic->playing=0;ts_mosaic_seek(ui->mosaic,0);ui->mosaic_scroll=0;}
    else {
        if(ui->mosaic->time>=ts_mosaic_end(ui->mosaic))ts_mosaic_seek(ui->mosaic,0);
        ui->mosaic->playing=!ui->mosaic->playing;
    }
    if(device)SDL_UnlockAudioDevice(device);
}
/* Move a selection as a rigid shape; spacing only shifts the whole group
   horizontally, preserving every relative start time and gap. */
static void mosaic_move_group(TsUiState *ui,double dx,double dt,int snap)
{
    TsMosaic *m=ui->mosaic;double minx=1e30,mint=1e30,maxt=0;
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
            for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
                const TsMosaicEvent *e=&m->events[i];if(!e->id || mosaic_chosen(ui,i))continue;
                for(int a=0;a<3;++a) {
                    double d=e->start+e->duration*.5*a-t;
                    if(fabs(d)<best){best=fabs(d);adjust=d;}
                }
            }
        }
        dt=fmin(86400-maxt,fmax(-mint,dt+adjust));
    }
    for(int pass=0;pass<TS_MOSAIC_EVENTS;++pass) {
        double shift=0;
        for(int j=0;j<ui->mosaic_ghost_count;++j) {
            const TsMosaicEvent *o=&ui->mosaic_ghosts[j];double t=o->start+dt,x=o->x+dx;
            for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
                const TsMosaicEvent *b=&m->events[i];if(!b->id || mosaic_chosen(ui,i))continue;
                if(t>=b->start+b->duration-1e-9 || b->start>=t+o->duration-1e-9)continue;
                if(x<b->x+b->width+TS_MOSAIC_GUTTER && b->x<x+o->width+TS_MOSAIC_GUTTER)
                    shift=fmax(shift,b->x+b->width+TS_MOSAIC_GUTTER-x);
            }
        }
        if(shift<=0)break;dx+=shift;
    }
    for(int j=0;j<ui->mosaic_ghost_count;++j) {
        const TsMosaicEvent *o=&ui->mosaic_ghosts[j];TsMosaicEvent *e=ts_mosaic_find(m,o->id);
        if(e){e->start=o->start+dt;e->x=o->x+dx;}
    }
}
static void mosaic_source_drag(TsUiState *ui,const TsInstrument *instrument,MosaicController *c,int n)
{
    if(n<0 || n>=ui->mosaic_source_count)return;
    char error[160];TsMosaicEvent *e=ts_mosaic_find(ui->mosaic,ui->mosaic_sources[n].event);
    if(e)c->source_drag=*e;
    else {
        int slot=ui->mosaic_sources[n].bank_slot;
        const TsSample *sample=ui->mosaic_sources[n].sample;
        TsMosaicSource *source=ts_mosaic_source(ui->mosaic,sample,error,sizeof(error));
        if(!source)return;
        const TsBankSlot *b=&instrument->bank[slot];int current=slot==instrument->selected_slot;
        TsMosaicEvent *d=&c->source_drag;*d=(TsMosaicEvent){.source=source,.width=86,.looping=1,.note_count=1,.notes={60},.gain=1};
        d->tuning=current?instrument->tuning:b->tuning;
        int loop=current?instrument->has_loop:b->has_loop;
        d->first=loop?(current?instrument->loop_first:b->loop_first):0;
        d->last=loop?(current?instrument->loop_last:b->loop_last):sample->frames;
        d->mode=current?instrument->loop_mode:b->loop_mode;
        float fade=current?instrument->loop_crossfade_ms:b->loop_crossfade_ms;
        d->crossfade=loop?(size_t)llround(fade*sample->sample_rate/1000.0):0;
        if(d->crossfade>(d->last-d->first)/2)d->crossfade=(d->last-d->first)/2;
        snprintf(d->name,sizeof(d->name),"%.63s",sample->name);
    }
    c->source_drag.duration=(double)(c->source_drag.last-c->source_drag.first)/c->source_drag.source->sample.sample_rate;
    c->source_drag.muted=c->source_drag.solo=0;
    ++c->source_drag.source->pins;c->drag_epoch=ui->mosaic->epoch;c->drag=3;c->drag_slot=n;
    ui->mosaic_source_selected=n;
}

static int mosaic_event(const SDL_Event *event,SDL_Window *window,SDL_AudioDeviceID device,
    AudioState *audio,TsUiState *ui,TsInstrument *instrument,MosaicController *c,
    PortalController *portal,SisterWindow *sister,TransformController *transform,int rate)
{
    if(!ui->mosaic)return 0;
    double horizontal=ui->mosaic_hscale>0?ui->mosaic_hscale:1;
    int down=event->type==SDL_MOUSEBUTTONDOWN,up=event->type==SDL_MOUSEBUTTONUP;
    int motion=event->type==SDL_MOUSEMOTION,key=event->type==SDL_KEYDOWN;
    int x=0,y=0;
    if(down || up)logical_mouse(window,event->button.x,event->button.y,&x,&y);
    if(motion)logical_mouse(window,event->motion.x,event->motion.y,&x,&y);
    SDL_Keymod mod=key?(SDL_Keymod)event->key.keysym.mod:SDL_GetModState();
    if(ui->mosaic_edit_choice) {
        int choice=-1;
        if(key && !event->key.repeat) {
            if(event->key.keysym.sym==SDLK_ESCAPE)choice=0;
            if(event->key.keysym.sym==SDLK_RETURN || event->key.keysym.sym==SDLK_n)choice=1;
            if(event->key.keysym.sym==SDLK_u)choice=2;
        }
        if(down && event->button.button==SDL_BUTTON_LEFT && y>=235 && y<259) {
            if(x>=148 && x<254)choice=1;
            else if(x>=264 && x<380)choice=2;
            else if(x>=390 && x<492)choice=0;
        }
        if(choice>=0) {
            uint64_t owner=c->pending_id,revision=c->pending_revision,epoch=c->pending_epoch;
            int resolved=mosaic_resolve_edit(device,audio,ui,instrument,c,choice);
            if(resolved && choice==2 && portal->source_event==owner && portal->source_event_epoch==epoch && portal->source_event_revision==revision) {
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
    int toggle=(key && event->key.keysym.sym==SDLK_m && (mod&KMOD_CTRL) && !(mod&KMOD_SHIFT)) ||
        (down && event->button.button==SDL_BUTTON_LEFT && y>=4 && y<28 && x>=300 && x<356 && !ui->portal.open);
    if(toggle && (!ui_dialog_open(ui) || ui->portal.open)) {
        if(key && event->key.repeat)return 1;
        if(ui->external_record_bank || ui->capture_state!=TS_CAPTURE_IDLE || transform->worker || portal_edit_gesture_active(ui)) {
            snprintf(ui->status,sizeof(ui->status),"FINISH CAPTURE OR EDIT BEFORE OPENING MOSAIC");return 1;
        }
        if(ui->portal.open)portal_close(device,audio,ui,portal);
        if(c->active){if(!mosaic_leave(device,audio,ui,instrument,c))return 1;}
        else {ui->mosaic_open=!ui->mosaic_open;if(ui->mosaic_open)stop_all_force(device,audio,ui);}
        mosaic_drag_end(ui,c);
        if(ui->mosaic_open)snprintf(ui->status,sizeof(ui->status),"MOSAIC - DRAG A SOURCE INTO THE CANVAS; DOUBLE CLICK AN EVENT TO EDIT");
        return 1;
    }
    if(ui_dialog_open(ui) || ui->master_output_dragging)return 0;
    if(c->active) {
        if(!ui->show_recipes && !ui->show_ingredients)ui->show_keyboard=1;
        if((key && event->key.keysym.sym==SDLK_ESCAPE) ||
           (key && (mod&KMOD_CTRL) && (event->key.keysym.sym==SDLK_s || event->key.keysym.sym==SDLK_o)) ||
           (down && y>=4 && y<28 && x>=360 && x<398)) {
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
    if(event->type==SDL_WINDOWEVENT && event->window.event==SDL_WINDOWEVENT_FOCUS_LOST)mosaic_drag_end(ui,c);
    mosaic_sources_refresh(ui,instrument,c);
    if(down && y<32) {
        if(x>=402 && x<494 && ui->mosaic_selected)
            (void)mosaic_enter(device,audio,ui,instrument,c,ui->mosaic_selected);
        if((x>=214 && x<296) || (x>=360 && x<494))ui->mosaic_open=0;
        return 0; /* Existing global output, Sister, project and configuration controls. */
    }
    if(key) {
        if(event->key.repeat)return 1;
        mosaic_drag_end(ui,c);
        SDL_Keycode k=event->key.keysym.sym;
        if(k==SDLK_F11 || k==SDLK_TAB)return 0;
        if(k==SDLK_SPACE){mosaic_transport(device,ui,0);return 1;}
        if(k==SDLK_ESCAPE){mosaic_transport(device,ui,1);return 1;}
        if(k==SDLK_s && (mod&KMOD_CTRL)){ui->mosaic_open=0;return 0;}
        if(k==SDLK_f && (mod&(KMOD_CTRL|KMOD_SHIFT))==(KMOD_CTRL|KMOD_SHIFT)) {
            main_file_capture_toggle(audio,ui,sister,(uint32_t)rate);return 1;
        }
        if(device)SDL_LockAudioDevice(device);
        if(k==SDLK_c && (mod&KMOD_CTRL)) {
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
            int count=0,all=1;
            for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(mosaic_chosen(ui,i)) {
                ++count;all&=k==SDLK_m?ui->mosaic->events[i].muted:ui->mosaic->events[i].solo;
            }
            if(count) {
                ts_mosaic_checkpoint(ui->mosaic);
                for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(mosaic_chosen(ui,i)) {
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
        int mx,my;SDL_GetMouseState(&mx,&my);logical_mouse(window,mx,my,&x,&y);
        double amount=event->wheel.y;
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
    if(down && event->button.button==SDL_BUTTON_MIDDLE && x>=113 && x<631 && y>=66 && y<376) {
        if(device)SDL_LockAudioDevice(device);ts_mosaic_seek(ui->mosaic,ui->mosaic_scroll+(y-66)/ui->mosaic_scale);
        if(device)SDL_UnlockAudioDevice(device);return 1;
    }
    if(down && event->button.button==SDL_BUTTON_LEFT) {
        if(y>=39 && y<58) {
            if(x>=118 && x<160)mosaic_transport(device,ui,0);
            else if(x>=164 && x<202)mosaic_transport(device,ui,1);
            else if(x>=206 && x<270)main_file_capture_toggle(audio,ui,sister,(uint32_t)rate);
            else if(x>=274 && x<318) {if(device)SDL_LockAudioDevice(device);ui->mosaic->repeat=!ui->mosaic->repeat;if(device)SDL_UnlockAudioDevice(device);}
            else if(x>=406 && x<480)ui->mosaic_follow=!ui->mosaic_follow;
            else if(x>=490 && x<542){ui->mosaic_scale=fmin(1200,300/fmax(.25,ts_mosaic_end(ui->mosaic)));ui->mosaic_scroll=0;ui->mosaic_xscroll=0;
                double right=0;for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(ui->mosaic->events[i].id)right=fmax(right,ui->mosaic->events[i].x+ui->mosaic->events[i].width);
                ui->mosaic_hscale=fmin(1,480/fmax(1,right));}
            else if(x>=548 && x<572){ui->mosaic_scale=fmax(.003,ui->mosaic_scale/1.4);ui->mosaic_hscale=fmax(.02,horizontal/1.4);}
            else if(x>=578 && x<604){ui->mosaic_scale=fmin(1200,ui->mosaic_scale*1.4);ui->mosaic_hscale=fmin(4,horizontal*1.4);}
            return 1;
        }
        if(x>=10 && x<108 && y>=74 && y<346) {
            int n=ui->mosaic_source_page*16+((y-74)/34)*2+(x-10)/49;
            mosaic_drag_end(ui,c);mosaic_source_drag(ui,instrument,c,n);c->drag_x=x;c->drag_y=y;return 1;
        }
        if(x>=10 && x<108 && y>=351 && y<373) {
            int pages=(ui->mosaic_source_count+15)/16;
            if(x<35 && ui->mosaic_source_page>0)--ui->mosaic_source_page;
            if(x>=84 && ui->mosaic_source_page+1<pages)++ui->mosaic_source_page;
            return 1;
        }
        if(x>=113 && x<151 && y>=66 && y<376) {
            if(device)SDL_LockAudioDevice(device);ts_mosaic_seek(ui->mosaic,ui->mosaic_scroll+(y-66)/ui->mosaic_scale);
            if(device)SDL_UnlockAudioDevice(device);return 1;
        }
        if(x>=151 && x<631 && y>=66 && y<376) {
            double time=ui->mosaic_scroll+(y-66)/ui->mosaic_scale,left=(x-151)/horizontal+ui->mosaic_xscroll;
            for(int i=TS_MOSAIC_EVENTS-1;i>=0;--i) {
                TsMosaicEvent *e=&ui->mosaic->events[i];
                if(!e->id || time<e->start || time>=e->start+e->duration || left<e->x || left>=e->x+e->width)continue;
                if(!mosaic_chosen(ui,i))mosaic_select_only(ui,e->id);else ui->mosaic_selected=e->id;
                mosaic_source_select_event(ui,e);
                if(event->button.clicks>=2){mosaic_enter(device,audio,ui,instrument,c,e->id);return 1;}
                if(device)SDL_LockAudioDevice(device);ts_mosaic_checkpoint(ui->mosaic);if(device)SDL_UnlockAudioDevice(device);
                ui->mosaic_ghost_count=0;
                for(int n=0;n<TS_MOSAIC_EVENTS;++n)if(mosaic_chosen(ui,n))ui->mosaic_ghosts[ui->mosaic_ghost_count++]=ui->mosaic->events[n];
                c->drag=ui->mosaic_ghost_count>1?1:e->start+e->duration-time<6/ui->mosaic_scale?2:e->x+e->width-left<5/horizontal?4:1;
                c->drag_x=x;c->drag_y=y;c->drag_start=e->start;c->drag_duration=e->duration;c->drag_left=e->x;c->drag_width=e->width;
                return 1;
            }
            mosaic_select_only(ui,0);
            if(mod&KMOD_SHIFT) {
                c->drag=5;ui->mosaic_box=1;ui->mosaic_box_x=ui->mosaic_box_end_x=x;
                ui->mosaic_box_y=ui->mosaic_box_end_y=y;
            }
        }
        return 1;
    }
    if(motion && c->drag) {
        c->drag_moved=1;
        if(c->drag==5) {
            ui->mosaic_box_end_x=x<151?151:x>630?630:x;ui->mosaic_box_end_y=y<66?66:y>375?375:y;
            double left=ui->mosaic_xscroll+(fmin(ui->mosaic_box_x,ui->mosaic_box_end_x)-151)/horizontal;
            double right=ui->mosaic_xscroll+(fmax(ui->mosaic_box_x,ui->mosaic_box_end_x)-151)/horizontal;
            double top=ui->mosaic_scroll+(fmin(ui->mosaic_box_y,ui->mosaic_box_end_y)-66)/ui->mosaic_scale;
            double bottom=ui->mosaic_scroll+(fmax(ui->mosaic_box_y,ui->mosaic_box_end_y)-66)/ui->mosaic_scale;
            mosaic_select_only(ui,0);
            for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
                TsMosaicEvent *e=&ui->mosaic->events[i];
                if(e->id && e->x<right && e->x+e->width>left && e->start<bottom && e->start+e->duration>top) {
                    ui->mosaic_selection[i]=e->id;if(!ui->mosaic_selected)ui->mosaic_selected=e->id;
                }
            }
            return 1;
        }
        if(c->drag==3) {
            ui->mosaic_ghost_count=0;
            if(x>=151 && x<631 && y>=66 && y<376) {
                ui->mosaic_ghosts[0]=c->source_drag;ui->mosaic_ghost_count=1;
                ui->mosaic_ghosts[0].x=(x-151)/horizontal+ui->mosaic_xscroll;
                ui->mosaic_ghosts[0].start=ui->mosaic_scroll+(y-66)/ui->mosaic_scale;
                ts_mosaic_space(ui->mosaic,&ui->mosaic_ghosts[0]);
            }
            return 1;
        }
        TsMosaicEvent *e=ts_mosaic_find(ui->mosaic,ui->mosaic_selected);if(!e)return 1;
        double delta=(y-c->drag_y)/ui->mosaic_scale;
        if(device)SDL_LockAudioDevice(device);
        if(c->drag==1)mosaic_move_group(ui,(x-c->drag_x)/horizontal,delta,(mod&KMOD_SHIFT)!=0);
        else {
            if(c->drag==2) {
                double end=c->drag_start+c->drag_duration+delta;
                if(mod&KMOD_SHIFT)end=ts_mosaic_snap(ui->mosaic,e->id,end,7/ui->mosaic_scale);
                e->duration=fmin(86400,fmax(.005,end-e->start));
            } else e->width=fmin(460,fmax(36,c->drag_width+(x-c->drag_x)/horizontal));
            ts_mosaic_space(ui->mosaic,e);
        }
        if(device)SDL_UnlockAudioDevice(device);return 1;
    }
    if(up && event->button.button==SDL_BUTTON_LEFT) {
        if(c->drag==3 && x>=151 && x<631 && y>=66 && y<376 && c->source_drag.source) {
            if(device)SDL_LockAudioDevice(device);ts_mosaic_checkpoint(ui->mosaic);
            double start=ui->mosaic_scroll+(y-66)/ui->mosaic_scale,left=(x-151)/horizontal+ui->mosaic_xscroll;
            TsMosaicEvent *e=ts_mosaic_add(ui->mosaic,c->source_drag.source,start,left);
            if(e) {
                uint64_t id=e->id;*e=c->source_drag;e->id=id;e->revision=1;e->start=start;e->x=left;
                ts_mosaic_space(ui->mosaic,e);mosaic_select_only(ui,e->id);
            } else snprintf(ui->status,sizeof(ui->status),"MOSAIC IS FULL (128 EVENTS)");
            if(device)SDL_UnlockAudioDevice(device);
        }
        mosaic_drag_end(ui,c);ts_mosaic_collect(ui->mosaic);mosaic_sources_refresh(ui,instrument,c);return 1;
    }
    return motion || down;
}
