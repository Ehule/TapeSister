static void fade_handle(const TsMosaicEvent *e,int part,int *x,int *y)
{
    double hx,hy;assert(ts_ui_mosaic_fade_handle(&ui,e,part,&hx,&hy));
    *x=(int)round(hx);*y=(int)round(hy);
}
static void test_mosaic_fades(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*scene=ts_mosaic_create();assert(scene);ui.mosaic=audio.mosaic=scene;
    char error[160];TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *a=ts_mosaic_add(scene,source,1,20);a->duration=8;a->width=100;
    TsMosaicEvent *b=ts_mosaic_add(scene,source,2,150);b->duration=6;
    a->notes[1]=67;a->note_count=2;a->pan=-.2f;
    ui.mosaic_open=1;ui.mosaic_follow=0;ui.mosaic_time=-1;
    ui.mosaic_scale=24;ui.mosaic_hscale=1;ui.mosaic_scroll=ui.mosaic_xscroll=0;
    mosaic_poll(0,&ui,&instrument,&mosaic);mosaic_select_only(&ui,a->id);
    TsMosaicEvent original=*a;int x,y,history=scene->history_count;
    fade_handle(a,1,&x,&y);motion(window,x,y);assert(ui.mosaic_fade_event==a->id && ui.mosaic_fade_part==1);
    click(window,x,y,1);assert(mosaic.drag==7);release(window,x,y);
    assert(a->fade_in==0 && scene->history_count==history); /* No jump from the inset zero handle. */
    ui.mosaic_selection[1]=b->id;
    scene->playing=1;ts_mosaic_seek(scene,1.5);for(int i=0;i<300;++i)ts_mosaic_read(scene,48000);
    uint64_t attack=scene->voices[0].attack;
    click(window,x,y,1);motion(window,x,y+24);motion(window,x,y+48);release(window,x,y+48);
    assert(a->fade_in==2 && a->fade_out==0 && b->fade_in==0 && !mosaic_chosen(&ui,1));
    assert(scene->history_count==history+1 && !mosaic.drag);
    TsMosaicEvent edited=*a;edited.fade_in=0;assert(!memcmp(&original,&edited,sizeof(edited)));
    ts_mosaic_read(scene,48000);assert(scene->voices[0].attack==attack+1 && a->revision==original.revision);
    scene->playing=0;
    key(window,SDLK_z,KMOD_CTRL);assert(a->fade_in==0);
    key(window,SDLK_y,KMOD_CTRL);assert(a->fade_in==2);
    fade_handle(a,2,&x,&y);click(window,x,y,1);assert(mosaic.drag==8);
    motion(window,x,y-72);release(window,x,y-72);assert(a->fade_out==3);
    fade_handle(a,1,&x,&y);click(window,x,y,1);motion(window,x,y+240);release(window,x,y+240);
    assert(a->fade_in==5 && a->fade_out==3); /* The two ends cannot cross. */
    fade_handle(a,2,&x,&y);click(window,x,y,1);motion(window,x,y+48);
    assert(a->fade_out==1);key(window,SDLK_ESCAPE,KMOD_NONE);assert(a->fade_out==3 && !mosaic.drag);
    fade_handle(a,1,&x,&y);mosaic_nav_mouse(window,SDL_MOUSEBUTTONDOWN,SDL_BUTTON_RIGHT,x,y);
    assert(a->fade_in==0 && a->fade_out==3);
    fade_handle(a,2,&x,&y);click(window,x,y,2);assert(a->fade_out==0 && !mosaic.active);
    /* Footer edits and legacy overlapping fades share the audible envelope. */
    ui.mosaic_selection[1]=b->id;click(window,275,390,1);release(window,275,390);
    assert(a->fade_in>1.9 && a->fade_in<2.1 && b->fade_in==a->fade_in);
    double hx,hy;assert(ts_ui_mosaic_fade_handle(&ui,a,1,&hx,&hy));
    assert(fabs(hy-(90+a->fade_in*24))<1e-9);
    a->fade_in=8;a->fade_out=8;fade_handle(a,1,&x,&y);
    assert(y==186);click(window,x,y,1);motion(window,x,y-24);release(window,x,y-24);
    assert(a->fade_in==3 && a->fade_out==4);
    key(window,SDLK_z,KMOD_CTRL);assert(a->fade_in==8 && a->fade_out==8);
    /* Shades change only the faded portion; labels remain legible. */
    a->fade_in=2;a->fade_out=2;b->fade_in=0;ui.mosaic_fade_event=0;
    static TsFramebuffer faded,plain;ts_ui_render(&faded,&ui,&instrument);
    a->fade_in=a->fade_out=0;ts_ui_render(&plain,&ui,&instrument);
    assert(faded.pixels[125*640+255]!=plain.pixels[125*640+255]);
    assert(faded.pixels[245*640+255]!=plain.pixels[245*640+255]);
    assert(faded.pixels[185*640+255]==plain.pixels[185*640+255]);
    /* Top/bottom resize areas and modified card gestures still work. */
    click(window,180,91,1);assert(mosaic.drag==6);release(window,180,91);
    click(window,180,281,1);assert(mosaic.drag==2);release(window,180,281);
    fade_handle(a,1,&x,&y);SDL_SetModState(KMOD_SHIFT);click(window,x,y,1);
    assert(mosaic.drag!=7 && mosaic.drag!=8);key(window,SDLK_ESCAPE,KMOD_NONE);SDL_SetModState(KMOD_NONE);
    /* A foreground card masks a covered card's handle. */
    b->start=a->start;b->x=a->x;b->width=a->width;b->duration=a->duration;b->fade_in=2;
    mosaic_select_only(&ui,b->id);motion(window,x,y);assert(ui.mosaic_fade_event!=a->id);
    b->x=150;b->start=2;b->duration=6;
    mosaic_select_only(&ui,a->id);
    for(int pass=0;pass<3;++pass) {
        ui.mosaic_scale=pass==0?12:pass==1?24:37.3;ui.mosaic_hscale=pass==1?.65:1;
        ui.mosaic_scroll=.17;ui.mosaic_xscroll=3.1;a->fade_in=1;
        fade_handle(a,1,&x,&y);click(window,x,y,1);assert(mosaic.drag==7);
        motion(window,x,y+12);release(window,x,y+12);
        assert(fabs(a->fade_in-1-12/ui.mosaic_scale)<1e-9);
    }
    ui.mosaic_scroll=20;assert(!ts_ui_mosaic_fade_handle(&ui,a,1,&hx,&hy));
    ui.mosaic_scroll=0;ui.mosaic_hscale=.1;assert(!ts_ui_mosaic_fade_handle(&ui,a,1,&hx,&hy));
    ui.mosaic_hscale=1;ui.mosaic_scale=1;assert(!ts_ui_mosaic_fade_handle(&ui,a,1,&hx,&hy));
    ui.mosaic_scale=24;fade_handle(a,1,&x,&y);click(window,x,y,1);assert(mosaic.drag==7);
    SDL_Event lost;SDL_zero(lost);lost.type=SDL_WINDOWEVENT;lost.window.windowID=SDL_GetWindowID(window);
    lost.window.event=SDL_WINDOWEVENT_FOCUS_LOST;dispatch(&lost,window);
    assert(!mosaic.drag && !ui.mosaic_fade_event);
    fade_handle(a,1,&x,&y);click(window,x,y,1);assert(mosaic.drag==7);
    ui.mosaic=audio.mosaic=saved;mosaic_poll(0,&ui,&instrument,&mosaic);
    assert(!mosaic.drag && !ui.mosaic_fade_event);ts_mosaic_free(scene);
}
