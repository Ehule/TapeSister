/* Optional reproducible screenshots: actual native renderer and controller
   state, with a headless duplex loop. Not a physical-interface validation. */
static void router_demo_shot(const char *directory,const char *name)
{
    router_sync(&audio,&ui);ui.performance_tooltip[0]=0;
    TsSisterRoutingSnapshot s;assert(ts_sister_runtime_get_snapshot(&audio.sister,&s));
    ui.router_enabled=s.router_enabled;ui.insert_send_peak=s.insert_send_peak;ui.insert_return_peak=s.insert_return_peak;
    memcpy(ui.router_peaks,s.router_peaks,sizeof(ui.router_peaks));
    char path[1200];snprintf(path,sizeof(path),"%s/%s.ppm",directory,name);
    static TsFramebuffer fb;ts_ui_render(&fb,&ui,&instrument);assert(ts_ui_write_ppm(&fb,path));
}
static void router_performance_screenshots(void)
{
    const char *directory=getenv("TS_TEST_ROUTER_DEMO_DIR");if(!directory)return;
    char error[160];reset_route(3);ui.router_open=1;ui.router_performance_open=ui.insert_open=ui.master_eq_open=0;
    assert(ts_sister_runtime_enable(&audio.sister,44100,2,2,1,error,sizeof(error)));
    TsSisterParameters p=audio.sister.parameters;p.prism.enabled=1;p.fx.enabled=1;p.fx.fallout.enabled=1;
    ts_sister_runtime_set_parameters(&audio.sister,&p);
    TsRouter *r=&audio.sister.router;TsRouterControls c;ts_router_default(&c);c.bypass_mask=0;ts_router_set(r,&c);
    ts_router_reorder(r,1,0);ts_router_reorder(r,4,1);ts_router_performance_default(&r->performance);
    ts_router_store(r,0);ts_router_manual(r,TS_ROUTER_INSERT,0);ts_router_store(r,1);
    ts_router_recall(r,0);ts_router_manual(r,TS_ROUTER_PRISM,1);ts_router_store(r,2);ts_router_recall(r,0);
    r->performance.length=4;r->performance.step[0]=(TsRouterStep){0,30};r->performance.step[1]=(TsRouterStep){1,15};
    r->performance.step[2]=(TsRouterStep){2,8};r->performance.step[3]=(TsRouterStep){0,45};
    audio.output_device_channels=8;audio.sister.insert.output_channels=8;
    TsInsertControls ports={1,1,0,0};ts_sister_runtime_set_insert(&audio.sister,&ports);
    ts_insert_capture_prepare(&audio.sister.insert,8,44100,256);
    toggle_fm_hold(device,&audio,&ui,&instrument);click_note(0,0);
    static float output[256*8],input[256*8];
    for(int n=0;n<20;++n) {
        for(int i=0;i<256;++i){input[i*8+2]=output[i*8+2];input[i*8+3]=output[i*8+3];}
        ts_insert_duplex_block(&audio.sister.insert,1,input,256,8);
        audio_callback(&audio,(Uint8*)output,sizeof(output));
    }
    snprintf(ui.status,sizeof(ui.status),"ROUTER PERFORMANCE / HEADLESS DUPLEX LOOP / F9 OPENS AND CLOSES");
    router_demo_shot(directory,"router-performance-controls");
    ui.router_performance_open=1;ui.router_slot=2;ui.router_step=2;
    router_demo_shot(directory,"router-performance-states");
    ui.router_slot=0;ui.router_step=3;
    router_demo_shot(directory,"router-performance-sequence");
    ts_router_sequence_play(r);ts_router_performance_advance(r,44100u*30);ui.router_step=1;
    router_demo_shot(directory,"router-performance-playing");
    r->performance.timer_seconds[TS_ROUTER_PRISM]=20;ts_router_timer_start(r,TS_ROUTER_PRISM,TS_ROUTER_TIMER_SOLO);
    ts_router_performance_advance(r,44100u*7);ui.router_performance_open=0;
    snprintf(ui.status,sizeof(ui.status),"SEQUENCE CONTINUES / TIMED SOLO EXPIRES TO THE CURRENT STEP / ORDER LOCKED");
    router_demo_shot(directory,"router-performance-timer");
    ts_router_sequence_restore(r);ts_insert_duplex_block(&audio.sister.insert,0,NULL,0,0);
    audio.output_device_channels=2;ui.router_open=0;
}
