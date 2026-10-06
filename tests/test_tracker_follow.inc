static void tracker_follow_and_selection(void)
{
    audit_blank_song(64);assert(ts_tapehead_following());editor.editRowSkip=1;
    ts_tapehead_tick();
    /* A mouse mark away from the edit row owns the Alt-arrow anchor. */
    for(int i=0;i<16;++i)press(SDLK_DOWN,SDL_SCANCODE_DOWN,KMOD_NONE);
    ts_tapehead_tick();
    int old_row=editor.row;
    const pattCoordsMouse_t *geometry=&pattCoordMouseTable[config.ptnStretch][ui.pattChanScrollShown][getPatternEditorView()];
    click(40,geometry->midRowY-24);
    int anchor=pattMark.markY1;assert(anchor!=old_row && pattMark.markY2==anchor+1);
    press(SDLK_DOWN,SDL_SCANCODE_DOWN,KMOD_ALT);
    assert(pattMark.markY1==anchor && pattMark.markY2==anchor+2 && editor.row==old_row);
    press(SDLK_RIGHT,SDL_SCANCODE_RIGHT,KMOD_ALT);
    assert(pattMark.markX1==0 && pattMark.markX2==1 && pattMark.markY1==anchor);
    press(SDLK_UP,SDL_SCANCODE_UP,KMOD_ALT);press(SDLK_LEFT,SDL_SCANCODE_LEFT,KMOD_ALT);
    assert(pattMark.markY1==anchor && pattMark.markY2==anchor+1 && pattMark.markX2==0);
    /* Moving the edit cursor does not move the established highlight. */
    press(SDLK_DOWN,SDL_SCANCODE_DOWN,KMOD_NONE);
    press(SDLK_DOWN,SDL_SCANCODE_DOWN,KMOD_ALT);
    assert(pattMark.markY1==anchor && pattMark.markY2==anchor+2);
    click(390,27);assert(ts_tapehead_running());
    press(SDLK_SPACE,SDL_SCANCODE_SPACE,KMOD_NONE);assert(!ts_tapehead_running());
    press(SDLK_SPACE,SDL_SCANCODE_SPACE,KMOD_NONE);assert(playMode==PLAYMODE_EDIT);

    /* Follow-off editing leaves the real row/tick/transport mode unchanged. */
    click(390,163);assert(!ts_tapehead_following());
    click(390,27);assert(ts_tapehead_running());
    uint8_t mode=playMode;int row=song.row,tick=song.tick;
    press(SDLK_SPACE,SDL_SCANCODE_SPACE,KMOD_NONE);
    assert(ts_tapehead_running() && ts_tapehead_live_editing() && playMode==mode);
    press(SDLK_END,SDL_SCANCODE_END,KMOD_NONE);assert(editor.row==63);
    press(SDLK_UP,SDL_SCANCODE_UP,KMOD_NONE);assert(editor.row==62);
    press(SDLK_z,SDL_SCANCODE_Z,KMOD_NONE);
    assert(pat()->cells[62][0].note_kind==TS_TRACKER_NOTE_PITCH && editor.row==63);
    press(SDLK_z,SDL_SCANCODE_Z,KMOD_CTRL);assert(pat()->cells[62][0].note_kind==TS_TRACKER_NOTE_NONE);
    press(SDLK_y,SDL_SCANCODE_Y,KMOD_CTRL);assert(pat()->cells[62][0].note_kind==TS_TRACKER_NOTE_PITCH);
    press(SDLK_UP,SDL_SCANCODE_UP,KMOD_NONE);
    press(SDLK_DELETE,SDL_SCANCODE_DELETE,KMOD_NONE);
    assert(pat()->cells[62][0].note_kind==TS_TRACKER_NOTE_NONE && editor.row==63);
    press(SDLK_UP,SDL_SCANCODE_UP,KMOD_NONE);press(SDLK_z,SDL_SCANCODE_Z,KMOD_NONE);
    press(SDLK_UP,SDL_SCANCODE_UP,KMOD_NONE);press(SDLK_BACKSPACE,SDL_SCANCODE_BACKSPACE,KMOD_NONE);
    assert(pat()->cells[62][0].note_kind==TS_TRACKER_NOTE_NONE && editor.row==61);
    assert(song.row==row && song.tick==tick && playMode==mode && ts_tapehead_running());
    TsMidiEvent note;assert(ts_midi_decode_short_message(0x90,67,100,&note));
    assert(ts_tapehead_midi(&note,1));note.action=TS_MIDI_ACTION_NOTE_OFF;assert(ts_tapehead_midi(&note,0));
    export_score();assert(pat()->cells[61][0].note==67 && song.row==row && song.tick==tick);
    SDL_Event held={0};held.type=SDL_KEYDOWN;held.key.windowID=SDL_GetWindowID(test_window);
    held.key.keysym.sym=SDLK_SPACE;held.key.keysym.scancode=SDL_SCANCODE_SPACE;held.key.repeat=1;
    assert(tracker_event(&held,test_window,0,test_audio,test_ui,test_pages,test_bank,48000));
    assert(ts_tapehead_running());
    /* Click and wheel navigation are available on the stationary pattern. */
    ts_tapehead_tick();click(40,geometry->midRowY-24);
    assert(editor.row==pattMark.markY1 && editor.row!=62 && song.row==row);
    int clicked=editor.row;wheel(40,300,-1);assert(editor.row==clicked+1 && song.row==row);
    export_score();assert(!test_pages->tracker.follow);project_roundtrip();
    /* Rendering and audio can advance without chasing the editor. */
    int fixed=editor.row;float out[1024];
    for(int i=0;i<24;++i)audio_callback(test_audio,(Uint8*)out,sizeof(out));
    ts_tapehead_tick();assert(editor.row==fixed && ts_tapehead_running());
    snapshot("tracker-follow-off.bmp");
    press(SDLK_SPACE,SDL_SCANCODE_SPACE,KMOD_NONE);
    assert(!ts_tapehead_running() && editor.row==fixed);
    /* Shortcut is available in the expanded pattern-only view as well. */
    press(SDLK_BACKSPACE,SDL_SCANCODE_BACKSPACE,KMOD_CTRL|KMOD_ALT);
    press(SDLK_f,SDL_SCANCODE_F,KMOD_CTRL);assert(ts_tapehead_following());
    press(SDLK_BACKSPACE,SDL_SCANCODE_BACKSPACE,KMOD_CTRL|KMOD_ALT);
    export_score();assert(test_pages->tracker.follow);
}
