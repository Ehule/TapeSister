static void test_marker_storage(void)
{
    TsMosaic *m=fixture(),*loaded=ts_mosaic_create();assert(loaded);
    const char *dir="mosaic-marker-test";MKDIR(dir);char error[160];
    uint64_t before=ts_mosaic_hash(m);int history=m->history_count;
    TsMosaicMarker value={.stored=1,.time=1440.123456789};strcpy(value.name,"A pale weight / low E");
    assert(ts_mosaic_set_marker(m,15,&value));assert(m->history_count==history+1 && ts_mosaic_hash(m)!=before);
    assert(ts_mosaic_undo(m,0) && !m->markers[15].stored && ts_mosaic_hash(m)==before);
    assert(ts_mosaic_undo(m,1) && m->markers[15].time==value.time);
    history=m->history_count;assert(ts_mosaic_set_marker(m,15,&value));assert(m->history_count==history);
    value.time=NAN;assert(!ts_mosaic_set_marker(m,0,&value));value.time=-1;assert(!ts_mosaic_set_marker(m,0,&value));
    value.time=1000001;assert(!ts_mosaic_set_marker(m,0,&value));value.time=0;value.name[0]='\n';assert(!ts_mosaic_set_marker(m,0,&value));
    assert(!ts_mosaic_set_marker(m,16,&m->markers[15]));
    m->marker_loop=loaded->marker_loop=1;m->marker_active=16;loaded->marker_active=2;
    assert(ts_mosaic_save(m,dir,error,sizeof(error)));storage_portable_load(loaded,dir);
    assert(!loaded->marker_loop && !loaded->marker_active);
    assert(ts_mosaic_hash(m)==ts_mosaic_hash(loaded) && loaded->markers[15].time==m->markers[15].time);
    assert(!strcmp(loaded->markers[15].name,m->markers[15].name));
    /* Bad metadata cannot partially replace a live document. */
    const char *invalid[]={"MARKER 1 nan 00\n","MARKER 1 1 zz\n","MARKER 2 1 00\n",
        "MARKER 1 -1 0000000000000000000000000000000000000000000000000000000000000000\n",
        "MARKER 1 1 0a00000000000000000000000000000000000000000000000000000000000000\n",
        "MARKER 1 1 4141414141414141414141414141414141414141414141414141414141414141\n"};
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        FILE *in=fopen("mosaic-marker-test/mosaic.tsm","r"),*out=fopen("mosaic-marker-test/edit.tsm","w");assert(in && out);
        char line[1024];int replaced=0;
        while(fgets(line,sizeof(line),in)) {
            if(!replaced && !strncmp(line,"MARKER ",7)){fputs(invalid[i],out);replaced=1;}else fputs(line,out);
        }
        fclose(in);fclose(out);remove("mosaic-marker-test/mosaic.tsm");assert(!rename("mosaic-marker-test/edit.tsm","mosaic-marker-test/mosaic.tsm"));
        before=ts_mosaic_hash(loaded);assert(!ts_mosaic_load(loaded,dir,error,sizeof(error)));assert(ts_mosaic_hash(loaded)==before);
        assert(ts_mosaic_save(m,dir,error,sizeof(error)));
    }
    /* Formats 12/13 retain views and loop ranges and start with empty cues. */
    m->views[2]=(TsMosaicView){1,3,4,5,1};ts_mosaic_set_loop_range(m,1,4);
    for(int version=12;version<=13;++version) {
        assert(ts_mosaic_save(m,dir,error,sizeof(error)));
        FILE *in=fopen("mosaic-marker-test/mosaic.tsm","r"),*out=fopen("mosaic-marker-test/edit.tsm","w");assert(in && out);
        char line[1024];assert(fgets(line,sizeof(line),in));fprintf(out,"TAPESISTER_MOSAIC %d\n",version);
        while(fgets(line,sizeof(line),in)) {
            if(!strncmp(line,"MARKER ",7) || (version==12 && !strncmp(line,"PATTERN ",8)))continue;
            fputs(line,out);
        }
        fclose(in);fclose(out);remove("mosaic-marker-test/mosaic.tsm");assert(!rename("mosaic-marker-test/edit.tsm","mosaic-marker-test/mosaic.tsm"));
        storage_portable_load(loaded,dir);
        for(int i=0;i<TS_MOSAIC_MARKERS;++i)assert(!loaded->markers[i].stored);
        assert(loaded->views[2].stored && loaded->views[2].scroll==3 && loaded->loop_range.end==4);
    }
    remove("mosaic-marker-test/mosaic.tsm");remove("mosaic-marker-test/mosaic-000.wav");RMDIR(dir);
    ts_mosaic_free(loaded);ts_mosaic_free(m);
}
