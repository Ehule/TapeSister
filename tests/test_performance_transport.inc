static void test_performance_transport(const char *directory)
{
    TsMosaic *saved=a.mosaic,*m=ts_mosaic_create();assert(m);a.mosaic=m;
    TsMosaicSource *source=ts_mosaic_source(m,&bank.current,error,sizeof(error));assert(source);
    TsMosaicEvent *card=ts_mosaic_add(m,source,0,0);assert(card);card->duration=20;
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
    if(directory) {
        performance_show(&u);performance_transport.painted=SDL_GetTicks()-100;performance_update(0,&a);
        char path[1024];snprintf(path,sizeof(path),"%s/performance-transport.bmp",directory);
        SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,PERF_W,PERF_H,32,SDL_PIXELFORMAT_ARGB8888);assert(s);
        assert(!SDL_RenderReadPixels(performance_transport.renderer,NULL,SDL_PIXELFORMAT_ARGB8888,s->pixels,s->pitch));assert(!SDL_SaveBMP(s,path));SDL_FreeSurface(s);
    }
    performance_close();ts_tapehead_stop();a.sister.performance_transport=mask;a.mosaic=saved;ts_mosaic_free(m);
}
