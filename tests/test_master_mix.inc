static void master_mix_controls(const char *directory)
{
    ts_tapehead_stop();ts_note_bank_clear(&a.notes);
    TsSisterParameters p=a.sister.parameters;
    p.fx.enabled=0;p.prism.enabled=1;p.prism.lenses=1;p.prism.mix=.5f;
    p.prism.body=p.prism.stereo=p.prism.color=p.prism.group_octave=0;
    p.prism.output_db=0;p.prism.morph_enabled=0;
    ts_sister_runtime_set_parameters(&a.sister,&p);
    TsRouterControls controls;ts_router_default(&controls);
    controls.send_mask=1;controls.return_route[0]=(TsSourceRoute){.mode=TS_SOURCE_PAIR,.speaker=2,.second=3};
    ts_sister_runtime_set_router(&a.sister,&controls);
    bank.bank[0].output_route=(TsSourceRoute){.mode=TS_SOURCE_PAIR,.speaker=2,.second=3,
        .mix_enabled=1,.clean_level=100,.pan=-25,.send_level={50,0,0}};
    assert(ts_note_bank_start(&a.notes,&bank,TS_AUDITION_CURRENT,12,0,48000)==TS_NOTE_STARTED);
    double energy[4];for(int i=0;i<8;++i)render(energy);
    assert(energy[0]==0 && energy[1]==0 && energy[2]>1 && energy[3]>1);
    assert(source_route_show(0,&a,&u,&pages,&bank,-2,0));
    click(100,89,SDL_BUTTON_LEFT);
    assert(a.sister.router.controls.master_mix);
    ts_eq_spectrum_enable(&a.sister.eq_spectrum,1);
    for(int i=0;i<8;++i)render(energy);
    assert(a.sister.router.master_mix && energy[0]>1 && energy[1]>1 && energy[2]==0 && energy[3]==0);
    assert(atomic_load(&a.sister.eq_spectrum.ready));
    double spectrum=0;for(int i=0;i<TS_EQ_SPECTRUM_FRAMES;++i)
        spectrum+=fabsf(a.sister.eq_spectrum.samples[i].l)+fabsf(a.sister.eq_spectrum.samples[i].r);
    assert(spectrum>1);double before=energy[0]+energy[1];
    TsMasterEqControls eq;ts_master_eq_default(&eq);eq.enabled=1;
    eq.band[0]=(TsEqBand){.enabled=1,.type=TS_EQ_HIGH_PASS,.frequency=2000,.q=.707f};
    ts_master_eq_set(&a.sister.master_eq,&eq);
    for(int i=0;i<8;++i)render(energy);
    assert(energy[0]+energy[1]<before*.001); /* Actual filtering, not just a meter tap. */
    ts_master_eq_default(&eq);ts_master_eq_set(&a.sister.master_eq,&eq);
    for(int i=0;i<4;++i)render(energy);
    assert(energy[0]+energy[1]>before*.99);
    a.mixer.program_gain=2;
    ts_sister_runtime_configure_limiter(&a.sister,1,-12,1,50);
    for(int i=0;i<8;++i)render(energy);
    assert(a.sister.limiter_input_peak>.2512f && energy[0]+energy[1]<before*1.6);
    assert(fabsf(a.last_output.l)<=.2513f && fabsf(a.last_output.r)<=.2513f);
    a.mixer.program_gain=1;
    ts_sister_runtime_set_limiter_enabled(&a.sister,0);
    shot(directory,"shared-master-mix");

    /* One external stereo loop replaces the combined clean+wet signal.
       Its position in the serial Router no longer excludes later sends. */
    TsInsertControls ports={.send_pair=1,.return_pair=0};ts_insert_set(&a.sister.insert,&ports);
    ts_insert_capture_prepare(&a.sister.insert,2,48000,512);
    a.sister.insert.output_channels=4;
    ts_router_reorder(&a.sister.router,4,0);
    ts_router_manual(&a.sister.router,TS_ROUTER_INSERT,0);
    float returned[512*2],out[512*4];
    for(int i=0;i<512;++i){returned[i*2]=.03f;returned[i*2+1]=-.02f;}
    for(int block=0;block<12;++block) {
        ts_insert_capture(&a.sister.insert,returned,512,2);
        audio_callback(&a,(Uint8*)out,sizeof(out));
    }
    assert(a.sister.insert.send_peak>.02f && a.sister.insert.return_peak>.01f);
    assert(fabsf(out[2000]-.03f)<.0002f && fabsf(out[2001]+.02f)<.0002f);
    assert(out[2002]>.05f && out[2003]>.1f); /* SEND includes clean and the Prism return. */
    a.sister.insert.send_gain=0;
    float expected=1-powf(1-a.sister.insert.slew,32);
    uint64_t clock=a.sister.prism.clock;
    audio_callback(&a,(Uint8*)out,32*4*sizeof(float));
    assert(fabsf(a.sister.insert.send_gain-expected)<.00001f);
    assert(a.sister.prism.clock-clock==32); /* No duplicate Insert or effect instances. */
    assert(ts_sister_runtime_enable(&a.sister,48000,2,2,5,error,sizeof(error)));
    for(int block=0;block<8;++block) {
        ts_insert_capture(&a.sister.insert,returned,512,2);
        audio_callback(&a,(Uint8*)out,sizeof(out));
    }
    assert(fabsf(out[2000]-.03f)<.0002f && fabsf(out[2001]+.02f)<.0002f);
    a.sister.insert.send_gain=0;clock=a.sister.prism.clock;
    audio_callback(&a,(Uint8*)out,32*4*sizeof(float));
    assert(fabsf(a.sister.insert.send_gain-expected)<.00001f && a.sister.prism.clock-clock==32);
    ts_sister_runtime_disable(&a.sister);
    ts_router_manual(&a.sister.router,TS_ROUTER_INSERT,0);
    for(int i=0;i<4;++i)render(energy);
    assert(energy[0]+energy[1]>before*.99 && energy[2]==0 && energy[3]==0);
    ports=(TsInsertControls){0};ts_insert_set(&a.sister.insert,&ports);
    source_route_capture(&a,&pages,&bank);click(500,89,SDL_BUTTON_LEFT);
    for(int i=0;i<4;++i)render(energy);
    assert(!a.sister.router.master_mix && energy[0]==0 && energy[1]==0 && energy[2]>1 && energy[3]>1);
    ts_sister_runtime_set_master_output_gain(&a.sister,0);
    for(int i=0;i<4;++i)render(energy);
    assert(energy[0]==0 && energy[1]==0);
    ts_sister_runtime_set_master_output_gain(&a.sister,1);
    ts_note_bank_clear(&a.notes);
}

/* Optional local reproduction against the user's actual project. */
static void master_mix_project(const char *path,const char *directory)
{
    if(!path)return;
    TsInstrument *record=malloc(sizeof(*record));assert(record);ts_instrument_init(record);
    assert(load_instrument(0,&a,&u,&bank,&pages,record,0,path));
    assert(embedded_open(window,0,&a,&u,&pages,&bank,48000));u.tracker_open=1;
    a.output_device_channels=2;
    assert(source_route_show(0,&a,&u,&pages,&bank,-2,0));
    if(!a.sister.router.controls.master_mix)click(100,89,SDL_BUTTON_LEFT);
    startPlaying(PLAYMODE_SONG,0);
    float out[1024];double signal=0,eq_input=0;
    for(int block=0;block<256;++block) {
        audio_callback(&a,(Uint8*)out,sizeof(out));
        signal+=fabsf(out[1000])+fabsf(out[1001]);
        eq_input+=a.sister.limiter_input_peak;
    }
    assert(signal>.01 && eq_input>.01);
    printf("Project Master mix verified: audible=%g, EQ/limiter input=%g\n",signal,eq_input);
    shot(directory,"embers-master-mix");
    ts_tapehead_stop();ts_instrument_free(record);free(record);
}
