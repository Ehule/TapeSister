/* Option 1 discovers content, independent of file names, movement names, effect
   IDs and baseline card counts. The bounded plan fits all TS_GALLERY_LIMIT scenes. */
static int live_test_gallery_scan(const TsSamplePages *pages)
{
    const TsMosaicGallery *g=pages?pages->gallery:NULL;
    if(!g || g->count<1 || g->count>TS_GALLERY_LIMIT)return 0;
    for(int p=0;p<g->count;++p) {
        const TsMosaic *m=g->canvas[p].scene;
        if(!m)return 0;
        double end=ts_mosaic_end(m);
        if(!isfinite(end) || end<0)return 0;
        live_test.movement_end[p]=end;
        live_test.movement_cards[p]=live_test.movement_markers[p]=0;
        for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id)++live_test.movement_cards[p];
        for(int i=0;i<TS_MOSAIC_MARKERS;++i)
            if(m->markers[i].stored && isfinite(m->markers[i].time) && m->markers[i].time>=0 && m->markers[i].time<end)
                ++live_test.movement_markers[p];
    }
    live_test.movements=g->count;return 1;
}
static void live_test_gallery_plan(void)
{
    live_test.count=0;live_test_add(0,LT_CHECK,0,0);
    live_test_add(100,LT_BACKSPACE,0,0);live_test_add(8000,LT_ASSERT,2,0);
    unsigned t=8100;
    for(int p=0;p<live_test.movements;++p) {
        live_test_add(t,LT_GALLERY,p,0);
        live_test_add(t+100,LT_INVENTORY,p,0);
        if(live_test.movement_end[p]<=0) {
            /* Empty canvases still get a routing view and stopped-engine sample. */
            live_test_add(t+200,LT_FX,0,0);
            live_test_add(t+10000,LT_ASSERT,2,0);
            live_test_add(t+11000,LT_CLOSE,0,0);
            t+=12000;continue;
        }
        live_test_add(t+200,LT_SPACE,1,0);
        live_test_add(t+300,LT_SEEK,0,0);
        live_test_add(t+400,LT_CLOSE,0,0);
        live_test_add(t+12000,LT_CHECK,1+p*10,0);
        live_test_add(t+13000,LT_FX,0,0);
        live_test_add(t+14000,LT_SEEK,0,.5f);
        live_test_add(t+26000,LT_CHECK,2+p*10,0);
        live_test_add(t+27000,LT_SEEK,0,.9f);
        live_test_add(t+39000,LT_CHECK,3+p*10,0);
        if(live_test.movement_markers[p]) {
            live_test_add(t+40000,LT_SEEK,1,.5f);
            live_test_add(t+50000,LT_CHECK,4+p*10,0);
            t+=11000;
        }
        live_test_add(t+40000,LT_SPACE,0,0);
        live_test_add(t+48000,LT_ASSERT,1,0);
        live_test_add(t+49000,LT_BACKSPACE,0,0);
        live_test_add(t+57000,LT_ASSERT,2,0);
        live_test_add(t+58000,LT_CLOSE,0,0);
        live_test_add(t+66000,LT_ASSERT,2,0);
        live_test_add(t+66050,LT_BACKSPACE,0,0);
        live_test_add(t+66100,LT_BACKSPACE,1,0);
        live_test_add(t+66200,LT_ASSERT,3,0);
        t+=67000;
    }
    live_test_add(t,LT_END,0,0);live_test.duration_ms=t;
}
static void live_test_gallery_inventory(const TsSamplePages *pages,const TsMatrixControls *controls,int p)
{
    const TsMosaicCanvas *c=&pages->gallery->canvas[p];
    const TsMosaic *m=c->scene;int samples=0,patterns=0,live=0,video=0,missing=0,effects=0;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id) {
        const TsMosaicEvent *e=&m->events[i];
        if(e->live)++live;
        if(!e->source)++missing;
        else if(e->source->video_path[0])++video;
        else if(e->source->pattern)++patterns;
        else ++samples;
    }
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(controls->effect[i].type)++effects;
    ts_session_log_event("live_test_movement",
        "movement=%d id=%llu cards=%d samples=%d patterns=%d live=%d video=%d missing_sources=%d markers=%d length_s=%.6f speed=%.6f matrix_enabled=%d project_effects=%d playback=%s name=%s",
        p,(unsigned long long)c->id,live_test.movement_cards[p],samples,patterns,live,video,missing,
        live_test.movement_markers[p],live_test.movement_end[p],m->speed,controls->enabled,effects,
        live_test.movement_end[p]>0?(live_test.mode==4?(live_test.movement_end[p]>=.001?"full_and_stress":"unsupported_submillisecond"):"sampled_loop"):"skipped_empty",c->name);
}
