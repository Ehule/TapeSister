/* Controller owns disk/sample work. Input/output callbacks only append frames
   and publish small range descriptors. EXT is deliberately the default. */
static void mosaic_select_only(TsUiState *,uint64_t);
static void mosaic_reveal_event(TsUiState *,const TsMosaicEvent *);
static const char *mosaic_live_source_name(int source)
{return source==1?"DRY KEYS":source==2?"OUTPUT":"EXT INPUT";}
static void mosaic_live_keyboard_snapshot(AudioState *a,TsUiState *ui,const TsInstrument *instrument)
{
    if(a->mosaic_keyboard_sample && (!ui->mosaic || a->mosaic_keyboard_epoch!=ui->mosaic->epoch))a->mosaic_keyboard_sample=NULL;
    ui->mosaic_live_key_valid=a->mosaic_keyboard_sample!=NULL;
    if(a->mosaic_keyboard_sample) {
        ui->mosaic_live_key=a->mosaic_keyboard_key;
        snprintf(ui->mosaic_live_keyboard,sizeof(ui->mosaic_live_keyboard),"%s",a->mosaic_keyboard_label);
    }
    else if(a->sister.enabled && ts_sister_runtime_source_mask(&a->sister))
        snprintf(ui->mosaic_live_keyboard,sizeof(ui->mosaic_live_keyboard),"KEYS: SISTER GROUP");
    else if(a->performance_group_latched && a->performance_source_mask)
        snprintf(ui->mosaic_live_keyboard,sizeof(ui->mosaic_live_keyboard),"KEYS: TILE GROUP");
    else snprintf(ui->mosaic_live_keyboard,sizeof(ui->mosaic_live_keyboard),"TILE %02d / %s",instrument->selected_slot+1,ts_audition_source_name(ui->audition_source));
}
static void toggle_external_monitor(SDL_AudioDeviceID,SDL_AudioDeviceID *,AudioState *,ExternalInputState *,TsUiState *);
static int mosaic_live_writer_main(void *data)
{
    TsLoopCapture *c=data;
    while(ts_loop_capture_pump(c,8192)) {
        if(atomic_load(&c->file.read_cursor)==atomic_load(&c->file.write_cursor))SDL_Delay(2);
    }
    return 0;
}
static SDL_AudioDeviceID mosaic_live_capture_device(SDL_AudioDeviceID output,SDL_AudioDeviceID input,const ExternalInputState *s)
{return s->loop_source==0?input:output;}
static void mosaic_live_capture_end(SDL_AudioDeviceID output,SDL_AudioDeviceID input,ExternalInputState *s,int discard)
{
    SDL_AudioDeviceID d=mosaic_live_capture_device(output,input,s);
    if(d)SDL_LockAudioDevice(d);
    ts_loop_capture_boundary(&s->loop_capture,1,discard);
    atomic_store_explicit(&s->loop_tap,-1,memory_order_release);
    if(d)SDL_UnlockAudioDevice(d);
}
static int mosaic_live_capture_begin(SDL_AudioDeviceID output,SDL_AudioDeviceID *input_device,AudioState *a,ExternalInputState *s,TsUiState *ui)
{
    TsMosaic *m=ui->mosaic;char error[160],path[1200];
    if(!m || ui->file_busy || external_capture_busy(s) || s->loop_session || a->capture.state!=TS_CAPTURE_IDLE || a->sister.capture.state!=TS_CAPTURE_IDLE) {
        snprintf(ui->mosaic_live_status,sizeof(ui->mosaic_live_status),"FINISH THE CURRENT CAPTURE FIRST");return 0;
    }
    if(!mosaic_record_has_space(m)){snprintf(ui->mosaic_live_status,sizeof(ui->mosaic_live_status),"MOSAIC FULL / NO RECORDING STARTED");return 0;}
    int source=m->live_capture.source;
    if(!source) {
        ts_input_ownership_request(&s->ownership,TS_INPUT_CONSUMER_RECORD_ACTIVE);
        if(!sync_external_input_consumers(input_device,a,s,&ui->config,error,sizeof(error)))goto fail;
    }
    unsigned rate=source?(unsigned)a->output_rate:s->sample_rate;
    unsigned channels=source?2:ts_input_channel_record_channels(ui->config.record_input_channel);
    if(!rate || (source && !output)){snprintf(error,sizeof(error),"No audio device available");goto fail;}
    if(output)SDL_LockAudioDevice(output);
    double quarter=m->live_master?m->live_quarter:a->sister.clock_controls.bpm?60.0/a->sister.clock_controls.bpm:.5;
    s->loop_epoch=m->epoch;s->loop_start=m->time;s->loop_x=ui->mosaic_xscroll;s->loop_number=0;
    if(output)SDL_UnlockAudioDevice(output);
    /* New layers start beside cards already crossing the insertion line;
       free placement remains available after creation. */
    for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        const TsMosaicEvent *e=&m->events[i];
        if(e->id && e->start<=s->loop_start && e->start+e->duration>s->loop_start)
            s->loop_x=fmax(s->loop_x,e->x+e->width+TS_MOSAIC_GUTTER);
    }
    if(m->live_capture.units && (!a->sister.clock_controls.enabled || !m->live_capture.length)) {
        snprintf(error,sizeof(error),"BEAT LENGTH NEEDS MASTER ON AND A NONZERO LENGTH");goto fail;
    }
    double target=rate*m->live_capture.length*(m->live_capture.units?quarter:1);
    s->loop_source=source; /* UI-only; callbacks use the separately published tap. */
    if(!ts_capture_archive_unique_path(capture_archive_directory(),"LIVE-LOOPS",path,sizeof(path),error,sizeof(error)) ||
       !ts_loop_capture_start(&s->loop_capture,path,rate,channels,target,m->live_capture.continuous,error,sizeof(error)))goto fail;
    snprintf(ui->mosaic_live_archive,sizeof(ui->mosaic_live_archive),"%s",path);
    s->loop_source=source;s->loop_tempo=m->live_capture.units;s->loop_quarter=quarter;
    s->loop_quantize=m->live_capture.quantize;s->loop_session=1;s->loop_stop_playback=0;
    s->loop_writer=SDL_CreateThread(mosaic_live_writer_main,"live-loop-writer",&s->loop_capture);
    if(!s->loop_writer) {
        mosaic_live_capture_end(output,*input_device,s,1);
        while(ts_performance_recorder_pump(&s->loop_capture.file,8192)){}
        ts_loop_capture_free(&s->loop_capture);s->loop_session=0;
        snprintf(error,sizeof(error),"Cannot start loop archive writer");goto fail;
    }
    /* Only publish a producer after every field and its writer are ready.
       NONE between sessions also excludes the other device's callback while
       the old recorder is retired/reinitialized. */
    atomic_store_explicit(&s->loop_tap,source,memory_order_release);
    ui->mosaic_live_recording=1;ui->mosaic_live_finishing=0;
    ui->mosaic_live_seconds=0;
    ui->mosaic_live_duration=target?target/rate:0;
    ui->mosaic_live_rate=rate;
    ui->mosaic_live_preview=(TsMosaicRecordPreview){.epoch=m->epoch,.start=s->loop_start,.x=s->loop_x,.frames_per_peak=1};
    if(ui->mosaic_open) {
        TsMosaicEvent preview={.start=s->loop_start,.duration=.1,.x=s->loop_x,.width=86};
        mosaic_reveal_event(ui,&preview);
    }
    snprintf(ui->mosaic_live_status,sizeof(ui->mosaic_live_status),"REC %s / %s / NEXT KEEPS LAYER / STOP KEEPS PARTIAL",
        mosaic_live_source_name(source),m->live_capture.continuous?"CONTINUOUS":"SINGLE");
    return 1;
fail:
    if(!source){ts_input_ownership_release(&s->ownership,TS_INPUT_CONSUMER_RECORD_ACTIVE);char ignored[160];sync_external_input_consumers(input_device,a,s,&ui->config,ignored,sizeof(ignored));}
    snprintf(ui->mosaic_live_status,sizeof(ui->mosaic_live_status),"LOOP CAPTURE FAILED: %.135s",error);return 0;
}
static int mosaic_live_command(SDL_AudioDeviceID output,SDL_AudioDeviceID *input_device,AudioState *a,ExternalInputState *s,TsUiState *ui,const char *command)
{
    TsMosaic *m=ui->mosaic;if(!m)return 0;
    int layers=!strcmp(command,"primary");
    if(layers || !strcmp(command,"record") || !strcmp(command,"next")) {
        if(!s || !input_device)return 0;
        /* The big performance control always means record another separate
           layer. SINGLE remains available through Advanced's RECORD action. */
        if(layers && !m->live_capture.continuous) {
            if(output)SDL_LockAudioDevice(output);
            m->live_capture.continuous=1;++m->revision;
            if(output)SDL_UnlockAudioDevice(output);
        }
        if(!s->loop_session)return mosaic_live_capture_begin(output,input_device,a,s,ui);
        if(!atomic_load_explicit(&s->loop_capture.active,memory_order_acquire)) {
            snprintf(ui->mosaic_live_status,sizeof(ui->mosaic_live_status),"FINISHING CAPTURE / WAIT FOR THE COMPLETED LAYERS");return 1;
        }
        SDL_AudioDeviceID d=mosaic_live_capture_device(output,*input_device,s);
        if(d)SDL_LockAudioDevice(d);
        if(layers)s->loop_capture.continuous=1;
        ts_loop_capture_boundary(&s->loop_capture,0,0);
        if(d)SDL_UnlockAudioDevice(d);return 1;
    }
    if(!strcmp(command,"stop_capture") || !strcmp(command,"discard_partial")) {
        if(s && input_device && s->loop_session)mosaic_live_capture_end(output,*input_device,s,!strcmp(command,"discard_partial"));return 1;
    }
    if(!strcmp(command,"scope")) {ui->mosaic_live_play_all^=1;return 1;}
    if(!strcmp(command,"halt") && s && input_device && s->loop_session) {
        s->loop_stop_playback=1;mosaic_live_capture_end(output,*input_device,s,0);
    }
    uint64_t id=ui->mosaic_selected;unsigned long long named;int used=0;
    int named_toggle=sscanf(command,"%llu.toggle%n",&named,&used)==1 && used>0 && !command[used];
    if(named_toggle)id=(uint64_t)named;
    if(output)SDL_LockAudioDevice(output);
    TsMosaicEvent *e=ts_mosaic_find(m,id);
    int reveal=0;
    if(!strcmp(command,"role")) {
        if(!e || e->source->pattern)snprintf(ui->mosaic_live_status,sizeof(ui->mosaic_live_status),"SELECT AN AUDIO CARD / PATTERN LOOP EVENTS NEED THEIR OWN PLAYERS");
        else if(!e->live && (ts_loop_base_mode(e->mode)!=TS_LOOP_FORWARD || (ts_loop_starts_at_sample(e->mode) && e->first)))
            snprintf(ui->mosaic_live_status,sizeof(ui->mosaic_live_status),"CARD KEPT / LIVE LOOPS NEED FORWARD RANGE PLAYBACK / EDIT CARD DIRECTION FIRST");
        else {
            ts_mosaic_checkpoint(m);ts_mosaic_live_stop(m,e->id);e->live^=1;
            m->live[e-m->events]=(TsMosaicLive){0};m->voices[e-m->events].active=0;
            if(e->live)e->live_beats=ts_mosaic_live_cycle(e)/(m->live_quarter>0?m->live_quarter:.5);
            ++e->revision;++m->revision;
            snprintf(ui->mosaic_live_status,sizeof(ui->mosaic_live_status),"CARD #%llu / %s",(unsigned long long)e->id,
                e->live?"LOOP EVENT READY / PLAY TO START / OWN SOURCE CYCLE":"TIMELINE MODE / ORIGINAL SOURCE REPEAT SETTING RETAINED");
        }
    } else if(!strcmp(command,"first_loop") || !strcmp(command,"previous_loop") ||
        !strcmp(command,"next_loop") || !strcmp(command,"last_loop")) {
        uint64_t first=0,last=0,before=0,after=0;
        for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
            const TsMosaicEvent *card=&m->events[i];if(!card->id || !card->live)continue;
            if(!first || card->id<first)first=card->id;
            if(card->id>last)last=card->id;
            if(card->id<id && card->id>before)before=card->id;
            if(card->id>id && (!after || card->id<after))after=card->id;
        }
        id=!strcmp(command,"first_loop")?first:!strcmp(command,"last_loop")?last:
            !strcmp(command,"previous_loop")?(id?(before?before:first):last):(id?(after?after:last):first);
        if(id){mosaic_select_only(ui,id);e=ts_mosaic_find(m,id);(void)ts_mosaic_live_launch(m,id,m->live_capture.quantize);reveal=1;}
        else snprintf(ui->mosaic_live_status,sizeof(ui->mosaic_live_status),"NO LOOP EVENTS / SELECT AN AUDIO CARD AND ENABLE LOOP EVENT");
    } else if(!strcmp(command,"play")) {
        if(ui->mosaic_live_play_all) {
            for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id && m->events[i].live)
                (void)ts_mosaic_live_launch(m,m->events[i].id,m->live_capture.quantize);
        } else if(e && e->live)(void)ts_mosaic_live_launch(m,id,m->live_capture.quantize);
        else snprintf(ui->mosaic_live_status,sizeof(ui->mosaic_live_status),"SELECT A LOOP EVENT / LOOP EVENT BUTTON CONVERTS AN AUDIO CARD");
    } else if(!strcmp(command,"stop_all") || !strcmp(command,"halt"))ts_mosaic_live_stop(m,0);
    else if(e && e->live) {
        if(!strcmp(command,"restart"))ts_mosaic_live_restart(m,id);
        else if(!strcmp(command,"stop"))ts_mosaic_live_stop(m,id);
        else if(!strcmp(command,"launch"))(void)ts_mosaic_live_launch(m,id,m->live_capture.quantize);
        else if(named_toggle || !strcmp(command,"toggle")) {
            if(m->live[e-m->events].state)ts_mosaic_live_stop(m,id);
            else (void)ts_mosaic_live_launch(m,id,m->live_capture.quantize);
        }
    }
    if(output)SDL_UnlockAudioDevice(output);
    if(reveal)mosaic_reveal_event(ui,e);
    return 1;
}
static void mosaic_live_poll(SDL_AudioDeviceID output,SDL_AudioDeviceID *input_device,AudioState *a,ExternalInputState *s,TsUiState *ui,int defer)
{
    if(!s->loop_session)return;
    TsLoopCapture *c=&s->loop_capture;TsLoopTake take;char error[160];
    SDL_AudioDeviceID d=mosaic_live_capture_device(output,*input_device,s);
    if(d)SDL_LockAudioDevice(d);
    if(ts_performance_recorder_state(&c->file)==TS_PERFORMANCE_FILE_FAILED) {
        atomic_store(&c->failed,TS_LOOP_CAPTURE_WRITER_FAILED);ts_loop_capture_boundary(c,1,0);
    }
    ui->mosaic_live_recording=atomic_load(&c->active);
    ui->mosaic_live_finishing=!ui->mosaic_live_recording;
    ui->mosaic_live_seconds=c->file.sample_rate?(double)(c->frames-c->first)/c->file.sample_rate:0;
    ui->mosaic_live_duration=c->file.sample_rate?(double)c->target/c->file.sample_rate:0;
    ui->mosaic_live_rate=c->file.sample_rate;
    uint64_t preview_first=c->first,preview_frames=c->frames-c->first;
    unsigned pending=atomic_load(&c->produced)-atomic_load(&c->consumed);
    if(d)SDL_UnlockAudioDevice(d);
    if(ui->mosaic_live_recording && s->loop_epoch==ui->mosaic->epoch) {
        TsMosaicRecordPreview *p=&ui->mosaic_live_preview;
        p->epoch=s->loop_epoch;p->start=s->loop_start;p->x=s->loop_x+pending*(86+TS_MOSAIC_GUTTER);
        p->frames=(size_t)preview_frames;p->frames_per_peak=(p->frames+TS_MOSAIC_RECORD_PEAKS-1)/TS_MOSAIC_RECORD_PEAKS;
        if(!p->frames_per_peak)p->frames_per_peak=1;
        ts_loop_capture_preview(c,preview_first,preview_first+p->frames_per_peak*TS_MOSAIC_RECORD_PEAKS,p->peaks,TS_MOSAIC_RECORD_PEAKS);
        if(!defer && ui->mosaic_open && ui->mosaic_follow) {
            TsMosaicEvent preview={.start=p->start,.duration=ui->mosaic_live_seconds,.x=p->x,.width=86};mosaic_reveal_event(ui,&preview);
        }
    }
    if(s->loop_epoch!=ui->mosaic->epoch)mosaic_live_capture_end(output,*input_device,s,0);
    if(!defer && ts_loop_capture_peek(c,&take) && take.last<=atomic_load_explicit(&c->file.flushed_frames,memory_order_acquire)) {
        int ok=0;TsSample sample={0};TsMosaic *m=ui->mosaic;
        size_t bytes=0;for(TsMosaicSource *source=m->sources;source;source=source->next)
            bytes+=source->sample.frames*source->sample.channels*sizeof(float)+source->eq_dry.frames*source->eq_dry.channels*sizeof(float);
        uint64_t needed=(take.last-take.first)*c->file.channels*sizeof(float);
        if(s->loop_epoch!=m->epoch)snprintf(error,sizeof(error),"Scene changed / layer stays in archive");
        else if(!mosaic_record_has_space(m))snprintf(error,sizeof(error),"Mosaic full / layer stays in archive");
        else if(needed>128u*1024u*1024u || bytes>128u*1024u*1024u-needed)snprintf(error,sizeof(error),"128 MiB live import budget reached / WAV retained");
        else if(ts_performance_recorder_load_range(&c->file,take.first,take.last,&sample,error,sizeof(error))) {
            snprintf(sample.name,sizeof(sample.name),"LIVE %03u",++s->loop_number);
            TsMosaicSource *source=ts_mosaic_source(m,&sample,error,sizeof(error));
            if(source) {
                if(output)SDL_LockAudioDevice(output);ts_mosaic_checkpoint(m);
                TsMosaicEvent *e=ts_mosaic_add(m,source,s->loop_start,s->loop_x);
                if(e){e->live=1;e->looping=1;e->live_tempo=s->loop_tempo;
                    /* Preserve the integer beat preset, not the rounded WAV
                       frame count. Partial/manual cuts keep their measured length. */
                    e->live_beats=s->loop_tempo && take.full_length?
                        round(c->period/(c->file.sample_rate*s->loop_quarter)):e->duration/s->loop_quarter;
                    if(!s->loop_stop_playback)(void)ts_mosaic_live_launch(m,e->id,s->loop_quantize);
                    s->loop_x=e->x+e->width+TS_MOSAIC_GUTTER;
                    if(!ui->mosaic_editing)mosaic_select_only(ui,e->id);ok=1;}
                if(output)SDL_UnlockAudioDevice(output);
                if(!ok)snprintf(error,sizeof(error),"Card insertion failed / WAV retained");
            }
        }
        ts_sample_free(&sample);ts_loop_capture_consume(c);
        if(!ok){mosaic_live_capture_end(output,*input_device,s,0);snprintf(ui->mosaic_live_status,sizeof(ui->mosaic_live_status),"LAYER ARCHIVED: %.140s",error);s->loop_import_failed=1;}
        else {
            if(ui->mosaic_open && !ui->mosaic_editing)mosaic_reveal_event(ui,ts_mosaic_find(m,ui->mosaic_selected));
            snprintf(ui->mosaic_live_status,sizeof(ui->mosaic_live_status),"LAYER %u ADDED / %s / %s",s->loop_number,mosaic_live_source_name(s->loop_source),s->loop_stop_playback?"LOOPS STOPPED":ui->mosaic_live_recording?"RECORDING NEXT":"FINISHING");
        }
    }
    TsPerformanceFileState state=ts_performance_recorder_state(&c->file);
    int queued=ts_loop_capture_peek(c,&take);
    /* A failed flush can leave a final range unreadable. Keep the session WAV
       and report it; never spin forever waiting on a writer that has ended. */
    if(state==TS_PERFORMANCE_FILE_FAILED && queued && take.last>atomic_load(&c->file.flushed_frames)) {
        ts_loop_capture_consume(c);s->loop_import_failed=1;queued=ts_loop_capture_peek(c,&take);
    }
    if(!atomic_load(&c->active) && !queued && (state==TS_PERFORMANCE_FILE_COMPLETED || state==TS_PERFORMANCE_FILE_FAILED)) {
        if(s->loop_writer){SDL_WaitThread(s->loop_writer,NULL);s->loop_writer=NULL;}
        int reason=atomic_load(&c->failed),failed=reason || s->loop_import_failed;
        const char *why=reason==TS_LOOP_CAPTURE_QUEUE_FULL?"LAYER QUEUE FULL":reason==TS_LOOP_CAPTURE_WRITER_FAILED?"DISK/WRITER LIMIT":reason==TS_LOOP_CAPTURE_LENGTH_LIMIT?"60 SECOND TAKE LIMIT":"CAPTURE STOPPED";
        const char *ending=s->loop_stop_playback?"LOOPS STOPPED":"LOOPS KEEP PLAYING";
        snprintf(ui->status,sizeof(ui->status),"%s / %s / ARCHIVE %.105s",failed?"LOOP CAPTURE INTERRUPTED":"LOOP CAPTURE KEPT",ending,c->file.path);
        if(!s->loop_import_failed)snprintf(ui->mosaic_live_status,sizeof(ui->mosaic_live_status),"%s / %u SEPARATE LAYERS / %s",why,s->loop_number,ending);
        if(d)SDL_LockAudioDevice(d);atomic_store_explicit(&s->loop_tap,-1,memory_order_release);ts_loop_capture_free(c);s->loop_session=0;if(d)SDL_UnlockAudioDevice(d);
        s->loop_import_failed=0;ui->mosaic_live_recording=ui->mosaic_live_finishing=0;
        if(!s->loop_source){ts_input_ownership_release(&s->ownership,TS_INPUT_CONSUMER_RECORD_ACTIVE);char ignored[160];sync_external_input_consumers(input_device,a,s,&ui->config,ignored,sizeof(ignored));}
    }
}
static int mosaic_live_event(const SDL_Event *event,SDL_Window *window,SDL_AudioDeviceID output,
    SDL_AudioDeviceID *input_device,AudioState *a,ExternalInputState *s,TsUiState *ui)
{
    Uint32 window_id=SDL_GetWindowID(window);
    if(ui->mosaic_live_drag) {
        int lost=event->type==SDL_WINDOWEVENT && event->window.windowID==window_id &&
            (event->window.event==SDL_WINDOWEVENT_FOCUS_LOST || event->window.event==SDL_WINDOWEVENT_HIDDEN || event->window.event==SDL_WINDOWEVENT_MINIMIZED);
        int cancel=event->type==SDL_KEYDOWN && event->key.windowID==window_id && event->key.keysym.sym==SDLK_ESCAPE;
        int release=event->type==SDL_MOUSEBUTTONUP && event->button.button==SDL_BUTTON_LEFT;
        if(lost || cancel || release || !ui->mosaic_open || ui->mosaic_live_advanced || ui->midi_learn_active || ui_dialog_open(ui)) {
            if(cancel){ui->config.mosaic_looper_x=ui->mosaic_live_drag_before_x;ui->config.mosaic_looper_y=ui->mosaic_live_drag_before_y;}
            ui->mosaic_live_drag=0;SDL_CaptureMouse(SDL_FALSE);
            if(cancel || release)return 1;
        } else if(event->type==SDL_MOUSEMOTION && event->motion.windowID==window_id) {
            if(!(event->motion.state&SDL_BUTTON_LMASK)){ui->mosaic_live_drag=0;SDL_CaptureMouse(SDL_FALSE);return 1;}
            int x,y;logical_mouse(window,event->motion.x,event->motion.y,&x,&y);
            ui->config.mosaic_looper_x=SDL_clamp(x-ui->mosaic_live_grab_x,0,TS_UI_WIDTH-TS_MOSAIC_LIVE_W);
            ui->config.mosaic_looper_y=SDL_clamp(y-ui->mosaic_live_grab_y,32,TS_UI_HEIGHT-TS_MOSAIC_LIVE_H);return 1;
        }
    }
    if(!ui->mosaic_open || ui->mosaic_editing || ui->file_busy || ui_dialog_open(ui) || !ui->mosaic)return 0;
    if(ui->midi_learn_active)return 0; /* Global learner owns clicks and Escape. */
    if(event->type==SDL_KEYDOWN && event->key.windowID==SDL_GetWindowID(window) && !event->key.repeat) {
        if(event->key.keysym.sym==SDLK_l && (event->key.keysym.mod&KMOD_CTRL)) {ui->mosaic_live_open^=1;ui->mosaic_live_drag=0;SDL_CaptureMouse(SDL_FALSE);return 1;}
        if(ui->mosaic_live_open && event->key.keysym.sym==SDLK_ESCAPE) {
            if(ui->mosaic_live_advanced)ui->mosaic_live_advanced=0;else ui->mosaic_live_open=0;return 1;
        }
    }
    int down=event->type==SDL_MOUSEBUTTONDOWN,wheel=event->type==SDL_MOUSEWHEEL,motion=event->type==SDL_MOUSEMOTION;
    if((!down && !wheel && !motion) || (down?event->button.windowID:motion?event->motion.windowID:event->wheel.windowID)!=window_id)return 0;
    int x,y;if(down)logical_mouse(window,event->button.x,event->button.y,&x,&y);
    else if(motion)logical_mouse(window,event->motion.x,event->motion.y,&x,&y);
    else {int mx,my;wheel_event_mouse(event,&mx,&my);logical_mouse(window,mx,my,&x,&y);}
    if(down && event->button.button==SDL_BUTTON_LEFT &&
       x>=TS_MOSAIC_LIVE_BUTTON_X && x<TS_MOSAIC_LIVE_BUTTON_X+TS_MOSAIC_LIVE_BUTTON_W &&
       y>=TS_MOSAIC_LIVE_BUTTON_Y && y<TS_MOSAIC_LIVE_BUTTON_Y+TS_MOSAIC_LIVE_BUTTON_H) {
        ui->mosaic_live_open^=1;return 1;
    }
    if(!ts_ui_mosaic_live_contains(ui,x,y)){if(motion)ui->mosaic_live_hover[0]=0;return 0;}
    TsMosaic *m=ui->mosaic;
    int px,py;ts_ui_mosaic_live_origin(ui,&px,&py);
    if(motion) {
        const char *help=ts_ui_mosaic_live_help(ts_ui_mosaic_live_command_at(ui,x,y));
        if(!ui->mosaic_live_advanced && y>=py+244 && y<py+260)help=x<px+80?"QWERTY KEYS / EDIT LETTERS":"ADVANCED RECORDING SETUP";
        snprintf(ui->mosaic_live_hover,sizeof(ui->mosaic_live_hover),"%s",help);return 1;
    }
    if(!ui->mosaic_live_advanced && down && event->button.button==SDL_BUTTON_LEFT && y>=py+4 && y<py+20 && x>=px+4 && x<px+140) {
        if(event->button.clicks>=2){ui->config.mosaic_looper_x=ui->config.mosaic_looper_y=-1;return 1;}
        ui->mosaic_live_drag_before_x=ui->config.mosaic_looper_x;ui->mosaic_live_drag_before_y=ui->config.mosaic_looper_y;
        ui->mosaic_live_grab_x=x-px;ui->mosaic_live_grab_y=y-py;ui->mosaic_live_drag=1;SDL_CaptureMouse(SDL_TRUE);return 1;
    }
    if(down && event->button.button==SDL_BUTTON_LEFT) {
        const char *command=ts_ui_mosaic_live_command_at(ui,x,y);
        if(command){mosaic_live_command(output,input_device,a,s,ui,command);return 1;}
    }
    if(!ui->mosaic_live_advanced) {
        if(down && event->button.button==SDL_BUTTON_LEFT) {
            if(x>=px+144 && x<px+158 && y>=py+4 && y<py+20)ui->mosaic_live_open=0;
            else if(y>=py+244 && y<py+260) {
                if(x>=px+86 && x<px+158)ui->mosaic_live_advanced=1;
                else if(x>=px+8 && x<px+80)ui->mosaic_live_keys^=1;
            }
        }
        return 1;
    }
    if(down && event->button.button==SDL_BUTTON_LEFT && y>=310 && y<326 && x>=132 && x<276) {
        ui->mosaic_live_keys^=1;return 1;
    }
    if(down && y>=166 && y<182) {
        if(x>=526 && x<608){ui->mosaic_live_advanced=0;return 1;}
        if(x>=424 && x<520) {
            if(ui->mosaic_live_archive[0]) {SDL_SetClipboardText(ui->mosaic_live_archive);snprintf(ui->mosaic_live_status,sizeof(ui->mosaic_live_status),"SESSION WAV PATH COPIED / COMPLETE LAYERS + CURRENT PARTIAL");}
            return 1;
        }
        if(x>=292 && x<418) {
            TsRecordSource source=ui->record_source;ui->record_source=TS_RECORD_SOURCE_EXT;
            toggle_external_monitor(output,input_device,a,s,ui);ui->record_source=source;return 1;
        }
    }
    if((y>=140 && y<156 && x>=132 && x<608) || (y>=166 && y<182 && x>=132 && x<282)) {
        if(s->loop_session){snprintf(ui->mosaic_live_status,sizeof(ui->mosaic_live_status),"STOP CAPTURE BEFORE CHANGING ITS SOURCE OR MODE");return 1;}
        if(output)SDL_LockAudioDevice(output);
        if(y>=166)m->live_capture.quantize^=1;
        else if(x<266)m->live_capture.source=(m->live_capture.source+1)%3;
        else if(x<406)m->live_capture.continuous^=1;
        else if(wheel) {
            int delta=wheel_event_delta(event,0);m->live_capture.length=SDL_max(0,SDL_min(60,m->live_capture.length+delta));
        } else if(event->button.button==SDL_BUTTON_RIGHT)m->live_capture.units^=1;
        else {static const int lengths[]={0,1,2,4,8,16,32,60};int n=0;while(n<8 && lengths[n]<=m->live_capture.length)++n;m->live_capture.length=lengths[n%8];}
        ++m->revision;
        if(output)SDL_UnlockAudioDevice(output);return 1;
    }
    if(down && y>=224 && y<240 && x>=240 && x<332) {
        if(output)SDL_LockAudioDevice(output);
        TsMosaicEvent *e=ts_mosaic_find(m,ui->mosaic_selected);
        if(e && !e->source->pattern) {
            ts_mosaic_checkpoint(m);
            if(e->live)e->live_finite^=1;
            ++e->revision;++m->revision;
        } else snprintf(ui->mosaic_live_status,sizeof(ui->mosaic_live_status),"SELECT AN AUDIO CARD / PATTERNS KEEP THEIR TIMELINE CLOCK");
        if(output)SDL_UnlockAudioDevice(output);return 1;
    }
    if(down && y>=254 && y<270 && x>=132 && x<608) {
        if(output)SDL_LockAudioDevice(output);
        TsMosaicEvent *e=ts_mosaic_find(m,ui->mosaic_selected);
        if(e && e->live){ts_mosaic_checkpoint(m);
            if(x<292){e->live_tempo^=1;if(e->live_beats<=0)e->live_beats=ts_mosaic_live_cycle(e)/(m->live_quarter>0?m->live_quarter:.5);}
            else if(x<452)e->live_transport^=1;
            else e->live_position^=1;
            ++m->revision;}
        if(output)SDL_UnlockAudioDevice(output);return 1;
    }
    return 1; /* Pointer belongs to this panel; notes/transport still pass. */
}
