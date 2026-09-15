static void test_prism_capture_workflow(void)
{
    AudioState *a=routing_audio(0);SisterWindow *s=calloc(1,sizeof(*s));assert(s);
    ts_sister_ui_model_init(&s->model,&ui.config);s->model.fx_page=3;
    s->window=SDL_CreateWindow("Prism capture workflow",0,0,1280,800,SDL_WINDOW_HIDDEN);assert(s->window);
    s->window_id=SDL_GetWindowID(s->window);
    TsPrismControls c=a->sister.parameters.prism;
    c.enabled=1;c.mode=TS_PRISM_MICRO;ts_prism_capture(&c,0);
    c.mode=TS_PRISM_FIFTHS;c.spread=.9f;ts_prism_capture(&c,1);
    sister_prism_publish(0,a,s,&c);
    /* Recall -> Presets -> Vary/New must leave the edited endpoint amber,
       and recapture must store the new sound without touching its partner. */
    for(int endpoint=0;endpoint<2;++endpoint)for(int vary=0;vary<2;++vary) {
        s->model.prism_panel=1;
        assert(sister_prism_performance_click(0,a,s,25+endpoint*75,290,1,KMOD_NONE));
        TsPrismControls before=a->sister.parameters.prism;
        s->model.prism_panel=2;
        assert(sister_prism_performance_click(0,a,s,vary?550:450,290,0,KMOD_NONE));
        assert(s->model.prism_edit_endpoint==endpoint+1);
        assert(ts_sister_ui_prism_capture_visual(&s->model,endpoint)==TS_PRISM_CAPTURE_DIRTY);
        c=a->sister.parameters.prism;
        assert(!memcmp(&c.a,&before.a,sizeof(c.a)) && !memcmp(&c.b,&before.b,sizeof(c.b)));
        s->model.prism_panel=1;
        assert(sister_prism_performance_click(0,a,s,25+endpoint*75,290,0,KMOD_NONE));
        TsPrismPatch *stored=endpoint?&a->sister.parameters.prism.b:&a->sister.parameters.prism.a;
        assert(!memcmp(stored,&c,sizeof(*stored)) && a->sister.parameters.prism.captured==3);
        assert(ts_sister_ui_prism_capture_visual(&s->model,endpoint)==TS_PRISM_CAPTURE_READY);
    }
    s->prism_presets.count=2;
    ts_prism_controls_default(&s->prism_presets.entries[0].controls);
    s->prism_presets.entries[1].controls=c;
    s->prism_presets.entries[1].controls.spread=.17f;
    for(int i=0;i<2;++i)snprintf(s->prism_presets.entries[i].name,32,"TEST %d",i);
    s->model.prism_panel=1;
    assert(sister_prism_performance_click(0,a,s,100,290,1,KMOD_NONE));
    TsPrismControls endpoints=a->sister.parameters.prism;
    s->model.prism_panel=2;
    for(int i=0;i<2;++i) {
        assert(sister_prism_performance_click(0,a,s,285,290,0,KMOD_NONE));
        c=a->sister.parameters.prism;
        assert(c.captured==3 && !c.morph_enabled && !c.morph_trigger);
        assert(!memcmp(&c.a,&endpoints.a,sizeof(c.a)) && !memcmp(&c.b,&endpoints.b,sizeof(c.b)));
        assert(s->model.prism_edit_endpoint==2);
        assert(ts_sister_ui_prism_capture_visual(&s->model,1)==TS_PRISM_CAPTURE_DIRTY);
    }
    s->model.prism_panel=1;
    assert(sister_prism_performance_click(0,a,s,100,290,0,KMOD_NONE));
    assert(!memcmp(&a->sister.parameters.prism.b,&c,sizeof(c.b)));
    assert(sister_prism_performance_click(0,a,s,245,290,0,KMOD_NONE));
    assert(a->sister.parameters.prism.morph_enabled);
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=s->window_id;
    e.button.button=SDL_BUTTON_MIDDLE;e.button.x=50;e.button.y=580;
    assert(sister_prism_event(0,a,&ui,s,&e));
    assert(a->sister.parameters.prism.captured==2 && !a->sister.parameters.prism.morph_enabled);
    assert(!a->sister.parameters.prism.morph_trigger);
    assert(ts_sister_ui_prism_capture_visual(&s->model,0)==TS_PRISM_CAPTURE_EMPTY);
    e.button.x=200;assert(sister_prism_event(0,a,&ui,s,&e));
    assert(!a->sister.parameters.prism.captured && !s->model.prism_edit_endpoint);
    assert(ts_sister_ui_prism_capture_visual(&s->model,1)==TS_PRISM_CAPTURE_EMPTY);
    TsPrismControls cleared=a->sister.parameters.prism;
    e.button.y=520;assert(!sister_prism_event(0,a,&ui,s,&e));
    assert(!memcmp(&cleared,&a->sister.parameters.prism,sizeof(cleared)));
    /* Full-setup recall remains explicit and restores saved endpoints. */
    s->model.prism_panel=2;s->model.prism_preset_index=1;
    assert(sister_prism_performance_click(0,a,s,100,290,0,KMOD_SHIFT));
    assert(!memcmp(&a->sister.parameters.prism,&s->prism_presets.entries[1].controls,sizeof(c)));
    SDL_DestroyWindow(s->window);free(s);routing_free(a);
}
