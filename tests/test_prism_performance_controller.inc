static void test_prism_performance_ui(void)
{
    AudioState *a=routing_audio(0);SisterWindow *s=calloc(1,sizeof(*s));assert(s);
    ts_sister_ui_model_init(&s->model,&ui.config);s->model.parameters=a->sister.parameters;s->model.fx_page=3;
    s->window=SDL_CreateWindow("Prism performance",0,0,1280,800,SDL_WINDOW_HIDDEN);assert(s->window);
    s->window_id=SDL_GetWindowID(s->window);
    assert(sister_prism_performance_click(0,a,s,120,260,0,KMOD_NONE) && s->model.prism_panel==1);
    assert(ts_sister_ui_prism_capture_visual(&s->model,0)==TS_PRISM_CAPTURE_EMPTY);
    assert(sister_prism_performance_click(0,a,s,25,290,1,KMOD_NONE));
    assert(!s->model.prism_edit_endpoint && !a->sister.parameters.prism.captured);
    assert(sister_prism_performance_click(0,a,s,25,290,0,KMOD_NONE));
    assert(a->sister.parameters.prism.captured==1);
    assert(ts_sister_ui_prism_capture_visual(&s->model,0)==TS_PRISM_CAPTURE_READY);
    assert(sister_prism_performance_click(0,a,s,25,290,1,KMOD_NONE));
    assert(ts_sister_ui_prism_capture_visual(&s->model,0)==TS_PRISM_CAPTURE_EDITING);
    sister_prism_edit(0,a,s,2,PRISM_TRIM,1,0);
    assert(ts_sister_ui_prism_capture_visual(&s->model,0)==TS_PRISM_CAPTURE_DIRTY);
    assert(a->sister.parameters.prism.a.trim_db[2]==0);
    sister_prism_edit(0,a,s,2,PRISM_TRIM,-1,0);
    assert(ts_sister_ui_prism_capture_visual(&s->model,0)==TS_PRISM_CAPTURE_EDITING);
    sister_prism_edit(0,a,s,2,PRISM_SEQUENCE,0,0);
    assert(ts_sister_ui_prism_capture_visual(&s->model,0)==TS_PRISM_CAPTURE_EDITING);
    sister_prism_edit(0,a,s,2,PRISM_SEQUENCE,0,0);
    assert(sister_prism_performance_click(0,a,s,25,290,0,KMOD_NONE));
    assert(ts_sister_ui_prism_capture_visual(&s->model,0)==TS_PRISM_CAPTURE_READY);
    s->model.parameters.prism.mode=TS_PRISM_FIFTHS;s->model.parameters.prism.lenses=24;
    ts_sister_runtime_set_parameters(&a->sister,&s->model.parameters);
    assert(sister_prism_performance_click(0,a,s,100,290,0,KMOD_NONE));
    assert(a->sister.parameters.prism.captured==3);
    const int sliders[][3]={{385,288,TS_SISTER_UI_PARAM_PRISM_MORPH},{543,288,TS_SISTER_UI_PARAM_PRISM_TIME},
        {385,316,TS_SISTER_UI_PARAM_PRISM_SEQ_RATE},{87,344,TS_SISTER_UI_PARAM_PRISM_RATE},{241,344,TS_SISTER_UI_PARAM_PRISM_OCTAVE}};
    for(unsigned i=0;i<sizeof(sliders)/sizeof(*sliders);++i) {
        TsSisterUiHit hit=ts_sister_ui_hit_test_model(&s->model,sliders[i][0],sliders[i][1]);
        assert(hit.action==TS_SISTER_UI_ACTION_PARAMETER && hit.index==sliders[i][2]);
        char target[80];assert(ts_sister_ui_midi_target(hit,target,sizeof(target)));
        assert(midi_sister_hit_from_target(target,.5f,&hit));
        sister_apply_action(0,a,&ui,&instrument,s,NULL,NULL,hit,48000,2);
        if(i==0) {
            assert(a->sister.parameters.prism.morph_enabled && a->sister.parameters.prism.morph==.5f);
            assert(sister_prism_performance_click(0,a,s,25,290,1,KMOD_NONE));
        }
    }
    assert(sister_prism_performance_click(0,a,s,245,290,0,KMOD_NONE));
    assert(a->sister.parameters.prism.morph_trigger && a->sister.parameters.prism.morph_target==1);
    assert(sister_prism_performance_click(0,a,s,100,290,1,KMOD_NONE));
    assert(!a->sister.parameters.prism.morph_enabled && a->sister.parameters.prism.mode==TS_PRISM_FIFTHS);
    assert(sister_prism_performance_click(0,a,s,100,318,0,KMOD_NONE) && s->model.prism_seq_edit);
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_LEFT;e.button.y=(TS_PRISM_STRIP_Y+4)*2;
    SDL_SetModState(KMOD_CTRL);
    const int order[]={20,2,7};
    for(int j=0;j<3;++j) {
        e.button.x=(TS_PRISM_STRIP_X+order[j]*TS_PRISM_STRIP_STEP+7)*2;
        assert(sister_prism_event(0,a,&ui,s,&e));
    }
    assert(a->sister.parameters.prism.seq_count==3 && !a->sister.parameters.prism.solo_mask);
    s->model.parameters.prism.lenses=5;ts_sister_runtime_set_parameters(&a->sister,&s->model.parameters);
    e.button.x=(TS_PRISM_STRIP_X+20*TS_PRISM_STRIP_STEP+7)*2;
    assert(sister_prism_event(0,a,&ui,s,&e));assert(a->sister.parameters.prism.seq_count==2 && a->sister.parameters.prism.sequence[0]==2);
    SDL_SetModState(KMOD_NONE);
    s->model.prism_selected=3;s->model.parameters.prism.output_shape=TS_PRISM_BICONCAVE;s->model.parameters.prism.focus=0;s->model.parameters.prism.drift=0;
    ts_sister_runtime_set_parameters(&a->sister,&s->model.parameters);
    float before=ts_prism_control_view(&a->sister.parameters.prism).lens[2].cents;
    e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_UP;e.key.keysym.mod=0;
    assert(sister_prism_event(0,a,&ui,s,&e));
    assert(fabsf(ts_prism_control_view(&a->sister.parameters.prism).lens[2].cents-before-1)<.01f);
    e.key.keysym.mod=KMOD_SHIFT;assert(sister_prism_event(0,a,&ui,s,&e));
    assert(!a->sister.parameters.prism.octave_offset[2]);
    assert(fabsf(ts_prism_control_view(&a->sister.parameters.prism).lens[2].cents-before-101)<.01f);
    e.key.keysym.mod=KMOD_CTRL|KMOD_SHIFT;assert(sister_prism_event(0,a,&ui,s,&e));
    assert(a->sister.parameters.prism.octave_offset[2]==1);
    assert(fabsf(ts_prism_control_view(&a->sister.parameters.prism).lens[2].cents-before-1301)<.01f);
    /* All glass maps must give the same audible pan increments and hard ends.
       Include the body and a voice near the edge of automatic Stereo. */
    for(int shape=0;shape<TS_PRISM_SHAPE_COUNT;++shape)for(int body=0;body<2;++body) {
        int lens=body?0:8;s->model.prism_selected=lens+1;
        TsPrismControls c=a->sister.parameters.prism;
        c.lenses=24;c.stereo=1;c.output_shape=shape;c.pan_offset[lens]=0;
        sister_prism_publish(0,a,s,&c);
        float pan_before=ts_prism_control_view(&c).lens[lens].pan;
        e.key.keysym.sym=SDLK_LEFT;e.key.keysym.mod=KMOD_CTRL|KMOD_SHIFT;
        assert(sister_prism_event(0,a,&ui,s,&e));
        assert(fabsf(ts_prism_control_view(&a->sister.parameters.prism).lens[lens].pan+1)<.00001f);
        e.key.keysym.sym=SDLK_RIGHT;e.key.keysym.mod=KMOD_NONE;
        assert(sister_prism_event(0,a,&ui,s,&e));
        assert(fabsf(ts_prism_control_view(&a->sister.parameters.prism).lens[lens].pan+.99f)<.00001f);
        e.key.keysym.mod=KMOD_SHIFT;assert(sister_prism_event(0,a,&ui,s,&e));
        assert(fabsf(ts_prism_control_view(&a->sister.parameters.prism).lens[lens].pan+.89f)<.00001f);
        e.key.keysym.mod=KMOD_CTRL|KMOD_SHIFT;assert(sister_prism_event(0,a,&ui,s,&e));
        assert(fabsf(ts_prism_control_view(&a->sister.parameters.prism).lens[lens].pan-1)<.00001f);
        e.key.keysym.sym=SDLK_LEFT;e.key.keysym.mod=KMOD_SHIFT;
        assert(sister_prism_event(0,a,&ui,s,&e));
        assert(fabsf(ts_prism_control_view(&a->sister.parameters.prism).lens[lens].pan-.9f)<.00001f);
        e.key.keysym.mod=KMOD_NONE;assert(sister_prism_event(0,a,&ui,s,&e));
        assert(fabsf(ts_prism_control_view(&a->sister.parameters.prism).lens[lens].pan-.89f)<.00001f);
        c.pan_offset[lens]=sister_prism_pan_offset(&c,lens,pan_before);
        assert(fabsf(ts_prism_control_view(&c).lens[lens].pan-pan_before)<.00001f);
    }
    /* Downward semitones/cents and octave clamps; full Focus keeps octave control. */
    s->model.prism_selected=3;
    TsPrismControls keyed=a->sister.parameters.prism;
    keyed.output_shape=TS_PRISM_BICONCAVE;keyed.focus=0;
    sister_prism_publish(0,a,s,&keyed);
    before=ts_prism_control_view(&keyed).lens[2].cents;
    e.key.keysym.sym=SDLK_DOWN;e.key.keysym.mod=KMOD_SHIFT;
    assert(sister_prism_event(0,a,&ui,s,&e));
    e.key.keysym.mod=KMOD_NONE;assert(sister_prism_event(0,a,&ui,s,&e));
    assert(fabsf(ts_prism_control_view(&a->sister.parameters.prism).lens[2].cents-before+101)<.01f);
    keyed=a->sister.parameters.prism;keyed.focus=1;sister_prism_publish(0,a,s,&keyed);
    e.key.keysym.mod=KMOD_CTRL|KMOD_SHIFT;
    for(int i=0;i<8;++i)assert(sister_prism_event(0,a,&ui,s,&e));
    assert(a->sister.parameters.prism.octave_offset[2]==-3);
    e.key.keysym.sym=SDLK_UP;
    for(int i=0;i<8;++i)assert(sister_prism_event(0,a,&ui,s,&e));
    assert(a->sister.parameters.prism.octave_offset[2]==3);
    e.key.keysym.mod=KMOD_CTRL;assert(!sister_prism_event(0,a,&ui,s,&e));
    /* Morph locks still consume edits without changing the captured/playing sound. */
    keyed=a->sister.parameters.prism;keyed.morph_enabled=1;sister_prism_publish(0,a,s,&keyed);
    e.key.keysym.mod=KMOD_CTRL|KMOD_SHIFT;assert(sister_prism_event(0,a,&ui,s,&e));
    assert(!memcmp(&keyed,&a->sister.parameters.prism,sizeof(keyed)));
    assert(strstr(s->model.status,"MORPH LOCKED"));
    assert(sister_prism_performance_click(0,a,s,25,290,1,KMOD_NONE));

    /* Shared sustain, exact recording hit area, and two-line hover help. */
    for(int page=0;page<3;++page) {
        s->model.prism_panel=page;
        TsSisterUiHit rec=ts_sister_ui_hit_test_model(&s->model,TS_PRISM_REC_X+4,TS_PRISM_FOOTER_Y+4);
        assert(rec.action==TS_SISTER_UI_ACTION_RECORD_FILE);
        rec=ts_sister_ui_hit_test_model(&s->model,580,339);assert(rec.action!=TS_SISTER_UI_ACTION_RECORD_FILE);
        for(int x=0;x<640;x+=5)for(int y=0;y<400;y+=5) {
            char help[192];ts_sister_ui_prism_help(&s->model,x,y,help,sizeof(help));
            size_t split=strlen(help);
            if(split>63) { split=63;while(split && help[split]!=' ')--split; }
            if(strlen(help)>split && strlen(help+split+1)>63) {
                fprintf(stderr,"Tooltip too long at %d,%d page %d: %s\n",x,y,page,help);assert(0);
            }
        }
    }
    s->model.prism_panel=1;
    char help[192];ts_sister_ui_prism_help(&s->model,180,318,help,sizeof(help));assert(strstr(help,"SEQUENCER RESET"));
    ts_sister_ui_prism_help(&s->model,245,318,help,sizeof(help));assert(strstr(help,"SEQUENCER CLEAR"));
    e.type=SDL_MOUSEMOTION;e.motion.x=180*2;e.motion.y=318*2;
    sister_prism_event(0,a,&ui,s,&e);assert(s->model.prism_hover_valid && s->model.prism_hover_x==180);
    e.type=SDL_WINDOWEVENT;e.window.event=SDL_WINDOWEVENT_LEAVE;
    sister_prism_event(0,a,&ui,s,&e);assert(!s->model.prism_hover_valid);
    e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=s->window_id;e.button.button=SDL_BUTTON_LEFT;
    e.button.x=(TS_PRISM_SUSTAIN_X+4)*2;e.button.y=(TS_PRISM_FOOTER_Y+4)*2;
    int sustain_before=ui.keyboard_sustain;
    assert(keyboard_sustain_event(&e,s->window,0,a,&ui,s,&instrument));
    assert(ui.keyboard_sustain!=sustain_before && s->model.keyboard_sustain==ui.keyboard_sustain);
    assert(keyboard_sustain_event(&e,s->window,0,a,&ui,s,&instrument));
    s->model.prism_panel=0;assert(!sister_prism_performance_click(0,a,s,420,290,0,KMOD_NONE));
    /* Save/recall affects Prism alone, including when tape/FX have changed. */
    s->model.prism_panel=2;
    assert(sister_prism_performance_click(0,a,s,350,290,0,KMOD_NONE));
    assert(s->prism_presets.count==1);
    TsPrismControls saved=s->prism_presets.entries[0].controls;
    s->model.parameters.head1_level=.37f;s->model.parameters.fx.slot[0].mix=.123f;
    ts_sister_runtime_set_parameters(&a->sister,&s->model.parameters);
    assert(sister_prism_performance_click(0,a,s,450,290,0,KMOD_NONE));
    assert(memcmp(&saved,&a->sister.parameters.prism,sizeof(saved)));
    assert(sister_prism_performance_click(0,a,s,100,290,0,KMOD_NONE));
    assert(!memcmp(&saved,&a->sister.parameters.prism,sizeof(saved)));
    assert(a->sister.parameters.head1_level==.37f && a->sister.parameters.fx.slot[0].mix==.123f);
    char bank_path[TS_RUNTIME_PATH_MAX];assert(prism_presets_file_path(bank_path,sizeof(bank_path)));remove(bank_path);
    /* Render the real native UI, including a dense 24-lens sequence. */
    s->model.parameters.prism.lenses=24;s->model.parameters.prism.enabled=1;
    s->model.parameters.prism.seq_count=3;memcpy(s->model.parameters.prism.sequence,order,sizeof(order));
    const char *folder=getenv("TS_PRISM_PERFORMANCE_SHOTS");
    if(folder) {
        TsPalette palette;ts_palette_default(&palette);
        TsFramebuffer *fb=calloc(1,sizeof(*fb));assert(fb);
        for(int page=0;page<3;++page) {
            s->model.prism_panel=page;ts_sister_ui_render(fb,&s->model,&palette);
            char path[1024];snprintf(path,sizeof(path),"%s/prism-%d.ppm",folder,page);assert(ts_ui_write_ppm(fb,path));
        }
        s->model.prism_panel=1;s->model.prism_hover_valid=1;
        s->model.prism_hover_x=180;s->model.prism_hover_y=318;
        s->model.parameters.prism.captured=0;s->model.prism_edit_endpoint=0;
        for(int state=0;state<5;++state) {
            if(state==1) { ts_prism_capture(&s->model.parameters.prism,0);ts_prism_capture(&s->model.parameters.prism,1); }
            if(state==2)s->model.prism_edit_endpoint=1;
            if(state==3)s->model.parameters.prism.trim_db[1]+=1;
            if(state==4) { s->model.parameters.prism.morph_enabled=1;s->model.parameters.prism.morph=.5f; }
            ts_sister_ui_render(fb,&s->model,&palette);
            char path[1024];snprintf(path,sizeof(path),"%s/prism-state-%d.ppm",folder,state);assert(ts_ui_write_ppm(fb,path));
        }
        s->model.file_capture_state=TS_PERFORMANCE_FILE_RECORDING;
        s->model.file_capture_sample_rate=48000;s->model.file_capture_frames=48000*123;
        s->model.text_cursor_visible=1;s->model.prism_hover_x=245;
        ts_sister_ui_render(fb,&s->model,&palette);
        char path[1024];snprintf(path,sizeof(path),"%s/prism-recording.ppm",folder);assert(ts_ui_write_ppm(fb,path));
        free(fb);
    }
    SDL_DestroyWindow(s->window);free(s);routing_free(a);
    puts("Prism performance gestures, independent sequence selection, MIDI and octave nudges passed.");
}
