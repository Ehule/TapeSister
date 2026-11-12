/* Option 1 recognizes the complete ten-movement composition. Existing Tree04
   fixtures retain their versioned replay for before/after comparisons. */
static int live_test_angelic_matches(const TsMatrixControls *controls,const TsSamplePages *pages,const char *path)
{
    if(strcmp(path_basename(path),"Tree-of-Life-Angelic-Noise.tsr"))return 0;
    TsMosaicGallery *g=pages->gallery;
    if(!g || g->count!=10 || g->outgoing>=0 || !controls->enabled)return 0;
    static const char *names[]={"KETER","CHOKHMAH","BINAH","CHESED","GEVURAH","TIFERET","NETZACH","HOD","YESOD","MALKHUT"};
    for(int p=0;p<10;++p) {
        TsMosaic *m=g->canvas[p].scene;uint64_t id=0;int count=0;
        if(!m || !strstr(g->canvas[p].name,names[p]) || m->playing || m->audition_id || fabs(ts_mosaic_end(m)-600)>1)return 0;
        for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id) {
            const TsMosaicEvent *e=&m->events[i];++count;
            if(!e->source || e->muted || e->solo || e->live)return 0;
            /* Mute/solo happen 11..14 seconds after the midpoint seek. */
            if(!e->source->video_path[0] && e->start<=311 && e->start+e->duration>=314 && (!id || e->id<id))id=e->id;
        }
        if(!count || !id)return 0;live_test.card[p]=id;
    }
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)live_test.mixes[i]=controls->effect[i].mix;
    int spring=live_test_slot(controls,12),grain=live_test_slot(controls,4);
    if(spring<0 || grain<0 || controls->effect[grain].type!=TS_SISTER_FX_GRAIN ||
       controls->effect[spring].type!=TS_MATRIX_FX_SUNVOX || !strstr(ts_sunvox_name(&controls->effect[spring].sunvox),"Taneb Spring"))return 0;
    live_test.grain=controls->effect[grain].a;return 1;
}
static void live_test_angelic_plan(void)
{
    live_test.count=0;live_test_add(0,LT_CHECK,0,0);
    live_test_add(100,LT_ESCAPE,0,0);live_test_add(1000,LT_ASSERT,2,0);
    live_test_add(10000,LT_ASSERT,2,0);live_test_add(11000,LT_SPACE,1,0);
    static const int order[]={0,5,2,8,1,7,3,9,4,6};
    for(int p=0;p<10;++p) {
        unsigned t=12000+76000*p;
        if(p)live_test_add(t,LT_GALLERY,order[p],0);
        live_test_add(t+9000,LT_CHECK,20+p,0);
        live_test_add(t+10000,LT_SEEK,0,.5f);
        live_test_add(t+12000,LT_FOLLOW,1,0);
        live_test_add(t+14000,LT_FX,0,0);
        for(int i=0;i<20;++i)live_test_add(t+16000+100*i,LT_MIX,12,.25f+.25f*(i<10?i:19-i)/9.f);
        live_test_add(t+19000,LT_MIX,12,-1);
        live_test_add(t+20000,LT_GRAIN,4,.4f);live_test_add(t+20500,LT_GRAIN,4,-1);
        live_test_add(t+21000,LT_MUTE,0,1);live_test_add(t+22000,LT_MUTE,0,0);
        live_test_add(t+23000,LT_SOLO,0,1);live_test_add(t+24000,LT_SOLO,0,0);
        live_test_add(t+24500,LT_ASSERT,4,0);
        live_test_add(t+25000,LT_SPACE,0,0);live_test_add(t+30000,LT_ASSERT,1,0);
        live_test_add(t+32000,LT_ESCAPE,0,0);live_test_add(t+40000,LT_ASSERT,2,0);
        live_test_add(t+41000,LT_CLOSE,0,0);live_test_add(t+49000,LT_ASSERT,2,0);
        live_test_add(t+50000,LT_SPACE,1,0);live_test_add(t+52000,LT_SEEK,0,.9f);
        live_test_add(t+54000,LT_TRACKER,0,0);live_test_add(t+56000,LT_MOSAIC,0,0);
        live_test_add(t+58000,LT_MATRIX,0,0);live_test_add(t+60000,LT_FX,0,0);
        live_test_add(t+62000,LT_ESCAPE,0,0);live_test_add(t+62200,LT_ESCAPE,1,0);
        live_test_add(t+63000,LT_ASSERT,3,0);live_test_add(t+64000,LT_SPACE,1,0);
        live_test_add(t+66000,LT_ASSERT,0,0);live_test_add(t+68000,LT_SEEK,1,.35f);
        live_test_add(t+70000,LT_HEALTH,0,1);live_test_add(t+72000,LT_HEALTH,0,0);
        live_test_add(t+74000,LT_CHECK,10+p,0);
    }
    live_test_add(772000,LT_GALLERY,0,0);live_test_add(781000,LT_CHECK,30,0);
    live_test_add(782000,LT_ESCAPE,0,0);live_test_add(792000,LT_ASSERT,2,0);
    live_test_add(792100,LT_END,0,0);live_test.duration_ms=792100;
}
