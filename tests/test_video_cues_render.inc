static void cue_render_pump(Mandala *m,VisualShared *shared,unsigned ms)
{
    Uint64 start=SDL_GetTicks64();
    do {m_video_poll(m,shared,0);m_scene(m,.02);SDL_Delay(5);}while(SDL_GetTicks64()-start<ms);
}
static void video_cue_render_checks(void)
{
    TsMandalaConfig config;ts_mandala_default(&config);config.geometry_enabled=0;config.video_enabled=1;
    Mandala *m=calloc(1,sizeof(*m));assert(m && m_init(m,&config,0));
    VisualShared shared;TsMandalaPreferences prefs={0};shared_init(&shared,&prefs);
    TsVisualCue cue={.serial=1,.random_start=1,.transition=1,.fade=.5,.monochrome=1};
    snprintf(cue.path,sizeof(cue.path),"%s",TS_VIDEO_FIXTURE);m_cue_request(m,&cue);
    cue_render_pump(m,&shared,350);assert(m->video_texture && m->video_status.state!=TS_VIDEO_ERROR);
    /* Clean video ignores mandala blend/zoom and contains only grayscale ink. */
    config.video_mix=0;config.video_scale=100;m_configure(m,&config);cue_render_pump(m,&shared,100);
    uint32_t *pixels=malloc((size_t)m->width*m->height*4);assert(pixels);
    SDL_SetRenderTarget(m->renderer,m->history);assert(!SDL_RenderReadPixels(m->renderer,NULL,SDL_PIXELFORMAT_ARGB8888,pixels,m->width*4));SDL_SetRenderTarget(m->renderer,NULL);
    unsigned nonblack=0;for(int i=0;i<m->width*m->height;++i){uint32_t p=pixels[i];assert(((p>>16)&255)==((p>>8)&255) && ((p>>8)&255)==(p&255));nonblack+=(p&0xffffff)!=0;}assert(nonblack>100);free(pixels);
    ++cue.serial;cue.random_start=0;m_cue_request(m,&cue);cue_render_pump(m,&shared,130);
    assert(m->cue_fading && m->old_video && m->video_texture);
    ++cue.serial;m_cue_request(m,&cue);assert(m->cue_snapshot && !m->old_video);
    cue_render_pump(m,&shared,650);assert(!m->cue_fading && !m->cue_snapshot && !m->old_video);
    cue.transition=2;++cue.serial;m_cue_request(m,&cue);cue_render_pump(m,&shared,650);assert(!m->cue_fading);
    /* Changing mandala mode keeps the same video player and source position. */
    TsVideo *video=m->video;config.geometry_enabled=1;m_configure(m,&config);cue_render_pump(m,&shared,80);assert(m->video==video);
    config.geometry_enabled=0;m_configure(m,&config);cue_render_pump(m,&shared,80);assert(m->video==video);
    /* Directory shuffle uses each entry once. A one-file pool remains usable. */
    TsVisualCue pool=cue;pool.pool=1;snprintf(pool.path,sizeof(pool.path),"%s",TS_VIDEO_FIXTURE);char *slash=strrchr(pool.path,'/');assert(slash);*slash=0;
    char first[1024],second[1024];assert(m_cue_path(m,&pool,first) && m_cue_path(m,&pool,second));assert(strcmp(first,second));
    if(getenv("TS_TEST_VIDEO_RENDER")){SDL_SetRenderTarget(m->renderer,m->history);SDL_Surface *image=SDL_CreateRGBSurfaceWithFormat(0,m->width,m->height,32,SDL_PIXELFORMAT_ARGB8888);assert(image);assert(!SDL_RenderReadPixels(m->renderer,NULL,SDL_PIXELFORMAT_ARGB8888,image->pixels,image->pitch));assert(!SDL_SaveBMP(image,getenv("TS_TEST_VIDEO_RENDER")));SDL_FreeSurface(image);SDL_SetRenderTarget(m->renderer,NULL);}
    #ifdef _WIN32
    assert(!_mkdir("video-one-file"));
    #else
    assert(!mkdir("video-one-file",0700));
    #endif
    FILE *in=fopen(TS_VIDEO_FIXTURE,"rb"),*out=fopen("video-one-file/only.mp4","wb");assert(in && out);char bytes[4096];size_t count;
    while((count=fread(bytes,1,sizeof(bytes),in)))assert(fwrite(bytes,1,count,out)==count);assert(!ferror(in));fclose(in);assert(!fclose(out));
    strcpy(pool.path,"video-one-file");for(int i=0;i<4;++i){assert(m_cue_path(m,&pool,first));assert(strstr(first,"only.mp4"));}
    m_destroy(m);free(m);assert(!remove("video-one-file/only.mp4"));
    #ifdef _WIN32
    assert(!_rmdir("video-one-file"));
    #else
    assert(!rmdir("video-one-file"));
    #endif
    /* Exercise the real existing editor controls. */
    mandala_editor_video_loaded(&u,&a.sister.mandala);unsigned geometry=a.sister.mandala.geometry_enabled;
    editor_click(570,215,SDL_BUTTON_LEFT);assert(a.sister.mandala.geometry_enabled!=geometry);
    editor_click(580,76,SDL_BUTTON_LEFT);assert(a.sister.mandala.video_monochrome);mandala_editor_close();
    /* Full audio-frame latch -> main UI follower -> IPC -> child decode. */
    a.sister.mandala.geometry_enabled=0;a.sister.mandala.video_enabled=1;
    TsMosaicSource *source=ts_mosaic_video_source(a.mosaic,TS_VIDEO_FIXTURE,0);assert(source);
    TsMosaicEvent *event=ts_mosaic_add(a.mosaic,source,a.mosaic->time+.01,0);assert(event);event->video_random_start=1;
    for(int i=0;i<1000;++i)ts_mosaic_read(a.mosaic,48000);assert(a.mosaic->video_id==event->id);
    TsMandalaPreferences output={0};assert(ts_mandala_open(&a.sister.mandala,&output,error,sizeof(error)));
    u.mosaic=a.mosaic;mosaic_video_follow(0,&a,&u);assert(!strcmp(host.cue.path,TS_VIDEO_FIXTURE) && host.cue.random_start);
    uint64_t sent=host.cue.serial;mosaic_video_follow(0,&a,&u);assert(host.cue.serial==sent);
    Uint32 started=SDL_GetTicks();while(ts_mandala_video_status().state!=TS_VIDEO_PLAYING && SDL_GetTicks()-started<5000)video_pump(20,&output);
    assert(ts_mandala_video_status().state==TS_VIDEO_PLAYING && ts_tapehead_running() && a.mosaic->playing);
    TsMosaicGallery *gallery=ts_gallery_create(a.mosaic);assert(gallery);assert(ts_gallery_add(gallery,error,sizeof(error)));
    TsMosaic *destination=gallery->canvas[1].scene;source=ts_mosaic_video_source(destination,TS_VIDEO_FIXTURE,0);assert(source);
    TsMosaicEvent *next=ts_mosaic_add(destination,source,0,0);assert(next);next->video_transition=2;next->video_fade=.25;
    a.gallery=gallery;assert(ts_gallery_switch(gallery,1,error,sizeof(error)));mosaic_video_follow(0,&a,&u);
    assert(host.cue.serial>sent && host.cue.transition==2 && host.cue.fade==.25);
    a.gallery=NULL;ts_gallery_free(gallery);ts_mandala_shutdown();
    puts("video render: clean grayscale, random seek, pool shuffle, moving dissolves, interruption snapshot, black transition and mandala toggle PASS");
}
