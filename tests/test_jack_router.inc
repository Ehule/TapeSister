/* A real JACK cable loop through the production Router + Insert engines.
   Host applications and physical interfaces are deliberately not simulated. */
static TsRouter live_router;
static TsInsert live_insert;
static _Atomic unsigned live_frames,live_returned;
static TsStereoFrame live_stage(void *unused,int stage,TsStereoFrame in)
{
    (void)unused;
    return stage==TS_ROUTER_INSERT?ts_insert_process(&live_insert,in,
        live_router.wet[stage]*live_router.gain):in;
}
static void live_master(void *userdata,Uint8 *bytes,int size)
{
    (void)userdata;float *out=(float *)bytes;int frames=size/(8*(int)sizeof(float));
    ts_insert_begin_output_block(&live_insert,(unsigned)frames);
    for(int i=0;i<frames;++i) {
        TsStereoFrame y=ts_router_process(&live_router,(TsStereoFrame){.25f,-.5f},live_stage,NULL);
        ts_insert_write_output(&live_insert,out+i*8,8,y);
        assert(isfinite(y.l+y.r));
        if(live_router.wet[TS_ROUTER_INSERT]>.999f && fabsf(y.l-.25f)<.01f && fabsf(y.r+.5f)<.01f)
            atomic_fetch_add(&live_returned,1);
    }
    atomic_fetch_add(&live_frames,(unsigned)frames);
}
static void live_send(void *userdata,Uint8 *bytes,int size)
{ (void)userdata;ts_insert_render_send(&live_insert,(float *)bytes,(size_t)size/(2*sizeof(float)),2,live_insert.sample_rate); }
static void live_capture(void *userdata,Uint8 *bytes,int size)
{ (void)userdata;ts_insert_capture_from(&live_insert,(float *)bytes,(size_t)size/(2*sizeof(float)),2,1); }
static void live_performance(SDL_AudioDeviceID master,SDL_AudioDeviceID send,SDL_AudioDeviceID ret)
{
    ts_native_pause(master,1);ts_native_pause(send,1);ts_native_pause(ret,1);
    TsJackEndpoint *me=ts_jack_endpoint(master),*se=ts_jack_endpoint(send),*re=ts_jack_endpoint(ret);
    TsJackClient *clients[]={me->client,se->client,re->client};
    unsigned rate=(unsigned)me->spec.freq,period=me->spec.samples;
    ts_router_init(&live_router);ts_router_prepare(&live_router,rate);
    ts_insert_init(&live_insert);ts_insert_prepare(&live_insert,rate);live_insert.output_channels=8;
    TsInsertControls ports={1,0,0,0};ts_insert_set(&live_insert,&ports);
    ts_insert_io_mode(&live_insert,1,1);
    ts_insert_send_prepare(&live_insert,2,rate,period);ts_insert_capture_prepare_from(&live_insert,2,rate,period,1);
    live_router.performance.occupied=7;live_router.performance.length=3;
    live_router.performance.state[0]=(TsRouterState){31,0};
    live_router.performance.state[1]=(TsRouterState){15,0};
    live_router.performance.state[2]=(TsRouterState){31,TS_ROUTER_INSERT+1};
    for(int i=0;i<3;++i)live_router.performance.step[i]=(TsRouterStep){i,.15f};
    ts_router_sequence_play(&live_router);
    me->spec.callback=live_master;se->spec.callback=live_send;re->spec.callback=live_capture;
    ts_native_pause(ret,0);ts_native_pause(send,0);ts_native_pause(master,0);
    for(int i=0;i<400 && atomic_load(&live_frames)<rate*2;++i)SDL_Delay(10);
    ts_native_lock(master);
    assert(atomic_load(&live_frames)>=rate*2 && live_router.transport.running);
    assert(atomic_load(&live_returned)>period*2);
    uint64_t left=live_router.transport.frames;
    ts_router_manual(&live_router,TS_ROUTER_INSERT,0);
    assert(live_router.transport.running && live_router.transport.frames==left);
    ts_router_sequence_restore(&live_router);
    assert(!live_router.transport.running);
    ts_native_unlock(master);
    ts_native_pause(master,1);ts_native_pause(send,1);ts_native_pause(ret,1);
    assert(me->client==clients[0] && se->client==clients[1] && re->client==clients[2]);
    printf("Live JACK Router sequenced Insert ON/BYPASS/SOLO: %u frames, %u returned frames, unchanged clients\n",
        atomic_load(&live_frames),atomic_load(&live_returned));
}
