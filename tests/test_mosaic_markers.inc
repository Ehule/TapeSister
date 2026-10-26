static void marker_text(SDL_Window *window,const char *value)
{
    SDL_Event e;SDL_zero(e);e.type=SDL_TEXTINPUT;e.text.windowID=SDL_GetWindowID(window);
    snprintf(e.text.text,sizeof(e.text.text),"%s",value);dispatch(&e,window);
}
static void marker_snapshot(const TsInstrument *bank,int width,int height,const char *path)
{
    SDL_Window *w=SDL_CreateWindow("Mosaic markers",0,0,width,height,SDL_WINDOW_HIDDEN);assert(w);
    SDL_Renderer *r=SDL_CreateRenderer(w,-1,SDL_RENDERER_SOFTWARE);assert(r);
    SDL_Texture *base=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,640,400);assert(base);
    TsUiWaveformDetail details[2]={0};SDL_Texture *textures[2]={0};static TsFramebuffer fb;
    native_waveform_prepare(r,details,main_waveform_detail_allowed(&ui));ts_audio_ui_render(&fb,&ui,bank);
    assert(!SDL_UpdateTexture(base,NULL,fb.pixels,640*sizeof(uint32_t)));
    assert(!SDL_RenderCopy(r,base,NULL,NULL));native_waveform_copy(r,textures,details,&fb);
    uint32_t *pixels=malloc((size_t)width*height*sizeof(*pixels));assert(pixels);
    assert(!SDL_RenderReadPixels(r,NULL,SDL_PIXELFORMAT_ARGB8888,pixels,width*sizeof(*pixels)));
    if(width==1280)for(int y=358;y<400;++y)for(int x=0;x<640;++x)
        assert(pixels[(size_t)(y*2+1)*width+x*2+1]==fb.pixels[y*640+x]);
    if(path) {
        FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",width,height);
        for(size_t i=0;i<(size_t)width*height;++i) {
            unsigned char rgb[]={(unsigned char)(pixels[i]>>16),(unsigned char)(pixels[i]>>8),(unsigned char)pixels[i]};
            assert(fwrite(rgb,1,3,f)==3);
        }
        assert(!fclose(f));
    }
    free(pixels);
    for(int i=0;i<2;++i){ts_ui_waveform_detail_free(&details[i]);SDL_DestroyTexture(textures[i]);}
    SDL_DestroyTexture(base);SDL_DestroyRenderer(r);SDL_DestroyWindow(w);
}
static void test_mosaic_markers(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*scene=ts_mosaic_create();assert(scene);
    ui.mosaic=audio.mosaic=scene;ui.mosaic_open=1;ui.mosaic_follow=0;ui.mosaic_scale=24;
    ui.mosaic_scroll=ui.mosaic_xscroll=0;ui.mosaic_hscale=1;mosaic_poll(0,&ui,&instrument,&mosaic);
    char error[160];TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *a=ts_mosaic_add(scene,source,0,20);a->duration=10;a->fade_in=1;a->fade_out=2;
    TsMosaicEvent *b=ts_mosaic_add(scene,source,2,140);b->duration=8;b->pan=.5;
    mosaic_select_only(&ui,a->id);scene->time=4.35;scene->playing=1;
    uint64_t hash=ts_mosaic_hash(scene);key(window,SDLK_m,KMOD_NONE);
    assert(scene->markers[0].stored && scene->markers[0].time==4.35 && !a->muted && scene->playing);
    assert(ts_mosaic_hash(scene)!=hash);hash=ts_mosaic_hash(scene);
    SDL_Event e;SDL_zero(e);e.type=SDL_KEYDOWN;e.key.windowID=SDL_GetWindowID(window);e.key.keysym.sym=SDLK_m;e.key.repeat=1;
    dispatch(&e,window);assert(!scene->markers[1].stored && ts_mosaic_hash(scene)==hash);
    scene->time=8.25;key(window,SDLK_m,KMOD_NONE);assert(scene->markers[1].time==8.25);
    int history=scene->history_count;scene->playing=0;
    click(window,122,366,1);assert(scene->time==4.35 && !scene->playing && scene->history_count==history);
    scene->playing=1;click(window,154,366,1);assert(scene->time==8.25 && scene->playing);
    /* Cue jumps use exactly the established middle-click reconstruction. */
    TsMosaic *reference=ts_mosaic_create();assert(reference);
    TsMosaicSource *rs=ts_mosaic_source(reference,&instrument.current,error,sizeof(error));assert(rs);
    TsMosaicEvent *ra=ts_mosaic_add(reference,rs,0,20),*rb=ts_mosaic_add(reference,rs,2,140);assert(ra && rb);
    *ra=*a;ra->source=rs;*rb=*b;rb->source=rs;reference->playing=1;
    scene->last_output=reference->last_output=(TsStereoFrame){0};
    for(int repeat=0;repeat<12;++repeat) {
        int slot=repeat%2;click(window,122+slot*32,366,1);ts_mosaic_seek(reference,scene->markers[slot].time);
        for(int sample=0;sample<256;++sample) {
            TsStereoFrame x=ts_mosaic_read(scene,48000),y=ts_mosaic_read(reference,48000);
            assert(isfinite(x.l) && isfinite(x.r) && fabs(x.l-y.l)<1e-6 && fabs(x.r-y.r)<1e-6);
        }
    }
    ts_mosaic_free(reference);
    /* Empty clicks do not seek; Shift-click captures the exact current clock. */
    scene->time=7.123456;click(window,186,366,1);assert(!scene->markers[2].stored && scene->time==7.123456);
    SDL_SetModState(KMOD_SHIFT);click(window,186,366,1);SDL_SetModState(KMOD_NONE);assert(scene->markers[2].time==7.123456);
    /* Drag preview neither edits the model nor seeks; release is one undo. */
    int y=ts_ui_mosaic_marker_y(&ui,0)+5;double time=scene->time;history=scene->history_count;
    mosaic_nav_mouse(window,SDL_MOUSEBUTTONDOWN,SDL_BUTTON_LEFT,135,y);
    mosaic_nav_mouse(window,SDL_MOUSEMOTION,SDL_BUTTON_LEFT,135,y+24);
    assert(scene->markers[0].time==4.35 && scene->time==time && scene->history_count==history);
    mosaic_nav_mouse(window,SDL_MOUSEBUTTONUP,SDL_BUTTON_LEFT,135,y+24);
    assert(fabs(scene->markers[0].time-5.35)<1e-9 && scene->history_count==history+1);
    key(window,SDLK_z,KMOD_CTRL);assert(scene->markers[0].time==4.35);
    key(window,SDLK_y,KMOD_CTRL);assert(fabs(scene->markers[0].time-5.35)<1e-9);
    y=ts_ui_mosaic_marker_y(&ui,0)+5;
    mosaic_nav_mouse(window,SDL_MOUSEBUTTONDOWN,SDL_BUTTON_LEFT,135,y);
    mosaic_nav_mouse(window,SDL_MOUSEMOTION,SDL_BUTTON_LEFT,135,y-24);
    key(window,SDLK_ESCAPE,KMOD_NONE);assert(!ui.mosaic_marker_drag && scene->playing && fabs(scene->markers[0].time-5.35)<1e-9);
    mosaic_nav_mouse(window,SDL_MOUSEBUTTONDOWN,SDL_BUTTON_LEFT,135,y);
    e.type=SDL_WINDOWEVENT;e.window.windowID=SDL_GetWindowID(window);e.window.event=SDL_WINDOWEVENT_FOCUS_LOST;dispatch(&e,window);
    assert(!ui.mosaic_marker_drag && scene->playing);
    /* Editor ownership, validation, name/time, delete and undo. */
    mosaic_nav_mouse(window,SDL_MOUSEBUTTONDOWN,SDL_BUTTON_RIGHT,122,366);assert(ui.mosaic_marker_open==1);
    ui_sync_dialog_workspaces(&ui);assert(ui.mosaic_open && !main_waveform_detail_allowed(&ui));
    history=scene->history_count;key(window,SDLK_m,KMOD_NONE);assert(scene->history_count==history);
    marker_text(window,"Low E bloom");key(window,SDLK_TAB,KMOD_NONE);key(window,SDLK_a,KMOD_CTRL);marker_text(window,"nan");
    key(window,SDLK_RETURN,KMOD_NONE);assert(ui.mosaic_marker_open && scene->history_count==history);
    key(window,SDLK_a,KMOD_CTRL);marker_text(window,"3.125");key(window,SDLK_RETURN,KMOD_NONE);
    assert(!ui.mosaic_marker_open && !strcmp(scene->markers[0].name,"Low E bloom") && scene->markers[0].time==3.125);
    mosaic_nav_mouse(window,SDL_MOUSEBUTTONDOWN,SDL_BUTTON_RIGHT,122,366);click(window,280,250,1);assert(!scene->markers[0].stored);
    key(window,SDLK_z,KMOD_CTRL);assert(scene->markers[0].stored);
    key(window,SDLK_m,KMOD_ALT);assert(a->muted);key(window,SDLK_m,KMOD_ALT);assert(!a->muted);
    /* Capacity never overwrites. Reopen/new-project UI cannot keep stale slots. */
    for(int i=3;i<TS_MOSAIC_MARKERS;++i){scene->time=i*.5;key(window,SDLK_m,KMOD_NONE);}
    hash=ts_mosaic_hash(scene);key(window,SDLK_m,KMOD_NONE);assert(ts_mosaic_hash(scene)==hash);
    for(int i=6;i<TS_MOSAIC_MARKERS;++i)scene->markers[i]=(TsMosaicMarker){0};
    for(int i=0;i<6;++i)scene->markers[i].time=i*1.75;
    scene->time=ui.mosaic_time=3.5;ui.mosaic_marker_selected=3;ui.mosaic_playing=1;
    snprintf(ui.status,sizeof(ui.status),"M: ADD MARKER / CLICK: JUMP / RIGHT: EDIT");
    marker_snapshot(&instrument,1280,800,getenv("TS_MOSAIC_MARKER_SCREENSHOT"));
    marker_snapshot(&instrument,1729,973,getenv("TS_MOSAIC_MARKER_FRACTIONAL_SCREENSHOT"));
    mosaic_nav_mouse(window,SDL_MOUSEBUTTONDOWN,SDL_BUTTON_RIGHT,122,366);
    marker_snapshot(&instrument,1280,800,getenv("TS_MOSAIC_MARKER_EDITOR_SCREENSHOT"));
    ++scene->epoch;key(window,SDLK_RETURN,KMOD_NONE);assert(!ui.mosaic_marker_open);
    mosaic_drag_end(&ui,&mosaic);ui.mosaic=audio.mosaic=saved;ts_mosaic_free(scene);
    mosaic_poll(0,&ui,&instrument,&mosaic);assert(!ui.mosaic_marker_selected);
}
