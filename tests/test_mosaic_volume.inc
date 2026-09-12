static void test_mosaic_volume(void)
{
    TsMosaic *m=fixture(),*dry=fixture();
    ts_mosaic_checkpoint(m);uint64_t flat=ts_mosaic_hash(m);
    ts_mosaic_volume_draw(m,0,.2f,1,.8f);
    assert(fabsf(ts_mosaic_volume_at(m,.5)-.5f)<1e-6f);
    assert(ts_mosaic_hash(m)!=flat && ts_mosaic_undo(m,0));
    assert(ts_mosaic_volume_at(m,.5)==1 && ts_mosaic_undo(m,1));
    assert(fabsf(ts_mosaic_volume_at(m,.5)-.5f)<1e-6f);
    /* Stretching/shrinking the arrangement keeps the midpoint's gain at 0.5. */
    for(int pass=0;pass<3;++pass) {
        double end=pass==0?5:pass==1?10:2;
        m->events[0].duration=dry->events[0].duration=end;
        ts_mosaic_seek(m,end*.5);ts_mosaic_seek(dry,end*.5);
        for(int i=0;i<1000;++i) {
            double time=m->time;TsStereoFrame a=ts_mosaic_read(m,48000),b=ts_mosaic_read(dry,48000);
            if(i>480) {
                float expected=(float)(.2+.6*time/end);
                assert(fabsf(a.l-b.l*expected)<2e-6f && fabsf(a.r-b.r*expected)<2e-6f);
            }
        }
    }
    ts_mosaic_volume_action(m,TS_MOSAIC_ENV_MATCH_START);
    assert(m->volume[0]==m->volume[256]);
    ts_mosaic_volume_draw(m,0,.3f,1,.9f);
    ts_mosaic_volume_action(m,TS_MOSAIC_ENV_MATCH_END);assert(m->volume[256]==m->volume[0]);
    ts_mosaic_volume_draw(m,0,.3f,1,.9f);
    ts_mosaic_volume_draw(m,.5,0,.5,0);
    ts_mosaic_volume_action(m,TS_MOSAIC_ENV_SMOOTH);
    assert(m->volume[128]>0 && m->volume[128]<.6f && m->volume[0]==.3f && fabsf(m->volume[256]-.9f)<1e-6f);
    ts_mosaic_volume_action(m,TS_MOSAIC_ENV_RAMP);assert(fabsf(m->volume[128]-.6f)<1e-6f);
    ts_mosaic_checkpoint(m);ts_mosaic_set_repeat(m,1);
    assert(ts_mosaic_volume_at(m,0)==ts_mosaic_volume_at(m,1));
    ts_mosaic_volume_draw(m,1,.4f,1,.4f);assert(m->volume[0]==.4f && m->volume[256]==.4f);
    ts_mosaic_volume_draw(m,0,.7f,0,.7f);assert(m->volume[0]==.7f && m->volume[256]==.7f);
    assert(ts_mosaic_undo(m,0) && !m->repeat && fabsf(m->volume[256]-.9f)<1e-6f);
    assert(ts_mosaic_undo(m,1) && m->repeat && m->volume[0]==m->volume[256]);
    ts_mosaic_volume_action(m,TS_MOSAIC_ENV_RESET);
    for(int i=0;i<257;++i)assert(m->volume[i]==1);
    ts_mosaic_volume_draw(m,0,0,1,0);ts_mosaic_seek(m,0);m->was_playing=0;m->playing=1;
    /* A silent envelope must also be silent on the first playback frame. */
    m->last_output=m->transition_from=(TsStereoFrame){0,0};m->transition_remaining=0;
    for(int i=0;i<500;++i){TsStereoFrame f=ts_mosaic_read(m,48000);assert(f.l==0 && f.r==0);}
    ts_mosaic_free(m);ts_mosaic_free(dry);
}
