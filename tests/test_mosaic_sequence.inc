static TsMosaic *sequence_fixture(void)
{
    TsMosaic *m=live_fixture();
    assert(ts_mosaic_copy(m,m->events[0].id,5,150));assert(ts_mosaic_copy(m,m->events[0].id,2,300));
    assert(ts_mosaic_copy(m,m->events[0].id,0,450));m->events[3].live_order=-1;
    m->events[0].duration=.013;m->events[1].duration=.017;m->events[2].duration=.023;
    return m;
}
static void test_mosaic_sequence(void)
{
    TsMosaic *m=sequence_fixture();TsMosaicSequence *s=&m->sequence;
    assert(ts_mosaic_live_launch(m,m->events[3].id,0));live_render(m,20);
    uint64_t background=m->voices[3].generation;
    assert(ts_mosaic_sequence_cycle(m) && s->count==3);live_render(m,14);assert(s->index==1);
    double elapsed=m->live[1].elapsed;ts_mosaic_sequence_finish(m);assert(s->finishing);
    assert(ts_mosaic_sequence_cycle(m) && !s->finishing && m->live[1].elapsed==elapsed);
    /* Finishing is idempotent, continues from this step, and never starts idle. */
    ts_mosaic_sequence_finish(m);ts_mosaic_sequence_finish(m);live_render(m,40);
    assert(!s->running && m->live[3].state && m->voices[3].generation==background && m->time==0);
    ts_mosaic_sequence_finish(m);assert(!s->running);
    /* Explicit order and background membership; manual/timeline launch is
       idempotent for an owned step and cannot layer up the queued steps. */
    m->events[2].live_order=1;m->events[0].live_order=2;
    assert(ts_mosaic_sequence_cycle(m));live_render(m,4);assert(s->slots[0]==2);
    uint64_t generation=m->voices[2].generation;
    m->events[2].live_transport=m->events[2].live_position=1;
    ts_mosaic_live_transport(m,0);assert(s->running && m->live[2].state==TS_MOSAIC_LIVE_PLAYING);
    assert(ts_mosaic_live_launch(m,m->events[1].id,0));assert(!m->live[1].state);
    ts_mosaic_seek(m,0);m->playing=1;live_render(m,2);assert(m->voices[2].generation==generation);
    m->playing=0;ts_mosaic_sequence_stop(m);live_render(m,10);
    assert(!s->running && !m->live[2].state && m->live[3].state && m->voices[3].generation==background);
    /* Removing queued cards cannot leave a stale source pointer or hang. */
    ts_mosaic_sequence_cycle(m);live_render(m,1);ts_mosaic_delete(m,m->events[2].id);live_render(m,1);
    assert(s->running && s->index==1);
    ts_mosaic_delete(m,m->events[0].id);ts_mosaic_delete(m,m->events[1].id);live_render(m,1);assert(!s->running);
    assert(m->live[3].state);ts_mosaic_free(m);

    m=sequence_fixture();s=&m->sequence;
    m->events[0].duration=.0105;m->events[1].duration=.01325;m->events[2].duration=.007125;
    assert(ts_mosaic_sequence_cycle(m));int passes=0,previous=0;
    for(int frame=0;frame<=61750;++frame) {
        live_render(m,1);
        if(previous==2 && s->index==0){++passes;assert(fabs(frame/1000.-passes*.030875)<=.0010001);}
        previous=s->index;
    }
    assert(passes==2000);ts_mosaic_free(m);
    m=sequence_fixture();for(int i=0;i<3;++i)m->events[i].duration=.00001;
    assert(ts_mosaic_sequence_cycle(m));live_render(m,1000);assert(m->sequence.running);ts_mosaic_free(m);

    m=sequence_fixture();s=&m->sequence;ts_mosaic_live_clock(m,1,0,.5,.5);m->live_capture.quantize=1;
    assert(ts_mosaic_sequence_cycle(m));live_render(m,240);assert(m->live[0].state==TS_MOSAIC_LIVE_WAITING);
    live_render(m,20);assert(m->live[0].state==TS_MOSAIC_LIVE_PLAYING);
    live_render(m,100);assert(s->running && m->live[s->slots[s->index]].state==TS_MOSAIC_LIVE_PLAYING);
    /* Per-card tape tempo changes duration without moving the main playhead;
       the next free step receives fractional real-time carry at its own rate. */
    ts_mosaic_sequence_stop(m);live_render(m,10);m->live_capture.quantize=0;
    m->events[0].live_tempo=1;ts_mosaic_live_clock(m,1,0,.25,m->live_phase);
    ts_mosaic_sequence_cycle(m);live_render(m,8);assert(s->index==1 && fabs(m->live[1].elapsed-.0015)<1e-9);
    double before=m->live[1].elapsed;ts_mosaic_begin_block(m,1);ts_mosaic_read(m,2000);
    assert(s->index==1 && fabs(m->live[1].elapsed-before-.0005)<1e-9);
    ts_mosaic_live_stop(m,0);assert(!s->running);ts_mosaic_free(m);
}
static void test_mosaic_finish(void)
{
    TsMosaic *m=live_fixture();TsMosaicEvent *e=&m->events[0];
    for(size_t i=0;i<e->source->sample.frames;++i)e->source->sample.data[i]=.5f;
    e->fade_in=.5;e->finish_fade=.1;ts_mosaic_live_launch(m,e->id,0);live_render(m,100);
    float previous=ts_mosaic_read(m,1000).l;ts_mosaic_live_finish(m,e->id);
    for(int i=0;i<100;++i){float value=ts_mosaic_read(m,1000).l;assert(value<=previous+1e-6f);previous=value;}
    assert(!m->live[0].state && !e->live_finite && e->fade_in==.5);
    ts_mosaic_live_launch(m,e->id,0);live_render(m,100);ts_mosaic_live_finish(m,e->id);live_render(m,50);
    ts_mosaic_live_stop(m,e->id);live_render(m,10);assert(!m->live[0].state);
    ts_mosaic_free(m);
}
static void test_mosaic_sequence_storage(void)
{
    TsMosaic *m=sequence_fixture(),*loaded=ts_mosaic_create();char error[160],header[80];
    m->events[1].live_order=1;m->events[0].finish_fade=2;
    MKDIR("mosaic-sequence-storage");assert(ts_mosaic_sequence_cycle(m));live_render(m,1);
    uint64_t hash=ts_mosaic_hash(m);assert(ts_mosaic_save(m,"mosaic-sequence-storage",error,sizeof(error)));
    FILE *f=fopen("mosaic-sequence-storage/mosaic.tsm","rb");assert(f && fgets(header,sizeof(header),f));fclose(f);
    assert(!strcmp(header,"TAPESISTER_MOSAIC 16\n"));storage_portable_load(loaded,"mosaic-sequence-storage");
    assert(ts_mosaic_hash(loaded)==hash && !loaded->sequence.running && !ts_mosaic_live_count(loaded));
    assert(loaded->events[0].finish_fade==2 && loaded->events[1].live_order==1 && loaded->events[3].live_order==-1);
    m->events[0].finish_fade=NAN;assert(!ts_mosaic_save(m,"mosaic-sequence-storage",error,sizeof(error)));
    storage_portable_load(loaded,"mosaic-sequence-storage");assert(ts_mosaic_hash(loaded)==hash);
    char contents[16384];f=fopen("mosaic-sequence-storage/mosaic.tsm","rb");assert(f);
    size_t bytes=fread(contents,1,sizeof(contents)-1,f);fclose(f);contents[bytes]=0;
    char *bad=strstr(contents,"STEP -1 0");assert(bad);bad[6]='2';
    f=fopen("mosaic-sequence-storage/mosaic.tsm","wb");assert(f && fwrite(contents,1,bytes,f)==bytes);fclose(f);
    assert(!ts_mosaic_load(loaded,"mosaic-sequence-storage",error,sizeof(error)) && ts_mosaic_hash(loaded)==hash);
    ts_mosaic_free(m);ts_mosaic_free(loaded);
    remove("mosaic-sequence-storage/mosaic.tsm");remove("mosaic-sequence-storage/mosaic-000.wav");RMDIR("mosaic-sequence-storage");
}
