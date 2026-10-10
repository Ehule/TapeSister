/* Real embedded playback -> source selector -> rolling tape. The independent
   output path must not acquire a second dry monitor when TRACK is selected. */
static void tracker_tape_input(void)
{
    ts_tapehead_stop();ts_note_bank_clear(&a.notes);
    ts_sister_runtime_free(&a.sister);ts_sister_runtime_init(&a.sister);
    assert(ts_sister_runtime_reconfigure(&a.sister,48000,2,error,sizeof(error)));
    assert(ts_spatial_prepare(&a.sister.spatial,48000));
    a.sister.spatial.controls.output[2]=2;a.sister.spatial.controls.output[3]=3;
    TsSisterParameters p=a.sister.parameters;
    p.fx.enabled=0;p.prism.enabled=0;p.monitor_dry=1;p.monitor_wet=0;
    p.head1_feedback=p.head2_feedback=0;p.input_gain=1;
    ts_sister_runtime_set_parameters(&a.sister,&p);
    assert(ts_sister_runtime_enable(&a.sister,48000,2,2,5,error,sizeof(error)));
    ts_sister_runtime_set_monitor(&a.sister,1);
    ts_sister_runtime_set_rolling(&a.sister,1);ts_sister_runtime_set_hold(&a.sister,0);
    TsRouterControls router;ts_router_default(&router);router.master_mix=1;
    ts_sister_runtime_set_router(&a.sister,&router);
    TsSourceRoute route={.mode=TS_SOURCE_PAIR,.speaker=2,.second=3,
        .mix_enabled=1,.clean_level=0,.send_level={100,0,0}};
    pages.tracker.lanes[0].output_route=route;
    ts_tapehead_routes_changed(0,(TsSourceRoute){0});
    startPlaying(PLAYMODE_SONG,0);
    ts_sister_runtime_set_sources(&a.sister,TS_SISTER_SOURCE_TRACK);
    double energy[4];render(energy);render(energy);
    assert(a.sister.last_frame.input.l>.01f && a.sister.last_frame.input.r>.01f);
    assert(a.sister.machine.last_output.wrote);
    double tape=0;for(int i=0;i<4096;++i)tape+=fabsf(a.sister.machine.buffer.data[i]);
    assert(tape>.1); /* Samples reached the tape, not just its input meter. */
    assert(energy[0]==0 && energy[1]==0 && energy[2]==0 && energy[3]==0);
    TsStereoFrame raw=a.sister.last_frame.input;

    /* A panned direct destination still gets exactly its existing clean sound. */
    route.mix_enabled=0;route.pan=-100;
    pages.tracker.lanes[0].output_route=route;ts_tapehead_routes_changed(0,(TsSourceRoute){0});
    ts_sister_runtime_set_sources(&a.sister,0);render(energy);render(energy);
    assert(a.sister.last_frame.input.l==0 && a.sister.last_frame.input.r==0);
    double direct=energy[2];assert(direct>1 && energy[0]==0 && energy[1]==0 && energy[3]==0);
    ts_sister_runtime_set_sources(&a.sister,TS_SISTER_SOURCE_TRACK);render(energy);render(energy);
    assert(fabsf(a.sister.last_frame.input.l-raw.l)<.00001f);
    assert(fabsf(a.sister.last_frame.input.r-raw.r)<.00001f);
    assert(fabs(energy[2]-direct)<.00001 && energy[0]==0 && energy[1]==0 && energy[3]==0);
    p.input_gain=.5f;ts_sister_runtime_set_parameters(&a.sister,&p);render(energy);
    assert(fabsf(a.sister.last_frame.input.l-raw.l*.5f)<.00001f);
    assert(fabs(energy[2]-direct)<.00001); /* INPUT changes the tape, not the direct speaker. */
    p.input_gain=1;ts_sister_runtime_set_parameters(&a.sister,&p);
    ts_sister_runtime_set_sources(&a.sister,TS_SISTER_SOURCE_TRACK|TS_SISTER_SOURCE_FM);render(energy);
    assert(fabsf(a.sister.last_frame.input.l-raw.l/sqrtf(2))<.00001f);
    ts_sister_runtime_set_sources(&a.sister,TS_SISTER_SOURCE_TRACK);
    ts_sister_runtime_set_hold(&a.sister,1);render(energy);assert(!a.sister.machine.last_output.wrote);
    ts_sister_runtime_set_hold(&a.sister,0);ts_sister_runtime_set_rolling(&a.sister,0);
    render(energy);assert(!a.sister.machine.last_output.wrote);
    ts_sister_runtime_set_rolling(&a.sister,1);render(energy);assert(a.sister.machine.last_output.wrote);
    router.bypass_mask|=1u<<TS_ROUTER_SISTER;ts_sister_runtime_set_router(&a.sister,&router);
    render(energy);assert(a.sister.last_frame.input.l==0 && a.sister.last_frame.input.r==0);
    router.bypass_mask&=~(1u<<TS_ROUTER_SISTER);ts_sister_runtime_set_router(&a.sister,&router);
    render(energy);assert(a.sister.last_frame.input.l>.01f);
    editor.channelMuted[0]=true;setChannelMute(0,true);render(energy);render(energy);
    assert(a.sister.last_frame.input.l==0 && a.sister.last_frame.input.r==0);
    editor.channelMuted[0]=false;setChannelMute(0,false);
    ts_tapehead_stop();startPlaying(PLAYMODE_SONG,0);

    /* Main keeps its existing pan and INPUT trim; its DRY reference must not
       restore the side intentionally silenced by pan, or double the source. */
    pages.tracker.lanes[0].output_route=(TsSourceRoute){.mode=TS_SOURCE_MAIN,.pan=-100};
    ts_tapehead_routes_changed(0,(TsSourceRoute){0});
    p.input_gain=.5f;ts_sister_runtime_set_parameters(&a.sister,&p);
    render(energy);render(energy);
    assert(fabsf(a.sister.last_frame.input.l-raw.l*.5f)<.00001f);
    assert(a.sister.last_frame.input.r==0 && energy[0]>1 && energy[1]==0 && energy[2]==0 && energy[3]==0);
    ts_tapehead_stop();ts_sister_runtime_disable(&a.sister);
}
