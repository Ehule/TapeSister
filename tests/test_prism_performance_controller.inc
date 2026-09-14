static void test_prism_performance_ui(void)
{
    AudioState *a=routing_audio(0);SisterWindow *s=calloc(1,sizeof(*s));assert(s);
    ts_sister_ui_model_init(&s->model,&ui.config);s->model.parameters=a->sister.parameters;s->model.fx_page=3;
    s->window=SDL_CreateWindow("Prism performance",0,0,1280,800,SDL_WINDOW_HIDDEN);assert(s->window);
    s->window_id=SDL_GetWindowID(s->window);
    assert(sister_prism_performance_click(0,a,s,120,260,0,KMOD_NONE) && s->model.prism_panel==1);
    assert(sister_prism_performance_click(0,a,s,25,290,0,KMOD_NONE));
    assert(a->sister.parameters.prism.captured==1);
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
    assert(a->sister.parameters.prism.octave_offset[2]==1);
    assert(fabsf(ts_prism_control_view(&a->sister.parameters.prism).lens[2].cents-before-1201)<.01f);
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
        free(fb);
    }
    SDL_DestroyWindow(s->window);free(s);routing_free(a);
    puts("Prism performance gestures, independent sequence selection, MIDI and octave nudges passed.");
}
