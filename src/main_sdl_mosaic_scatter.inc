/* Independent seeds keep position and note rolls reproducible through preview,
   Apply and undo. All note planning and spacing happen before the audio lock. */
static double mosaic_scatter_random(uint32_t *seed)
{
    *seed=*seed*1664525u+1013904223u;return (double)(*seed>>8)/16777216.0;
}
static void mosaic_scatter_preview(TsUiState *ui,MosaicController *c)
{
    ui->mosaic_ghost_count=0;ui->mosaic_scatter_valid=0;ui->mosaic_scatter_notes[0]=0;
    if(!c->scatter_pool_count)return;
    int count=ui->mosaic_scatter_copies?ui->mosaic_scatter_count:c->scatter_pool_count,free_count=0;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)free_count+=!ui->mosaic->events[i].id;
    if(count<1 || count>TS_MOSAIC_EVENTS || (ui->mosaic_scatter_copies && count>free_count)) {
        snprintf(ui->mosaic_scatter_status,sizeof(ui->mosaic_scatter_status),"NOT ENOUGH SPACE: %d FREE CARDS",free_count);return;
    }
    TsMosaicPitchSettings *pitch=&ui->mosaic_scatter_pitch;
    int choices[128],intervals[TS_MOSAIC_NOTES]={0},voices=0,choice_count=0,bag[128];
    if(pitch->mode!=TS_MOSAIC_PITCH_KEEP) {
        choice_count=ts_mosaic_pitch_choices(pitch,c->scatter_pool[0].notes,c->scatter_pool[0].note_count,choices,intervals,&voices);
        if(!choice_count) {
            snprintf(ui->mosaic_scatter_status,sizeof(ui->mosaic_scatter_status),"NO NOTES FIT / WIDEN THE OCTAVE RANGE");return;
        }
    }
    uint32_t seed=c->scatter_seed,note_seed=c->scatter_note_seed;
    double clusters[3];for(int i=0;i<3;++i)clusters[i]=mosaic_scatter_random(&seed);
    double span=ui->mosaic_scatter_end-ui->mosaic_scatter_start;
    int keep=ui->mosaic_scatter_keep_positions && !c->scatter_from_sources;
    for(int i=0;i<count;++i) {
        TsMosaicEvent *e=&ui->mosaic_ghosts[i];*e=c->scatter_pool[i%c->scatter_pool_count];
        if(!keep) {
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
                if(shift<=0)break;
                if(e->x+shift>TS_MOSAIC_LAYOUT_LIMIT) {
                    snprintf(ui->mosaic_scatter_status,sizeof(ui->mosaic_scatter_status),"LAYOUT LIMIT / ENABLE OVERLAP OR REDUCE COUNT");return;
                }
                e->x+=shift;
            }
        }
        if(choice_count) {
            int pick=i%choice_count;
            if(pitch->order==TS_MOSAIC_PITCH_RANDOM)pick=(int)(mosaic_scatter_random(&note_seed)*choice_count);
            else if(pitch->order==TS_MOSAIC_PITCH_DESCENDING)pick=choice_count-1-pick;
            else if(pitch->order==TS_MOSAIC_PITCH_SHUFFLED) {
                if(!pick) {
                    for(int j=0;j<choice_count;++j)bag[j]=j;
                    for(int j=choice_count-1;j>0;--j) {
                        int k=(int)(mosaic_scatter_random(&note_seed)*(j+1)),swap=bag[j];bag[j]=bag[k];bag[k]=swap;
                    }
                }
                pick=bag[pick];
            }
            memset(e->notes,0,sizeof(e->notes));e->note_count=voices;
            for(int n=0;n<voices;++n)e->notes[n]=choices[pick]+intervals[n];
        }
    }
    ui->mosaic_ghost_count=count;ui->mosaic_scatter_valid=1;
    if(choice_count) {
        snprintf(ui->mosaic_scatter_notes,sizeof(ui->mosaic_scatter_notes),"%s",pitch->mode==TS_MOSAIC_PITCH_CHORD?"FIRST CHORD:":"FIRST NOTES:");
        int shown=pitch->mode==TS_MOSAIC_PITCH_CHORD?voices:(count<9?count:9);
        for(int n=0;n<shown;++n) {
            char name[12];size_t used=strlen(ui->mosaic_scatter_notes);
            int note=pitch->mode==TS_MOSAIC_PITCH_CHORD?ui->mosaic_ghosts[0].notes[n]:ui->mosaic_ghosts[n].notes[0];
            snprintf(ui->mosaic_scatter_notes+used,sizeof(ui->mosaic_scatter_notes)-used," %s",ts_midi_note_name(note,name,sizeof(name)));
        }
    } else snprintf(ui->mosaic_scatter_notes,sizeof(ui->mosaic_scatter_notes),"EXISTING NOTES / CHORDS PRESERVED");
    if(c->scatter_pool[0].source->pattern)snprintf(ui->mosaic_scatter_notes,sizeof(ui->mosaic_scatter_notes),pitch->mode==TS_MOSAIC_PITCH_KEEP?"PATTERN NOTES / INSTANCE TRANSPOSE PRESERVED":"PATTERN TRANSPOSE: C4 = ORIGINAL, C5 = +12");
    int notes=0;for(int i=0;i<count;++i)notes+=ui->mosaic_ghosts[i].note_count;
    snprintf(ui->mosaic_scatter_status,sizeof(ui->mosaic_scatter_status),"%d CARDS / %d ASSIGNED NOTES / PREVIEW",count,notes);
}
static void mosaic_scatter_open(TsUiState *ui,MosaicController *c)
{
    mosaic_drag_end(ui,c);c->scatter_pool_count=0;c->scatter_from_sources=c->picked_count>0;
    if(c->scatter_from_sources) {
        c->scatter_pool_count=c->picked_count;
        memcpy(c->scatter_pool,c->picked_sources,c->picked_count*sizeof(c->scatter_pool[0]));
    } else c->scatter_pool_count=mosaic_selected_events(ui,c->scatter_pool);
    if(!c->scatter_pool_count) {
        snprintf(ui->status,sizeof(ui->status),"SELECT CARDS OR SHIFT-CLICK SOURCES, THEN CTRL+R");return;
    }
    c->scatter_epoch=ui->mosaic->epoch;
    c->scatter_seed=(uint32_t)SDL_GetTicks()^0x9e3779b9u;c->scatter_note_seed=c->scatter_seed^0x85ebca6bu;
    ui->mosaic_follow=0;
    ui->mosaic_scatter_open=1;ui->mosaic_scatter_copies=c->scatter_from_sources;
    ui->mosaic_scatter_start=ui->mosaic_scroll;
    ui->mosaic_scatter_end=fmin(86400,ui->mosaic_scroll+310/ui->mosaic_scale);
    ui->mosaic_scatter_count=c->scatter_pool_count;
    ui->mosaic_scatter_overlap=!ui->mosaic->auto_space;ui->mosaic_scatter_field=0;
    ui->mosaic_scatter_keep_positions=0;
    int base=c->scatter_pool[0].notes[0];
    for(int n=1;n<c->scatter_pool[0].note_count;++n)if(c->scatter_pool[0].notes[n]<base)base=c->scatter_pool[0].notes[n];
    ui->mosaic_scatter_pitch=(TsMosaicPitchSettings){.mode=TS_MOSAIC_PITCH_KEEP,.root=base%12,
        .scale=TS_FM_PITCH_SCALE_MAJOR,.chord=TS_MOSAIC_CHORD_MAJOR,.order=TS_MOSAIC_PITCH_SHUFFLED,
        .low_octave=3,.high_octave=5};
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
    /* Keeping positions is a pitch-only edit, including the spacing preference. */
    if(ui->mosaic_scatter_overlap && !ui->mosaic_scatter_keep_positions)ui->mosaic->auto_space=0;
    mosaic_select_only(ui,0);
    for(int i=0;i<ui->mosaic_ghost_count;++i) {
        TsMosaicEvent *preview=&ui->mosaic_ghosts[i],*e=NULL;
        if(ui->mosaic_scatter_copies) {
            for(int j=0;j<TS_MOSAIC_EVENTS;++j)if(!ui->mosaic->events[j].id) {
                e=&ui->mosaic->events[j];*e=*preview;e->id=ui->mosaic->next_id++;e->revision=1;break;
            }
        } else {
            e=ts_mosaic_find(ui->mosaic,preview->id);
            if(e) {
                e->start=preview->start;e->x=preview->x;
                if(e->note_count!=preview->note_count || memcmp(e->notes,preview->notes,sizeof(e->notes))) {
                    e->note_count=preview->note_count;memcpy(e->notes,preview->notes,sizeof(e->notes));++e->revision;
                }
            }
        }
        if(e)mosaic_select_add(ui,e->id);
    }
    if(device)SDL_UnlockAudioDevice(device);
    snprintf(ui->status,sizeof(ui->status),"SCATTER APPLIED / CTRL+Z UNDO / Z FIT SELECTION");
    mosaic_scatter_close(ui);mosaic_picks_clear(ui,c);
}
static int mosaic_scatter_cycle(int value,int count,int delta)
{return (value+count+delta)%count;}
static int mosaic_scatter_event(const SDL_Event *event,SDL_Window *window,SDL_AudioDeviceID device,
    TsUiState *ui,MosaicController *c,int x,int y,SDL_Keymod mod)
{
    int key=event->type==SDL_KEYDOWN,down=event->type==SDL_MOUSEBUTTONDOWN,wheel=event->type==SDL_MOUSEWHEEL;
    SDL_Keycode k=key?event->key.keysym.sym:0;
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
        if(k==SDLK_RETURN || k==SDLK_KP_ENTER || k==SDLK_TAB)mosaic_scatter_number(ui,c);
        return 1;
    }
    if(key && !event->key.repeat && (k==SDLK_RETURN || k==SDLK_KP_ENTER)){mosaic_scatter_apply(device,ui,c);return 1;}
    if(wheel){int mx,my;SDL_GetMouseState(&mx,&my);logical_mouse(window,mx,my,&x,&y);}
    if((down && event->button.button==SDL_BUTTON_LEFT) || (wheel && event->wheel.y)) {
        int left=x>=96 && x<300,right=x>=316 && x<520;
        if(down && y>=334 && y<358 && right){mosaic_scatter_close(ui);return 1;}
        if(!mosaic_scatter_number(ui,c))return 1;
        int delta=wheel?(event->wheel.y>0?1:-1):(mod&KMOD_SHIFT?-1:1);
        if(wheel && event->wheel.direction==SDL_MOUSEWHEEL_FLIPPED)delta=-delta;
        int field=0,keep=ui->mosaic_scatter_keep_positions;
        if(down && !keep && y>=64 && y<88){if(left)field=1;else if(right)field=2;}
        if(down && ui->mosaic_scatter_copies && y>=94 && y<118 && x>=96 && x<204)field=3;
        if(field) {
            ui->mosaic_scatter_field=field;
            snprintf(ui->mosaic_scatter_text,sizeof(ui->mosaic_scatter_text),"%.3f",field==1?ui->mosaic_scatter_start:ui->mosaic_scatter_end);
            if(field==3)snprintf(ui->mosaic_scatter_text,sizeof(ui->mosaic_scatter_text),"%d",ui->mosaic_scatter_count);
            SDL_StartTextInput();return 1;
        }
        if(down && y>=94 && y<118 && !c->scatter_from_sources) {
            if(x>=220 && x<360){ui->mosaic_scatter_copies=!ui->mosaic_scatter_copies;ui->mosaic_scatter_keep_positions=0;}
            if(x>=376 && x<520 && !ui->mosaic_scatter_copies)ui->mosaic_scatter_keep_positions=!keep;
        }
        if(down && !keep && y>=124 && y<148){if(left)ui->mosaic_scatter_cluster=!ui->mosaic_scatter_cluster;else if(right)ui->mosaic_scatter_overlap=!ui->mosaic_scatter_overlap;}
        TsMosaicPitchSettings *p=&ui->mosaic_scatter_pitch;
        if(y>=156 && y<180) {
            if(left)p->mode=mosaic_scatter_cycle(p->mode,TS_MOSAIC_PITCH_MODE_COUNT,delta);
            if(right && p->mode)p->root=mosaic_scatter_cycle(p->root,12,delta);
        }
        if(p->mode && y>=186 && y<210) {
            if(left) {
                if(p->mode==TS_MOSAIC_PITCH_SPREAD_SCALE)p->scale=mosaic_scatter_cycle(p->scale,TS_FM_PITCH_SCALE_COUNT,delta);
                else p->chord=mosaic_scatter_cycle(p->chord,TS_MOSAIC_CHORD_COUNT,delta);
            }
            if(right)p->order=mosaic_scatter_cycle(p->order,TS_MOSAIC_PITCH_ORDER_COUNT,delta);
        }
        if(p->mode && y>=216 && y<240) {
            if(left)p->low_octave=(int)fmax(-1,fmin(p->high_octave,p->low_octave+delta));
            if(right)p->high_octave=(int)fmax(p->low_octave,fmin(9,p->high_octave+delta));
        }
        if(down && y>=302 && y<326) {
            if(left && !keep)c->scatter_seed+=0x9e3779b9u;
            if(right && p->mode && (p->order==TS_MOSAIC_PITCH_RANDOM || p->order==TS_MOSAIC_PITCH_SHUFFLED))c->scatter_note_seed+=0x9e3779b9u;
        }
        if(down && y>=334 && y<358 && left){mosaic_scatter_apply(device,ui,c);return 1;}
        mosaic_scatter_preview(ui,c);
    }
    return key || down || wheel || event->type==SDL_TEXTINPUT || event->type==SDL_MOUSEMOTION ||
        event->type==SDL_KEYUP || event->type==SDL_MOUSEBUTTONUP;
}
