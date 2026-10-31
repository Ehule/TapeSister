/* Drive the visible controls, including text validation, before checking audio
   continuity and document history. No recipe values are written into sources. */
static void scatter_mix_type(SDL_Window *window,int x,int y,const char *value)
{
    click(window,x,y,1);assert(ui.mosaic_scatter_field);
    key(window,SDLK_a,KMOD_CTRL);
    SDL_Event input;SDL_zero(input);input.type=SDL_TEXTINPUT;
    snprintf(input.text.text,sizeof(input.text.text),"%s",value);dispatch(&input,window);
    key(window,SDLK_RETURN,KMOD_NONE);
}
static void test_mosaic_scatter_mix(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*scene=ts_mosaic_create();assert(scene);
    ui.mosaic=audio.mosaic=scene;ui.mosaic_open=1;ui.mosaic_scroll=ui.mosaic_xscroll=0;
    ui.mosaic_scale=24;ui.mosaic_hscale=1;ui.mosaic_follow=0;
    char error[160];TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
    scene->auto_space=0;
    for(int i=0;i<9;++i) {
        TsMosaicEvent *e=ts_mosaic_add(scene,source,i*.3,(i%4)*105);assert(e);
        e->duration=i==7?.6:6;e->fade_in=.1;e->fade_out=.2;e->pan=.15f;
        e->gain=.3f;e->notes[0]=60+i;
    }
    mosaic_poll(0,&ui,&instrument,&mosaic);mosaic_picks_clear(&ui,&mosaic);
    mosaic_select_only(&ui,scene->events[0].id);
    for(int i=1;i<8;++i)mosaic_select_add(&ui,scene->events[i].id);
    TsMosaicEvent before[9];memcpy(before,scene->events,sizeof(before));
    uint64_t hash=ts_mosaic_hash(scene);int history=scene->history_count;
    /* Header controls stay reachable at native and fractional window scales. */
    for(int size=0;size<3;++size) {
        double scale=size==0?1:size==1?1.5:2;
        SDL_SetWindowSize(window,(int)(640*scale),(int)(400*scale));
        key(window,SDLK_r,KMOD_CTRL);click(window,(int)(430*scale),(int)(45*scale),1);
        assert(ui.mosaic_scatter_page==1 && ui.mosaic_scatter_keep_positions);
        click(window,(int)(250*scale),(int)(45*scale),1);assert(!ui.mosaic_scatter_page);
        key(window,SDLK_ESCAPE,KMOD_NONE);
    }
    SDL_SetWindowSize(window,640,400);
    key(window,SDLK_r,KMOD_CTRL);click(window,430,45,1);
    click(window,210,150,1);scatter_mix_type(window,330,150,"0.5");
    click(window,210,186,1);scatter_mix_type(window,330,186,"1");
    click(window,210,222,1);scatter_mix_type(window,330,222,"-25");
    assert(ui.mosaic_scatter_valid && !ui.mosaic_scatter_field);
    for(int i=0;i<8;++i) {
        TsMosaicEvent *e=&ui.mosaic_ghosts[i];
        assert(e->fade_in==.5 && e->fade_out==fmin(1,e->duration) && e->pan==-.25f);
        assert(e->start==before[i].start && e->x==before[i].x && e->notes[0]==before[i].notes[0]);
    }
    assert(ts_mosaic_hash(scene)==hash && scene->history_count==history);
    scene->playing=1;ts_mosaic_seek(scene,.15);ts_mosaic_begin_block(scene,512);ts_mosaic_read(scene,48000);
    double position=scene->voices[0].position[0];uint64_t generation=scene->voices[0].generation;
    click(window,150,342,1);assert(!ui.mosaic_scatter_open && scene->history_count==history+1);
    ts_mosaic_begin_block(scene,512);ts_mosaic_read(scene,48000);
    assert(scene->voices[0].generation==generation && scene->voices[0].position[0]>position);
    for(int i=0;i<8;++i) {
        TsMosaicEvent expected=before[i];expected.fade_in=.5;expected.fade_out=fmin(1,expected.duration);expected.pan=-.25f;
        assert(!memcmp(&scene->events[i],&expected,sizeof(expected)));
    }
    assert(!memcmp(&scene->events[8],&before[8],sizeof(before[8])));
    double in,out;ts_mosaic_event_fades(&scene->events[7],&in,&out);
    assert(fabs(in+out-.6)<1e-9 && fabs(in/out-.5/.6)<1e-9);
    uint64_t applied=ts_mosaic_hash(scene);
    key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(scene)==hash);
    key(window,SDLK_y,KMOD_CTRL);assert(ts_mosaic_hash(scene)==applied);
    scene->playing=0;
    /* Each random control has its own stable roll; Set and Keep don't disturb
       other rolls, position rolls don't change mix, and mix rolls don't move. */
    key(window,SDLK_r,KMOD_CTRL);click(window,430,45,1);
    for(int i=0;i<3;++i){click(window,210,150+i*36,1);click(window,210,150+i*36,1);}
    scatter_mix_type(window,460,150,"2");scatter_mix_type(window,330,150,"0.25");
    scatter_mix_type(window,460,186,"3");scatter_mix_type(window,330,186,"0.5");
    scatter_mix_type(window,330,222,"-80");scatter_mix_type(window,460,222,"80");
    for(int i=0;i<3;++i)mosaic.scatter_mix_seed[i]=123+i*100;
    mosaic_scatter_preview(&ui,&mosaic);
    TsMosaicEvent preview[8];memcpy(preview,ui.mosaic_ghosts,sizeof(preview));
    mosaic_scatter_preview(&ui,&mosaic);assert(!memcmp(preview,ui.mosaic_ghosts,sizeof(preview)));
    click(window,150,310,1);
    for(int i=0;i<8;++i) {
        TsMosaicEvent *e=&ui.mosaic_ghosts[i];
        assert(e->fade_in>=.25 && e->fade_in<=fmin(2,e->duration));
        assert(e->fade_out>=.5 && e->fade_out<=fmin(3,e->duration));
        assert(e->pan>=-.8f && e->pan<=.8f && e->pan!=preview[i].pan);
        assert(e->start==preview[i].start && e->x==preview[i].x && e->notes[0]==preview[i].notes[0]);
    }
    memcpy(preview,ui.mosaic_ghosts,sizeof(preview));
    click(window,210,150,1);assert(!ui.mosaic_scatter_mix[0].mode);
    for(int i=0;i<8;++i)assert(ui.mosaic_ghosts[i].fade_in==scene->events[i].fade_in &&
        ui.mosaic_ghosts[i].fade_out==preview[i].fade_out && ui.mosaic_ghosts[i].pan==preview[i].pan);
    click(window,210,150,1);click(window,210,150,1);
    scatter_pitch_snapshot("TS_SCATTER_MIX_SCREENSHOT");
    click(window,250,45,1);click(window,410,105,1);click(window,150,310,1);
    assert(!ui.mosaic_scatter_keep_positions);
    for(int i=0;i<8;++i)assert(ui.mosaic_ghosts[i].fade_in==preview[i].fade_in &&
        ui.mosaic_ghosts[i].fade_out==preview[i].fade_out && ui.mosaic_ghosts[i].pan==preview[i].pan);
    click(window,430,45,1);assert(!ui.mosaic_scatter_keep_positions); /* Explicit layout choice survives. */
    click(window,410,105,1);assert(ui.mosaic_scatter_keep_positions);
    /* Fractional/flipped wheel and fine movement use event-local coordinates. */
    SDL_Event wheel;SDL_zero(wheel);wheel.type=SDL_MOUSEWHEEL;wheel.wheel.windowID=SDL_GetWindowID(window);
    wheel.wheel.y=1;wheel.wheel.preciseY=.5f;
#if SDL_VERSION_ATLEAST(2,26,0)
    wheel.wheel.mouseX=330;wheel.wheel.mouseY=222;
#else
    SDL_WarpMouseInWindow(window,330,222);
#endif
    wheel.wheel.direction=SDL_MOUSEWHEEL_FLIPPED;SDL_SetModState(KMOD_SHIFT);dispatch(&wheel,window);SDL_SetModState(KMOD_NONE);
    assert(ui.mosaic_scatter_mix[2].low==-80.25);
    /* Invalid text cannot publish, but Cancel always works even with an error. */
    scatter_mix_type(window,330,222,"101");assert(ui.mosaic_scatter_field==8);
    click(window,150,342,1);assert(ui.mosaic_scatter_open && ts_mosaic_hash(scene)==applied);
    scatter_mix_type(window,330,222,"nan");assert(ui.mosaic_scatter_field==8);
    click(window,350,342,1);assert(!ui.mosaic_scatter_open && ts_mosaic_hash(scene)==applied);
    key(window,SDLK_r,KMOD_CTRL);click(window,430,45,1);
    click(window,210,150,1);click(window,210,150,1);
    scatter_mix_type(window,330,150,"2");assert(ui.mosaic_scatter_field==4);
    key(window,SDLK_ESCAPE,KMOD_NONE);assert(ts_mosaic_hash(scene)==applied);
    /* Apply variation, then round-trip the ordinary card fields. */
    key(window,SDLK_r,KMOD_CTRL);click(window,430,45,1);
    for(int i=0;i<3;++i){click(window,210,150+i*36,1);click(window,210,150+i*36,1);}
    scatter_mix_type(window,330,222,"-100");scatter_mix_type(window,460,222,"100");
    scatter_mix_type(window,460,150,"2");scatter_mix_type(window,460,186,"3");
    click(window,150,342,1);assert(!ui.mosaic_scatter_open);
    scatter_pitch_snapshot("TS_SCATTER_MIX_CARDS_SCREENSHOT");
    const char *dir="test-scatter-mix-project";
#ifdef _WIN32
    assert(!_mkdir(dir));
#else
    assert(!mkdir(dir,0700));
#endif
    TsMosaic *loaded=ts_mosaic_create();assert(loaded);
    assert(ts_mosaic_save(scene,dir,error,sizeof(error)) && ts_mosaic_load(loaded,dir,error,sizeof(error)));
    assert(ts_mosaic_hash(loaded)==ts_mosaic_hash(scene));ts_mosaic_free(loaded);
    assert(!remove("test-scatter-mix-project/mosaic.tsm"));assert(!remove("test-scatter-mix-project/mosaic-000.wav"));
#ifdef _WIN32
    assert(!_rmdir(dir));
#else
    assert(!rmdir(dir));
#endif
    /* Full-capacity preview remains bounded work on the UI thread. */
    key(window,SDLK_r,KMOD_CTRL);click(window,430,45,1);
    for(int i=0;i<3;++i){click(window,210,150+i*36,1);click(window,210,150+i*36,1);}
    click(window,350,310,1);
    for(int i=0;i<8;++i)assert(!memcmp(&ui.mosaic_ghosts[i],&scene->events[i],sizeof(TsMosaicEvent)));
    click(window,260,105,1);ui.mosaic_scatter_count=TS_MOSAIC_EVENTS-9;ui.mosaic_scatter_overlap=1;
    for(int i=0;i<3;++i){click(window,210,150+i*36,1);click(window,210,150+i*36,1);}
    uint64_t tick=SDL_GetPerformanceCounter();
    for(int i=0;i<100;++i)mosaic_scatter_preview(&ui,&mosaic);
    fprintf(stderr,"Scatter mix: 1015-card preview %.3f ms mean wall (100 rolls)\n",
        1000.0*(SDL_GetPerformanceCounter()-tick)/SDL_GetPerformanceFrequency()/100);
    assert(ui.mosaic_scatter_valid && ui.mosaic_ghost_count==TS_MOSAIC_EVENTS-9);
    key(window,SDLK_ESCAPE,KMOD_NONE);
    mosaic_picks_clear(&ui,&mosaic);mosaic_select_only(&ui,0);ts_mosaic_free(scene);ui.mosaic=audio.mosaic=saved;mosaic_poll(0,&ui,&instrument,&mosaic);
}
