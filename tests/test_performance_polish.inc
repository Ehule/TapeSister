static void test_performance_polish(void)
{
    reset_route(0);ui.keyboard_sequence_open=1;ui.keyboard_sequence_edit=0;
    ui.master_eq_open=0;
    snprintf(ui.status,sizeof(ui.status),"OPERATION STATUS");
    ts_ui_performance_hover(&ui,36,260,100,0);assert(!ui.performance_tooltip[0]);
    ts_ui_performance_hover(&ui,36,260,700,0);assert(strstr(ui.performance_tooltip,"START/STOP"));
    assert(!strcmp(ui.status,"OPERATION STATUS"));
    snprintf(ui.status,sizeof(ui.status),"FRESH RESULT");
    ts_ui_performance_hover(&ui,36,260,800,0);assert(!ui.performance_tooltip[0]);
    ts_ui_performance_hover(&ui,36,260,3200,0);assert(ui.performance_tooltip[0]);
    const char *help_shot=getenv("TS_TEST_ARP_HELP_PPM");
    if(help_shot){static TsFramebuffer fb;ts_ui_render(&fb,&ui,&instrument);assert(ts_ui_write_ppm(&fb,help_shot));}
    ui.master_eq_open=1;
    ts_ui_performance_hover(&ui,40,260,3300,0);
    ts_ui_performance_hover(&ui,40,260,3900,0);assert(strstr(ui.performance_tooltip,"CTRL-CLICK"));
    help_shot=getenv("TS_TEST_EQ_HELP_PPM");
    if(help_shot){static TsFramebuffer fb;ts_ui_render(&fb,&ui,&instrument);assert(ts_ui_write_ppm(&fb,help_shot));}
    ui.config_open=1;ts_ui_performance_hover(&ui,40,260,4500,0);assert(!ui.performance_tooltip[0]);
    ui.config_open=ui.master_eq_open=ui.keyboard_sequence_open=0;
    /* Filename bytes and their displayed glyphs both retain case. */
    assert(ts_browser_open(&ui.browser,TS_BROWSER_EXPORT_WAV,"DroneMix.wav"));
    static TsFramebuffer lower,upper;
    ts_ui_render(&lower,&ui,&instrument);
    ts_browser_set_filename(&ui.browser,"DRONEMIX.WAV");ts_ui_render(&upper,&ui,&instrument);
    int difference=0;
    for(int y=303;y<310;++y)for(int x=64;x<180;++x)
        difference|=lower.pixels[y*640+x]!=upper.pixels[y*640+x];
    assert(difference);
    ts_browser_set_filename(&ui.browser,"DroneMix.wav");
    assert(!strcmp(ui.browser.filename,"DroneMix.wav"));
    const char *shot=getenv("TS_TEST_FILENAME_CASE_PPM");
    if(shot){ts_ui_render(&lower,&ui,&instrument);assert(ts_ui_write_ppm(&lower,shot));}
    ts_browser_close(&ui.browser);

    MosaicController c={0};ui.mosaic=audio.mosaic=ts_mosaic_create();assert(ui.mosaic);
    char error[160];
    TsMosaicSource *source=ts_mosaic_source(ui.mosaic,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *same=ts_mosaic_add(ui.mosaic,source,0,0);assert(same);
    TsSample other={0};assert(ts_sample_clone(&other,&instrument.current,error,sizeof(error)));
    for(size_t i=0;i<other.frames*other.channels;++i)other.data[i]*=.3f;
    TsMosaicSource *different=ts_mosaic_source(ui.mosaic,&other,error,sizeof(error));assert(different);
    ts_sample_free(&other);
    TsMosaicEvent *second=ts_mosaic_add(ui.mosaic,different,1,110);assert(second);
    uint64_t original=ts_sample_hash(&instrument.current);
    for(int route=0;route<4;++route)for(int card=0;card<2;++card) {
        if(route<3)ts_sister_runtime_disable(&audio.sister);
        else assert(ts_sister_runtime_enable(&audio.sister,44100,2,2,1,error,sizeof(error)));
        reset_route(route);ui.mosaic_open=0;
        toggle_fm_hold(device,&audio,&ui,&instrument);click_note(0,0);click_note(4,1);
        assert(voices()==2 && power_audio_energy()>.01);
        ui.fm_open=0;ui.mosaic_open=1;
        TsMosaicEvent *owner=card?second:same;
        uint64_t owner_hash=owner->source->hash;
        int bank_count=ts_instrument_bank_count(&instrument),event_count=0;
        for(int i=0;i<TS_MOSAIC_EVENTS;++i)event_count+=ui.mosaic->events[i].id!=0;
        assert(mosaic_enter(device,&audio,&ui,&instrument,&c,owner->id));
        assert(voices()==2 && ui.keyboard_hold);
        if(route==0)assert(ts_sample_hash(audio.notes.voices[0].sample)==original);
        refresh_workbench_loop(device,&audio,&ui,&instrument);
        assert(voices()==2 && power_audio_energy()>.01);
        lock_edit(device,&audio);
        assert(ts_instrument_apply_smear(&instrument,.65f,error,sizeof(error)));
        unlock_edit(device,&audio,&ui,&instrument);
        assert(voices()==2 && power_audio_energy()>.01);
        assert(!mosaic_leave(device,&audio,&ui,&instrument,&c) && ui.mosaic_edit_choice);
        if(route==0 && card==0 && (shot=getenv("TS_TEST_MOSAIC_CANCEL_PPM"))) {
            ts_ui_render(&lower,&ui,&instrument);ts_ui_render_mosaic_choice(&lower,&ui);
            assert(ts_ui_write_ppm(&lower,shot));
        }
        c.resume_exit=1;
        assert(mosaic_resolve_edit(device,&audio,&ui,&instrument,&c,4));
        assert(!c.active && !c.resume_exit && ui.mosaic_open && !ui.mosaic_edit_choice);
        assert(owner->source->hash==owner_hash && ts_sample_hash(&instrument.current)==original);
        assert(ts_instrument_bank_count(&instrument)==bank_count);
        int after=0;for(int i=0;i<TS_MOSAIC_EVENTS;++i)after+=ui.mosaic->events[i].id!=0;
        assert(after==event_count && voices()==2 && power_audio_energy()>.01);
        stop_all_force(device,&audio,&ui);runtime_note_collect_snapshots(device,&audio);
        assert(!voices());
        for(int i=0;i<TS_NOTE_BANK_VOICE_CAPACITY;++i)assert(!audio.note_snapshots[i].data);
    }
    mosaic_controller_free(&c);ts_mosaic_free(ui.mosaic);ui.mosaic=audio.mosaic=NULL;
    ui.mosaic_open=0;ts_sister_runtime_disable(&audio.sister);reset_route(0);
    /* Success flash is tested without requiring external CDP executables. */
    PortalController portal;portal_init(&portal,&ui.portal);ui.portal.open=1;
    assert(portal_source(device,&audio,&ui,&instrument,&portal));
    assert(ts_sample_clone(&portal.history[0].output,&instrument.current,error,sizeof(error)));
    ui.portal.result=&portal.history[0].output;ui.portal.valid=1;
    int selected=instrument.selected_slot;
    assert(portal_apply(device,&audio,&ui,&instrument,&portal,1));
    assert(ui.portal.applied_flash==1);
    portal_hover_poll(&ui.portal,&portal,-1,-1,ui.portal.applied_flash_until-1,0);assert(ui.portal.applied_flash==1);
    portal_hover_poll(&ui.portal,&portal,-1,-1,ui.portal.applied_flash_until,0);assert(!ui.portal.applied_flash);
    portal_close(device,&audio,&ui,&portal);portal_free(&portal);
    assert(ts_instrument_select_bank(&instrument,selected,error,sizeof(error)));
}
