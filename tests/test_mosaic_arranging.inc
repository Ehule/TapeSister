static int mosaic_event_count(const TsMosaic *m)
{
    int count=0;for(int i=0;i<TS_MOSAIC_EVENTS;++i)count+=m->events[i].id!=0;return count;
}
static void test_mosaic_copy_drag(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*scene=ts_mosaic_create();assert(scene);ui.mosaic=audio.mosaic=scene;
    char error[160];TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *a=ts_mosaic_add(scene,source,.5,20);a->duration=2;a->looping=0;
    TsMosaicEvent *b=ts_mosaic_add(scene,source,1,130);b->duration=3;b->note_count=2;b->notes[1]=67;
    TsMosaicEvent *obstacle=ts_mosaic_add(scene,source,1,250);obstacle->duration=3;
    ui.mosaic_open=1;ui.mosaic_scale=24;ui.mosaic_hscale=1;ui.mosaic_scroll=ui.mosaic_xscroll=0;
    mosaic_poll(0,&ui,&instrument,&mosaic);
    SDL_SetModState(KMOD_SHIFT);click(window,155,70,1);motion(window,385,180);release(window,385,180);
    assert(mosaic_chosen(&ui,0) && mosaic_chosen(&ui,1));
    int history=scene->history_count;click(window,180,96,1);release(window,180,96);
    assert(mosaic_event_count(scene)==3 && scene->history_count==history); /* Shift-click is not a copy. */
    click(window,180,96,1);assert(mosaic.drag_copy && mosaic.drag==1);
    key(window,SDLK_LSHIFT,KMOD_SHIFT);assert(mosaic.drag==1); /* Modifier events don't end a drag. */
    motion(window,220,120);assert(mosaic_event_count(scene)==5);
    TsMosaicEvent *copies[2];int n=0;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(mosaic_chosen(&ui,i))copies[n++]=&scene->events[i];
    assert(n==2 && copies[0]!=a && copies[1]!=b);
    assert(copies[0]->source==source && copies[1]->source==source);
    assert(!copies[0]->looping && copies[1]->note_count==2 && copies[1]->notes[1]==67);
    assert(copies[1]->x-copies[0]->x==b->x-a->x && copies[1]->start-copies[0]->start==b->start-a->start);
    assert(a->x==20 && a->start==.5 && b->x==130 && b->start==1);
    assert(copies[0]->x>=obstacle->x+obstacle->width+TS_MOSAIC_GUTTER);
    motion(window,230,132);assert(mosaic_event_count(scene)==5);release(window,230,132);SDL_SetModState(KMOD_NONE);
    assert(scene->history_count==history+1);
    uint64_t copied_id=copies[0]->id;key(window,SDLK_z,KMOD_CTRL);assert(mosaic_event_count(scene)==3 && !ts_mosaic_find(scene,copied_id));
    key(window,SDLK_y,KMOD_CTRL);assert(mosaic_event_count(scene)==5 && ts_mosaic_find(scene,copied_id));
    key(window,SDLK_z,KMOD_CTRL);
    /* Alt still aligns an ordinary move without making a copy. */
    mosaic_select_only(&ui,a->id);SDL_SetModState(KMOD_ALT);click(window,180,96,1);motion(window,180,107);release(window,180,107);SDL_SetModState(KMOD_NONE);
    assert(a->start==1 && mosaic_event_count(scene)==3);key(window,SDLK_z,KMOD_CTRL);
    SDL_SetModState(KMOD_SHIFT);click(window,155,70,1);motion(window,385,180);release(window,385,180);
    click(window,180,96,1);motion(window,220,120);assert(mosaic_event_count(scene)==5);
    key(window,SDLK_ESCAPE,KMOD_SHIFT);SDL_SetModState(KMOD_NONE);assert(mosaic_event_count(scene)==3 && !mosaic.drag);
    /* No partial group, no movement and no history entry when only one slot remains. */
    while(mosaic_event_count(scene)<127)assert(ts_mosaic_add(scene,source,20+mosaic_event_count(scene),0));
    SDL_SetModState(KMOD_SHIFT);click(window,155,70,1);motion(window,385,180);release(window,385,180);
    history=scene->history_count;click(window,180,96,1);motion(window,220,120);release(window,220,120);SDL_SetModState(KMOD_NONE);
    assert(mosaic_event_count(scene)==127 && a->x==20 && a->start==.5 && scene->history_count==history);
    assert(strstr(ui.status,"WHOLE SELECTION"));
    key(window,SDLK_BACKQUOTE,KMOD_SHIFT);assert(!ui.mosaic_open);
    key(window,SDLK_BACKQUOTE,KMOD_SHIFT);assert(ui.mosaic_open);
    ui.config_open=1;key(window,SDLK_BACKQUOTE,KMOD_SHIFT);assert(ui.mosaic_open);ui.config_open=0;
    ui.mosaic=audio.mosaic=saved;mosaic_poll(0,&ui,&instrument,&mosaic);ts_mosaic_free(scene);
}
static void test_mosaic_external_banks(SDL_Window *window)
{
    TsInstrument *bank=malloc(sizeof(*bank));TsUiState *view=malloc(sizeof(*view));AudioState *a=calloc(1,sizeof(*a));
    FmBankHistory *history=malloc(sizeof(*history));assert(bank && view && a && history);
    ts_instrument_init(bank);ts_ui_init(view);fm_bank_history_init(history);
    ts_note_bank_init(&a->notes);ts_performance_init(&a->performance);ts_performance_init(&a->tile_launchers);
    ts_sister_runtime_init(&a->sister);ts_capture_init(&a->capture);a->output_rate=48000;a->bank_slot=-1;
    char error[160];TsSamplePages pages;assert(ts_sample_pages_init(&pages,error,sizeof(error)));
    MosaicController browser={.pages=&pages};PortalController p={.mosaic=&browser};TransformController t;transform_controller_init(&t);
    view->mosaic=a->mosaic=ts_mosaic_create();view->mosaic_open=1;view->mosaic_scale=24;view->mosaic_hscale=1;assert(view->mosaic);
    assert(ts_instrument_generate(bank,TS_GENERATOR_METALLIC,777,error,sizeof(error)));
    uint64_t welcome=ts_sample_hash(&bank->current);
    mosaic_poll(0,view,bank,&browser);assert(view->mosaic_source_pages==1 && view->mosaic_source_page==0);
    /* Reproduce the actual FM workspace's Create Bank -> New Sample Page path. */
    TsFmSeedSequence seeds;ts_fm_seed_sequence_init(&seeds,0x42414e4b);
    make_fm_bank_workspace(0,a,view,bank,&pages,history,&seeds,1);
    assert(ts_sample_pages_count(&pages)==2 && ts_sample_pages_active(&pages)==1 && ts_instrument_bank_count(bank)==16);
    uint64_t fm=ts_sample_hash(&bank->current);
    mosaic_poll(0,view,bank,&browser);
    assert(view->mosaic_source_pages==2 && view->mosaic_source_page==1 && view->mosaic_source_offset==0);
    for(int i=0;i<16;++i)assert(view->mosaic_sources[i].sample && view->mosaic_sources[i].bank_page==1);
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=SDL_GetWindowID(window);e.button.button=SDL_BUTTON_LEFT;e.button.x=20;e.button.y=359;
#define PAGE_EVENT() mosaic_event(&e,window,0,a,view,bank,&browser,&p,&sister,&t,48000)
    assert(PAGE_EVENT());assert(view->mosaic_source_page==0 && ts_sample_pages_active(&pages)==1 && ts_sample_hash(&bank->current)==fm);
    assert(view->mosaic_sources[0].hash==welcome && view->mosaic_sources[1].sample==NULL);
    const TsInstrument *first=ts_sample_pages_page(&pages,bank,0);assert(first);
    TsInstrument *mutable_first=ts_sample_pages_page_mut(&pages,bank,0);
    mutable_first->has_loop=1;mutable_first->loop_first=100;mutable_first->loop_last=1000;mutable_first->loop_mode=TS_LOOP_REVERSE;
    mutable_first->tuning.root_note=65;
    e.button.y=85;assert(PAGE_EVENT() && browser.drag==3);
    e.type=SDL_MOUSEBUTTONUP;e.button.x=170;e.button.y=80;assert(PAGE_EVENT());
    TsMosaicEvent *placed=ts_mosaic_find(view->mosaic,view->mosaic_selected);assert(placed && placed->source->hash==welcome);
    assert(placed->first==100 && placed->last==1000 && placed->mode==TS_LOOP_REVERSE && placed->tuning.root_note==65);
    e.type=SDL_MOUSEBUTTONDOWN;e.button.x=97;e.button.y=359;assert(PAGE_EVENT());
    assert(view->mosaic_source_page==1 && view->mosaic_sources[0].hash==fm);
    assert(PAGE_EVENT());assert(view->mosaic_source_page==2 && view->mosaic_source_offset==16 && view->mosaic_sources[16].event==placed->id);
    /* Bank growth while viewing events keeps the event browser's identity. */
    size_t new_page;assert(ts_sample_pages_append_and_switch(&pages,bank,&new_page,error,sizeof(error)));
    assert(ts_sample_pages_switch(&pages,bank,1,error,sizeof(error)));mosaic_poll(0,view,bank,&browser);
    assert(view->mosaic_bank_count==3 && view->mosaic_source_page==3 && view->mosaic_source_offset==16);
    /* All pages survive normal project persistence, including the original. */
    TsInstrument *record=malloc(sizeof(*record));assert(record);ts_instrument_init(record);
    pages.mosaic=view->mosaic;
    const char *project="test-mosaic-pages/test-mosaic-pages.tsr";
    assert(ts_sample_pages_save_project(&pages,bank,record,NULL,project,error,sizeof(error)));
    assert(ts_sample_pages_load_project(&pages,bank,record,project,error,sizeof(error)));
    mosaic_poll(0,view,bank,&browser);assert(view->mosaic_source_page==1 && view->mosaic_sources[0].hash==fm);
    ts_instrument_free(record);free(record);
    assert(ts_sample_pages_count(&pages)==3);
    assert(ts_sample_hash(&ts_sample_pages_page(&pages,bank,0)->current)==welcome);
    assert(ts_sample_hash(&ts_sample_pages_page(&pages,bank,1)->current)==fm);
    mosaic_controller_free(&browser);ts_mosaic_free(view->mosaic);fm_bank_history_free(history);free(history);
    ts_sample_pages_free(&pages);ts_instrument_free(bank);free(bank);
    ts_performance_free(&a->performance);ts_performance_free(&a->tile_launchers);ts_sister_runtime_free(&a->sister);ts_capture_free(&a->capture);free(a);free(view);
#undef PAGE_EVENT
}

static void test_mosaic_palette_controls(void)
{
    TsPalette original=ui.palette;int opened=ui.mosaic_open;
    ui.mosaic_open=0;ts_ui_begin_palette_edit(&ui);
    for(int i=0;i<TS_PALETTE_MOSAIC_COLOR_COUNT;++i) {
        int x=i?TS_PALETTE_MOSAIC_TILE_X+(i-1)*TS_PALETTE_MOSAIC_TILE_STEP:TS_PALETTE_MOSAIC_HIGHLIGHT_X;
        ui.palette_entry=ts_ui_palette_entry_from_point(x+2,TS_PALETTE_MOSAIC_Y+2);
        assert(ui.palette_entry==TS_PALETTE_MOSAIC_HIGHLIGHT+i);
        ui.palette_channel=0;palette_adjust(&ui,17);
        assert(ts_palette_sample_tapehead_from(&ui.palette,&ui.palette_suggestions,(TsPaletteColor)ui.palette_entry,i));
        assert(ui.palette.colors[ui.palette_entry]==ui.palette_suggestions.colors[ts_palette_tapehead_swatch_color(i)]);
    }
    ts_ui_finish_palette_edit(&ui,1);assert(!memcmp(&ui.palette,&original,sizeof(original)));
    ts_ui_begin_palette_edit(&ui);ui.palette_entry=TS_PALETTE_MOSAIC_HIGHLIGHT;
    static TsFramebuffer fb;ts_audio_ui_render(&fb,&ui,&instrument);
    const char *shot=getenv("TS_MOSAIC_PALETTE_SCREENSHOT");if(shot)assert(ts_ui_write_ppm(&fb,shot));
    ts_ui_finish_palette_edit(&ui,0);ui.config_open=0;ui.mosaic_open=opened;
}
