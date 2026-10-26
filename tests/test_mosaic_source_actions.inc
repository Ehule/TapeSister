static double mosaic_test_audio(TsMosaic *scene)
{
    double energy=0;
    for(int block=0;block<3;++block) {
        ts_mosaic_begin_block(scene,512);
        for(int i=0;i<512;++i) {
            TsSourceRouteMix routes={0};TsStereoFrame f=ts_mosaic_read_routed(scene,48000,&routes);
            if(block==2)energy+=fabs(f.l)+fabs(f.r);
        }
    }
    return energy;
}
static void test_mosaic_group_mute_audio(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*scene=ts_mosaic_create();assert(scene);
    ui.mosaic=audio.mosaic=scene;ui.mosaic_open=1;
    char error[160];TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
    TsSample sample={0};assert(ts_sample_clone(&sample,&instrument.current,error,sizeof(error)));
    sample.data[0]+=.1f;TsMosaicSource *other=ts_mosaic_source(scene,&sample,error,sizeof(error));ts_sample_free(&sample);assert(other);
    for(int i=0;i<1020;++i) {
        TsMosaicEvent *e=ts_mosaic_add(scene,i<510?source:other,0,0);assert(e);
        e->duration=30;e->gain=.001f;
    }
    mosaic_poll(0,&ui,&instrument,&mosaic);mosaic_select_only(&ui,scene->events[0].id);
    scene->playing=1;assert(mosaic_test_audio(scene)>0);
    key(window,SDLK_m,(KMOD_ALT|KMOD_SHIFT));
    assert(scene->events[0].muted && !scene->events[1019].muted);
    assert(mosaic_test_audio(scene)>0);
    key(window,SDLK_a,KMOD_CTRL);key(window,SDLK_m,(KMOD_ALT|KMOD_SHIFT));
    assert(scene->events[0].muted && scene->events[1019].muted);
    assert(mosaic_test_audio(scene)==0);
    key(window,SDLK_m,(KMOD_ALT|KMOD_SHIFT));
    assert(!scene->events[0].muted && !scene->events[1019].muted);
    assert(mosaic_test_audio(scene)>0);
    key(window,SDLK_z,KMOD_CTRL);assert(mosaic_test_audio(scene)==0);
    key(window,SDLK_y,KMOD_CTRL);assert(mosaic_test_audio(scene)>0);
    ui.mosaic=audio.mosaic=saved;mosaic_poll(0,&ui,&instrument,&mosaic);ts_mosaic_free(scene);
}
static void test_mosaic_source_actions(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*scene=ts_mosaic_create();assert(scene);
    ui.mosaic=audio.mosaic=scene;ui.mosaic_open=1;
    char error[160];TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *a=ts_mosaic_add(scene,source,4,0),*b=ts_mosaic_copy(scene,a->id,8,100);assert(b);
    a->duration=b->duration=20;a->notes[0]=72;
    mosaic_poll(0,&ui,&instrument,&mosaic);mosaic_select_only(&ui,b->id);
    int n=instrument.selected_slot,x=20+(n%2)*49,y=85+(n/2)*34;
    uint64_t hash=ts_mosaic_hash(scene);scene->time=3.5;
    click(window,x,y,1);release(window,x,y);assert(!ui.mosaic_selected && ui.mosaic_source_selected==n);
    key(window,SDLK_a,KMOD_NONE);
    assert(scene->audition_source.source && scene->audition_id && !scene->playing);
    assert(mosaic_test_audio(scene)>0 && scene->time==3.5);
    ts_mosaic_collect(scene);assert(ts_mosaic_hash(scene)==hash);
    key(window,SDLK_a,KMOD_NONE);mosaic_test_audio(scene);assert(!scene->audition_id);
    /* Selecting a timeline card also highlights its source; the card wins. */
    mosaic_select_only(&ui,a->id);mosaic_source_select_event(&ui,a);
    key(window,SDLK_a,KMOD_NONE);assert(scene->audition_id==a->id && !scene->audition_source.source);
    mosaic_test_audio(scene);
    double expected_step=(double)source->sample.sample_rate/48000*ts_tuning_note_pitch(&a->tuning,12);
    assert(fabs(scene->audition_voice.step[0]-expected_step)<1e-10);
    key(window,SDLK_a,KMOD_NONE);mosaic_test_audio(scene);assert(!scene->audition_id);
    /* Right click selects source instances and preserves a matching primary. */
    mosaic_select_only(&ui,b->id);
    SDL_Event right;SDL_zero(right);right.type=SDL_MOUSEBUTTONDOWN;right.button.windowID=SDL_GetWindowID(window);
    right.button.button=SDL_BUTTON_RIGHT;right.button.x=x;right.button.y=y;
    assert(dispatch(&right,window));assert(mosaic_chosen(&ui,0) && mosaic_chosen(&ui,1) && ui.mosaic_selected==b->id);
    assert(ts_mosaic_hash(scene)==hash && !mosaic.drag);
    /* Rapid modified source clicks retain group-selection ownership. */
    SDL_SetModState(KMOD_SHIFT);click(window,x,y,2);release(window,x,y);SDL_SetModState(KMOD_NONE);
    assert(ui.mosaic_open && !mosaic.active && ts_mosaic_hash(scene)==hash);
    /* Bank double-click opens the real bank tile without making a timeline card. */
    scene->playing=1;
    click(window,x,y,2);assert(!ui.mosaic_open && !mosaic.active && instrument.selected_slot==n);
    assert(ts_mosaic_hash(scene)==hash && scene->playing && scene->time==3.5);
    ui.mosaic_open=1;mosaic_poll(0,&ui,&instrument,&mosaic);
    /* Event-version sources open the existing instance editing workflow. */
    ui.mosaic_source_page=ui.mosaic_bank_count;mosaic_poll(0,&ui,&instrument,&mosaic);
    click(window,20,85,2);assert(mosaic.active==a->id && ui.mosaic_editing==a->id && !ui.mosaic_open);
    assert(mosaic_leave(0,&audio,&ui,&instrument,&mosaic));
    assert(ts_mosaic_hash(scene)==hash);
    mosaic_picks_clear(&ui,&mosaic);mosaic_drag_end(&ui,&mosaic);
    ui.mosaic=audio.mosaic=saved;mosaic_poll(0,&ui,&instrument,&mosaic);ts_mosaic_free(scene);
}
