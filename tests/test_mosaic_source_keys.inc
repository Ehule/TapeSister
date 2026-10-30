static void source_keys_click(SDL_Window *window,SDL_AudioDeviceID device,AudioState *a,ExternalInputState *s,
    TsUiState *u,TsInstrument *bank,MosaicController *c,int slot,SDL_Keymod mod)
{
    int x=20+(slot%2)*49,y=84+(slot/2)*34;
    SDL_SetModState(mod);live_performance_click(window,device,a,s,u,bank,c,x,y);SDL_SetModState(KMOD_NONE);
}
static void test_mosaic_source_keys(SDL_Window *window,SDL_AudioDeviceID device)
{
    SDL_SetWindowSize(window,640,400);
    AudioState *a=routing_audio(0);TsUiState *u=mosaic_record_ui_fixture(a);
    ExternalInputState *s=mosaic_record_input_fixture(a);MosaicController *c=calloc(1,sizeof(*c));
    TsInstrument *bank=malloc(sizeof(*bank));assert(c && bank);ts_instrument_init(bank);
    char error[160];assert(ts_instrument_clone(bank,&instrument,error,sizeof(error)));
    TsSamplePages pages;assert(ts_sample_pages_init(&pages,error,sizeof(error)));
    pages.mosaic=a->mosaic;c->pages=&pages;
    assert(ts_sample_pages_append(&pages,error,sizeof(error)));
    TsInstrument *second=ts_sample_pages_page_mut(&pages,bank,1);assert(second);
    const int slots[]={0,5,8,13};
    TsSample sample={.frames=4800,.sample_rate=48000,.channels=2};sample.data=malloc(9600*sizeof(float));assert(sample.data);
    for(int n=0;n<4;++n) {
        assert(ts_instrument_select_bank(second,slots[n],error,sizeof(error)));
        for(int i=0;i<4800;++i){sample.data[i*2]=.04f*(n+1);sample.data[i*2+1]=.02f*(n+1);}
        snprintf(sample.name,sizeof(sample.name),"SOURCE %d",17+slots[n]);
        assert(ts_instrument_import_sample(second,&sample,1,0,4800,TS_LOOP_FORWARD,error,sizeof(error)));
    }
    ts_sample_free(&sample);
    mosaic_poll(device,u,bank,c);u->mosaic_source_page=1;mosaic_poll(device,u,bank,c);
    assert(!u->mosaic_live_open && !u->mosaic_source_keys && u->mosaic_bank_count==2);
    for(int n=0;n<4;++n)source_keys_click(window,device,a,s,u,bank,c,slots[n],n?KMOD_SHIFT:KMOD_NONE);
    assert(c->picked_count==4 && !a->mosaic_source_count);
    live_performance_click(window,device,a,s,u,bank,c,TS_MOSAIC_SOURCE_KEYS_X+5,TS_MOSAIC_SOURCE_KEYS_Y+5);
    assert(u->mosaic_source_keys && a->mosaic_source_count==4 && !u->mosaic_live_open);
    for(int n=0;n<4;++n)assert(a->mosaic_source_keys[n].page==1 && a->mosaic_source_keys[n].slot==slots[n]);
    live_performance_key(window,device,a,s,u,bank,c,SDLK_z,1,KMOD_NONE);
    assert(ts_performance_count(&a->mosaic_source_performance)==4 && !ts_note_bank_count(&a->notes));
    float output[512];for(int n=0;n<16;++n)audio_callback(a,(Uint8 *)output,sizeof(output));
    assert(fabsf(a->mosaic_source_dry.l-.2f)<1e-5f && fabsf(a->mosaic_source_dry.r-.1f)<1e-5f);
    /* Browsing away and back does not change the chosen instrument. Another
       page's source can be added while the old four-layer key stays held. */
    live_performance_click(window,device,a,s,u,bank,c,20,TS_MOSAIC_SOURCE_PAGE_Y+5);
    assert(u->mosaic_source_page==0 && a->mosaic_source_count==4);
    source_keys_click(window,device,a,s,u,bank,c,bank->selected_slot,KMOD_SHIFT);
    assert(a->mosaic_source_count==5 && ts_performance_count(&a->mosaic_source_performance)==4);
    live_performance_key(window,device,a,s,u,bank,c,SDLK_x,1,KMOD_NONE);
    assert(ts_performance_count(&a->mosaic_source_performance)==9);
    live_performance_key(window,device,a,s,u,bank,c,SDLK_z,0,KMOD_NONE);
    assert(ts_performance_count(&a->mosaic_source_performance)==5);
    live_performance_key(window,device,a,s,u,bank,c,SDLK_x,0,KMOD_NONE);
    assert(!ts_performance_count(&a->mosaic_source_performance));
    source_keys_click(window,device,a,s,u,bank,c,bank->selected_slot,KMOD_CTRL);
    assert(a->mosaic_source_count==4);
    live_performance_click(window,device,a,s,u,bank,c,96,TS_MOSAIC_SOURCE_PAGE_Y+5);assert(u->mosaic_source_page==1);
    /* Real MIDI dispatch selects this source group too; channels release
       independently, rather than falling through to the Canvas tile. */
    TsMidiEvent midi;assert(ts_midi_decode_short_message(0x92,60,127,&midi));
    handle_midi_event(device,a,u,bank,NULL,&midi,48000,0);
    assert(ts_performance_count(&a->mosaic_source_performance)==4 && !ts_note_bank_count(&a->notes));
    assert(ts_midi_decode_short_message(0x93,60,127,&midi));handle_midi_event(device,a,u,bank,NULL,&midi,48000,0);
    assert(ts_performance_count(&a->mosaic_source_performance)==8);
    assert(ts_midi_decode_short_message(0x82,60,0,&midi));handle_midi_event(device,a,u,bank,NULL,&midi,48000,0);
    assert(ts_performance_count(&a->mosaic_source_performance)==4);
    midi=(TsMidiEvent){.action=TS_MIDI_ACTION_PANIC,.channel=3};handle_midi_event(device,a,u,bank,NULL,&midi,48000,0);
    assert(!ts_performance_count(&a->mosaic_source_performance));
    /* Top REC SRC captures only the played layers. Backing remains running,
       and no newly recorded card is available as its own input during capture. */
    SDL_Event rec;SDL_zero(rec);rec.type=SDL_MOUSEBUTTONDOWN;rec.button.windowID=SDL_GetWindowID(window);
    rec.button.button=SDL_BUTTON_RIGHT;rec.button.x=TS_MOSAIC_REC_TILE_X+5;rec.button.y=45;
    SDL_AudioDeviceID input=0;assert(mosaic_record_tile_event(&rec,window,device,&input,a,s,u,bank));
    assert(u->mosaic_record_source==TS_RECORD_SOURCE_SOURCES);
    u->config.mosaic_record_seconds=0;u->config.mosaic_silence_seconds=0;
    char folder[160];snprintf(folder,sizeof(folder),"source-keys-capture-%lu",(unsigned long)SDL_GetTicks());SDL_setenv("TAPESISTER_CAPTURES",folder,1);
    uint64_t next=a->mosaic->next_id;double before=a->mosaic->time;
    rec.button.button=SDL_BUTTON_LEFT;assert(mosaic_record_tile_event(&rec,window,device,&input,a,s,u,bank));
    assert(s->recorder.state==TS_EXTERNAL_CAPTURE_RECORDING && s->recorder.channels==2);
    live_performance_key(window,device,a,s,u,bank,c,SDLK_z,1,KMOD_NONE);
    for(int n=0;n<20;++n)audio_callback(a,(Uint8 *)output,sizeof(output));
    assert(s->recorder.recorded_frames==5120 && a->mosaic->next_id==next);
    assert(a->mosaic->playing && a->mosaic->time>before);
    assert(fabsf(s->recorder.buffer[5118*2]-.2f)<1e-5f && fabsf(s->recorder.buffer[5118*2+1]-.1f)<1e-5f);
    assert(mosaic_record_tile_event(&rec,window,device,&input,a,s,u,bank));
    assert(s->recorder.state==TS_EXTERNAL_CAPTURE_COMPLETED);
    uint64_t bank_hash=ts_sample_hash(&bank->current);int selected=bank->selected_slot;
    finalize_external_recording(device,&input,a,s,u,bank,&pages);
    const TsMosaicEvent *take=ts_mosaic_find(a->mosaic,next);assert(take && strstr(take->name,"SOURCES"));
    assert(take->source->sample.frames==5120 && take->source->sample.channels==2);
    assert(fabsf(take->source->sample.data[5118*2]-.2f)<1e-5f && a->mosaic->playing);
    assert(bank->selected_slot==selected && ts_sample_hash(&bank->current)==bank_hash);
    mosaic_poll(device,u,bank,c);
    const char *shot=getenv("TS_TEST_MOSAIC_SOURCE_KEYS");if(shot)live_controller_screenshot_size(u,shot,1280,800);
    if(shot) {
        /* Compare the real Canvas labels with the same source-browser page. */
        assert(ts_instrument_select_bank(second,0,error,sizeof(error)));
        int old_page=u->sample_page,old_keyboard=u->show_keyboard,old_recipes=u->show_recipes,old_ingredients=u->show_ingredients;
        u->sample_page=1;u->mosaic_open=0;u->show_keyboard=u->show_recipes=u->show_ingredients=0;u->overlay[0]=0;
        TsFramebuffer *fb=malloc(sizeof(*fb));assert(fb);ts_ui_render(fb,u,second);
        char path[1200];snprintf(path,sizeof(path),"%s-canvas.ppm",shot);
        FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n640 400\n255\n");
        for(int i=0;i<640*400;++i){unsigned char rgb[]={(unsigned char)(fb->pixels[i]>>16),(unsigned char)(fb->pixels[i]>>8),(unsigned char)fb->pixels[i]};assert(fwrite(rgb,1,3,f)==3);}
        assert(!fclose(f));free(fb);u->sample_page=old_page;u->mosaic_open=1;u->show_keyboard=old_keyboard;u->show_recipes=old_recipes;u->show_ingredients=old_ingredients;
    }
    /* Deselecting all four changes future notes; held voices still have their
       original snapshots until their physical key-up. No last item reappears. */
    for(int n=0;n<4;++n)source_keys_click(window,device,a,s,u,bank,c,slots[n],KMOD_CTRL);
    assert(a->mosaic_source_count==0 && !c->picked_count);
    assert(ts_performance_count(&a->mosaic_source_performance)==4);
    live_performance_key(window,device,a,s,u,bank,c,SDLK_z,0,KMOD_NONE);assert(!ts_performance_count(&a->mosaic_source_performance));
    live_performance_click(window,device,a,s,u,bank,c,TS_MOSAIC_SOURCE_KEYS_X+5,TS_MOSAIC_SOURCE_KEYS_Y+5);
    assert(!u->mosaic_source_keys);
    /* A stale source-set epoch never silently plays the underlying Canvas. */
    u->mosaic_source_keys=1;++a->mosaic->epoch;
    live_performance_key(window,device,a,s,u,bank,c,SDLK_x,1,KMOD_NONE);
    assert(!ts_performance_count(&a->mosaic_source_performance) && !ts_note_bank_count(&a->notes));
    live_performance_key(window,device,a,s,u,bank,c,SDLK_x,0,KMOD_NONE);
    mosaic_poll(device,u,bank,c);assert(!u->mosaic_source_keys);
    /* A fresh project starts with its explicit keyboard owner disabled. */
    TsUiState *fresh=malloc(sizeof(*fresh));assert(fresh);ts_ui_init(fresh);assert(!fresh->mosaic_source_keys);free(fresh);
    SDL_setenv("TAPESISTER_CAPTURES","",1);ts_external_recorder_free(&s->recorder);free(s);
    ts_instrument_free(bank);free(bank);ts_sample_pages_free(&pages);free(u);mosaic_controller_free(c);free(c);routing_free(a);
}
