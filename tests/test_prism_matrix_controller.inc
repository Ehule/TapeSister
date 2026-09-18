static void matrix_ui_click(AudioState *a,SisterWindow *s,int x,int y,int button)
{
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=s->window_id;
    e.button.x=x*2;e.button.y=y*2;e.button.button=button;
    assert(sister_prism_event(0,a,&ui,s,&e));
}
static void matrix_ui_wheel(AudioState *a,SisterWindow *s,int x,int y,int amount)
{
    SDL_Event e={0};e.type=SDL_MOUSEWHEEL;e.wheel.windowID=s->window_id;e.wheel.y=amount;
#if SDL_VERSION_ATLEAST(2,26,0)
    e.wheel.mouseX=x*2;e.wheel.mouseY=y*2;
#else
    SDL_WarpMouseInWindow(s->window,x*2,y*2);
#endif
    memset(&ui.wheel_guard,0,sizeof(ui.wheel_guard));assert(sister_prism_event(0,a,&ui,s,&e));
}
static void matrix_ui_audio(AudioState *a,SisterWindow *s,unsigned frames)
{
    for(unsigned n=0;n<frames;++n) {
        TsStereoFrame out=ts_prism_process(&a->sister.prism,(TsStereoFrame){.1f,-.2f});
        assert(isfinite(out.l) && isfinite(out.r));
    }
    s->model.routing.prism=ts_prism_view(&a->sister.prism);
}
static void test_prism_matrix_ui(void)
{
    AudioState *a=routing_audio(0);SisterWindow *s=calloc(1,sizeof(*s));assert(s);
    ts_sister_ui_model_init(&s->model,&ui.config);s->model.parameters=a->sister.parameters;
    s->model.fx_page=3;s->model.prism_panel=1;
    s->window=SDL_CreateWindow("Prism Morph Matrix",0,0,1280,800,SDL_WINDOW_HIDDEN);assert(s->window);
    s->window_id=SDL_GetWindowID(s->window);
    TsPrismControls c=a->sister.parameters.prism;c.enabled=1;c.morph_seconds=.05f;c.matrix.step_seconds=.1f;
    for(int i=0;i<TS_PRISM_STATES;++i) {c.lenses=2+i%(TS_PRISM_LENSES-1);c.pitch_offset[2]=i*31;ts_prism_capture(&c,i);}
    ts_prism_recall(&c,0);sister_prism_publish(0,a,s,&c);
    TsPrismControls before=c;
    matrix_ui_click(a,s,550,55,SDL_BUTTON_LEFT);assert(s->model.prism_panel==3);
    assert(!memcmp(&before,&a->sister.parameters.prism,sizeof(before)));
    assert(ts_sister_ui_hit_test_model(&s->model,130,55).action==TS_SISTER_UI_ACTION_NONE);
    /* Both rows expose all 26 drafts; gaps never select a neighboring letter. */
    for(int state=0;state<26;++state) {
        int x=TS_PRISM_BANK_X+(state%13)*TS_PRISM_BANK_DX;
        int y=TS_PRISM_BANK_Y+(state/13)*TS_PRISM_BANK_DY;
        assert(ts_sister_ui_prism_matrix_bank_state(x,y)==state);
        assert(ts_sister_ui_prism_matrix_bank_state(x+TS_PRISM_BANK_W-1,y+TS_PRISM_BANK_H-1)==state);
        assert(ts_sister_ui_prism_matrix_bank_state(x+TS_PRISM_BANK_W,y)==-1);
        assert(ts_sister_ui_prism_matrix_bank_state(x,y+TS_PRISM_BANK_H)==-1);
        char help[256];ts_sister_ui_prism_help(&s->model,x+3,y+3,help,sizeof(help));assert(strstr(help,"BANK A-Z"));
        matrix_ui_click(a,s,x+3,y+3,SDL_BUTTON_LEFT);
        assert(s->model.prism_matrix_edit==state+1 && s->model.prism_panel==0);
        assert(a->sister.parameters.prism.pitch_offset[2]==state*31);
        matrix_ui_click(a,s,550,257,SDL_BUTTON_LEFT);
        assert(s->model.prism_panel==3 && !s->model.prism_matrix_edit);
        for(int other=0;other<26;++other)
            assert(!memcmp(ts_prism_state_const(&before,other),ts_prism_state_const(&a->sister.parameters.prism,other),sizeof(TsPrismPatch)));
    }
    sister_prism_publish(0,a,s,&before);
    for(int i=0;i<64;++i) {
        int x=TS_PRISM_MATRIX_X+(i%8)*TS_PRISM_MATRIX_DX+8;
        int y=TS_PRISM_MATRIX_Y+(i/8)*TS_PRISM_MATRIX_DY+8;
        assert(ts_sister_ui_prism_matrix_cell(x,y)==i);
        assert(ts_sister_ui_prism_matrix_cell(x-8+TS_PRISM_MATRIX_W,y)==-1);
        for(int state=0;state<TS_PRISM_STATES;++state) {
            matrix_ui_click(a,s,x,y,SDL_BUTTON_LEFT);assert(a->sister.parameters.prism.matrix.step[i]==state);
        }
        matrix_ui_click(a,s,x,y,SDL_BUTTON_LEFT);assert(a->sister.parameters.prism.matrix.step[i]==-1);
        matrix_ui_click(a,s,x,y,SDL_BUTTON_RIGHT);assert(a->sister.parameters.prism.matrix.step[i]==25);
        matrix_ui_wheel(a,s,x,y,1);assert(a->sister.parameters.prism.matrix.step[i]==-1);
        matrix_ui_wheel(a,s,x,y,-1);assert(a->sister.parameters.prism.matrix.step[i]==25);
        matrix_ui_click(a,s,x,y,SDL_BUTTON_MIDDLE);assert(a->sister.parameters.prism.matrix.step[i]==-1);
        matrix_ui_wheel(a,s,x,y,1+i%TS_PRISM_STATES);assert(a->sister.parameters.prism.matrix.step[i]==i%TS_PRISM_STATES);
    }
    SDL_SetModState(KMOD_SHIFT);matrix_ui_click(a,s,16+2*76+8,114+8,SDL_BUTTON_LEFT);SDL_SetModState(KMOD_NONE);
    assert(a->sister.parameters.prism.matrix.length==3);
    matrix_ui_click(a,s,30,85,SDL_BUTTON_LEFT);matrix_ui_audio(a,s,64);
    assert(a->sister.prism.matrix.running && a->sister.prism.matrix.step==0);
    TsSisterSourceFrames sources={0};ts_sister_runtime_begin_audio_block(&a->sister);
    ts_sister_runtime_process_frame(&a->sister,&sources);ts_sister_runtime_end_audio_block(&a->sister);
    TsSisterRoutingSnapshot snapshot;assert(ts_sister_runtime_get_snapshot(&a->sister,&snapshot));
    assert(snapshot.prism.matrix_running && snapshot.prism.matrix_step==0);
    assert(snapshot.prism.matrix_morph==a->sister.prism.matrix.position);
    assert(snapshot.prism.matrix_lenses==a->sister.prism.matrix.pair[1].lenses);
    TsSisterProjectState saved;ts_sister_project_state_capture(&saved,&a->sister,1,"MATRIX");
    assert(saved.parameters.prism.active_pair_valid && !saved.parameters.prism.matrix_run);
    assert(!memcmp(saved.parameters.prism.active_pair,a->sister.prism.matrix.pair,sizeof(saved.parameters.prism.active_pair)));
    int current=a->sister.prism.matrix.step;
    matrix_ui_click(a,s,30,55,SDL_BUTTON_LEFT);assert(s->model.prism_panel==1);
    assert(a->sister.prism.matrix.running && a->sister.prism.matrix.step==current);
    /* Ordinary mode edits remain locked; bank draft editing leaves audio alone. */
    TsSisterUiHit mode=ts_sister_ui_hit_test_model(&s->model,150,55);
    s->model.routing.prism.matrix_active=0; /* UI snapshot may lag the audio clock. */
    sister_apply_action(0,a,&ui,&instrument,s,NULL,NULL,mode,48000,2);
    assert(a->sister.parameters.prism.mode==c.mode && strstr(s->model.status,"LOCKED"));
    matrix_ui_click(a,s,550,55,SDL_BUTTON_LEFT);
    TsPrismPatch frozen=a->sister.prism.matrix.pair[1];
    matrix_ui_click(a,s,25,TS_PRISM_BANK_Y+5,SDL_BUTTON_LEFT);
    assert(s->model.prism_matrix_edit==1 && s->model.prism_panel==0 && a->sister.prism.matrix.running);
    sister_apply_action(0,a,&ui,&instrument,s,NULL,NULL,mode,48000,2);
    assert(ts_prism_factory_find(&a->sister.parameters.prism)==1); /* Ensemble draft. */
    assert(!memcmp(&frozen,&a->sister.prism.matrix.pair[1],sizeof(frozen)));
    /* Vary a draft while the sequence continues, retaining all cell references. */
    s->model.prism_panel=2;before=a->sister.parameters.prism;
    assert(sister_prism_performance_click(0,a,s,550,290,0,KMOD_NONE));
    assert(a->sister.prism.matrix.running && a->sister.parameters.prism.matrix_run);
    assert(!memcmp(&before.matrix,&a->sister.parameters.prism.matrix,sizeof(before.matrix)));
    assert(!memcmp(&before.a,&a->sister.parameters.prism.a,sizeof(before.a)));
    TsPrismPatch draft;memcpy(&draft,&a->sister.parameters.prism,sizeof(draft));
    matrix_ui_click(a,s,550,257,SDL_BUTTON_LEFT);assert(s->model.prism_panel==3 && !s->model.prism_matrix_edit);
    assert(!memcmp(&a->sister.parameters.prism.a,&draft,sizeof(draft)));
    assert(!memcmp(&frozen,&a->sister.prism.matrix.pair[1],sizeof(frozen)));
    matrix_ui_audio(a,s,22000);assert(a->sister.prism.matrix.step==0);
    assert(!memcmp(&draft,&a->sister.prism.matrix.pair[1],sizeof(draft)));
    /* MIDI/manual fader takes over the exact Matrix pair; bank stays intact. */
    before=a->sister.parameters.prism;
    TsSisterUiHit fader={TS_SISTER_UI_ACTION_PARAMETER,TS_SISTER_UI_PARAM_PRISM_MORPH,.375f};
    sister_apply_action(0,a,&ui,&instrument,s,NULL,NULL,fader,48000,2);
    assert(!a->sister.prism.matrix.active && !a->sister.parameters.prism.matrix_run);
    assert(a->sister.parameters.prism.active_pair_valid && a->sister.parameters.prism.morph==.375f);
    for(int i=0;i<TS_PRISM_STATES;++i)assert(!memcmp(ts_prism_state_const(&before,i),ts_prism_state_const(&a->sister.parameters.prism,i),sizeof(TsPrismPatch)));
    matrix_ui_click(a,s,30,85,SDL_BUTTON_LEFT);matrix_ui_audio(a,s,1000);
    assert(a->sister.prism.matrix.waiting);
    matrix_ui_click(a,s,110,85,SDL_BUTTON_LEFT);assert(!a->sister.prism.matrix.active);
    assert(strstr(s->model.status,"01"));
    TsSisterUiHit time=ts_sister_ui_hit_test_model(&s->model,515,85);char target[80];
    assert(time.action==TS_SISTER_UI_ACTION_PARAMETER && time.index==TS_SISTER_UI_PARAM_PRISM_TIME);
    assert(ts_sister_ui_midi_target(time,target,sizeof(target)));
    float seconds=a->sister.parameters.prism.morph_seconds;
    assert(ts_sister_ui_parameter_lock_toggle(&s->model,TS_SISTER_UI_PARAM_PRISM_TIME));
    matrix_ui_wheel(a,s,515,85,1);assert(a->sister.parameters.prism.morph_seconds==seconds);
    assert(ts_sister_ui_parameter_lock_toggle(&s->model,TS_SISTER_UI_PARAM_PRISM_TIME));
    assert(ts_sister_ui_hit_test_model(&s->model,TS_PRISM_REC_X+4,TS_PRISM_FOOTER_Y+4).action==TS_SISTER_UI_ACTION_RECORD_FILE);
    /* Saving Z during a Z-to-Z leg protects the frozen pair until the next visit. */
    c=a->sister.parameters.prism;c.morph_enabled=0;c.active_pair_valid=0;
    c.matrix.length=2;c.matrix.step[0]=c.matrix.step[1]=25;c.matrix.loop=1;
    c.matrix_run=1;++c.matrix_restart;sister_prism_publish(0,a,s,&c);matrix_ui_audio(a,s,64);
    frozen=a->sister.prism.matrix.pair[1];
    matrix_ui_click(a,s,TS_PRISM_BANK_X+12*TS_PRISM_BANK_DX+3,TS_PRISM_BANK_Y+TS_PRISM_BANK_DY+3,SDL_BUTTON_LEFT);
    assert(s->model.prism_matrix_edit==26 && a->sister.prism.matrix.running);
    c=a->sister.parameters.prism;c.pitch_offset[2]=901;sister_prism_publish(0,a,s,&c);
    matrix_ui_click(a,s,550,257,SDL_BUTTON_LEFT);
    assert(ts_prism_state_const(&a->sister.parameters.prism,25)->pitch_offset[2]==901);
    assert(!memcmp(&frozen,&a->sister.prism.matrix.pair[1],sizeof(frozen)));
    for(int i=0;i<200 && a->sister.prism.matrix.step==0;++i)matrix_ui_audio(a,s,64);
    assert(a->sister.prism.matrix.step==1 && a->sister.prism.matrix.pair[1].pitch_offset[2]==901);
    /* Wheel and MIDI Time retime the active phase without Reset. */
    matrix_ui_click(a,s,110,85,SDL_BUTTON_LEFT);
    c=a->sister.parameters.prism;c.morph_enabled=0;c.active_pair_valid=0;
    c.matrix.step_seconds=c.morph_seconds=240;c.matrix_run=1;++c.matrix_restart;
    sister_prism_publish(0,a,s,&c);matrix_ui_audio(a,s,48000);
    frozen=a->sister.prism.matrix.pair[1];
    assert(midi_sister_hit_from_target(target,ts_prism_time_normalized(4),&time));
    sister_apply_action(0,a,&ui,&instrument,s,NULL,NULL,time,48000,2);matrix_ui_audio(a,s,64);
    assert(a->sister.prism.matrix.running && a->sister.prism.matrix.step==0);
    assert(fabsf(a->sister.prism.matrix.position-.25f)<.002f);
    matrix_ui_wheel(a,s,515,85,-60);matrix_ui_audio(a,s,64);
    assert(a->sister.prism.matrix.position==1 && a->sister.prism.matrix.holding);
    matrix_ui_audio(a,s,48000);float progress=s->model.routing.prism.matrix_progress;
    matrix_ui_wheel(a,s,380,85,-10);matrix_ui_audio(a,s,64);
    assert(a->sister.prism.matrix.step==0 && a->sister.prism.matrix.duration<100);
    assert(s->model.routing.prism.matrix_progress>progress);
    assert(!memcmp(&frozen,&a->sister.prism.matrix.pair[1],sizeof(frozen)));
    matrix_ui_click(a,s,380,85,SDL_BUTTON_RIGHT);matrix_ui_audio(a,s,64);
    assert(a->sister.prism.matrix.step==1 && a->sister.parameters.prism.matrix.step_seconds==0);
    matrix_ui_wheel(a,s,380,85,1);assert(a->sister.parameters.prism.matrix.step_seconds>0);
    /* Screenshot uses the real native renderer and controller, with the audio
       scheduler driving the current-step indication, not painted test labels. */
    const char *shot=getenv("TS_PRISM_MATRIX_SCREENSHOT");
    if(shot) {
        c=a->sister.parameters.prism;c.morph_enabled=0;c.active_pair_valid=0;c.matrix_run=0;
        c.matrix.length=64;c.matrix.loop=1;c.matrix.step_seconds=240.f;c.morph_seconds=240.f;
        static const int melody[]={0,12,1,25,14,3,23,5,13,7,24,2,9,21,0,11};
        for(int i=0;i<64;++i)c.matrix.step[i]=melody[i%16];
        c.matrix.step[20]=-1;c.matrix.step[30]=-1;c.captured&=~(1<<9);
        const char *preset=getenv("TS_PRISM_MATRIX_DEMO_PRESET");
        if(preset) {
            TsPrismBank *bank=calloc(1,sizeof(*bank));assert(bank);bank->count=1;
            strcpy(bank->entries[0].name,"MATRIX PERFORMANCE");bank->entries[0].controls=c;
            char error[160];assert(ts_prism_bank_save(bank,preset,error,sizeof(error)));free(bank);
        }
        sister_prism_publish(0,a,s,&c);matrix_ui_click(a,s,30,85,SDL_BUTTON_LEFT);
        matrix_ui_audio(a,s,48000*16);s->model.visible=1;
        snprintf(s->model.status,sizeof(s->model.status),"WHEEL CELLS: A-Z / MIDDLE: HOLD / SHIFT-CLICK: SET END");
        static TsFramebuffer fb;ts_sister_ui_render(&fb,&s->model,&ui.palette);
        SDL_Renderer *renderer=SDL_CreateRenderer(s->window,-1,SDL_RENDERER_SOFTWARE);assert(renderer);
        SDL_Texture *texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,640,400);assert(texture);
        assert(SDL_UpdateTexture(texture,NULL,fb.pixels,640*4)==0);
        SDL_RenderCopy(renderer,texture,NULL,NULL);SDL_RenderPresent(renderer);
        assert(ts_ui_write_ppm(&fb,shot));SDL_DestroyTexture(texture);SDL_DestroyRenderer(renderer);
    }
    SDL_DestroyWindow(s->window);free(s);routing_free(a);
    puts("Prism Matrix native controls, 64 cells, live draft save, takeover and UI passed.");
}
