static void test_mosaic_video(void)
{
    TsMosaic *m=ts_mosaic_create();assert(m);char error[160];
    const char *path="/a folder/dogs playing.mp4";
    TsMosaicSource *source=ts_mosaic_video_source(m,path,0);assert(source && !source->sample.data);
    assert(ts_mosaic_video_source(m,path,0)==source);
    TsMosaicEvent *a=ts_mosaic_add(m,source,0,0);assert(a);a->video_random_start=1;a->video_transition=1;a->video_fade=2.75;a->video_bw=1;
    TsMosaicEvent *b=ts_mosaic_copy(m,a->id,0,120);assert(b);
    assert(ts_mosaic_video_overridden(m,a) && !ts_mosaic_video_overridden(m,b));
    assert(ts_mosaic_video_at(m,0)==b);
    b->muted=1;assert(ts_mosaic_video_at(m,0)==a && !ts_mosaic_video_overridden(m,a));b->muted=0;
    ts_mosaic_checkpoint(m);ts_mosaic_delete(m,b->id);assert(ts_mosaic_video_at(m,0)==a);
    assert(ts_mosaic_undo(m,0));b=&m->events[1];assert(ts_mosaic_video_at(m,0)==b);
    m->playing=1;ts_mosaic_read(m,48000);assert(m->video_id==b->id && !m->schedule.active_count);
    for(int i=0;i<300;++i){TsStereoFrame v=ts_mosaic_read(m,48000);assert(v.l==0 && v.r==0);}
    uint64_t serial=m->video_serial;ts_mosaic_seek(m,0);assert(m->video_serial==serial+1);ts_mosaic_read(m,48000);assert(m->video_serial==serial+1);
    assert(!ts_mosaic_live_launch(m,a->id,0));
    assert(!MKDIR("mosaic-video-test"));assert(ts_mosaic_save(m,"mosaic-video-test",error,sizeof(error)));
    TsMosaic *loaded=ts_mosaic_create();assert(loaded);assert(ts_mosaic_load(loaded,"mosaic-video-test",error,sizeof(error)));
    assert(ts_mosaic_hash(m)==ts_mosaic_hash(loaded));
    assert(loaded->events[0].video_random_start && loaded->events[0].video_bw && loaded->events[0].video_fade==2.75);
    assert(!strcmp(loaded->events[0].source->video_path,path));assert(!loaded->events[0].source->sample.data);
    storage_line_endings("mosaic-video-test/mosaic.tsm",1);assert(ts_mosaic_load(loaded,"mosaic-video-test",error,sizeof(error)));
    assert(ts_mosaic_hash(m)==ts_mosaic_hash(loaded));
    remove("mosaic-video-test/mosaic.tsm");assert(!RMDIR("mosaic-video-test"));ts_mosaic_free(loaded);ts_mosaic_free(m);
    /* Video cards and their solo flags do not affect a single output sample. */
    m=fixture();TsMosaic *reference=fixture();source=ts_mosaic_video_source(m,path,0);a=ts_mosaic_add(m,source,.01,0);assert(a);a->solo=1;
    for(int i=0;i<10000;++i){TsStereoFrame v=ts_mosaic_read(m,48000),w=ts_mosaic_read(reference,48000);assert(v.l==w.l && v.r==w.r);}
    assert(m->video_id==a->id);ts_mosaic_free(m);ts_mosaic_free(reference);
}
