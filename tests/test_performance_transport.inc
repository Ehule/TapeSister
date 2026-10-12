static void test_performance_transport(const char *directory)
{
    TsMosaic *saved=a.mosaic,*m=ts_mosaic_create();assert(m);a.mosaic=m;
    TsMosaicSource *source=ts_mosaic_source(m,&bank.current,error,sizeof(error));assert(source);
    TsMosaicEvent *card=ts_mosaic_add(m,source,0,0);assert(card);card->duration=20;
    TsPerformanceMixer controls=a.sister.performance_mixer;
    unsigned mask=a.sister.performance_transport;a.sister.performance_transport=PERF_TRACKER|PERF_MOSAIC;
    ts_tapehead_stop();m->playing=0;m->time=2;
    assert(performance_command(1,window,0,&a,&u,&pages,&bank,NULL,48000));assert(ts_tapehead_running() && m->playing && m->time==2);
    assert(performance_command(-1,window,0,&a,&u,&pages,&bank,NULL,48000));assert(!ts_tapehead_running() && !m->playing && m->time==2);
    m->playing=1;assert(performance_command(-1,window,0,&a,&u,&pages,&bank,NULL,48000));assert(!m->playing && !ts_tapehead_running());
    assert(performance_command(2,window,0,&a,&u,&pages,&bank,NULL,48000));assert(m->time==0);
    a.sister.performance_transport=PERF_MOSAIC;startPlaying(PLAYMODE_SONG,0);
    assert(performance_command(0,window,0,&a,&u,&pages,&bank,NULL,48000));assert(ts_tapehead_running());
    a.sister.performance_transport=7;assert(!performance_command(1,window,0,&a,&u,&pages,&bank,NULL,48000));assert(!m->playing);
    a.sister.performance_transport=6;
    a.sister.performance_mixer.restart=1;m->time=3;
    assert(performance_command(1,window,0,&a,&u,&pages,&bank,NULL,48000));assert(m->playing && m->time==0 && ts_tapehead_running());
    assert(performance_command_for(PERF_MOSAIC,0,0,window,0,&a,&u,&pages,&bank,NULL,48000));assert(!m->playing && ts_tapehead_running());
    performance_show(&u,&a);
    SDL_Event e;SDL_zero(e);e.type=SDL_KEYDOWN;e.key.windowID=SDL_GetWindowID(window);e.key.keysym.sym=SDLK_F11;
    int tracker_open=u.tracker_open;u.tracker_open=0;matrix_window.rename=-1;
    assert(!application_fullscreen_event(&e,window,NULL,&u));
    assert(performance_event(&e,window,0,&a,&u,&pages,&bank,NULL,48000));assert(performance_transport.visible);
    e.key.keysym.mod=KMOD_ALT;
    assert(!performance_event(&e,window,0,&a,&u,&pages,&bank,NULL,48000));
    int fullscreen=(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN)!=0;
    u.tracker_open=1;assert(application_fullscreen_event(&e,window,NULL,&u));
    assert(((SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN)!=0)!=fullscreen);
    e.key.repeat=1;assert(application_fullscreen_event(&e,window,NULL,&u));
    assert(((SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN)!=0)!=fullscreen);
    e.key.repeat=0;e.key.windowID=performance_transport.id;assert(application_fullscreen_event(&e,window,NULL,&u));
    assert(((SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN)!=0)==fullscreen);u.tracker_open=0;
    e.key.windowID=SDL_GetWindowID(window);e.key.keysym.mod=KMOD_CTRL;
    assert(!application_fullscreen_event(&e,window,NULL,&u));
    assert(!performance_event(&e,window,0,&a,&u,&pages,&bank,NULL,48000));
    e.key.keysym.sym=SDLK_SPACE;e.key.keysym.mod=KMOD_CTRL|KMOD_SHIFT;
    assert(performance_event(&e,window,0,&a,&u,&pages,&bank,NULL,48000));
    e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=performance_transport.id;e.button.button=SDL_BUTTON_LEFT;e.button.clicks=1;
    e.button.x=430;e.button.y=24;assert(performance_event(&e,window,0,&a,&u,&pages,&bank,NULL,48000));assert(a.sister.performance_mixer.always_on_top);
    /* SDL dummy has no window manager; persistence is checked independently. */
    if(strcmp(SDL_GetCurrentVideoDriver(),"dummy"))assert(SDL_GetWindowFlags(performance_transport.window)&SDL_WINDOW_ALWAYS_ON_TOP);
    e.button.x=352;e.button.y=100;assert(performance_event(&e,window,0,&a,&u,&pages,&bank,NULL,48000));assert(a.sister.performance_mixer.gain[0]==0);
    e.type=SDL_MOUSEMOTION;e.motion.windowID=performance_transport.id;e.motion.x=500;
    assert(performance_event(&e,window,0,&a,&u,&pages,&bank,NULL,48000));assert(a.sister.performance_mixer.gain[0]==2);
    e.type=SDL_MOUSEBUTTONUP;e.button.windowID=performance_transport.id;performance_event(&e,window,0,&a,&u,&pages,&bank,NULL,48000);assert(!performance_transport.drag);
    e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_RIGHT;e.button.x=400;e.button.y=100;
    performance_event(&e,window,0,&a,&u,&pages,&bank,NULL,48000);assert(a.sister.performance_mixer.gain[0]==1);
    e.button.button=SDL_BUTTON_LEFT;e.button.x=320;e.button.y=250;
    for(int i=1;i<=4;++i) {performance_event(&e,window,0,&a,&u,&pages,&bank,NULL,48000);assert(a.sister.performance_mixer.arp_sync==(unsigned)i%4);}
    /* Fader edits affect saved-state dirty detection. */
    uint64_t h=paged_project_state_hash(&pages,&bank,NULL,&a.sister,&a.keyboard_sequence);
    performance_gain(0,&a,1,.5f);assert(h!=paged_project_state_hash(&pages,&bank,NULL,&a.sister,&a.keyboard_sequence));
    performance_gain(0,&a,1,1);u.tracker_open=tracker_open;
    if(directory) {
        performance_show(&u,&a);performance_transport.painted=SDL_GetTicks()-100;performance_update(0,&a);
        char path[1024];snprintf(path,sizeof(path),"%s/performance-transport.bmp",directory);
        SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,PERF_W,PERF_H,32,SDL_PIXELFORMAT_ARGB8888);assert(s);
        assert(!SDL_RenderReadPixels(performance_transport.renderer,NULL,SDL_PIXELFORMAT_ARGB8888,s->pixels,s->pitch));assert(!SDL_SaveBMP(s,path));SDL_FreeSurface(s);
    }
    ts_tapehead_stop();ts_note_bank_clear(&a.notes);
    TsMatrixControls matrix=a.sister.matrix.controls;TsMatrixControls direct;
    ts_matrix_default(&direct);direct.enabled=1;assert(ts_matrix_set(&a.sister.matrix,&direct,48000));
    card->output_route.matrix_enabled=1;card->output_route.matrix_gain[TS_MATRIX_NODES]=1000;
    ts_mosaic_seek(m,0);m->playing=1;
    double energy[4];render(energy);assert(fabsf(a.clean.graph[TS_MATRIX_NODES].l)>0);
    double before=m->time;performance_gain(0,&a,2,0);render(energy);
    assert(a.sister.performance_gain_current[2]==0 && a.clean.graph[TS_MATRIX_NODES].l==0 && a.clean.graph[TS_MATRIX_NODES].r==0);
    assert(m->playing && m->time>before);
    performance_gain(0,&a,2,1);render(energy);assert(fabsf(a.clean.graph[TS_MATRIX_NODES].l)>0);
    assert(ts_matrix_set(&a.sister.matrix,&matrix,48000));
    performance_close();ts_tapehead_stop();a.sister.performance_transport=mask;a.sister.performance_mixer=controls;a.mosaic=saved;ts_mosaic_free(m);
}
