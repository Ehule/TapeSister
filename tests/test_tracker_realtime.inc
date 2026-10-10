/* Exercise the real controller without the persistence checkpoints used by
   the editor tests. Large scores must not turn a note into a project save. */
static uint64_t realtime_lock_start,realtime_lock_peak;
static void realtime_lock(void *context) {
    (void)context;assert(!realtime_lock_depth++);
    realtime_lock_start=SDL_GetPerformanceCounter();
}
static void realtime_unlock(void *context) {
    (void)context;assert(realtime_lock_depth--==1);
    uint64_t elapsed=SDL_GetPerformanceCounter()-realtime_lock_start;
    if(elapsed>realtime_lock_peak)realtime_lock_peak=elapsed;
}
static void tracker_realtime(void) {
    ts_tapehead_stop();ts_tapehead_close();
    TsSisterTracker *t=&test_pages->tracker;
    free(t->embedded_data);t->embedded_data=NULL;t->embedded_size=0;
    while(t->pattern_count<128) {
        TsPatternId id;assert(ts_sister_tracker_add_pattern(t,256,&id,test_error,sizeof(test_error)));
    }
    TsTapeHeadHost host={test_window,NULL,realtime_lock,realtime_unlock,NULL};
    assert(ts_tapehead_init(&host,48000,test_error,sizeof(test_error)));
    assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));
    assert(ts_tapehead_export(t,test_error,sizeof(test_error)));
    test_ui->tracker_embedded_frame=ts_tapehead_frame();test_ui->tracker_open=1;
    test_ui->keyboard_hold=1;
    for(int key=0;key<3;++key) {
        TsNoteEvent note;assert(ts_note_event_qwerty(&note,key*4,60));
        assert(runtime_note_start_event(test_audio,test_bank,&test_bank->audible_tuning,
            TS_AUDITION_CURRENT,&note,1,48000)==TS_NOTE_STARTED);
    }
    assert(ts_note_bank_count(&test_audio->notes)==3);
    realtime_lock_peak=0;tracker_sync_calls=tracker_export_calls=tracker_locked_exports=0;
    uint64_t before=ts_sister_tracker_hash(t);
    TsPatternId edited=t->editor_pattern;unsigned first_row=editor.row;
    double key_ms=0,refresh_ms=0;
    for(int i=0;i<8;++i) {
        SDL_Event event;SDL_zero(event);event.type=SDL_KEYDOWN;
        event.key.windowID=SDL_GetWindowID(test_window);
        event.key.keysym.sym=SDLK_z;event.key.keysym.scancode=SDL_SCANCODE_Z;
        uint64_t started=SDL_GetPerformanceCounter();
        assert(tracker_event(&event,test_window,0,test_audio,test_ui,test_pages,test_bank,48000));
        event.type=SDL_KEYUP;assert(tracker_event(&event,test_window,0,test_audio,test_ui,test_pages,test_bank,48000));
        key_ms+=(SDL_GetPerformanceCounter()-started)*1000.0/SDL_GetPerformanceFrequency();
        started=SDL_GetPerformanceCounter();
        tracker_refresh(0,test_audio,test_ui,test_pages,test_bank,48000,NULL);
        refresh_ms+=(SDL_GetPerformanceCounter()-started)*1000.0/SDL_GetPerformanceFrequency();
        float out[1024];audio_callback(test_audio,(Uint8*)out,sizeof(out));
        double energy=0;for(int s=0;s<1024;++s){assert(isfinite(out[s]));energy+=fabsf(out[s]);}
        assert(energy>0 && ts_note_bank_count(&test_audio->notes)==3);
    }
    printf("Tracker 128 patterns / 3 held voices: key %.3f ms, refresh %.3f ms, max lock %.3f ms, exports %u, full syncs %u\n",
        key_ms/8,refresh_ms/8,realtime_lock_peak*1000.0/SDL_GetPerformanceFrequency(),tracker_export_calls,tracker_sync_calls);
    assert(!tracker_export_calls && !tracker_sync_calls);
    assert(ts_sister_tracker_hash(t)==before); /* Live score has not been serialized. */
    /* MIDI shares the live editor path and must also avoid project exports. */
    TsMidiEvent midi;assert(ts_midi_decode_short_message(0x90,67,100,&midi));
    unsigned midi_row=editor.row;
    handle_midi_event(0,test_audio,test_ui,test_bank,NULL,&midi,48000,1);
    assert(ts_midi_decode_short_message(0x80,67,0,&midi));
    handle_midi_event(0,test_audio,test_ui,test_bank,NULL,&midi,48000,1);
    assert(!tracker_export_calls && !tracker_sync_calls);
    /* Ctrl+S must checkpoint every deferred QWERTY/MIDI edit without an outer
       audio lock, including when the tracker stays open. */
    SDL_Event save;SDL_zero(save);save.type=SDL_KEYDOWN;save.key.windowID=SDL_GetWindowID(test_window);
    save.key.keysym.sym=SDLK_s;save.key.keysym.scancode=SDL_SCANCODE_S;save.key.keysym.mod=KMOD_CTRL;
    assert(!tracker_event(&save,test_window,0,test_audio,test_ui,test_pages,test_bank,48000));
    assert(tracker_export_calls==1 && !tracker_locked_exports);
    TsTrackerPattern *saved=ts_sister_tracker_pattern(t,edited);assert(saved);
    for(unsigned i=0;i<8;++i)assert(saved->cells[first_row+i][0].note_kind==TS_TRACKER_NOTE_PITCH);
    assert(saved->cells[midi_row][0].note==67);
    assert(ts_sister_tracker_save_file(t,"realtime-checkpoint.tst",test_error,sizeof(test_error)));
    TsSisterTracker loaded;ts_sister_tracker_init(&loaded);
    assert(ts_sister_tracker_load_file(&loaded,"realtime-checkpoint.tst",test_error,sizeof(test_error)));
    assert(ts_sister_tracker_hash(&loaded)==ts_sister_tracker_hash(t));
    ts_sister_tracker_free(&loaded);remove("realtime-checkpoint.tst");
    /* Exit's dirty check must also see edits made after the last checkpoint. */
    test_ui->saved_state_hash=runtime_project_state_hash(test_pages,test_bank,NULL,0,&test_audio->sister,&test_audio->keyboard_sequence);
    SDL_Event note=save;note.key.keysym.mod=KMOD_NONE;note.key.keysym.sym=SDLK_x;note.key.keysym.scancode=SDL_SCANCODE_X;
    assert(tracker_event(&note,test_window,0,test_audio,test_ui,test_pages,test_bank,48000));
    note.type=SDL_KEYUP;assert(tracker_event(&note,test_window,0,test_audio,test_ui,test_pages,test_bank,48000));
    begin_exit_confirmation(0,test_audio,test_ui,test_bank,test_pages,NULL,0);
    assert(test_ui->exit_has_unsaved && !tracker_locked_exports);
    assert(!realtime_lock_depth);
    ts_tapehead_stop();runtime_note_clear(test_audio);test_ui->keyboard_hold=0;
}
