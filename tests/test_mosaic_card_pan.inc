static void pan_handle(const TsMosaicEvent *e,int *x,int *y,double *span)
{
    double hx,hy;assert(ts_ui_mosaic_pan_handle(&ui,e,&hx,&hy,span));
    *x=(int)round(hx);*y=(int)round(hy);
}
static void test_mosaic_card_pan(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*scene=ts_mosaic_create();assert(scene);ui.mosaic=audio.mosaic=scene;
    char error[160];TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *a=ts_mosaic_add(scene,source,1,20);a->duration=8;a->width=100;
    TsMosaicEvent *b=ts_mosaic_add(scene,source,2,150);b->duration=6;
    a->fade_in=2;a->fade_out=3;a->notes[1]=67;a->note_count=2;
    ui.mosaic_open=1;ui.mosaic_follow=0;ui.mosaic_time=-1;
    ui.mosaic_scale=24;ui.mosaic_hscale=1;ui.mosaic_scroll=ui.mosaic_xscroll=0;
    mosaic_poll(0,&ui,&instrument,&mosaic);mosaic_select_only(&ui,a->id);
    TsMosaicEvent original=*a;int x,y,history=scene->history_count;double span;
    pan_handle(a,&x,&y,&span);assert(x==221 && y==120 && span==42);
    motion(window,x,y);assert(ui.mosaic_card_mix_event==a->id && ui.mosaic_card_mix_part==3);
    assert(strstr(ui.status,"PAN CENTER"));click(window,x,y,1);assert(mosaic.drag==9);
    motion(window,x+1,y);release(window,x+1,y);assert(a->pan==0 && scene->history_count==history);
    ui.mosaic_selection[1]=b->id;
    scene->playing=1;ts_mosaic_seek(scene,1.5);for(int i=0;i<300;++i)ts_mosaic_read(scene,48000);
    uint64_t attack=scene->voices[0].attack;
    click(window,x,y,1);motion(window,x+10,y);motion(window,x+21,y);release(window,x+21,y);
    assert(a->pan==.5f && b->pan==0 && !mosaic_chosen(&ui,1));
    assert(scene->history_count==history+1 && strstr(ui.status,"PAN R 50%"));
    TsMosaicEvent edited=*a;edited.pan=0;assert(!memcmp(&original,&edited,sizeof(edited)));
    ts_mosaic_read(scene,48000);assert(scene->voices[0].attack==attack+1 && a->revision==original.revision);
    scene->playing=0;
    key(window,SDLK_z,KMOD_CTRL);assert(a->pan==0);key(window,SDLK_y,KMOD_CTRL);assert(a->pan==.5f);
    pan_handle(a,&x,&y,&span);click(window,x,y,1);motion(window,x-200,y);release(window,x-200,y);assert(a->pan==-1);
    pan_handle(a,&x,&y,&span);click(window,x,y,1);motion(window,x+200,y);release(window,x+200,y);assert(a->pan==1);
    pan_handle(a,&x,&y,&span);click(window,x,y,1);motion(window,x-42,y);assert(a->pan==0);
    key(window,SDLK_ESCAPE,KMOD_NONE);assert(a->pan==1 && !mosaic.drag);
    pan_handle(a,&x,&y,&span);mosaic_nav_mouse(window,SDL_MOUSEBUTTONDOWN,SDL_BUTTON_RIGHT,x,y);assert(a->pan==0);
    a->pan=-.5f;pan_handle(a,&x,&y,&span);click(window,x,y,2);assert(a->pan==0 && !mosaic.active);
    /* The center detent is stable despite a one-pixel rounded mouse position. */
    a->pan=.5f;pan_handle(a,&x,&y,&span);click(window,x,y,1);motion(window,x-20,y);release(window,x-20,y);assert(a->pan==0);
    /* Footer group edits immediately move each card's displayed pan. */
    ui.mosaic_selection[1]=b->id;click(window,215,390,1);release(window,215,390);assert(a->pan==1 && b->pan==1);
    pan_handle(a,&x,&y,&span);assert(x==263);
    mosaic_select_only(&ui,a->id);
    /* Only the diamond owns pan drags; the lower line/body remains movable. */
    click(window,x,180,1);assert(mosaic.drag==1);release(window,x,180);
    SDL_SetModState(KMOD_SHIFT);click(window,x,y,1);assert(mosaic.drag!=9);
    key(window,SDLK_ESCAPE,KMOD_NONE);SDL_SetModState(KMOD_NONE);
    /* Fade handles retain priority when a fade endpoint crosses the pan grip. */
    a->pan=1;a->fade_in=0;a->fade_out=a->duration-30/ui.mosaic_scale;
    fade_handle(a,2,&x,&y);click(window,x,y,1);assert(mosaic.drag==8);release(window,x,y);
    a->fade_in=2;a->fade_out=3;
    /* Foreground cards mask the controls of covered cards. */
    a->pan=0;pan_handle(a,&x,&y,&span);b->start=a->start;b->x=a->x;b->width=a->width;b->duration=a->duration;b->pan=1;
    mosaic_select_only(&ui,b->id);motion(window,x,y);assert(ui.mosaic_card_mix_event!=a->id);
    b->x=150;b->start=2;b->duration=6;b->pan=0;mosaic_select_only(&ui,a->id);
    for(int pass=0;pass<3;++pass) {
        ui.mosaic_scale=pass==0?12:pass==1?24:37.3;ui.mosaic_hscale=pass==1?.65:1;
        ui.mosaic_scroll=.17;ui.mosaic_xscroll=3.1;a->pan=0;
        pan_handle(a,&x,&y,&span);click(window,x,y,1);assert(mosaic.drag==9);
        motion(window,x+8,y+19);release(window,x+8,y+19);assert(fabs(a->pan-8/span)<1e-6);
    }
    double hx,hy;ui.mosaic_scroll=20;assert(!ts_ui_mosaic_pan_handle(&ui,a,&hx,&hy,&span));
    ui.mosaic_scroll=0;ui.mosaic_hscale=.2;assert(!ts_ui_mosaic_pan_handle(&ui,a,&hx,&hy,&span));
    ui.mosaic_hscale=1;ui.mosaic_scale=1;assert(!ts_ui_mosaic_pan_handle(&ui,a,&hx,&hy,&span));
    ui.mosaic_scale=24;pan_handle(a,&x,&y,&span);click(window,x,y,1);assert(mosaic.drag==9);
    SDL_Event lost;SDL_zero(lost);lost.type=SDL_WINDOWEVENT;lost.window.windowID=SDL_GetWindowID(window);
    lost.window.event=SDL_WINDOWEVENT_FOCUS_LOST;dispatch(&lost,window);
    assert(!mosaic.drag && !ui.mosaic_card_mix_event);
    /* Both the neutral reference and moving line survive a visual pan edit. */
    ui.mosaic_xscroll=0;a->pan=0;static TsFramebuffer centered,panned;ts_ui_render(&centered,&ui,&instrument);
    a->pan=.5f;ts_ui_render(&panned,&ui,&instrument);
    assert(centered.pixels[180*640+242]!=panned.pixels[180*640+242]);
    pan_handle(a,&x,&y,&span);click(window,x,y,1);assert(mosaic.drag==9);
    ui.mosaic=audio.mosaic=saved;mosaic_poll(0,&ui,&instrument,&mosaic);
    assert(!mosaic.drag && !ui.mosaic_card_mix_event);ts_mosaic_free(scene);
}
