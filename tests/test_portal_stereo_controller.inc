static void test_stereo_workflow(SDL_Window *window,SDL_AudioDeviceID device)
{
    (void)window;
    static TsInstrument instrument;static TsUiState ui;static AudioState audio;
    PortalController c;char error[160];TsSample stereo;
    ts_instrument_init(&instrument);ts_ui_init(&ui);ts_sister_runtime_init(&audio.sister);
    ts_note_bank_init(&audio.notes);ts_performance_init(&audio.performance);ts_performance_init(&audio.tile_launchers);
    ts_capture_init(&audio.capture);audio.bank_slot=-1;
    ts_sample_init(&stereo);stereo.frames=44100;stereo.channels=2;stereo.sample_rate=44100;
    stereo.data=malloc(stereo.frames*2*sizeof(float));assert(stereo.data);
    for(size_t i=0;i<stereo.frames;++i){stereo.data[2*i]=.45f*sin(6.283185307179586*440*i/44100);stereo.data[2*i+1]=.25f*sin(6.283185307179586*713*i/44100);}
    snprintf(stereo.name,sizeof(stereo.name),"STEREO 440 / 713");
    assert(ts_instrument_import_sample(&instrument,&stereo,1,0,stereo.frames,TS_LOOP_FORWARD,error,sizeof(error)));
    uint64_t original=ts_sample_hash(&instrument.current);
    portal_init(&c,&ui.portal);ui.portal.open=1;TsPortalUi *p=&ui.portal;
    snprintf(ui.config.cdp_bin_path,sizeof(ui.config.cdp_bin_path),"%s",getenv("TS_TEST_CDP_BIN"));
    assert(portal_source(device,&audio,&ui,&instrument,&c));assert(p->source->channels==2 && ts_sample_hash(p->source)==original);
    ts_portal_recipe_default(&p->recipe,ts_portal_process_find("modify.speed.1"));p->recipe.values[0]=.5;
    p->process_selection=1;p->waves[0].has_selection=1;p->waves[0].selection_first=8000;p->waves[0].selection_last=16000;
    portal_preview(device,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);
    assert(p->valid && p->result->channels==2 && p->result->frames>stereo.frames);
    assert(!memcmp(p->result->data,stereo.data,8000*2*sizeof(float)));
    size_t tail=p->rendered_region.result_last;
    assert(!memcmp(p->result->data+tail*2,stereo.data+16000*2,(stereo.frames-16000)*2*sizeof(float)));
    p->listen_result=1;p->loop=1;ts_ui_keyboard_set_octave(&ui,4);
    portal_note_on(device,&audio,&ui,0,44100);portal_note_on(device,&audio,&ui,4,44100);
    assert(ts_note_bank_count(&audio.notes)==2);
    for(int i=0;i<TS_NOTE_BANK_VOICE_CAPACITY;++i)if(audio.notes.voices[i].active)assert(audio.notes.voices[i].sample->channels==2);
    selection_screenshot("TS_TEST_PORTAL_STEREO_SHOT",&ui,&instrument);
    assert(portal_apply(device,&audio,&ui,&instrument,&c,0));assert(p->source->channels==2 && instrument.current.channels==2);
    assert(ts_note_bank_count(&audio.notes)==0);
    assert(ts_instrument_undo(&instrument,error,sizeof(error)) && ts_sample_hash(&instrument.current)==original);
    assert(portal_source(device,&audio,&ui,&instrument,&c));
    ts_portal_recipe_default(&p->recipe,ts_portal_process_find("modify.loudness.1"));p->recipe.values[0]=.5;
    portal_preview(device,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);assert(p->valid);
    assert(portal_apply(device,&audio,&ui,&instrument,&c,1));
    assert(instrument.current.channels==2 && ts_sample_hash(p->source)==original);
    /* Every enabled stage must support stereo, before any work is started. */
    assert(ts_portal_instrument_recipe(0,&p->chain));p->chain_active=1;p->chain_stage=0;
    ts_portal_step_get(&p->chain.stages[0],&p->recipe);
    portal_preview(device,&audio,&ui,&c);assert(!c.worker && strstr(p->message,"MONO-ONLY"));
    assert(ts_portal_instrument_recipe(2,&p->chain));ts_portal_step_get(&p->chain.stages[0],&p->recipe);
    portal_preview(device,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);assert(p->valid && p->chain_cached==7 && p->result->channels==2);
    for(unsigned n=0;n<3;++n)assert(c.stages[n].output.channels==2);
    portal_close(device,&audio,&ui,&c);portal_free(&c);ts_sample_free(&stereo);
    ts_performance_free(&audio.performance);ts_performance_free(&audio.tile_launchers);ts_sister_runtime_free(&audio.sister);ts_capture_free(&audio.capture);ts_instrument_free(&instrument);
    puts("Stereo Portal controller: selection splice, notes, Apply/Undo, New Tile and chain caches passed");
}
