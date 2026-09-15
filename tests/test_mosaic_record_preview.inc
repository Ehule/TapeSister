static void test_mosaic_record_preview(SDL_AudioDeviceID device)
{
    AudioState *a=routing_audio(0);TsUiState *u=mosaic_record_ui_fixture(a);
    ExternalInputState *input=mosaic_record_input_fixture(a);SDL_AudioDeviceID input_device=0;
    /* A low recording rate keeps the complete long-take snapshot test small. */
    a->output_rate=1000;a->mosaic->playing=0;a->mosaic->time=1;
    u->config.record_max_seconds=30;u->mosaic_scale=24;u->mosaic_hscale=1;
    u->mosaic_xscroll=120;u->mosaic_scroll=0;
    uint64_t next=a->mosaic->next_id,revision=a->mosaic->revision;
    assert(arm_external_capture_to(device,&input_device,a,input,u,&instrument,1));
    assert(u->mosaic_record_preview.start==1 && u->mosaic_record_preview.x==120);
    assert(u->mosaic_record_preview.frames==0 && u->mosaic_record_preview.frames_per_peak==10);
    static TsFramebuffer idle,on,off;
    ts_ui_waveform_detail_begin(NULL);u->text_cursor_visible=1;ts_ui_render(&on,u,&instrument);
    u->mosaic_recording=0;ts_ui_render(&idle,u,&instrument);u->mosaic_recording=1;
    assert(on.pixels[100*640+155]!=idle.pixels[100*640+155]); /* Visible before any audio. */
    u->text_cursor_visible=0;ts_ui_render(&off,u,&instrument);
    assert(on.pixels[TS_MOSAIC_REC_TILE_Y*640+TS_MOSAIC_REC_TILE_X]!=
           off.pixels[TS_MOSAIC_REC_TILE_Y*640+TS_MOSAIC_REC_TILE_X]);
    /* The stop label is readable in both phases; only its background pulses. */
    for(int y=43;y<50;++y)for(int x=TS_MOSAIC_REC_TILE_X+5;x<TS_MOSAIC_REC_TILE_X+60;++x)
        if(on.pixels[y*640+x]==0xffffe5e1u)assert(off.pixels[y*640+x]==on.pixels[y*640+x]);
    for(int i=0;i<12000;++i) {
        float value=i<1000?.85f:i<2000?0:.25f;
        if(i>=4000)value=(.1f+.7f*fabsf(sinf((i-4000)*.002f)))*sinf(i*.067f);
        assert(!ts_external_recorder_write_frame(&input->recorder,(TsStereoFrame){value,-value}));
        if(i%500==499)sync_external_capture_ui(device,0,input,u);
        if(i==499) {
            ts_ui_render(&on,u,&instrument);int ink=0;
            for(int px=0;px<640*400;++px)ink+=on.pixels[px]==0xffee8b95u;
            assert(ink>0); /* A short take already has a waveform inside the placeholder. */
        }
    }
    assert(input->recorder.state==TS_EXTERNAL_CAPTURE_RECORDING);
    assert(u->capture_recorded_frames==12000 && u->mosaic_record_preview.frames==12000);
    assert(u->mosaic_record_preview.frames_per_peak==20); /* All history, no rolling-window loss. */
    assert(fabsf(u->mosaic_record_preview.peaks[0]-.85f)<1e-6f);
    assert(u->mosaic_record_preview.peaks[60]==0);
    assert(fabsf(u->mosaic_record_preview.peaks[150]-.25f)<1e-6f); /* Opposite-polarity stereo stays visible. */
    assert(input->recorder.buffer[0]==.85f && input->recorder.buffer[1]==-.85f);
    assert(a->mosaic->next_id==next && a->mosaic->revision==revision);
    u->text_cursor_visible=1;u->mosaic_time=1;ts_ui_render(&on,u,&instrument);
    assert(on.pixels[300*640+151]!=idle.pixels[300*640+151]); /* Card has grown down the timeline. */
    const char *shot=getenv("TS_MOSAIC_RECORD_SCREENSHOT");
    if(shot) {
        u->mosaic_scale=20;ts_ui_render(&on,u,&instrument);assert(ts_ui_write_ppm(&on,shot));u->mosaic_scale=24;
    }
    /* Native surfaces must include the recording waveform before composition,
       at integer and fractional window scales, without changing the scene. */
    for(int pass=0;pass<2;++pass) {
        TsUiWaveformDetail details[2]={0};int width=pass?1729:1920,height=pass?973:1200;
        ts_ui_waveform_details_begin(details,width,height);ts_ui_render(&on,u,&instrument);
        assert(details[0].valid);int colored=0;
        for(int y=0;y<details[0].height;++y)for(int x=0;x<details[0].width;++x)
            colored+=details[0].pixels[(size_t)y*details[0].width+x]==0xffee8b95u;
        assert(colored>100);
        for(int i=0;i<2;++i)ts_ui_waveform_detail_free(&details[i]);ts_ui_waveform_detail_begin(NULL);
    }
    MosaicController c={0};u->mosaic_follow=1;mosaic_poll(device,u,&instrument,&c);
    assert(fabs(u->mosaic_scroll-(13-220.0/24))<1e-6); /* Follow the take with transport stopped. */
    TsMosaicEvent *obstacle=NULL;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(a->mosaic->events[i].id){obstacle=&a->mosaic->events[i];break;}
    assert(obstacle);TsMosaicEvent saved=*obstacle;
    obstacle->x=120;obstacle->width=500;obstacle->start=0;obstacle->duration=30;
    mosaic_poll(device,u,&instrument,&c);assert(u->mosaic_xscroll>120);
    assert(obstacle->x==120 && obstacle->width==500 && a->mosaic->next_id==next);
    *obstacle=saved;u->mosaic_xscroll=120;
    u->mosaic_follow=0;u->mosaic_scroll=2;mosaic_poll(device,u,&instrument,&c);assert(u->mosaic_scroll==2);
    u->mosaic_scroll=0;
    ++u->mosaic_record_preview.epoch;ts_ui_render(&off,u,&instrument);
    u->mosaic_recording=0;ts_ui_render(&idle,u,&instrument);
    for(int y=66;y<376;++y)for(int x=151;x<TS_MOSAIC_CANVAS_RIGHT;++x)
        assert(off.pixels[y*640+x]==idle.pixels[y*640+x]); /* Never draw on a replacement scene. */
    u->mosaic_recording=1;cancel_external_capture(device,0,input,u);
    assert(!u->mosaic_recording && a->mosaic->next_id==next);
    assert(arm_external_capture_to(device,&input_device,a,input,u,&instrument,1));
    assert(u->mosaic_record_preview.frames==0 && u->mosaic_record_preview.peaks[0]==0);
    cancel_external_capture(device,0,input,u);
    mosaic_controller_free(&c);free(input);free(u);routing_free(a);
}
