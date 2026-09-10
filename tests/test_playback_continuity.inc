/* Exercise the shared controller and real sample streams, not just flags. */
static void test_keyboard_continuity(SDL_Window *window,SDL_AudioDeviceID device,
    AudioState *audio,TsUiState *ui,TsInstrument *instrument)
{
    TsSample fm;char error[160];ts_sample_init(&fm);
    ts_fm_patch_from_recipe(&instrument->generator,&ui->fm_patch);
    assert(ts_fm_render_sample(&fm,&ui->fm_patch,.04f,261.6256f,44100,71,error,sizeof(error)));
    instrument->has_loop=0;instrument->has_selection=0;instrument->view_first=0;
    instrument->view_last=instrument->current.frames;audio->output_rate=44100;
    uint64_t hash=ts_sample_hash(&instrument->current);
    SDL_Event pointer={0};pointer.type=SDL_MOUSEBUTTONDOWN;pointer.button.button=SDL_BUTTON_LEFT;
    pointer.button.windowID=SDL_GetWindowID(window);pointer.button.x=25;pointer.button.y=368;
    SDL_SetModState(KMOD_NONE);
    for(int synth=0;synth<2;++synth)for(int origin=0;origin<3;++origin) {
        stop_all_force(device,audio,ui);ui->fm_open=synth;ui->show_keyboard=1;
        TsNoteEvent note;
        if(origin==2)assert(ts_note_event_midi(&note,60,127,2));
        else assert(ts_note_event_qwerty(&note,0,60));
        ts_note_bank_set_sustain(&audio->notes,1);
#define CONT_START() do { if(origin==0) {pointer.type=SDL_MOUSEBUTTONDOWN;assert(keyboard_pointer_event(&pointer,window,device,audio,ui,instrument,&fm,44100));} else if(synth)begin_fm_note_event(device,audio,ui,instrument,&fm,&note,44100,0);else begin_note_event(device,audio,ui,instrument,&note,44100,0); } while(0)
        CONT_START();const TsNoteVoice *v=ts_note_bank_display_voice(&audio->notes);
        assert(v && !v->latched && !v->looping && !ui->keyboard_hold);
        ts_note_bank_release_event(&audio->notes,&note);
        for(size_t i=0;i<(synth?fm.frames:instrument->current.frames)+2;++i)(void)ts_note_bank_read(&audio->notes);
        assert(!ts_note_bank_count(&audio->notes));
        CONT_START();ts_note_bank_set_sustain(&audio->notes,0);
        /* Even opening a dialog before mouse-up must release the owned note. */
        if(origin==0) {ui->config_open=1;pointer.type=SDL_MOUSEBUTTONUP;assert(keyboard_pointer_event(&pointer,window,device,audio,ui,instrument,&fm,44100));ui->config_open=0;}
        else ts_note_bank_release_event(&audio->notes,&note);
        assert(!ts_note_bank_count(&audio->notes));
        /* HOLD can be armed before playing; neither pointer nor MIDI/QWERTY
           needs an extra Shift gesture. It survives parameter-only sync. */
        toggle_fm_hold(device,audio,ui,instrument);assert(ui->keyboard_hold);
        CONT_START();v=ts_note_bank_display_voice(&audio->notes);assert(v && v->looping && v->latched);
        ts_note_bank_release_event(&audio->notes,&note);
        if(synth)ts_note_bank_replace_sample(&audio->notes,&fm,&fm,44100);
        runtime_note_sync(audio,instrument,44100);
        assert(v->sample==(synth?&fm:&instrument->current));
        for(size_t i=0;i<(synth?fm.frames:instrument->current.frames)*3;++i)assert(isfinite(ts_note_bank_read(&audio->notes)));
        assert(ts_note_bank_count(&audio->notes)==1);
        toggle_fm_hold(device,audio,ui,instrument);assert(!ui->keyboard_hold && !ts_note_bank_count(&audio->notes));
        /* LOOP retargets played notes and never starts an extra C4 voice. */
        CONT_START();toggle_workbench_loop(device,audio,ui,instrument,44100,0);
        v=ts_note_bank_display_voice(&audio->notes);assert(v->looping && !v->latched && !audio->playing);
        ts_note_bank_set_sustain(&audio->notes,1);ts_note_bank_release_event(&audio->notes,&note);
        for(size_t i=0;i<(synth?fm.frames:instrument->current.frames)*2;++i)(void)ts_note_bank_read(&audio->notes);
        assert(ts_note_bank_count(&audio->notes)==1);
        refresh_workbench_loop(device,audio,ui,instrument);assert(!audio->playing && v->active);
        toggle_workbench_loop(device,audio,ui,instrument,44100,0);assert(!v->looping && !audio->playing);
        stop_all_force(device,audio,ui);
#undef CONT_START
    }
    /* Shift-click is an individual latch; it never turns on global HOLD.
       Without LOOP a latched note finishes once, in both workspaces. */
    for(int synth=0;synth<2;++synth) {
        ui->fm_open=synth;ui->show_keyboard=1;pointer.type=SDL_MOUSEBUTTONDOWN;SDL_SetModState(KMOD_SHIFT);
        assert(keyboard_pointer_event(&pointer,window,device,audio,ui,instrument,&fm,44100));
        const TsNoteVoice *v=ts_note_bank_display_voice(&audio->notes);
        assert(v && v->latched && !v->looping && !ui->keyboard_hold);
        for(size_t i=0;i<(synth?fm.frames:instrument->current.frames)+2;++i)(void)ts_note_bank_read(&audio->notes);
        assert(!ts_note_bank_count(&audio->notes));
        assert(keyboard_pointer_event(&pointer,window,device,audio,ui,instrument,&fm,44100));
        toggle_workbench_loop(device,audio,ui,instrument,44100,0);
        assert(ts_note_bank_display_voice(&audio->notes)->looping && !ui->keyboard_hold);
        assert(keyboard_pointer_event(&pointer,window,device,audio,ui,instrument,&fm,44100));
        assert(!ts_note_bank_count(&audio->notes));
        stop_all_force(device,audio,ui);
    }
    SDL_SetModState(KMOD_NONE);ui->fm_open=0;ui->show_keyboard=1;
    /* Two ordinary mouse presses use the same additive voice bank as QWERTY. */
    ts_note_bank_set_sustain(&audio->notes,1);
    assert(keyboard_pointer_event(&pointer,window,device,audio,ui,instrument,&fm,44100));
    pointer.type=SDL_MOUSEBUTTONUP;assert(keyboard_pointer_event(&pointer,window,device,audio,ui,instrument,&fm,44100));
    pointer.type=SDL_MOUSEBUTTONDOWN;pointer.button.x=120;
    assert(keyboard_pointer_event(&pointer,window,device,audio,ui,instrument,&fm,44100));
    assert(ts_note_bank_count(&audio->notes)==2);
    assert(ts_sample_hash(&instrument->current)==hash);
    shot("keyboard-controls",ui,instrument);ui->fm_open=1;ui->fm_preview_sample=&fm;shot("fm-keyboard-controls",ui,instrument);
    stop_all_force(device,audio,ui);ui->fm_open=0;ui->fm_preview_sample=NULL;
    static SisterWindow row_sister;
    pointer.type=SDL_MOUSEBUTTONDOWN;pointer.button.x=490;pointer.button.y=320;
    assert(keyboard_sustain_event(&pointer,window,device,audio,ui,&row_sister,instrument));
    assert(ui->keyboard_hold);
    assert(keyboard_sustain_event(&pointer,window,device,audio,ui,&row_sister,instrument));
    assert(!ui->keyboard_hold);
    ts_note_bank_set_sustain(&audio->notes,0);ts_sample_free(&fm);
}

static void test_start_loops(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    static TsInstrument instrument,loaded;TsSample source,decoded;char error[160];
    ts_instrument_init(&instrument);ts_instrument_init(&loaded);ts_sample_init(&source);ts_sample_init(&decoded);
    source.frames=32;source.sample_rate=44100;source.channels=2;source.data=malloc(64*sizeof(float));assert(source.data);
    for(int i=0;i<32;++i){source.data[i*2]=(float)(i+1)/64;source.data[i*2+1]=-source.data[i*2];}
    TsTuning unity={60,0};TsNoteEvent event;assert(ts_note_event_qwerty(&event,0,60));
    for(int mode=TS_LOOP_START_FORWARD;mode<TS_LOOP_MODE_COUNT;++mode) {
        assert(ts_instrument_import_sample(&instrument,&source,1,8,16,(TsLoopMode)mode,error,sizeof(error)));
        instrument.loop_crossfade_ms=0;instrument.bank[0].loop_crossfade_ms=0;
        TsNoteBank notes;ts_note_bank_init(&notes);ts_note_bank_set_attack_ms(&notes,0);
        assert(ts_note_bank_start_tuned_event(&notes,&instrument,&unity,TS_AUDITION_CURRENT,&event,0,44100)==TS_NOTE_STARTED);
        TsNoteVoice *v=&notes.voices[0];assert(v->position==0 && v->loop_intro && v->direction==1);
        for(int i=0;i<16;++i) {
            if(i==4){ts_note_bank_sync(&notes,&instrument,44100);assert(v->position==4 && v->loop_intro);}
            TsStereoFrame f=ts_note_bank_read_stereo(&notes);
            assert(fabsf(f.l-source.data[i*2])<.00001f && f.r==-f.l);
        }
        for(int i=0;i<80;++i) {
            TsStereoFrame f=ts_note_bank_read_stereo(&notes);assert(isfinite(f.l) && f.l>=9.0f/64 && f.l<=16.0f/64 && f.r==-f.l);
            assert(!v->loop_intro);
        }
        /* Tile launches and routed group notes share attack/loop behavior. */
        TsPerformanceBank *bank=calloc(1,sizeof(*bank));assert(bank);ts_performance_init(bank);ts_performance_set_attack_ms(bank,0);
        assert(ts_performance_trigger_group_event(bank,&instrument,1,&event,0,44100)==1);
        for(int i=0;i<16;++i){TsStereoFrame f=ts_performance_read_stereo(bank,NULL);assert(fabsf(f.l-source.data[i*2])<.00001f && f.r==-f.l);}
        for(int i=0;i<80;++i){TsStereoFrame f=ts_performance_read_stereo(bank,NULL);assert(f.l>=9.0f/64 && f.l<=16.0f/64 && f.r==-f.l);}
        ts_performance_clear(bank);
        instrument.has_loop=0;instrument.bank[0].has_loop=0;bank->keyboard_loop=1;
        assert(ts_performance_trigger_group_event(bank,&instrument,1,&event,0,44100)==1);
        for(int i=0;i<40;++i)(void)ts_performance_read_stereo(bank,NULL);
        bank->keyboard_loop=0;ts_performance_sync(bank,&instrument,44100);
        TsPerformanceVoice *group=&bank->voices[0];
        assert(group->active && !group->looping && group->direction==1 && !group->loop_intro);
        double before=group->position;
        (void)ts_performance_read_stereo(bank,NULL);assert(group->position>before);
        ts_performance_free(bank);free(bank);
        instrument.has_loop=1;instrument.bank[0].has_loop=1;
        /* The standalone PLAY LOOP path also includes the attack. */
        ui->audition_source=TS_AUDITION_CURRENT;begin_audition(device,audio,ui,&instrument,TS_AUDITION_LOOP,1,44100);
        assert(audio->position==0 && audio->loop_intro && audio->range_start==8 && audio->range_end==16);
        stop_all_force(device,audio,ui);
        char path[160];snprintf(path,sizeof(path),"/tmp/tapesister-start-loop-%d.wav",mode);
        assert(ts_sample_save_wav16_tuned_looped(&source,&unity,1,8,16,(TsLoopMode)mode,path,error,sizeof(error)));
        int has=0;size_t first=0,last=0;TsLoopMode read_mode;
        assert(ts_sample_load_wav_metadata(&decoded,&unity,&has,&first,&last,&read_mode,path,error,sizeof(error)));
        assert(has && first==8 && last==16 && read_mode==(TsLoopMode)mode);
        /* A reader ignoring the private chunk still sees the standard loop.
           No new type values are written into smpl. */
        FILE *wav=fopen(path,"r+b");assert(wav);assert(!fseek(wav,104,SEEK_SET));
        char tag[4];assert(fread(tag,1,4,wav)==4 && !memcmp(tag,"tslp",4));
        assert(!fseek(wav,104,SEEK_SET));assert(fwrite("JUNK",1,4,wav)==4);fclose(wav);
        assert(ts_sample_load_wav_metadata(&decoded,&unity,&has,&first,&last,&read_mode,path,error,sizeof(error)));
        assert(has && first==8 && last==16 && read_mode==ts_loop_base_mode((TsLoopMode)mode));remove(path);ts_sample_free(&decoded);
        snprintf(path,sizeof(path),"/tmp/tapesister-start-loop-%d.tsr",mode);
        assert(ts_instrument_save_recipe(&instrument,path,error,sizeof(error)));
        assert(ts_instrument_load_recipe(&loaded,path,error,sizeof(error)));
        assert(loaded.loop_mode==(TsLoopMode)mode && loaded.bank[0].loop_mode==(TsLoopMode)mode);
        assert(ts_instrument_set_loop_mode(&instrument,TS_LOOP_FORWARD,error,sizeof(error)));
        assert(ts_instrument_undo(&instrument,error,sizeof(error)) && instrument.loop_mode==(TsLoopMode)mode);
        /* High-rate notes must land inside the loop after crossing the attack. */
        v->position=1000.25;v->loop_intro=1;v->direction=1;
        TsStereoFrame f=ts_note_bank_read_stereo(&notes);assert(isfinite(f.l) && f.l>=9.0f/64 && f.l<=16.0f/64 && f.r==-f.l);
    }
    ts_sample_free(&source);ts_instrument_free(&loaded);ts_instrument_free(&instrument);
}

static void assert_caret_region(const TsFramebuffer *a,const TsFramebuffer *b,int x,int y,int w,int h)
{
    int changed=0;
    for(int yy=0;yy<TS_UI_HEIGHT;++yy)for(int xx=0;xx<TS_UI_WIDTH;++xx)
        if(a->pixels[yy*TS_UI_WIDTH+xx]!=b->pixels[yy*TS_UI_WIDTH+xx]) {
            assert(xx>=x && xx<x+w && yy>=y && yy<y+h);++changed;
        }
    assert(changed>=9);
}
static void test_continuity_ui(TsUiState *ui,TsInstrument *instrument)
{
    static TsFramebuffer off,on;
    ts_portal_ui_init(&ui->portal);TsPortalUi *p=&ui->portal;p->open=1;
    ts_portal_recipe_default(&p->recipe,ts_portal_process_find("modify.loudness.1"));
    const char *names[]={"","A","A LONG PERSONAL RECIPE WITH A LONG NAME"};
    for(int manager=0;manager<2;++manager)for(int n=0;n<3;++n) {
        p->name_focus=!manager;p->manage_open=manager;p->manage_name_focus=manager;
        snprintf(manager?p->manage_name:p->recipe.name,40,"%s",names[n]);
        ui->text_cursor_visible=0;ts_ui_render(&off,ui,instrument);
        ui->text_cursor_visible=1;ts_ui_render(&on,ui,instrument);
        if(manager)assert_caret_region(&off,&on,310,178,232,17);
        else assert_caret_region(&off,&on,478,242,148,17);
    }
    shot("portal-rename-caret",ui,instrument);
    p->open=0;p->name_focus=p->manage_name_focus=p->manage_open=0;
    ui->fm_open=0;ui->bank_view_slot=-1;ui->audition_source=TS_AUDITION_CURRENT;
    instrument->has_loop=0;instrument->has_selection=0;instrument->view_first=0;instrument->view_last=32;
    uint64_t hash=ts_sample_hash(&instrument->current);
    ts_ui_render(&on,ui,instrument);
    shot("waveform-sample-zoom",ui,instrument);
    /* At sub-sample zoom, every column contains the continuous signal trace. */
    uint32_t wave=ui->palette.colors[TS_PALETTE_STEREO_WAVE_LEFT];
    for(int x=TS_WAVE_X+12;x<TS_WAVE_X+TS_WAVE_W-12;++x) {
        int found=0;
        for(int y=TS_WAVE_Y+15;y<TS_WAVE_Y+TS_WAVE_H-15;++y)found|=(on.pixels[y*640+x]==wave || on.pixels[y*640+x]==ui->palette.colors[TS_PALETTE_PATTERN_VOLUME]);
        assert(found);
    }
    instrument->view_last=instrument->current.frames;
    ts_ui_render(&on,ui,instrument);shot("waveform-connected",ui,instrument);
    assert(ts_sample_hash(&instrument->current)==hash);
    instrument->has_loop=1;instrument->loop_first=1000;instrument->loop_last=2500;
    instrument->loop_mode=TS_LOOP_START_PING_PONG;ui->fx_page=TS_FX_LOOP;
    shot("loop-start-mode",ui,instrument);
    instrument->has_loop=0;instrument->loop_mode=TS_LOOP_FORWARD;
}
