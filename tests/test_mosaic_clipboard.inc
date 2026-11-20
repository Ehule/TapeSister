static int clipboard_card_count(const TsMosaic *m)
{int count=0;for(int i=0;i<TS_MOSAIC_EVENTS;++i)count+=m->events[i].id!=0;return count;}
static void test_mosaic_clipboard(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic;TsSamplePages *pages=calloc(1,sizeof(*pages));char error[256];assert(pages);
    assert(ts_sample_pages_init(pages,error,sizeof(error)));
    if(!pages->mosaic)pages->mosaic=ts_mosaic_create();
    TsMosaicGallery *g=pages->gallery=ts_gallery_create(pages->mosaic);assert(g);
    assert(ts_gallery_add(g,error,sizeof(error)) && ts_gallery_add(g,error,sizeof(error)));
    g->active=1;g->fade_seconds=0;TsMosaic *from=g->canvas[1].scene,*to=g->canvas[2].scene;
    ui.mosaic=audio.mosaic=pages->mosaic=from;audio.gallery=g;ui.mosaic_gallery=g;mosaic.pages=pages;
    ui.mosaic_open=1;mosaic_poll(0,&ui,&instrument,&mosaic);
    TsMasterEqControls eq;ts_master_eq_default(&eq);eq.band[0].gain_db=3;
    TsMosaicSource *source=ts_mosaic_source_eq(from,&instrument.current,&instrument.current,&eq,error,sizeof(error));assert(source);
    TsMosaicEvent *a=ts_mosaic_add(from,source,2,30),*b=ts_mosaic_add(from,source,5,160);assert(a && b);
    a->duration=4;a->gain=.7f;a->pan=-.4f;a->fade_in=.2;a->notes[0]=65;a->color=0xffba8fc0;
    snprintf(a->name,sizeof(a->name),"Copied melody");a->output_route.matrix_enabled=1;a->output_route.matrix_gain[TS_MATRIX_MAIN]=800;
    b->duration=2;b->muted=1;b->x=160;
    TsMosaicSource *video=ts_mosaic_video_source(from,"copied-video.mp4",0);assert(video);
    TsMosaicEvent *v=ts_mosaic_add(from,video,4,300);assert(v);v->video_fade=.5;
    /* A captured score owns its instrument audio after its source canvas dies. */
    TsMosaicSource *pattern=calloc(1,sizeof(*pattern));assert(pattern);
    pattern->pattern=calloc(1,sizeof(*pattern->pattern));assert(pattern->pattern);
    TsPatternCard *score=pattern->pattern;ts_sister_tracker_init(&score->score);
    assert(ts_sister_tracker_add_pattern(&score->score,16,&score->id,error,sizeof(error)));
    score->root=60;score->duration=2;score->enabled=1;score->global_volume=64;score->linear=1;
    score->assets=calloc(1,sizeof(*score->assets));assert(score->assets);score->asset_count=1;
    score->assets[0]=(TsPatternAsset){.tile=1,.audio=source,.last=source->sample.frames,.tuning={60,0}};
    assert(ts_sample_clone(&pattern->sample,&source->sample,error,sizeof(error)));
    pattern->hash=ts_pattern_hash(score);pattern->next=from->sources;from->sources=pattern;
    TsMosaicEvent *p=ts_mosaic_add(from,pattern,6,420);assert(p);
    TsMosaicEvent expected=*a;
    key(window,SDLK_a,KMOD_CTRL);key(window,SDLK_c,KMOD_CTRL);assert(mosaic.clipboard);
    assert(mosaic.clipboard->events[0].source!=source && mosaic.clipboard->events[1].source==mosaic.clipboard->events[0].source);
    assert(mosaic_gallery_enter(0,&audio,&ui,&instrument,&mosaic,2));assert(mosaic.clipboard && ui.mosaic==to);
    TsMosaic *retired=NULL;assert(ts_gallery_detach(g,1,&retired,error,sizeof(error)) && retired==from);ts_mosaic_free(retired);
    ui.mosaic_time=10;ui.mosaic_xscroll=80;int history=to->history_count;
    /* Paste is discoverable even on an empty destination canvas. */
    SDL_Event menu;SDL_zero(menu);menu.type=SDL_MOUSEBUTTONDOWN;menu.button.windowID=SDL_GetWindowID(window);
    menu.button.button=SDL_BUTTON_RIGHT;menu.button.x=250;menu.button.y=200;assert(dispatch(&menu,window));
    assert(ui.mosaic_context_open);click(window,ui.mosaic_context_x+20,ui.mosaic_context_y+2+5*24+10,1);
    assert(!ui.mosaic_context_open && to->history_count==history+1 && clipboard_card_count(to)==4);
    a=&to->events[0];b=&to->events[1];v=&to->events[2];p=&to->events[3];
    assert(a->start==10 && b->start==13 && v->start==12 && p->start==14);
    assert(a->x==80 && b->x-a->x==130 && a->source==b->source);
    expected.id=a->id;expected.revision=a->revision;expected.source=a->source;expected.start=a->start;expected.x=a->x;
    assert(!memcmp(a,&expected,sizeof(expected)) && a->source->eq.band[0].gain_db==3 && a->source->eq_dry.data);
    assert(!strcmp(v->source->video_path,"copied-video.mp4") && v->video_fade==.5);
    assert(p->source->pattern && p->source->pattern->assets[0].audio==a->source);
    assert(p->source->pattern!=mosaic.clipboard->events[3].source->pattern);
    for(int i=0;i<4;++i)assert(mosaic_chosen(&ui,i));
    key(window,SDLK_z,KMOD_CTRL);assert(!clipboard_card_count(to));key(window,SDLK_y,KMOD_CTRL);assert(clipboard_card_count(to)==4);
    /* Repeated paste preserves spacing under Auto-space, and full canvases
       reject the whole group without a partial paste or an undo entry. */
    key(window,SDLK_v,KMOD_CTRL);assert(clipboard_card_count(to)==8);
    assert(to->events[5].x-to->events[4].x==130 && to->events[4].start==10 && to->events[4].source==a->source);
    for(int i=8;i<TS_MOSAIC_EVENTS;++i){to->events[i]=to->events[0];to->events[i].id=to->next_id++;}
    history=to->history_count;uint64_t hash=ts_mosaic_hash(to);key(window,SDLK_v,KMOD_CTRL);
    assert(to->history_count==history && ts_mosaic_hash(to)==hash && strstr(ui.status,"NOTHING PASTED"));
    mosaic_controller_free(&mosaic);assert(!mosaic.clipboard);
    audio.gallery=NULL;ui.mosaic_gallery=NULL;ui.mosaic=audio.mosaic=saved;
    ts_sample_pages_free(pages);free(pages);mosaic_poll(0,&ui,&instrument,&mosaic);
    puts("Mosaic group clipboard survives Gallery switches/source deletion, preserves assets and settings, and pastes atomically.");
}
