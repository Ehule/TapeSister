static void test_mosaic_live_controls(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*scene=ts_mosaic_create();assert(scene);ui.mosaic=audio.mosaic=scene;
    char error[160];TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *a=ts_mosaic_add(scene,source,0,20);a->duration=8;
    TsMosaicEvent *b=ts_mosaic_add(scene,source,0,1000);b->duration=8;b->solo=1;
    ui.mosaic_open=1;ui.mosaic_scale=24;ui.mosaic_hscale=1;ui.mosaic_scroll=ui.mosaic_xscroll=0;
    mosaic_poll(0,&ui,&instrument,&mosaic);mosaic_select_only(&ui,a->id);
    click(window,480,389,1);assert(!b->solo);key(window,SDLK_z,KMOD_CTRL);assert(b->solo);
    click(window,480,389,1);assert(!b->solo);
    scene->playing=1;for(int i=0;i<1000;++i)ts_mosaic_read(scene,48000);
    uint64_t revision=a->revision,attack=scene->voices[0].attack;
    int history=scene->history_count;
    click(window,35,390,1);motion(window,59,390);release(window,59,390);
    assert(a->gain>.9f && a->gain<1.1f && b->gain==1 && scene->history_count==history+1);
    click(window,215,390,1);release(window,215,390);assert(a->pan==1 && b->pan==0);
    click(window,275,390,1);release(window,275,390);assert(a->fade_in>1.9 && a->fade_in<2.1);
    click(window,383,390,1);release(window,383,390);assert(a->fade_out>1.9 && a->fade_out<2.1);
    ts_mosaic_read(scene,48000);assert(a->revision==revision && scene->voices[0].attack==attack+1);
    click(window,627,60,1);release(window,627,60);assert(scene->speed==2);
    for(int i=0;i<1000;++i)ts_mosaic_read(scene,48000);assert(scene->speed_current==2);
    history=scene->history_count;click(window,500,60,1);motion(window,550,60);key(window,SDLK_ESCAPE,KMOD_NONE);
    assert(scene->speed==2 && scene->playing && scene->history_count==history+1);
    click(window,563,60,2);assert(scene->speed==1);
    SDL_Event reset={0};reset.type=SDL_MOUSEBUTTONDOWN;reset.button.windowID=SDL_GetWindowID(window);reset.button.button=SDL_BUTTON_RIGHT;reset.button.x=130;reset.button.y=390;
    dispatch(&reset,window);assert(a->pan==0);
    /* A group receives one undoable gesture, while audio document identity stays
       stable for a CDP render that might still be working on either member. */
    ui.mosaic_selection[1]=b->id;click(window,383,390,1);motion(window,395,390);release(window,395,390);
    assert(a->fade_out==b->fade_out && a->revision==revision);
    key(window,SDLK_z,KMOD_CTRL);assert(b->fade_out==0);
    ui.mosaic=audio.mosaic=saved;mosaic_poll(0,&ui,&instrument,&mosaic);ts_mosaic_free(scene);
}

static void test_mosaic_variation_banks(SDL_Window *window)
{
    TsInstrument *bank=malloc(sizeof(*bank)),*record=malloc(sizeof(*record));TsUiState *view=malloc(sizeof(*view));
    AudioState *a=calloc(1,sizeof(*a));MosaicController *c=calloc(1,sizeof(*c));assert(bank && record && view && a && c);
    ts_instrument_init(bank);ts_instrument_init(record);ts_ui_init(view);
    ts_note_bank_init(&a->notes);ts_performance_init(&a->performance);ts_performance_init(&a->tile_launchers);
    ts_sister_runtime_init(&a->sister);ts_capture_init(&a->capture);a->output_rate=48000;a->bank_slot=-1;
    char error[160];TsSamplePages pages;assert(ts_sample_pages_init(&pages,error,sizeof(error)));c->pages=&pages;
    view->mosaic=a->mosaic=pages.mosaic=ts_mosaic_create();view->mosaic_open=1;view->mosaic_scale=24;view->mosaic_hscale=1;
    TsSample sample={0};sample.sample_rate=48000;sample.channels=1;sample.frames=4800;sample.data=calloc(sample.frames,sizeof(float));assert(sample.data);
    for(size_t i=0;i<sample.frames;++i)sample.data[i]=.4f*sinf((float)i*.03f);
    strcpy(sample.name,"ORIGINAL");assert(ts_instrument_import_sample(bank,&sample,0,0,0,TS_LOOP_FORWARD,error,sizeof(error)));ts_sample_free(&sample);
    /* Fill every bank slot so accepting the variation has to create an ordinary
       Sample page while its original bank is parked behind the event editor. */
    for(int i=1;i<16;++i)assert(ts_instrument_copy_selected(bank,i,error,sizeof(error)));
    uint64_t original=ts_sample_hash(&bank->current);int selected=bank->selected_slot;
    TsMosaicSource *source=ts_mosaic_source(view->mosaic,&bank->current,error,sizeof(error));assert(source);
    TsMosaicEvent *owner=ts_mosaic_add(view->mosaic,source,0,0);owner->duration=8;owner->tuning.root_note=65;owner->crossfade=32;
    for(int i=1;i<8;++i){TsMosaicEvent *obstacle=ts_mosaic_copy(view->mosaic,owner->id,0,100*i);assert(obstacle);}
    assert(mosaic_enter(0,a,view,bank,c,owner->id));
    assert(ts_instrument_apply_smear(bank,.8f,error,sizeof(error)));
    uint64_t changed=ts_sample_hash(&bank->current);assert(changed!=original);
    assert(!mosaic_leave(0,a,view,bank,c) && view->mosaic_edit_choice);
    assert(mosaic_resolve_edit(0,a,view,bank,c,1));
    TsMosaicEvent *variation=ts_mosaic_find(view->mosaic,view->mosaic_selected);assert(variation && variation!=owner);
    assert(variation->source->hash==changed && owner->source->hash==original);
    assert(variation->x>=view->mosaic_xscroll && variation->x+variation->width<=view->mosaic_xscroll+480);
    assert(view->mosaic_source_page==1 && view->sample_page_count==2 && view->mosaic_source_selected>=0);
    assert(view->mosaic_sources[view->mosaic_source_selected].hash==changed);
    assert(ts_sample_pages_active(&pages)==0 && bank->selected_slot==selected && ts_sample_hash(&bank->current)==original);
    const TsInstrument *other=ts_sample_pages_page(&pages,bank,1);int slot=view->mosaic_source_selected;
    assert(other && ts_sample_hash(&other->bank[slot].sample)==changed);
    assert(slot==0 && ts_sample_hash(&other->current)==changed);
    assert(other->tuning.root_note==65 && other->bank[slot].tuning.root_note==65);
    /* UPDATE remains event-only, leaving the reusable NEW TILE bank snapshot
       intact. Save/reopen retains the bank snapshot and updated event audio. */
    assert(mosaic_enter(0,a,view,bank,c,variation->id));assert(ts_instrument_apply_smear(bank,.6f,error,sizeof(error)));
    uint64_t updated=ts_sample_hash(&bank->current);assert(updated!=changed);
    assert(!mosaic_leave(0,a,view,bank,c));assert(mosaic_resolve_edit(0,a,view,bank,c,2));
    assert(variation->source->hash==updated && ts_sample_hash(&other->bank[slot].sample)==changed);
    /* Empty slot 0 in an existing bank is not a freshly allocated bank. A new
       snapshot there must not replace the other fifteen sounds or its editor. */
    assert(ts_instrument_bank_clear(bank,0,error,sizeof(error)));
    assert(bank->selected_slot==selected);
    assert(mosaic_enter(0,a,view,bank,c,owner->id));assert(ts_instrument_apply_smear(bank,.7f,error,sizeof(error)));
    uint64_t zero_slot=ts_sample_hash(&bank->current);
    assert(!mosaic_leave(0,a,view,bank,c));assert(mosaic_resolve_edit(0,a,view,bank,c,1));
    assert(view->mosaic_source_page==0 && view->mosaic_source_selected==0);
    assert(bank->selected_slot==selected && ts_sample_hash(&bank->current)==original);
    assert(ts_sample_hash(&bank->bank[0].sample)==zero_slot);
    for(int i=1;i<16;++i)assert(ts_sample_hash(&bank->bank[i].sample)==original);
    assert(ts_sample_pages_save_project(&pages,bank,record,NULL,"test-mosaic-variations/test-mosaic-variations.tsr",error,sizeof(error)));
    assert(ts_sample_pages_load_project(&pages,bank,record,"test-mosaic-variations/test-mosaic-variations.tsr",error,sizeof(error)));
    assert(ts_sample_hash(&ts_sample_pages_page(&pages,bank,1)->bank[slot].sample)==changed);
    assert(ts_sample_hash(&bank->current)==original);
    mosaic_controller_free(c);free(c);ts_mosaic_free(view->mosaic);ts_sample_pages_free(&pages);
    ts_instrument_free(bank);ts_instrument_free(record);free(bank);free(record);free(view);
    ts_performance_free(&a->performance);ts_performance_free(&a->tile_launchers);ts_sister_runtime_free(&a->sister);ts_capture_free(&a->capture);free(a);
    (void)window;
}
