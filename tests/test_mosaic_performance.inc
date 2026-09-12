/* Musical behavior: tape rate preserves event relationships and voice phase;
   mixing/fades change amplitude without altering the immutable source. */
static void test_tape_speed(void)
{
    const double speeds[]={.5,1,2};
    for(int k=0;k<3;++k) {
        TsMosaic *m=fixture();m->playing=0;ts_mosaic_set_speed(m,speeds[k]);m->playing=1;
        uint64_t hash=m->events[0].source->hash;
        for(int i=0;i<4800;++i)ts_mosaic_read(m,48000);
        assert(fabs(m->time-.1*speeds[k])<1e-10);
        assert(fabs(m->voices[0].travel[1]-4800*speeds[k])<1e-7);
        assert(m->voices[0].travel[0]*4==m->voices[0].travel[4]);
        double time=m->time,travel=m->voices[0].travel[1];uint64_t attack=m->voices[0].attack;
        ts_mosaic_set_speed(m,k==2?.5:2);
        for(int i=0;i<1200;++i) {
            ts_mosaic_read(m,48000);
            assert(fabs((m->time-time)*48000-(m->voices[0].travel[1]-travel))<1e-6);
            assert(m->voices[0].attack==attack+(uint64_t)i+1);
        }
        assert(m->speed_current==m->speed && m->events[0].source->hash==hash);
        /* Seeking after a speed change reconstructs the same pitched phases. */
        TsMosaic *seek=fixture();seek->playing=0;ts_mosaic_set_speed(seek,m->speed);ts_mosaic_seek(seek,m->time);seek->playing=1;
        for(int i=0;i<120;++i){ts_mosaic_read(m,48000);ts_mosaic_read(seek,48000);}
        for(int n=0;n<5;++n)assert(fabs(m->voices[0].position[n]-seek->voices[0].position[n])<1e-5);
        m->playing=0;ts_mosaic_set_speed(m,speeds[k]);ts_mosaic_seek(m,0);
        m->events[0].note_count=1;m->events[0].notes[0]=60;m->events[0].duration=.1;m->repeat=0;m->playing=1;
        int frames=0;while(m->playing && frames<20000){ts_mosaic_read(m,48000);++frames;}
        assert(abs(frames-(int)llround(4800/speeds[k]))<=2);
        m->repeat=1;m->playing=1;ts_mosaic_seek(m,0);
        for(int i=0;i<(int)(7200/speeds[k]);++i)ts_mosaic_read(m,48000);
        assert(m->playing && fabs(m->time-.05)<1e-9);
        ts_mosaic_free(seek);ts_mosaic_free(m);
    }
    TsMosaic *m=fixture();m->playing=0;ts_mosaic_checkpoint(m);ts_mosaic_set_speed(m,2);
    assert(ts_mosaic_undo(m,0) && m->speed==1);assert(ts_mosaic_undo(m,1) && m->speed==2);
    ts_mosaic_set_speed(m,NAN);assert(m->speed==2);ts_mosaic_set_speed(m,.01);assert(m->speed==.5);
    ts_mosaic_free(m);
}
static void test_event_mix_and_fades(void)
{
    TsMosaic *m=ts_mosaic_create();assert(m);m->gain=1;
    TsSample s={0};char error[160];s.frames=4800;s.sample_rate=48000;s.channels=2;
    s.data=malloc(s.frames*2*sizeof(float));assert(s.data);
    for(size_t i=0;i<s.frames;++i){s.data[2*i]=.5f;s.data[2*i+1]=.25f;}
    TsMosaicSource *source=ts_mosaic_source(m,&s,error,sizeof(error));assert(source);ts_sample_free(&s);
    TsMosaicEvent *e=ts_mosaic_add(m,source,0,0);e->duration=4;e->gain=.8f;e->fade_in=1;e->fade_out=2;
    m->playing=1;TsStereoFrame f={0};
    for(int i=0;i<=500;++i)f=ts_mosaic_read(m,1000);
    assert(fabsf(f.l-.2f)<1e-6f && fabsf(f.r-.1f)<1e-6f);
    for(int i=501;i<=1500;++i)f=ts_mosaic_read(m,1000);
    assert(fabsf(f.l-.4f)<1e-6f && fabsf(f.r-.2f)<1e-6f);
    for(int i=1501;i<=3000;++i)f=ts_mosaic_read(m,1000);
    assert(fabsf(f.l-.2f)<1e-6f && fabsf(f.r-.1f)<1e-6f);
    /* Live pan/level changes retain source phase and settle to a true channel
       balance, including a silent endpoint without folding the other channel. */
    e->fade_in=e->fade_out=0;uint64_t revision=e->revision,attack=m->voices[0].attack;
    double travel=m->voices[0].travel[0];e->pan=1;e->gain=.5f;
    for(int i=0;i<20;++i)f=ts_mosaic_read(m,1000);
    assert(e->revision==revision && m->voices[0].attack==attack+20);
    assert(fabs(m->voices[0].travel[0]-travel-960)<1e-7);
    assert(f.l==0 && fabsf(f.r-.125f)<1e-6f);
    e->pan=-1;for(int i=0;i<20;++i)f=ts_mosaic_read(m,1000);
    assert(fabsf(f.l-.25f)<1e-6f && f.r==0);
    e->gain=0;for(int i=0;i<20;++i)f=ts_mosaic_read(m,1000);assert(f.l==0 && f.r==0);
    /* Long overlapping fades scale to the shortened event rather than creating
       a flat, attenuated middle or discontinuous jump at their crossing. */
    e->pan=0;e->gain=1;e->duration=2;e->fade_in=e->fade_out=2;ts_mosaic_seek(m,0);
    for(int i=0;i<=500;++i)f=ts_mosaic_read(m,1000);assert(fabsf(f.l-.25f)<1e-6f);
    for(int i=501;i<=1000;++i)f=ts_mosaic_read(m,1000);assert(fabsf(f.l-.5f)<1e-6f);
    assert(e->source==source && source->sample.data[0]==.5f);
    ts_mosaic_free(m);
}
