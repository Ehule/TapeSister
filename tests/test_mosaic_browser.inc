static void browser_mouse(SDL_Window *window,AudioState *a,TsUiState *u,TsInstrument *bank,
    MosaicController *c,PortalController *p,TransformController *t,int type,int x,int y,int clicks)
{
    SDL_Event e;SDL_zero(e);e.type=type;e.button.windowID=SDL_GetWindowID(window);
    e.button.button=SDL_BUTTON_LEFT;e.button.x=x;e.button.y=y;e.button.clicks=(Uint8)clicks;
    assert(mosaic_event(&e,window,0,a,u,bank,c,p,&sister,t,48000));
}
static void test_mosaic_browser(SDL_Window *window)
{
    SDL_SetWindowSize(window,640,400);SDL_SetModState(KMOD_NONE);
    AudioState *a=routing_audio(0);ts_mosaic_free(a->mosaic);a->mosaic=ts_mosaic_create();assert(a->mosaic);
    TsUiState *u=mosaic_record_ui_fixture(a);u->mosaic_scale=24;u->mosaic_hscale=1;u->mosaic_follow=0;
    TsInstrument *bank=malloc(sizeof(*bank));MosaicController *c=calloc(1,sizeof(*c));assert(bank && c);
    ts_instrument_init(bank);TsSamplePages pages;char error[160];
    assert(ts_sample_pages_init(&pages,error,sizeof(error)));pages.mosaic=a->mosaic;c->pages=&pages;
    PortalController p={.mosaic=c};TransformController t;transform_controller_init(&t);
    for(int i=1;i<5;++i)assert(ts_sample_pages_append(&pages,error,sizeof(error)));
    float data[128];TsSample sample={.data=data,.frames=128,.sample_rate=48000,.channels=1};
    /* The reported 75-original workflow: five library pages, many placed cards. */
    for(int i=0;i<75;++i) {
        TsInstrument *page=ts_sample_pages_page_mut(&pages,bank,(size_t)i/16);assert(page);
        assert(ts_instrument_select_bank(page,i%16,error,sizeof(error)));
        for(int j=0;j<128;++j)data[j]=.02f*(1+i)+j*.00001f;
        snprintf(sample.name,sizeof(sample.name),"Field recording %02d - river and tape.wav",i+1);
        assert(ts_instrument_import_sample(page,&sample,0,0,128,TS_LOOP_FORWARD,error,sizeof(error)));
    }
    u->sample_page_count=5;mosaic_poll(0,u,bank,c);
    assert(u->mosaic_source_filter==TS_MOSAIC_BROWSER_AUDIO && u->mosaic_source_pages==5);
    uint64_t original_hash[75];TsMosaicEvent *cards[75];
    for(int i=0;i<75;++i) {
        u->mosaic_source_page=i/16;mosaic_poll(0,u,bank,c);
        original_hash[i]=u->mosaic_sources[i%16].hash;
        int x=20+(i%2)*49,y=85+((i%16)/2)*34;
        browser_mouse(window,a,u,bank,c,&p,&t,SDL_MOUSEBUTTONDOWN,x,y,1);
        browser_mouse(window,a,u,bank,c,&p,&t,SDL_MOUSEBUTTONUP,180,100,1);
        cards[i]=ts_mosaic_find(u->mosaic,u->mosaic_selected);assert(cards[i]);
        cards[i]->duration=2+i*.1;cards[i]->fade_in=.1;cards[i]->pan=.2f;
        assert(cards[i]->source->hash==original_hash[i]);
        assert(u->mosaic_source_filter==TS_MOSAIC_BROWSER_AUDIO && u->mosaic_source_pages==5);
        assert(ts_sample_pages_count(&pages)==5);
    }
    int sources=0;for(TsMosaicSource *s=u->mosaic->sources;s;s=s->next)++sources;assert(sources==75);
    /* Repeating audio remains an Event; independent live cards live in Loops. */
    cards[0]->live=1;cards[1]->live=1;cards[2]->looping=1;
    snprintf(cards[2]->name,sizeof(cards[2]->name),"River foreground take");
    uint64_t scene_hash=ts_mosaic_hash(u->mosaic);u->mosaic->playing=1;u->mosaic->time=2.25;
    browser_mouse(window,a,u,bank,c,&p,&t,SDL_MOUSEBUTTONDOWN,25,54,1);
    assert(u->mosaic_source_filter==TS_MOSAIC_BROWSER_LOOPS && u->mosaic_source_pages==1);
    assert(u->mosaic_source_count==18 && u->mosaic_sources[16].event==cards[0]->id);
    browser_mouse(window,a,u,bank,c,&p,&t,SDL_MOUSEBUTTONDOWN,80,54,1);
    assert(u->mosaic_source_filter==TS_MOSAIC_BROWSER_EVENTS && u->mosaic_source_pages==5);
    assert(u->mosaic_source_count==16+73 && u->mosaic_sources[16].event==cards[2]->id);
    assert(!strcmp(u->mosaic_sources[16].name,"River foreground take"));
    mosaic_source_drag(u,bank,c,16);assert(c->source_drag.duration==cards[2]->duration);
    assert(c->source_drag.fade_in==.1 && c->source_drag.pan==.2f);mosaic_drag_end(u,c);
    u->mosaic_source_page=3;mosaic_poll(0,u,bank,c);
    browser_mouse(window,a,u,bank,c,&p,&t,SDL_MOUSEBUTTONDOWN,25,39,1);
    assert(u->mosaic_source_filter==TS_MOSAIC_BROWSER_AUDIO && u->mosaic_source_page==4 && u->mosaic_source_pages==5);
    mosaic_source_select_event(u,cards[2]);assert(u->mosaic_source_filter==TS_MOSAIC_BROWSER_AUDIO && u->mosaic_source_page==4);
    browser_mouse(window,a,u,bank,c,&p,&t,SDL_MOUSEBUTTONDOWN,80,54,1);
    assert(u->mosaic_source_page==3);mosaic_source_select_event(u,cards[2]);assert(u->mosaic_source_page==0);
    assert(ts_mosaic_hash(u->mosaic)==scene_hash && u->mosaic->playing && u->mosaic->time==2.25);
    const char *shot=getenv("TS_MOSAIC_BROWSER_SCREENSHOT");
    if(shot)live_controller_screenshot_size(u,shot,1729,973);
    /* Exact placed identity opens its private copy; Back restores the original bank. */
    uint64_t bank_hash=ts_sample_hash(&bank->current);
    browser_mouse(window,a,u,bank,c,&p,&t,SDL_MOUSEBUTTONDOWN,20,85,2);
    assert(c->active==cards[2]->id && u->mosaic_editing==cards[2]->id && !u->mosaic_open);
    assert(strstr(u->status,"MOSAIC CARD") && strstr(u->status,"COPY"));
    assert(c->bank->current.data!=bank->current.data && cards[2]->source->sample.data!=bank->current.data);
    SDL_Event back={.type=SDL_MOUSEBUTTONDOWN};back.button.windowID=SDL_GetWindowID(window);
    back.button.button=SDL_BUTTON_LEFT;back.button.x=TS_UI_MOSAIC_X+10;back.button.y=10;
    assert(workspace_event(&back,window,0,a,u,bank,c,&p,&sister,&t,&workspace_preview));
    assert(!c->active && !u->mosaic_editing && u->mosaic_open && ts_sample_hash(&bank->current)==bank_hash);
    /* Original double-click selects its actual bank/tile and makes no card. */
    mosaic_source_filter_set(u,bank,c,TS_MOSAIC_BROWSER_AUDIO);u->mosaic_source_page=2;mosaic_poll(0,u,bank,c);
    browser_mouse(window,a,u,bank,c,&p,&t,SDL_MOUSEBUTTONDOWN,20,85,2);
    assert(!c->active && !u->mosaic_open && ts_sample_pages_active(&pages)==2 && bank->selected_slot==0);
    assert(ts_sample_hash(&bank->current)==original_hash[32] && ts_sample_pages_count(&pages)==5);
    u->mosaic_open=1;mosaic_poll(0,u,bank,c);
    /* Saved V2 locations retain all four filters. Old mixed pages migrate. */
    for(int filter=0;filter<TS_MOSAIC_BROWSER_COUNT;++filter) {
        mosaic_source_filter_set(u,bank,c,filter);TsProjectView view;project_view_capture(&view,u);
        assert(view.version==2 && view.source_filter==filter);
        u->mosaic_source_filter=0;project_view_apply(u,bank,&view);mosaic_poll(0,u,bank,c);
        assert(u->mosaic_source_filter==filter);
    }
    TsProjectView legacy;project_view_capture(&legacy,u);legacy.version=1;
    legacy.source_filter=0;legacy.source_page=7;
    project_view_apply(u,bank,&legacy);mosaic_poll(0,u,bank,c);
    assert(u->mosaic_source_filter==TS_MOSAIC_BROWSER_EVENTS && u->mosaic_source_page==0);
    legacy.source_page=4;project_view_apply(u,bank,&legacy);mosaic_poll(0,u,bank,c);
    assert(u->mosaic_source_filter==TS_MOSAIC_BROWSER_AUDIO && u->mosaic_source_page==4);
    legacy.source_filter=1;legacy.source_page=7;project_view_apply(u,bank,&legacy);mosaic_poll(0,u,bank,c);
    assert(u->mosaic_source_filter==TS_MOSAIC_BROWSER_PATTERNS && u->mosaic_source_page==0);
    mosaic_picks_clear(u,c);mosaic_drag_end(u,c);mosaic_controller_free(c);free(c);
    ts_instrument_free(bank);free(bank);ts_sample_pages_free(&pages);free(u);routing_free(a);
}
