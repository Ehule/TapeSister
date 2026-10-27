static void test_mosaic_marker_midi(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*scene=ts_mosaic_create();assert(scene);
    TsMidiMap saved_map=ui.config.midi_map;ts_midi_map_init(&ui.config.midi_map);
    char saved_path[sizeof(runtime_config_path)];memcpy(saved_path,runtime_config_path,sizeof(saved_path));
    snprintf(runtime_config_path,sizeof(runtime_config_path),"marker-midi-test.ini");
    ui.mosaic=audio.mosaic=scene;ui.mosaic_open=1;ui.mosaic_follow=0;
    ui.mosaic_scroll=ui.mosaic_xscroll=0;ui.mosaic_scale=24;ui.mosaic_hscale=1;
    mosaic_poll(0,&ui,&instrument,&mosaic);
    char error[160],target[TS_MIDI_TARGET_ID_MAX];
    TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *card=ts_mosaic_add(scene,source,0,20);assert(card);card->duration=12;
    TsMidiEvent event;
    midi_learn_set_active(&ui,&sister,1);
    for(int i=0;i<16;++i) {
        TsMosaicMarker cue={.stored=1,.time=i*.5};assert(ts_mosaic_set_marker(scene,i,&cue));
        assert(ts_ui_midi_target_from_point(&ui,122+i*32,366,target,sizeof(target)));
        assert(ts_midi_mosaic_marker_slot(target)==i);
        midi_learn_click(&ui,&sister,target);
        assert(ts_midi_decode_short_message(0x99,36+i,100,&event));
        assert(handle_midi_mapping(0,&audio,&ui,&instrument,&sister,NULL,NULL,&event,48000,2));
        assert(!ui.midi_learn_pending[0] && scene->time==0); /* Learning never launches. */
    }
    assert(ts_ui_midi_target_from_point(&ui,80,366,target,sizeof(target)) && !strcmp(target,"mosaic.marker_loop"));
    midi_learn_click(&ui,&sister,target);
    assert(ts_midi_decode_short_message(0xb9,80,127,&event));
    assert(handle_midi_mapping(0,&audio,&ui,&instrument,&sister,NULL,NULL,&event,48000,2));
    assert(!scene->marker_loop);
    /* Modal controls and source cards never accidentally learn bank-tile targets. */
    ui.mosaic_views_open=1;assert(!ts_ui_midi_target_from_point(&ui,122,366,target,sizeof(target)));ui.mosaic_views_open=0;
    assert(!ts_ui_midi_target_from_point(&ui,20,340,target,sizeof(target)));
    assert(!ts_ui_midi_target_from_point(&ui,149,366,target,sizeof(target))); /* Button gap. */
    ui.mosaic_time=scene->time;ui.mosaic_playing=scene->playing;
    marker_snapshot(&instrument,1280,800,getenv("TS_MOSAIC_MARKER_MIDI_SCREENSHOT"));
    midi_learn_set_active(&ui,&sister,0);
    TsConfig *reopened=malloc(sizeof(*reopened));assert(reopened);ts_config_init(reopened);
    assert(ts_config_load(reopened,runtime_config_path,error,sizeof(error)) && reopened->midi_map.count==17);
    ui.config.midi_map=reopened->midi_map;free(reopened);
    for(int i=0;i<16;++i) {
        scene->playing=i&1;
        assert(ts_midi_decode_short_message(0x99,36+i,100,&event));
        assert(handle_midi_mapping(0,&audio,&ui,&instrument,&sister,NULL,NULL,&event,48000,2));
        assert(scene->time==i*.5 && scene->playing==(i&1) && ui.mosaic_marker_selected==i+1);
        scene->time+=.1;
        assert(ts_midi_decode_short_message(0x89,36+i,64,&event));
        assert(handle_midi_mapping(0,&audio,&ui,&instrument,&sister,NULL,NULL,&event,48000,2));
        assert(scene->time==i*.5+.1 && scene->playing==(i&1));
        assert(ts_midi_decode_short_message(0x99,36+i,0,&event));
        assert(handle_midi_mapping(0,&audio,&ui,&instrument,&sister,NULL,NULL,&event,48000,2));
        assert(scene->time==i*.5+.1);
    }
    /* CC toggle and note triggers also work away from the Mosaic workspace. */
    ui.mosaic_open=0;scene->time=2.75;scene->playing=1;
    assert(ts_midi_decode_short_message(0xb9,80,127,&event));
    assert(handle_midi_mapping(0,&audio,&ui,&instrument,&sister,NULL,NULL,&event,48000,2));
    assert(scene->marker_loop && scene->time==2.75 && scene->marker_active==6 && ui.mosaic_marker_selected==6);
    assert(ts_midi_decode_short_message(0xb9,80,0,&event));
    assert(handle_midi_mapping(0,&audio,&ui,&instrument,&sister,NULL,NULL,&event,48000,2));
    assert(scene->marker_loop); /* Release never toggles back. */
    assert(ts_midi_decode_short_message(0x99,38,127,&event));
    assert(handle_midi_mapping(0,&audio,&ui,&instrument,&sister,NULL,NULL,&event,48000,2));
    assert(scene->time==1 && scene->marker_active==3);
    for(int i=0;i<12000;++i)ts_mosaic_read(scene,48000);
    double midway=scene->time;assert(midway>1.24 && midway<1.26);
    assert(ts_midi_decode_short_message(0xb9,80,127,&event));
    assert(handle_midi_mapping(0,&audio,&ui,&instrument,&sister,NULL,NULL,&event,48000,2));
    assert(!scene->marker_loop && scene->time==midway && scene->playing);
    for(int i=0;i<24000;++i)ts_mosaic_read(scene,48000);
    assert(scene->time>1.74 && scene->time<1.76);
    ui.mosaic_open=1;click(window,80,366,1);assert(scene->marker_loop);
    midway=scene->time;key(window,SDLK_l,KMOD_NONE);assert(!scene->marker_loop && !scene->repeat && scene->time==midway);
    click(window,80,366,1);click(window,280,45,1);assert(!scene->marker_loop && scene->time==midway);
    click(window,80,366,1);click(window,80,366,1);assert(!scene->marker_loop && scene->time==midway);
    /* Empty/stale mapped slots are consumed, without seeking or sounding tiles. */
    assert(ts_mosaic_set_marker(scene,15,&(TsMosaicMarker){0}));
    int notes=ts_note_bank_count(&audio.notes),tiles=ts_performance_count(&audio.tile_launchers);
    assert(ts_midi_decode_short_message(0x99,51,127,&event));
    assert(handle_midi_mapping(0,&audio,&ui,&instrument,&sister,NULL,NULL,&event,48000,2));
    assert(scene->time==midway && ts_note_bank_count(&audio.notes)==notes && ts_performance_count(&audio.tile_launchers)==tiles);
    assert(ts_midi_decode_short_message(0x98,36,127,&event));
    assert(!handle_midi_mapping(0,&audio,&ui,&instrument,&sister,NULL,NULL,&event,48000,2));
    assert(!remove(runtime_config_path));memcpy(runtime_config_path,saved_path,sizeof(saved_path));
    ui.config.midi_map=saved_map;ui.mosaic=audio.mosaic=saved;ts_mosaic_free(scene);
    mosaic_poll(0,&ui,&instrument,&mosaic);
}
