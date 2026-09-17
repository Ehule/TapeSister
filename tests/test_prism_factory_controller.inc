static void test_prism_factory_controller(void)
{
    AudioState *a=routing_audio(0);a->mosaic->playing=0;
    SisterWindow *panel=calloc(1,sizeof(*panel));TsUiState *view=calloc(1,sizeof(*view));
    assert(panel && view);ts_ui_init(view);ts_sister_ui_model_init(&panel->model,&view->config);
    panel->model.fx_page=3;
    panel->window=SDL_CreateWindow("Prism factory browser",0,0,1280,800,SDL_WINDOW_HIDDEN);
    assert(panel->window);panel->window_id=SDL_GetWindowID(panel->window);
    ts_performance_recorder_init(&panel->performance_recorder);
    panel->model.parameters=a->sister.parameters;
    TsPrismControls *c=&panel->model.parameters.prism;
    c->enabled=1;ts_prism_factory_apply(c,0);c->group_octave=-2;
    ts_prism_capture(c,0);c->group_octave=2;ts_prism_capture(c,1);
    TsPrismPatch keep[2]={c->a,c->b};ts_prism_factory_apply(c,0);
    ts_sister_runtime_set_parameters(&a->sister,&panel->model.parameters);
    for(int index=1;index<=TS_PRISM_FACTORY_PRESETS;++index) {
        sister_apply_action(0,a,view,&instrument,panel,NULL,NULL,
            (TsSisterUiHit){TS_SISTER_UI_ACTION_PRISM_MODE,0,0},48000,2);
        assert(ts_prism_factory_find(c)==index%TS_PRISM_FACTORY_PRESETS);
        assert(!memcmp(&c->a,&keep[0],sizeof(keep[0])) && !memcmp(&c->b,&keep[1],sizeof(keep[1])));
    }
    matrix_ui_wheel(a,panel,170,58,-1);
    assert(ts_prism_factory_find(c)==TS_PRISM_FACTORY_PRESETS-1);
    matrix_ui_wheel(a,panel,170,58,1);
    assert(ts_prism_factory_find(c)==0);
    /* Recapture through the actual UI, then inspect the audio snapshot. */
    c->endpoint[0]=0;c->endpoint[1]=1;c->group_octave=-2;
    assert(sister_prism_state_click(0,a,panel,0,0));
    c->group_octave=2;assert(sister_prism_state_click(0,a,panel,1,0));
    c->morph_enabled=1;c->morph=.25f;
    sister_prism_publish(0,a,panel,c);
    for(int frame=0;frame<48000;++frame)ts_sister_runtime_process_frame(&a->sister,NULL);
    TsSisterRoutingSnapshot snapshot;assert(ts_sister_runtime_get_snapshot(&a->sister,&snapshot));
    assert(fabsf(snapshot.prism.group_octave+1)<.001f);
    TsPrismControls before=*c;
    sister_apply_action(0,a,view,&instrument,panel,NULL,NULL,
        (TsSisterUiHit){TS_SISTER_UI_ACTION_PRISM_MODE,0,0},48000,2);
    assert(!memcmp(c,&before,sizeof(before))); /* Active pair remains protected. */
    SDL_DestroyWindow(panel->window);free(panel);free(view);routing_free(a);
}
