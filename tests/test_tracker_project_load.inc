/* Exercise the normal TSR loader without closing/reinitializing TrackSister. */
static void save_tracker_project(const char *path)
{
    TsInstrument *record=malloc(sizeof(*record));assert(record);ts_instrument_init(record);
    TsSisterProjectState state;ts_sister_project_state_init(&state,48000);
    state.page_count=test_pages->page_count;state.active_page=test_pages->active_page;
    assert(ts_sample_pages_save_project(test_pages,test_bank,record,&state,path,test_error,sizeof(test_error)));
    ts_instrument_free(record);free(record);
}

static void tracker_project_load(void)
{
    TsInstrument *record=malloc(sizeof(*record));assert(record);ts_instrument_init(record);
    ts_tapehead_stop();
    /* Save two genuinely different projects, including state on hidden lanes. */
    for(int project=0;project<2;++project) {
        for(int lane=0;lane<TS_TRACKER_LANES;++lane) {
            editor.channelMuted[lane]=(lane%2)==project;
            setChannelMute(lane,editor.channelMuted[lane]);
            channelVolumeTrim[lane]=project?256:160;
            fastTracksPOCSetMode(lane,project?FAST_TRACKS_MODE_STANDARD:FAST_TRACKS_MODE_PATTERN);
            fastTracksPOCSetRatioIndex(lane,project?7:8);
            fastTracksPOCSetDirection(lane,project?0:2);
            fastTracksPOCSetTrackLength(0,lane,project?0:48);
            test_pages->tracker.lanes[lane].output_route=(TsSourceRoute){
                .mode=project?TS_SOURCE_INHERIT:TS_SOURCE_PAIR,.speaker=2,.second=3,.pan=-35};
        }
        ts_tapehead_routes_changed(0,(TsSourceRoute){0});
        fastTracksPOCSetControlTrack(0,project?-1:2);
        editor.editRowSkip=project?1:4;
        export_score();
        save_tracker_project(project?"project-load-b/project-load-b.tsr":"project-load-a/project-load-a.tsr");
        if(project) {
            free(test_pages->tracker.embedded_data);test_pages->tracker.embedded_data=NULL;
            test_pages->tracker.embedded_size=0;
            save_tracker_project("project-load-native/project-load-native.tsr");
        }
    }
    const char *paths[]={"project-load-a/project-load-a.tsr","project-load-b/project-load-b.tsr",
        "project-load-a/project-load-a.tsr","project-load-native/project-load-native.tsr"};
    for(unsigned pass=0;pass<sizeof(paths)/sizeof(paths[0]);++pass) {
        /* These live-only flags must not leak into the next project. */
        performanceMute[0]=performanceMute[31]=true;
        editor.songPos=song.songPos=7;
        assert(load_instrument(0,test_audio,test_ui,test_bank,test_pages,record,0,paths[pass]));
        uint64_t loaded_hash=ts_sister_tracker_hash(&test_pages->tracker);
        export_score();assert(ts_sister_tracker_hash(&test_pages->tracker)==loaded_hash);
        /* A visible workspace also imports through the lightweight service
           path once the host marks a replacement; reopening is not required. */
        test_ui->tracker_open=1;
        tracker_refresh(0,test_audio,test_ui,test_pages,test_bank,48000,NULL);
        assert(embedded_open(test_window,0,test_audio,test_ui,test_pages,test_bank,48000));
        assert(!ts_tapehead_running());
        int project=pass%2;
        for(int lane=0;lane<TS_TRACKER_LANES;++lane) {
            assert(editor.channelMuted[lane]==((lane%2)==project));
            assert(channel[lane].channelOff==editor.channelMuted[lane]);
            assert(!performanceMute[lane]);
            assert(channelVolumeTrim[lane]==(project?256:160));
            assert(fastTracksPOCGetMode(lane)==(project?FAST_TRACKS_MODE_STANDARD:FAST_TRACKS_MODE_PATTERN));
            assert(fastTracksPOCGetRatioIndex(lane)==(project?7:8));
            assert(fastTracksPOCGetDirection(lane)==(project?0:2));
            assert(fastTracksPOCGetTrackLength(0,lane)==(project?0:48));
            assert(test_pages->tracker.lanes[lane].output_route.mode==(project?TS_SOURCE_INHERIT:TS_SOURCE_PAIR));
        }
        assert(editor.editRowSkip==(project?1:4));
        assert(fastTracksPOCGetControlTrack(0)==(project?-1:2));
        assert(editor.songPos==0 && song.songPos==0);
        /* Starting playback must not reinstate the previous editor mutes. */
        startPlaying(PLAYMODE_SONG,0);assert(ts_tapehead_render(512,48000));
        for(int lane=0;lane<song.numChannels;++lane)
            assert(channel[lane].channelOff==((lane%2)==project));
        ts_tapehead_stop();export_score();
        for(int lane=0;lane<TS_TRACKER_LANES;++lane)
            assert(test_pages->tracker.lanes[lane].muted==((lane%2)==project));
        project_roundtrip();
    }
    /* Reloading the identical checkpoint must discard live edits too. Its
       saved hash equals the old native mirror, so hash comparison alone
       cannot identify this as a replacement. An immediate save is safe. */
    save_tracker_project("project-load-same/project-load-same.tsr");
    uint64_t same_hash=ts_sister_tracker_hash(&test_pages->tracker);
    int pattern_index=editor.editPattern;note_t original=pattern[pattern_index][0];
    editor.row=0;cursor.ch=0;cursor.object=CURSOR_NOTE;playMode=PLAYMODE_EDIT;
    SDL_Event note={0};note.type=SDL_KEYDOWN;note.key.windowID=SDL_GetWindowID(test_window);
    note.key.keysym.sym=SDLK_x;note.key.keysym.scancode=SDL_SCANCODE_X;
    assert(tracker_event(&note,test_window,0,test_audio,test_ui,test_pages,test_bank,48000));
    note.type=SDL_KEYUP;assert(tracker_event(&note,test_window,0,test_audio,test_ui,test_pages,test_bank,48000));
    assert(memcmp(&original,&pattern[pattern_index][0],sizeof(original)));
    assert(ts_sister_tracker_hash(&test_pages->tracker)==same_hash);
    assert(load_instrument(0,test_audio,test_ui,test_bank,test_pages,record,0,"project-load-same/project-load-same.tsr"));
    export_score();assert(ts_sister_tracker_hash(&test_pages->tracker)==same_hash);
    test_ui->tracker_open=1;
    tracker_refresh(0,test_audio,test_ui,test_pages,test_bank,48000,NULL);
    assert(!memcmp(&original,&pattern[pattern_index][0],sizeof(original)));
    ts_instrument_free(record);free(record);
}
