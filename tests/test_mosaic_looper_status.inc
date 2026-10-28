/* Public render paths: a four-second capture setting must not determine the
   status/progress of a longer loop. Audio/controller coverage below already
   exercises the stable central record action and capture/playback overlap. */
static void looper_status_check(TsUiState *u,TsUiLooperCircle *circle,uint32_t color,double phase)
{
    assert(ts_ui_mosaic_looper_circle(circle,u,1280,800));
    assert(circle->last_phase==(uint32_t)lrint(phase*65536));
    int cx=circle->width/2,cy=circle->height/2;
    assert(circle->pixels[(cy+44)*circle->width+cx]==color);
    TsFramebuffer *fb=malloc(sizeof(*fb));assert(fb);ts_ui_render(fb,u,&instrument);
    int x,y;ts_ui_mosaic_live_origin(u,&x,&y);
    assert(fb->pixels[(y+108)*TS_UI_WIDTH+x+83]==color);free(fb);
    /* Repainting an unchanged snapshot reuses its buffers and raster. */
    uint32_t *pixels=circle->pixels;uint64_t raster=circle->raster_count;
    assert(ts_ui_mosaic_looper_circle(circle,u,1280,800) && !circle->changed);
    assert(circle->pixels==pixels && circle->raster_count==raster);
}
static void looper_status_shot(TsUiState *u,const char *state)
{
    const char *prefix=getenv("TS_TEST_LIVE_LOOPS");
    if(prefix) {char path[1200];snprintf(path,sizeof(path),"%s-status-%s.ppm",prefix,state);live_controller_screenshot_size(u,path,1280,800);}
}
static void test_mosaic_looper_status(SDL_Window *window,SDL_AudioDeviceID device)
{
    AudioState *a=routing_audio(0);TsUiState *u=mosaic_record_ui_fixture(a);
    ExternalInputState *s=mosaic_record_input_fixture(a);MosaicController *c=calloc(1,sizeof(*c));assert(c);
    TsMosaic *m=u->mosaic;TsMosaicEvent *e=&m->events[0];uint64_t id=e->id;
    u->mosaic_live_open=1;m->playing=0;
    mosaic_poll(device,u,&instrument,c);mosaic_select_only(u,id);
    m->live_capture.length=4;m->live_capture.units=0;m->live_capture.quantize=0;
    SDL_AudioDeviceID input=0;assert(mosaic_live_command(device,&input,a,s,u,"role"));assert(e->live);
    e->duration=12;e->live_finite=1;
    mosaic_poll(device,u,&instrument,c);TsUiLooperCircle circle={0};
    looper_status_check(u,&circle,0xff4e5869u,0);looper_status_shot(u,"stopped");
    /* Actual Play plus one second of audio. Source cycles are much shorter
       than the 12-second finite event. Progress is elapsed / event duration. */
    live_performance_transport(window,device,a,s,u,&instrument,c,2);
    assert(m->live[e-m->events].state==TS_MOSAIC_LIVE_PLAYING);
    float output[512];for(int i=0;i<188;++i)audio_callback(a,(Uint8 *)output,sizeof(output));
    mosaic_poll(device,u,&instrument,c);
    int slot=(int)(e-m->events);double phase=u->mosaic_live[slot].elapsed/12;
    assert(phase>.08 && phase<.09);looper_status_check(u,&circle,0xff1e8b53u,phase);
    int center=circle.width/2;
    assert(circle.pixels[(center-4)*circle.width+center]==0xffffffffu); /* Play triangle. */
    looper_status_shot(u,"playing");
    /* Selecting a stopped ordinary card cannot turn running playback red or
       stopped. Keep the actual selected card's edit/Loop Event controls. */
    TsMosaicEvent *ordinary=ts_mosaic_add(m,e->source,0,250);assert(ordinary);
    mosaic_select_only(u,ordinary->id);mosaic_poll(device,u,&instrument,c);
    looper_status_check(u,&circle,0xff1e8b53u,phase);
    m->live_capture.length=7;m->live_capture.units=1;
    looper_status_check(u,&circle,0xff1e8b53u,phase);
    /* Until Stop uses the sounding source cycle, never its display footprint. */
    e->live_finite=0;mosaic_poll(device,u,&instrument,c);
    looper_status_check(u,&circle,0xff1e8b53u,u->mosaic_live[slot].phase);
    e->live_finite=1;
    /* The sequencer holds the current entry's length even if the card is
       resized. Its UI snapshot is the timing authority for the ring. */
    u->mosaic_sequence_running=1;u->mosaic_sequence_id=id;u->mosaic_sequence_duration=20;
    looper_status_check(u,&circle,0xff1e8b53u,u->mosaic_live[slot].elapsed/20);
    u->mosaic_sequence_running=0;
    /* Recording/preparation override playback without stopping that player. */
    u->mosaic_live_recording=1;u->mosaic_live_seconds=1;u->mosaic_live_duration=4;
    m->live_capture.length=4;m->live_capture.units=0;
    looper_status_check(u,&circle,0xffe51820u,.25);looper_status_shot(u,"recording");
    u->mosaic_live_duration=0;looper_status_check(u,&circle,0xffe51820u,0); /* Manual has no fake cycle. */
    u->mosaic_live_recording=0;u->mosaic_live_finishing=1;
    looper_status_check(u,&circle,0xff97651cu,0);looper_status_shot(u,"preparing");
    u->mosaic_live_finishing=0;looper_status_check(u,&circle,0xff1e8b53u,phase);
    /* Waiting for a beat is amber with pause bars, not green playback. */
    ts_mosaic_live_stop(m,0);for(int i=0;i<4;++i)audio_callback(a,(Uint8 *)output,sizeof(output));
    ts_mosaic_live_clock(m,1,0,.5,.25);assert(ts_mosaic_live_launch(m,id,1));mosaic_poll(device,u,&instrument,c);
    assert(u->mosaic_live[slot].state==TS_MOSAIC_LIVE_WAITING);
    looper_status_check(u,&circle,0xff97651cu,0);
    assert(circle.pixels[(center-4)*circle.width+center]==0xff97651cu);
    assert(circle.pixels[(center-4)*circle.width+center-12]==0xffffffffu);
    looper_status_shot(u,"waiting");
    /* A playing background takes precedence over a waiting selection. */
    TsMosaicEvent *bg=ts_mosaic_copy(m,id,0,150);assert(bg);bg->live_order=-1;
    mosaic_select_only(u,id);assert(ts_mosaic_live_launch(m,bg->id,0));mosaic_poll(device,u,&instrument,c);
    looper_status_check(u,&circle,0xff1e8b53u,0);
    ts_mosaic_live_stop(m,0);for(int i=0;i<4;++i)audio_callback(a,(Uint8 *)output,sizeof(output));
    mosaic_poll(device,u,&instrument,c);looper_status_check(u,&circle,0xff4e5869u,0);
    assert(circle.pixels[(center-4)*circle.width+center]==0xffffffffu); /* Stop square. */
    ts_ui_mosaic_looper_circle_free(&circle);ts_external_recorder_free(&s->recorder);free(s);free(u);
    mosaic_controller_free(c);free(c);routing_free(a);
}
