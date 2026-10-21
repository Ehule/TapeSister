static void mosaic_arrange_test_frame(void)
{
    /* Run the production frame's workspace synchronization between input
       events: a modal must retain the workspace that renders and owns it. */
    mosaic_poll(0,&ui,&instrument,&mosaic);
    ui_sync_dialog_workspaces(&ui);
    assert(ui.mosaic_open);
    static TsFramebuffer fb;
    ts_ui_render(&fb,&ui,&instrument);
}
static void test_mosaic_performance_edit(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*scene=ts_mosaic_create();assert(scene && !scene->auto_space);
    ui.mosaic=audio.mosaic=scene;ui.mosaic_open=1;ui.mosaic_scroll=ui.mosaic_xscroll=0;
    ui.mosaic_scale=24;ui.mosaic_hscale=1;ui.mosaic_follow=0;
    char error[160];TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *a=ts_mosaic_add(scene,source,2,20);a->duration=4;
    TsMosaicEvent *b=ts_mosaic_copy(scene,a->id,2,20);assert(b && b->x==20);
    TsSample variation={0};assert(ts_sample_clone(&variation,&instrument.current,error,sizeof(error)));
    variation.data[0]+=.01f;TsMosaicSource *different=ts_mosaic_source(scene,&variation,error,sizeof(error));assert(different && different!=source);ts_sample_free(&variation);
    TsMosaicEvent *other=ts_mosaic_add(scene,different,8,180);other->duration=2;
    mosaic_poll(0,&ui,&instrument,&mosaic);mosaic_select_only(&ui,a->id);
    key(window,SDLK_a,KMOD_CTRL);assert(mosaic_chosen(&ui,0) && mosaic_chosen(&ui,1) && mosaic_chosen(&ui,2));
    mosaic_select_only(&ui,a->id);key(window,SDLK_a,KMOD_SHIFT);assert(mosaic_chosen(&ui,1) && !mosaic_chosen(&ui,2));
    mosaic_select_only(&ui,a->id);a->note_count=2;a->notes[1]=67;a->muted=1;
    scene->time=37;key(window,SDLK_a,KMOD_NONE);assert(scene->audition_id==a->id && !scene->playing);
    double energy=0;for(int i=0;i<1000;++i){TsStereoFrame f=ts_mosaic_read(scene,48000);energy+=fabsf(f.l)+fabsf(f.r);}
    assert(energy>0 && scene->time==37 && scene->audition_voice.step[1]>scene->audition_voice.step[0]);
    key(window,SDLK_a,KMOD_NONE);for(int i=0;i<500;++i)ts_mosaic_read(scene,48000);assert(!scene->audition_id);
    a->muted=0;
    key(window,SDLK_z,KMOD_NONE);assert(ui.mosaic_scale>24);key(window,SDLK_z,KMOD_NONE);assert(ui.mosaic_scale==24 && ui.mosaic_scroll==0);
    click(window,180,115,1);assert(mosaic.drag==6);motion(window,180,91);release(window,180,91);
    assert(fabs(a->start-1)<1e-9 && a->duration==5);
    key(window,SDLK_z,KMOD_CTRL);assert(a->start==2 && a->duration==4);
    key(window,SDLK_UP,KMOD_NONE);assert(fabs(a->start-1.9)<1e-9);
    key(window,SDLK_DOWN,KMOD_SHIFT);assert(fabs(a->start-2.9)<1e-9);
    key(window,SDLK_z,KMOD_CTRL);key(window,SDLK_z,KMOD_CTRL);
    SDL_SetModState(KMOD_ALT);click(window,180,130,1);SDL_SetModState(KMOD_NONE);assert(ui.mosaic_selected==b->id);
    click(window,120,60,1);assert(ui.mosaic_spacing_confirm && !scene->auto_space);
    mosaic_arrange_test_frame();release(window,120,60);mosaic_arrange_test_frame();
    key(window,SDLK_ESCAPE,KMOD_NONE);assert(!ui.mosaic_spacing_confirm && !scene->auto_space);
    click(window,120,60,1);key(window,SDLK_RETURN,KMOD_NONE);assert(scene->auto_space && a->start==2 && b->start==2 && a->x!=b->x);
    uint64_t spaced=ts_mosaic_hash(scene);
    click(window,120,60,1);release(window,120,60);mosaic_arrange_test_frame();
    assert(!scene->auto_space && !ui.mosaic_spacing_confirm && a->start==2 && b->start==2);
    key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(scene)==spaced);
    key(window,SDLK_y,KMOD_CTRL);assert(!scene->auto_space);
    key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(scene)==spaced);
    key(window,SDLK_z,KMOD_CTRL);assert(!scene->auto_space && a->x==20 && b->x==20);
    key(window,SDLK_a,KMOD_CTRL);uint64_t before=ts_mosaic_hash(scene);
    key(window,SDLK_r,KMOD_CTRL);assert(ui.mosaic_scatter_open && ui.mosaic_ghost_count==3);
    mosaic_arrange_test_frame();mosaic_arrange_test_frame();
    key(window,SDLK_1,KMOD_NONE);assert(ui.mosaic_scatter_open && ui.mosaic_open);
    assert(ts_mosaic_hash(scene)==before);double first=ui.mosaic_ghosts[0].start;
    click(window,150,280,1);assert(first!=ui.mosaic_ghosts[0].start && ts_mosaic_hash(scene)==before);
    if(getenv("TS_SCATTER_SCREENSHOT")) {static TsFramebuffer fb;ts_ui_render(&fb,&ui,&instrument);assert(ts_ui_write_ppm(&fb,getenv("TS_SCATTER_SCREENSHOT")));}
    click(window,270,280,1);assert(!ui.mosaic_scatter_open && ts_mosaic_hash(scene)!=before);
    uint64_t scattered=ts_mosaic_hash(scene);key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(scene)==before);
    key(window,SDLK_y,KMOD_CTRL);assert(ts_mosaic_hash(scene)==scattered);key(window,SDLK_z,KMOD_CTRL);
    /* Cancelling a source scatter never adds cards; source pinning survives
       refresh and is released after Apply. Capacity errors are atomic. */
    SDL_SetModState(KMOD_CTRL);click(window,20,85,1);SDL_SetModState(KMOD_NONE);
    assert(mosaic.picked_count==1);key(window,SDLK_r,KMOD_CTRL);assert(ui.mosaic_scatter_copies);
    mosaic_arrange_test_frame();
    ui.mosaic_scatter_count=TS_MOSAIC_EVENTS;mosaic_scatter_preview(&ui,&mosaic);assert(!ui.mosaic_scatter_valid && !ui.mosaic_ghost_count);
    ui.mosaic_scatter_count=5;mosaic_scatter_preview(&ui,&mosaic);assert(ui.mosaic_ghost_count==5);
    click(window,130,120,1);key(window,SDLK_a,KMOD_CTRL);
    mosaic_arrange_test_frame();
    SDL_Event text;SDL_zero(text);text.type=SDL_TEXTINPUT;strcpy(text.text.text,"1.25");dispatch(&text,window);
    key(window,SDLK_RETURN,KMOD_NONE);assert(ui.mosaic_scatter_start==1.25 && ui.mosaic_scatter_open);
    key(window,SDLK_ESCAPE,KMOD_NONE);assert(mosaic_event_count(scene)==3 && ts_mosaic_hash(scene)==before);
    key(window,SDLK_r,KMOD_CTRL);ui.mosaic_scatter_count=5;
    click(window,270,280,1);assert(mosaic_event_count(scene)==8 && !mosaic.picked_count);
    key(window,SDLK_z,KMOD_CTRL);assert(mosaic_event_count(scene)==3 && ts_mosaic_hash(scene)==before);
    mosaic_drag_end(&ui,&mosaic);ui.mosaic=audio.mosaic=saved;ts_mosaic_free(scene);mosaic_poll(0,&ui,&instrument,&mosaic);
}
