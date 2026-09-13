static void check_optical_shapes(void)
{
    TsPrismControls c=settings();c.stereo=.8f;c.pitch_offset[1]=700;
    c.input_shape=TS_PRISM_BICONCAVE;
    TsPrismView a=ts_prism_control_view(&c);
    assert(fabsf(a.lens[1].cents-707)<.001f && a.lens[9].cents==1200);
    c.input_shape=TS_PRISM_BICONVEX;c.output_shape=TS_PRISM_BICONCAVE;
    TsPrismView b=ts_prism_control_view(&c);
    assert(fabsf(b.lens[1].cents+693)<.001f && b.lens[1].refraction_cents==693);
    assert(b.lens[1].pan==-b.lens[1].refraction_pan);
    c.input_shape=TS_PRISM_BICONCAVE;c.pitch_offset[1]=0;
    a=ts_prism_control_view(&c);c.input_shape=c.output_shape=0;b=ts_prism_control_view(&c);
    for(int i=0;i<TS_PRISM_LENSES;++i)assert(fabsf(a.lens[i].cents-b.lens[i].cents)<.001f);
    c.input_shape=-1;c.output_shape=999;c.color=NAN;ts_prism_controls_sanitize(&c);
    assert(!c.input_shape && !c.output_shape && c.color==.5f);

    /* All 36 compositions, moving controls and worst-range edits, through the
       actual final limiter. Low device rates also exercise filter stability. */
    const int rates[]={8,44100,48000,96000};
    for(int r=0;r<4;++r) {
        TsPrism p={0};assert(ts_prism_prepare(&p,rates[r]));
        TsSisterLimiter limiter;ts_sister_limiter_init(&limiter);assert(ts_sister_limiter_reconfigure(&limiter,rates[r]));
        c=settings();c.lenses=TS_PRISM_LENSES;c.color=1;c.spread=c.drift=2;c.body=3;c.output_db=12;
        for(int i=0;i<TS_PRISM_LENSES;++i)c.trim_db[i]=12;
        for(int in=0;in<6;++in)for(int out=0;out<6;++out) {
            c.input_shape=in;c.output_shape=out;c.pitch_offset[8]=(in&1)?1200:-1200;
            ts_prism_set_controls(&p,&c);
            for(int n=0;n<rates[r]/12+100;++n) {
                float x=.7f*sinf((float)n*.073f);
                TsStereoFrame y=ts_prism_process(&p,(TsStereoFrame){x,-x});
                assert(isfinite(y.l) && isfinite(y.r));
                y=ts_sister_limiter_process(&limiter,y,NULL,NULL);
                assert(fabsf(y.l)<=1 && fabsf(y.r)<=1);
            }
            TsPrismView v=ts_prism_view(&p);
            assert(v.lens[0].cents==0 && v.lens[0].refraction_cents==0);
            for(int i=0;i<TS_PRISM_LENSES;++i)assert(p.lens[i].phase>=0 && p.lens[i].phase<1);
        }
        ts_prism_free(&p);ts_sister_limiter_free(&limiter);
    }

    /* A concave input audibly moves the original sub-octave above the source. */
    TsPrism p={0};assert(ts_prism_prepare(&p,48000));
    c=settings();c.input_shape=TS_PRISM_BICONCAVE;c.color=0;c.solo_mask=1<<9;c.stereo=0;
    ts_prism_set_controls(&p,&c);double re=0,im=0,wrong_re=0,wrong_im=0;
    for(int n=0;n<96000;++n) {
        float input=.4f*sinf((float)(6.283185307179586*880*n/48000));
        float out=ts_prism_process(&p,(TsStereoFrame){input,input}).l;
        if(n>=48000) {
            double phase=6.283185307179586*1760*n/48000;
            re+=out*cos(phase);im+=out*sin(phase);
            wrong_re+=out*cos(phase/4);wrong_im+=out*sin(phase/4);
        }
    }
    assert(hypot(re,im)/24000>.015 && hypot(re,im)>10*hypot(wrong_re,wrong_im));
    ts_prism_free(&p);
}

static void check_glass_color(void)
{
    for(int shape=0;shape<6;++shape) {
        TsPrism clear={0},color={0};assert(ts_prism_prepare(&clear,48000) && ts_prism_prepare(&color,48000));
        TsPrismControls c=settings();c.lenses=2;c.solo_mask=2;c.spread=c.focus=c.drift=c.stereo=0;
        c.output_shape=shape;c.color=0;ts_prism_set_controls(&clear,&c);
        c.color=1;ts_prism_set_controls(&color,&c);
        double difference=0,dry_energy=0,color_energy=0;
        for(int n=0;n<48000;++n) {
            float x=.4f*source(n,48000,4);
            TsStereoFrame a=ts_prism_process(&clear,(TsStereoFrame){x,-x});
            TsStereoFrame b=ts_prism_process(&color,(TsStereoFrame){x,-x});
            assert(b.r==-b.l && isfinite(b.l));
            if(n>24000){difference+=fabsf(a.l-b.l);dry_energy+=a.l*a.l;color_energy+=b.l*b.l;}
        }
        if(shape==0)assert(difference==0);
        else assert(difference>10); /* Every colored profile audibly changes a voice. */
        if(shape==TS_PRISM_PLANO_CONVEX)assert(color_energy<dry_energy*.2);
        if(shape==TS_PRISM_MENISCUS_POSITIVE)assert(fabs(color_energy/dry_energy-1)<.05);
        /* Anchor and dry stay clean even with strongly colored shapes selected. */
        c.solo_mask=1;ts_prism_set_controls(&color,&c);c.color=0;ts_prism_set_controls(&clear,&c);
        for(int n=0;n<24000;++n) {
            float x=.25f*sinf(n*.3f);
            TsStereoFrame a=ts_prism_process(&clear,(TsStereoFrame){x,-x});
            TsStereoFrame b=ts_prism_process(&color,(TsStereoFrame){x,-x});
            if(n>20000)assert(a.l==b.l && a.r==b.r);
        }
        c.mix=0;c.dry_level=1;c.color=1;ts_prism_set_controls(&color,&c);
        for(int n=0;n<24000;++n)ts_prism_process(&color,(TsStereoFrame){0});
        TsStereoFrame y=ts_prism_process(&color,(TsStereoFrame){.27f,-.17f});assert(y.l==.27f && y.r==-.17f);
        ts_prism_free(&clear);ts_prism_free(&color);
    }
}
