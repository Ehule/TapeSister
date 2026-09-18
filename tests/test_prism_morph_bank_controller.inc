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
    s->window=SDL_CreateWindow("26 Prism states",0,0,1280,800,SDL_WINDOW_HIDDEN);assert(s->window);
    s->window_id=SDL_GetWindowID(s->window);
    for(int state=0;state<TS_PRISM_STATES;++state) {
        int side=state?1:0;TsPrismControls c=a->sister.parameters.prism;
        c.enabled=1;c.lenses=2+state%(TS_PRISM_LENSES-1);c.pitch_offset[3]=state*20;c.morph_seconds=.05f;
        sister_prism_publish(0,a,s,&c);morph_bank_choose(s,side,state);
        assert(sister_prism_performance_click(0,a,s,25+side*75,290,0,KMOD_NONE));
    }
    TsPrismControls bank=a->sister.parameters.prism;assert(bank.captured==((1<<TS_PRISM_STATES)-1));
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
        TsPrismControls shown=a->sister.parameters.prism;shown.enabled=1;shown.morph_seconds=240;
        sister_prism_publish(0,a,s,&shown);
        snprintf(s->model.status,sizeof(s->model.status),"26 STATES A-Z / WHEEL TO CHOOSE / CLICK TO MORPH");
        static TsFramebuffer fb;ts_sister_ui_render(&fb,&s->model,&ui.palette);assert(ts_ui_write_ppm(&fb,shot));}
    assert(sister_prism_clear_capture(0,a,s,25+side*75,290));
    assert(a->sister.parameters.prism.captured==(((1<<TS_PRISM_STATES)-1)&~4));
    for(int i=0;i<TS_PRISM_STATES;++i)if(i!=2)
        assert(!memcmp(ts_prism_state_const(&bank,i),ts_prism_state_const(&a->sister.parameters.prism,i),sizeof(TsPrismPatch)));
    bank=a->sister.parameters.prism;side=bank.endpoint[0]==25?0:1;
    morph_bank_choose(s,side,25);assert(sister_prism_clear_capture(0,a,s,25+side*75,290));
    assert(a->sister.parameters.prism.captured==(bank.captured&~(1<<25)));
    for(int i=0;i<25;++i)
        assert(!memcmp(ts_prism_state_const(&bank,i),ts_prism_state_const(&a->sister.parameters.prism,i),sizeof(TsPrismPatch)));
    SDL_DestroyWindow(s->window);free(s);routing_free(a);
}

static void test_prism_empty_state_edit(void)
{
    AudioState *a=routing_audio(0);SisterWindow *s=calloc(1,sizeof(*s));assert(s);
    ts_sister_ui_model_init(&s->model,&ui.config);s->model.fx_page=3;s->model.prism_panel=1;
    s->window=SDL_CreateWindow("Prism clear/edit regression",0,0,1280,800,SDL_WINDOW_HIDDEN);assert(s->window);
    s->window_id=SDL_GetWindowID(s->window);
    TsPrismControls bank=a->sister.parameters.prism;
    for(int i=0;i<TS_PRISM_STATES;++i) {
        bank.enabled=1;bank.mode=i%TS_PRISM_MODE_COUNT;bank.lenses=2+i%(TS_PRISM_LENSES-1);
        bank.spread=.07f*i;ts_prism_capture(&bank,i);
    }
    bank.endpoint[0]=6;bank.endpoint[1]=3; /* Screenshot: G/D with C browsed. */
    bank.seq_enabled=1;bank.seq_count=2;bank.sequence[0]=1;bank.sequence[1]=3;
    const float positions[]={0,.35f,1};
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=s->window_id;e.button.y=580;
    TsSisterUiHit mode=ts_sister_ui_hit_test_model(&s->model,170,58);
    assert(mode.action==TS_SISTER_UI_ACTION_PRISM_MODE);
    /* Both sides, stopped endpoints and mid-morph: clear an assigned state,
       clear a browsed saved/empty state, or right-click a browsed empty state. */
    for(int side=0;side<2;++side)for(int phase=0;phase<3;++phase)for(int action=0;action<4;++action) {
        TsPrismControls c=bank;int state=action?2:c.endpoint[side];
        if(action>=2)c.captured&=~(1<<state);
        c.morph=positions[phase];c.morph_enabled=0;c.morph_trigger=0;
        sister_prism_publish(0,a,s,&c);
        c.morph_enabled=1;c.morph_target=1;c.morph_trigger=1;sister_prism_publish(0,a,s,&c);
        memset(s->model.prism_browse,0,sizeof(s->model.prism_browse));
        morph_bank_choose(s,side,state);
        if(action) {
            assert(strstr(ts_sister_ui_prism_edit_status(&s->model),"CHOOSE C"));
            if(action>=2)assert(strstr(ts_sister_ui_prism_edit_status(&s->model),"RIGHT-CLICK CAP TO EDIT"));
        }
        sister_apply_action(0,a,&ui,&instrument,s,NULL,NULL,mode,48000,2);
        assert(a->sister.parameters.prism.morph_enabled && strstr(s->model.status,"MORPH LOCKED"));
        e.button.x=(25+75*side)*2;e.button.button=action==3?SDL_BUTTON_RIGHT:SDL_BUTTON_MIDDLE;
        assert(sister_prism_event(0,a,&ui,s,&e));
        TsPrismControls cleared=a->sister.parameters.prism;
        assert(!cleared.morph_enabled && !cleared.morph_trigger && cleared.endpoint[side]==state);
        assert(!s->model.prism_browse[0] && !s->model.prism_browse[1]);
        assert(cleared.captured==(((1<<TS_PRISM_STATES)-1)&~(1<<state)));
        assert(ts_sister_ui_prism_capture_visual(&s->model,side)==TS_PRISM_CAPTURE_EMPTY);
        assert(!strstr(ts_sister_ui_prism_edit_status(&s->model),"CHOOSE"));
        int source=action?bank.endpoint[positions[phase]>=.5f]:bank.endpoint[1-side];
        assert(!memcmp(&cleared,ts_prism_state_const(&bank,source),sizeof(TsPrismPatch)));
        assert(cleared.seq_enabled && cleared.seq_count==2 && cleared.sequence[1]==3);
        for(int i=0;i<TS_PRISM_STATES;++i)if(i!=state)
            assert(!memcmp(ts_prism_state_const(&cleared,i),ts_prism_state_const(&bank,i),sizeof(TsPrismPatch)));
        /* The factory control works immediately, without a preset-page detour.
           An edited patch continues from the last browsed factory, or its mode. */
        int factory=ts_prism_factory_find(&cleared);
        if(factory<0)factory=s->model.prism_factory_index;
        if(factory<0)factory=cleared.mode;
        sister_apply_action(0,a,&ui,&instrument,s,NULL,NULL,mode,48000,2);
        assert(ts_prism_factory_find(&a->sister.parameters.prism)==(factory+1)%TS_PRISM_FACTORY_PRESETS);
        e.button.button=SDL_BUTTON_LEFT;assert(sister_prism_event(0,a,&ui,s,&e));
        assert(a->sister.parameters.prism.captured==((1<<TS_PRISM_STATES)-1));
        assert(!memcmp(ts_prism_state_const(&a->sister.parameters.prism,state),&a->sister.parameters.prism,sizeof(TsPrismPatch)));
    }
    /* Clearing a stale candidate already bound by the other button must not
       produce duplicate ends or let sanitization silently select another letter. */
    TsPrismControls c=bank;c.endpoint[1]=2;sister_prism_publish(0,a,s,&c);
    s->model.prism_browse[0]=3;
    e.button.x=50;e.button.button=SDL_BUTTON_MIDDLE;assert(sister_prism_event(0,a,&ui,s,&e));
    assert(a->sister.parameters.prism.endpoint[0]==2 && a->sister.parameters.prism.endpoint[1]==6);
    assert(!s->model.prism_browse[0] && !a->sister.parameters.prism.morph_enabled);
    SDL_DestroyWindow(s->window);free(s);routing_free(a);
}
