static TsStereoFrame eq_test_tone(unsigned frame,unsigned bin)
{
    float x=.2f*sinf((float)(6.283185307179586*bin*frame/TS_EQ_SPECTRUM_FRAMES));
    return (TsStereoFrame){x,-x};
}
static void effect_eq_observation(void)
{
    static TsMatrixEffects observed,reference;
    static TsMatrixControls c;
    static TsEqSpectrum capture;
    static TsStereoFrame expected[TS_EQ_SPECTRUM_FRAMES];
    /* Real node/child output, scalar and block/in-place paths, with another
       unrelated EQ rendering a different tone between every observed span. */
    for(int mode=0;mode<5;++mode)for(int block=0;block<2;++block) {
        ts_matrix_default(&c);int eq=ts_matrix_effect_add(&c,TS_MATRIX_FX_EQ);
        int other=ts_matrix_effect_add(&c,TS_MATRIX_FX_EQ),board=ts_matrix_effect_add(&c,TS_MATRIX_FX_PEDALBOARD);
        TsPedalboardControls *b=&c.effect[board].board;b->count=2;
        for(int j=0;j<2;++j){b->slot[j].type=TS_SISTER_FX_EQ;b->slot[j].enabled=1;b->slot[j].mix=1;
            ts_master_eq_default(&b->slot[j].eq);b->slot[j].eq.enabled=1;
            b->slot[j].eq.band[2]=(TsEqBand){1,TS_EQ_BELL,1000,j?-9:9,1};}
        uint32_t child=b->slot[0].id;
        if(mode==2){TsSisterFxSlotControls swap=b->slot[0];b->slot[0]=b->slot[1];b->slot[1]=swap;}
        if(mode==3)b->slot[0].enabled=0;
        if(mode==4)b->slot[0].mix=0;
        ts_matrix_effects_init(&observed);ts_matrix_effects_init(&reference);
        assert(ts_matrix_effects_restore(&observed,&c,48000) && ts_matrix_effects_restore(&reference,&c,48000));
        ts_eq_spectrum_init(&capture);ts_eq_spectrum_enable(&capture,1);
        int node=mode?board:eq;
        ts_matrix_effects_observe_eq(&observed,c.effect[node].id,mode?child:0,&capture);
        TsSisterSingleFx *first=ts_sister_single_fx_new(TS_SISTER_FX_EQ,48000,1);
        TsSisterSingleFx *second=ts_sister_single_fx_new(TS_SISTER_FX_EQ,48000,2);assert(first && second);
        ts_sister_single_fx_set(first,&b->slot[0]);ts_sister_single_fx_set(second,&b->slot[1]);
        unsigned n=block?64:1;
        for(unsigned i=0;i<TS_EQ_SPECTRUM_FRAMES;i+=n) {
            TsStereoFrame in[64],out[64],ref[64],unrelated[64],key[64]={{0}};
            for(unsigned j=0;j<n;++j){in[j]=eq_test_tone(i+j,85);unrelated[j]=eq_test_tone(i+j,341);out[j]=in[j];
                TsStereoFrame a=ts_sister_single_fx_process(first,in[j],(float)b->slot[0].enabled);
                TsStereoFrame z=ts_sister_single_fx_process(second,a,(float)b->slot[1].enabled);
                expected[i+j]=mode==2?z:a;}
            if(block){ts_matrix_effects_process_span(&observed,node,out,key,out,n,1);
                ts_matrix_effects_process_span(&reference,node,in,key,ref,n,1);
                ts_matrix_effects_process_span(&observed,other,unrelated,key,unrelated,n,1);}
            else {out[0]=ts_matrix_effects_process(&observed,node,in[0],key[0],1);
                ref[0]=ts_matrix_effects_process(&reference,node,in[0],key[0],1);
                ts_matrix_effects_process(&observed,other,unrelated[0],key[0],1);}
            assert(!memcmp(out,ref,n*sizeof(*out))); /* Observation cannot alter audio. */
            if(!mode)memcpy(expected+i,out,n*sizeof(*out));
        }
        assert(atomic_load(&capture.ready) && !memcmp(expected,capture.samples,sizeof(expected)));
        TsEqSpectrumView view;ts_eq_spectrum_clear(&view,48000);
        assert(ts_eq_spectrum_poll(&capture,&view,48000,.1f) && view.valid);
        int peak=0;for(int j=1;j<TS_EQ_SPECTRUM_POINTS;++j)if(view.db[j]>view.db[peak])peak=j;
        double hz=20*pow(ts_master_eq_max_hz(48000)/20.,peak/(double)(TS_EQ_SPECTRUM_POINTS-1));
        assert(fabs(hz/(85.*48000/4096)-1)<.03);
        ts_eq_spectrum_enable(&capture,0);unsigned fill=capture.fill;
        ts_matrix_effects_process(&observed,node,(TsStereoFrame){1,1},(TsStereoFrame){0},1);assert(capture.fill==fill);
        /* Deleted/replaced node IDs cannot inherit the observation. */
        uint32_t stale=c.effect[node].id;int type=c.effect[node].type;
        ts_matrix_effect_remove(&c,node);assert(ts_matrix_effect_add(&c,type)==node && c.effect[node].id!=stale);
        assert(ts_matrix_effects_restore(&observed,&c,48000));ts_eq_spectrum_init(&capture);ts_eq_spectrum_enable(&capture,1);
        ts_matrix_effects_observe_eq(&observed,stale,mode?child:0,&capture);
        ts_matrix_effects_process(&observed,node,(TsStereoFrame){1,1},(TsStereoFrame){0},1);assert(!capture.fill);
        ts_sister_single_fx_free(first);ts_sister_single_fx_free(second);
        ts_matrix_effects_free(&observed);ts_matrix_effects_free(&reference);
    }
}
static void original_board_eq_observation(void)
{
    static TsSisterPostFxEngine observed,reference;static TsEqSpectrum capture[TS_SISTER_FX_LOCATION_COUNT];
    TsSisterFxControls c;ts_sister_fx_controls_default(&c);c.slot_count=1;
    for(int i=0;i<TS_SISTER_FX_SLOT_COUNT;++i)c.slot[i]=ts_sister_fx_empty_slot();
    c.slot[0].type=TS_SISTER_FX_EQ;c.slot[0].id=71;c.slot[0].enabled=1;c.slot[0].mix=1;c.slot[0].placement=TS_SISTER_FX_PLACE_HEADS;
    ts_master_eq_default(&c.slot[0].eq);c.slot[0].eq.enabled=1;
    assert(ts_sister_post_fx_init(&observed,48000) && ts_sister_post_fx_init(&reference,48000));
    ts_sister_post_fx_sync_controls(&observed,&c);ts_sister_post_fx_sync_controls(&reference,&c);
    for(int i=0;i<TS_SISTER_FX_LOCATION_COUNT;++i){ts_eq_spectrum_init(&capture[i]);ts_eq_spectrum_enable(&capture[i],1);}
    ts_sister_post_fx_observe_eq(&observed,71,capture);
    for(unsigned frame=0;frame<TS_EQ_SPECTRUM_FRAMES;++frame)for(unsigned loc=0;loc<TS_SISTER_FX_LOCATION_COUNT;++loc) {
        TsStereoFrame in=eq_test_tone(frame,32+loc*80);
        TsStereoFrame a=loc?ts_sister_post_fx_process(&observed,loc-1,in,0):ts_sister_post_fx_process_pre(&observed,in,0);
        TsStereoFrame b=loc?ts_sister_post_fx_process(&reference,loc-1,in,0):ts_sister_post_fx_process_pre(&reference,in,0);
        assert(!memcmp(&a,&b,sizeof(a)));
        if(c.slot[0].placement&(1u<<loc))assert(!memcmp(&a,&capture[loc].samples[frame],sizeof(a)));
        else assert(!capture[loc].fill && !atomic_load(&capture[loc].ready));
    }
    TsEqSpectrumView views[TS_SISTER_FX_LOCATION_COUNT],combined;
    for(unsigned loc=0;loc<TS_SISTER_FX_LOCATION_COUNT;++loc) {
        ts_eq_spectrum_clear(&views[loc],48000);
        int captured=ts_eq_spectrum_poll(&capture[loc],&views[loc],48000,.1f);
        if(!(c.slot[0].placement&(1u<<loc))){assert(!captured && !views[loc].valid);continue;}assert(captured);
        int peak=0;for(int i=1;i<TS_EQ_SPECTRUM_POINTS;++i)if(views[loc].db[i]>views[loc].db[peak])peak=i;
        double hz=20*pow(ts_master_eq_max_hz(48000)/20.,peak/(double)(TS_EQ_SPECTRUM_POINTS-1));
        assert(fabs(hz/((32+loc*80)*48000./4096)-1)<.03); /* No interleaved paths / false pitch. */
    }
    ts_eq_spectrum_combine(&combined,views,TS_SISTER_FX_LOCATION_COUNT,48000);assert(combined.valid);
    for(int i=0;i<TS_EQ_SPECTRUM_POINTS;++i)for(int j=0;j<TS_SISTER_FX_LOCATION_COUNT;++j)assert(combined.db[i]>=views[j].db[i]-.001f);
    ts_sister_post_fx_free(&observed);ts_sister_post_fx_free(&reference);
}
