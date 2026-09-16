static void morph_bank_choose(SisterWindow *s,int side,int state)
{
    for(int count=0;ts_sister_ui_prism_state(&s->model,side)!=state;++count) {
        assert(count<TS_PRISM_STATES);sister_prism_browse_state(s,side,1);
    }
}
static void morph_bank_finish(AudioState *a)
{
    for(int i=0;i<4000;++i) {
        TsStereoFrame out=ts_prism_process(&a->sister.prism,(TsStereoFrame){.1f,-.2f});
        assert(isfinite(out.l) && isfinite(out.r));
    }
}
static void test_prism_morph_bank_ui(void)
{
    AudioState *a=routing_audio(0);SisterWindow *s=calloc(1,sizeof(*s));assert(s);
    ts_sister_ui_model_init(&s->model,&ui.config);s->model.parameters=a->sister.parameters;
    s->model.fx_page=3;s->model.prism_panel=1;
    s->window=SDL_CreateWindow("Twelve Prism states",0,0,1280,800,SDL_WINDOW_HIDDEN);assert(s->window);
    s->window_id=SDL_GetWindowID(s->window);
    for(int state=0;state<TS_PRISM_STATES;++state) {
        int side=state?1:0;TsPrismControls c=a->sister.parameters.prism;
        c.enabled=1;c.lenses=state+2;c.pitch_offset[3]=state*20;c.morph_seconds=.05f;
        sister_prism_publish(0,a,s,&c);morph_bank_choose(s,side,state);
        assert(sister_prism_performance_click(0,a,s,25+side*75,290,0,KMOD_NONE));
    }
    TsPrismControls bank=a->sister.parameters.prism;assert(bank.captured==4095);
    assert(sister_prism_performance_click(0,a,s,25,290,1,KMOD_NONE));
    TsPrismControls before=a->sister.parameters.prism;
    SDL_Event wheel={0};wheel.type=SDL_MOUSEWHEEL;wheel.wheel.windowID=s->window_id;wheel.wheel.y=2;
#if SDL_VERSION_ATLEAST(2,26,0)
    wheel.wheel.mouseX=200;wheel.wheel.mouseY=580;
#else
    SDL_WarpMouseInWindow(s->window,200,580);
#endif
    memset(&ui.wheel_guard,0,sizeof(ui.wheel_guard));
    assert(sister_prism_event(0,a,&ui,s,&wheel));
    assert(ts_sister_ui_prism_state(&s->model,1)==2);
    assert(!memcmp(&before,&a->sister.parameters.prism,sizeof(before))); /* Browsing is silent. */
    assert(strstr(ts_sister_ui_prism_edit_status(&s->model),"CHOOSE C"));
    assert(sister_prism_performance_click(0,a,s,100,290,0,KMOD_NONE));
    assert(a->sister.parameters.prism.endpoint[1]==2 && a->sister.parameters.prism.morph_enabled);
    for(int i=0;i<1000;++i)ts_prism_process(&a->sister.prism,(TsStereoFrame){0});
    morph_bank_choose(s,0,3);before=a->sister.parameters.prism;
    assert(sister_prism_performance_click(0,a,s,180,290,0,KMOD_NONE));
    assert(!memcmp(&before,&a->sister.parameters.prism,sizeof(before))); /* No pair replacement mid-transition. */
    morph_bank_finish(a);assert(a->sister.prism.morph_position==1);
    assert(sister_prism_performance_click(0,a,s,180,290,0,KMOD_NONE));
    assert(a->sister.parameters.prism.endpoint[0]==3);morph_bank_finish(a);
    /* Traverse a complete performance, alternating the silent end, retaining every letter. */
    for(int state=0;state<TS_PRISM_STATES;++state) {
        int side=a->sister.prism.morph_position==0?1:0;
        if(a->sister.parameters.prism.endpoint[1-side]==state)continue;
        morph_bank_choose(s,side,state);
        assert(sister_prism_performance_click(0,a,s,180+side*72,290,0,KMOD_NONE));morph_bank_finish(a);
        assert(a->sister.prism.morph_position==side);
        assert(a->sister.parameters.prism.endpoint[side]==state);
        for(int i=0;i<TS_PRISM_STATES;++i)
            assert(!memcmp(ts_prism_state_const(&bank,i),ts_prism_state_const(&a->sister.parameters.prism,i),sizeof(TsPrismPatch)));
    }
    int side=a->sister.parameters.prism.endpoint[0]==2?0:1;
    morph_bank_choose(s,side,2);
    assert(sister_prism_performance_click(0,a,s,25+side*75,290,1,KMOD_NONE));
    assert(s->model.prism_edit_endpoint==3 && a->sister.parameters.prism.lenses==4);
    s->model.prism_panel=2;
    assert(sister_prism_performance_click(0,a,s,550,290,0,KMOD_NONE));
    assert(ts_sister_ui_prism_capture_visual(&s->model,side)==TS_PRISM_CAPTURE_DIRTY);
    s->model.prism_panel=1;
    assert(sister_prism_performance_click(0,a,s,25+side*75,290,0,KMOD_NONE));
    assert(!memcmp(ts_prism_state_const(&a->sister.parameters.prism,2),&a->sister.parameters.prism,sizeof(TsPrismPatch)));
    for(int i=0;i<TS_PRISM_STATES;++i)if(i!=2)
        assert(!memcmp(ts_prism_state_const(&bank,i),ts_prism_state_const(&a->sister.parameters.prism,i),sizeof(TsPrismPatch)));
    bank=a->sister.parameters.prism;
    s->prism_presets.count=1;ts_prism_controls_default(&s->prism_presets.entries[0].controls);
    s->model.prism_panel=2;assert(sister_prism_performance_click(0,a,s,100,290,0,KMOD_NONE));
    for(int i=0;i<TS_PRISM_STATES;++i)
        assert(!memcmp(ts_prism_state_const(&bank,i),ts_prism_state_const(&a->sister.parameters.prism,i),sizeof(TsPrismPatch)));
    s->model.prism_panel=1;
    TsSisterUiHit hit=ts_sister_ui_hit_test_model(&s->model,385,288);char target[80];
    assert(ts_sister_ui_midi_target(hit,target,sizeof(target)) && midi_sister_hit_from_target(target,.375f,&hit));
    sister_apply_action(0,a,&ui,&instrument,s,NULL,NULL,hit,48000,2);
    assert(a->sister.parameters.prism.morph_enabled && a->sister.parameters.prism.morph==.375f && !a->sister.parameters.prism.morph_trigger);
    const char *shot=getenv("TS_PRISM_BANK_SCREENSHOT");
    if(shot) {
        TsPrismControls shown=a->sister.parameters.prism;shown.enabled=1;sister_prism_publish(0,a,s,&shown);
        snprintf(s->model.status,sizeof(s->model.status),"12 STATES A-L / WHEEL TO CHOOSE / CLICK TO MORPH");
        static TsFramebuffer fb;ts_sister_ui_render(&fb,&s->model,&ui.palette);assert(ts_ui_write_ppm(&fb,shot));}
    assert(sister_prism_clear_capture(0,a,s,25+side*75,290));
    assert(a->sister.parameters.prism.captured==(4095&~4));
    for(int i=0;i<TS_PRISM_STATES;++i)if(i!=2)
        assert(!memcmp(ts_prism_state_const(&bank,i),ts_prism_state_const(&a->sister.parameters.prism,i),sizeof(TsPrismPatch)));
    SDL_DestroyWindow(s->window);free(s);routing_free(a);
}
