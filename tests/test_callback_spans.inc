/* Exercise the production source/graph/output split against its retained
   chronological oracle, including awkward callback sizes and duplex position. */
static void callback_span_parity(void)
{
    const Case c={"span_oracle",MATRIX,.2f,.6f,1,1,1,1};
    Fixture lanes[2]={make_fixture(&c),make_fixture(&c)};
    lanes[0].a->force_chronological=1;
    const unsigned sizes[]={1,17,63,64,127,128,257,512,1024};
    static float output[2][1024*26],input[1024*255],stereo[2][1024*2];
    for(unsigned block=0;block<100;++block) {
        unsigned n=sizes[block%9];
        for(unsigned j=0;j<n*255;++j)input[j]=.025f*sinf((block*331+j)*.007f);
        for(int lane=0;lane<2;++lane) {
            AudioState *a=lanes[lane].a;a->output_device_channels=26;a->offline_stereo=stereo[lane];
            a->tune_reference_target=.001f;a->tune_reference_frequency=220;
            if(block==0 || block==20) {
                TsMatrixControls controls=a->sister.matrix.controls;
                controls.row[ts_matrix_input_row(127)].matrix_gain[TS_MATRIX_EQ]=block==0?300:0;
                controls.row[TS_MATRIX_EQ].matrix_gain[ts_matrix_output_destination(12)]=500;
                controls.row[TS_MATRIX_MOSAIC].pan=block==0?0:23;
                assert(ts_matrix_set(&a->sister.matrix,&controls,RATE));
            }
            if(block==30) {
                ts_sister_runtime_set_master_output_gain(&a->sister,.35f);
                a->sister.performance_mixer.gain[2]=.4f;
            }
            if(block==40) {
                TsSisterFxControls controls=a->sister.post_fx.controls;
                controls.enabled=0;ts_sister_post_fx_set_controls(&a->sister.post_fx,&controls);
            }
            if(block==80) {
                char error[256];assert(ts_capture_arm_channels(&a->capture,1,777,RATE,2,error,sizeof(error)));
                assert(ts_capture_set_source(&a->capture,0,error,sizeof(error)));
                assert(ts_capture_trigger(&a->capture,error,sizeof(error)));
            }
            if(block==90)atomic_store(&a->tracker_capture,1);
            if(block==95)atomic_store(&a->tracker_capture,0);
            ts_insert_duplex_block(&a->sister.insert,1,input,n,255);
            audio_callback(a,(Uint8 *)output[lane],(int)(n*26*sizeof(float)));
            assert(a->sister.insert.duplex_position==n);
        }
        if(block>=80) {
            assert(lanes[0].a->capture.recorded_frames==lanes[1].a->capture.recorded_frames);
            assert(!memcmp(lanes[0].a->capture.buffer,lanes[1].a->capture.buffer,
                lanes[0].a->capture.recorded_frames*2*sizeof(float)));
        }
        assert(!memcmp(output[0],output[1],n*26*sizeof(float)));
        assert(!memcmp(stereo[0],stereo[1],n*2*sizeof(float)));
        assert(!memcmp(&lanes[0].a->keyboard_dry,&lanes[1].a->keyboard_dry,sizeof(TsStereoFrame)));
        assert(!memcmp(lanes[0].a->sister.matrix.input_peak,lanes[1].a->sister.matrix.input_peak,sizeof(lanes[0].a->sister.matrix.input_peak)));
        assert(!memcmp(lanes[0].a->sister.matrix.hardware_peak,lanes[1].a->sister.matrix.hardware_peak,sizeof(lanes[0].a->sister.matrix.hardware_peak)));
    }
    assert(lanes[1].a->span_frames>10000 && lanes[1].a->chronological_frames>100);
    for(int i=0;i<2;++i)dispose(&lanes[i]);
    puts("Canonical callback spans match chronological rendering: notes, audition, Mosaic, duplex input 255, output 26, source/output ramps and route/master transitions");
}
