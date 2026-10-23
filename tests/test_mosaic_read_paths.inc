/* Compare Mosaic with the shared sample/route readers, including the boundary
   paths which must not be treated as ordinary interior interpolation. */
static void test_mosaic_read_paths(void)
{
    for(int channels=1;channels<=2;++channels)for(int mode=0;mode<TS_LOOP_MODE_COUNT;++mode)
    for(int splice=0;splice<=1;++splice)for(int rate_index=0;rate_index<2;++rate_index) {
        int rate=rate_index?48000:44100;
        TsMosaic *m=ts_mosaic_create();assert(m);m->gain=1;m->speed=1.37;
        float samples[514];
        for(int i=0;i<514;++i)samples[i]=sinf(i*.13f)*.4f;
        samples[13]=NAN;samples[14]=INFINITY;samples[15]=-INFINITY;samples[16]=-0.0f;
        TsSample sample={.data=samples,.frames=257,.sample_rate=48000,.channels=(uint8_t)channels};
        char error[160];TsMosaicSource *s=ts_mosaic_source(m,&sample,error,sizeof(error));assert(s);
        TsMosaicEvent *e=ts_mosaic_add(m,s,0,0);assert(e);
        e->first=7;e->last=257;e->crossfade=splice?32:0;e->mode=(TsLoopMode)mode;
        e->notes[0]=67;e->duration=5;m->playing=1;
        TsSourceRouteVoice reference={0};
        for(int block=0;block<8;++block) {
            if(block==1){e->output_route.matrix_enabled=1;e->output_route.matrix_gain[7]=750;}
            if(block==2 || block==5)e->output_route=(TsSourceRoute){0};
            if(block==4){e->output_route.pan=35;e->output_route.width=-40;}
            ts_source_route_set(&reference,e->output_route,(unsigned)rate);
            ts_mosaic_begin_block(m,257);
            for(int frame=0;frame<257;++frame) {
                TsSourceRouteMix actual_routes={0},expected_routes={0};
                TsStereoFrame actual=ts_mosaic_read_routed(m,rate,&actual_routes);
                const TsMosaicVoice *v=&m->voices[0];
                TsStereoFrame f=v->intro[0]?ts_audition_read_frame(&s->sample,v->position[0],e->last):
                    ts_audition_read_looped_mode_frame(&s->sample,v->position[0],e->first,e->last,e->crossfade,e->mode);
                float attack=ts_audition_attack_gain(v->attack-1,v->attack_frames);
                TsStereoFrame expected={0+f.l*attack,0+f.r*attack};
                expected=ts_source_route_frame(&reference,expected,&expected_routes);
                expected.l=0.0f+expected.l;expected.r=0.0f+expected.r; /* Mosaic bus accumulation. */
                expected=ts_stereo_frame_sanitize(expected);
                if(memcmp(&actual,&expected,sizeof(actual)))fprintf(stderr,"read mismatch channels=%d mode=%d splice=%d rate=%d block=%d frame=%d pos=%.17g attack=%.9g actual=%a,%a expected=%a,%a\n",channels,mode,splice,rate,block,frame,v->position[0],attack,actual.l,actual.r,expected.l,expected.r);
                assert(!memcmp(&actual,&expected,sizeof(actual)));
                assert(!memcmp(&actual_routes,&expected_routes,sizeof(actual_routes)));
                assert(v->output_route.activity_source==reference.activity_source);
                assert(v->output_route.activity_send==reference.activity_send);
            }
        }
        assert(m->voices[0].route_passthrough);
        ts_mosaic_free(m);
    }
}
