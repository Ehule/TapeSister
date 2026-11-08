static void project_view_storage(void)
{
    TsSisterProjectState *state=malloc(sizeof(*state)),*loaded=malloc(sizeof(*loaded));assert(state && loaded);
    const char *path="project-view-test.ini";char error[160];int present;
    ts_sister_project_state_init(state,48000);
    state->view=(TsProjectView){.present=1,.workspace=TS_PROJECT_VIEW_MOSAIC,
        .scroll=15.75,.xscroll=80.5,.scale=.000332,.hscale=TS_MOSAIC_MIN_HSCALE,
        .time=543.125,.follow=1,.mosaic_seconds_only=1,.source_filter=TS_MOSAIC_BROWSER_EVENTS,.source_page=4,.parent_first=1,.parent_last=333,
        .selection_count=3,.selection={9,3,8},.selected=3};
    assert(ts_sister_project_state_save_file(state,path,error,sizeof(error)));
    assert(ts_sister_project_state_load_file(loaded,path,48000,&present,error,sizeof(error)) && present);
    assert(loaded->view.present && loaded->view.workspace==TS_PROJECT_VIEW_MOSAIC);
    assert(loaded->view.scroll==15.75 && loaded->view.scale==.000332 && loaded->view.hscale==TS_MOSAIC_MIN_HSCALE);
    assert(loaded->view.time==543.125 && loaded->view.selection_count==3 && loaded->view.selected==3);
    assert(loaded->view.mosaic_seconds_only==1);
    assert(!memcmp(loaded->view.selection,state->view.selection,3*sizeof(uint64_t)));
    assert(loaded->view.version==2 && loaded->view.source_filter==TS_MOSAIC_BROWSER_EVENTS && loaded->view.source_page==4);
    /* Switching back omits the optional field and reopens in M:SS mode. */
    state->view.mosaic_seconds_only=0;
    assert(ts_sister_project_state_save_file(state,path,error,sizeof(error)));
    assert(ts_sister_project_state_load_file(loaded,path,48000,&present,error,sizeof(error)) && !loaded->view.mosaic_seconds_only);
    state->view.mosaic_seconds_only=1;
    assert(ts_sister_project_state_save_file(state,path,error,sizeof(error)));
    assert(ts_sister_project_state_load_file(loaded,path,48000,&present,error,sizeof(error)) && loaded->view.mosaic_seconds_only);
    /* A malformed view must not replace an existing good sidecar. */
    state->view.mosaic_seconds_only=2;
    assert(!ts_sister_project_state_save_file(state,path,error,sizeof(error)));
    state->view.mosaic_seconds_only=1;
    state->view.scale=NAN;
    assert(!ts_sister_project_state_save_file(state,path,error,sizeof(error)));
    assert(ts_sister_project_state_load_file(loaded,path,48000,&present,error,sizeof(error)) && loaded->view.scroll==15.75);
    const char *bad[]={
        "ProjectView.Version=2\nProjectView.Workspace=1 0 0 0\nProjectView.Canvas=0 0\nProjectView.Mosaic=0 0 24 1 0 0 0 0 0\nProjectView.Tracker=0 0 0\nProjectView.MosaicSeconds=2\n",
        "ProjectView.Version=2\nProjectView.Workspace=1 0 0 0\nProjectView.Canvas=0 0\nProjectView.Mosaic=0 0 24 1 0 0 0 0 0\nProjectView.Tracker=0 0 0\nProjectView.MosaicSeconds=1\nProjectView.MosaicSeconds=1\n",
        "ProjectView.Version=3\n", /* unknown schema */
        "ProjectView.Version=1\n", /* incomplete */
        "ProjectView.Version=1\nProjectView.Workspace=1 0 0 0\nProjectView.Canvas=0 0\nProjectView.Mosaic=nan 0 24 1 0 0 0 0 0\nProjectView.Tracker=0 0 0\n",
        "ProjectView.Version=1\nProjectView.Workspace=1 0 0 0\nProjectView.Canvas=0 0\nProjectView.Mosaic=0 0 24 0 0 0 0 0 0\nProjectView.Tracker=0 0 0\n",
        "ProjectView.Version=1\nProjectView.Workspace=1 0 0 0\nProjectView.Canvas=0 0\nProjectView.Mosaic=0 0 24 1 0 0 0 0 9\nProjectView.Tracker=0 0 0\n", /* dangling primary */
        "ProjectView.Version=1\nProjectView.Workspace=1 0 0 0\nProjectView.Canvas=0 0\nProjectView.Mosaic=0 0 24 1 0 0 0 0 9\nProjectView.Tracker=0 0 0\nProjectView.Card=9\nProjectView.Card=9\n",
        "ProjectView.Version=1\nProjectView.Workspace=1 0 0 0\nProjectView.Canvas=0 0\nProjectView.Mosaic=0 0 24 1 0 0 0 0 0\nProjectView.Tracker=0 0 0 trailing\n",
        "ProjectView.Version=1\nProjectView.Workspace=1 0 0 0\nProjectView.Canvas=0 0\nProjectView.Mosaic=0 0 24 1 0 0 0 0 0\nProjectView.Tracker=0 0 0\nProjectView.Version=1\n"
    };
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i) {
        FILE *f=fopen(path,"wb");assert(f);
        fputs("TapeSister Sister Project State\nVersion=44\nPageCount=1\n",f);fputs(bad[i],f);assert(!fclose(f));
        assert(!ts_sister_project_state_load_file(loaded,path,48000,&present,error,sizeof(error)));
        assert(loaded->view.scroll==15.75 && loaded->view.selected==3);
    }
    FILE *f=fopen(path,"wb");assert(f);
    fputs("TapeSister Sister Project State\nVersion=43\nPageCount=1\n",f);assert(!fclose(f));
    assert(ts_sister_project_state_load_file(loaded,path,48000,&present,error,sizeof(error)) && present && !loaded->view.present);
    /* Both old browser modes still parse with their original page values. */
    for(int filter=0;filter<2;++filter) {
        f=fopen(path,"wb");assert(f);
        fprintf(f,"TapeSister Sister Project State\nVersion=44\nPageCount=1\nProjectView.Version=1\nProjectView.Workspace=1 0 0 0\nProjectView.Canvas=0 0\nProjectView.Mosaic=0 0 24 1 0 0 %d 7 0\nProjectView.Tracker=0 0 0\n",filter);assert(!fclose(f));
        assert(ts_sister_project_state_load_file(loaded,path,48000,&present,error,sizeof(error)));
        assert(loaded->view.mosaic_seconds_only==0);
        assert(loaded->view.version==1 && loaded->view.source_filter==filter && loaded->view.source_page==7);
    }
    assert(!remove(path));free(state);free(loaded);
}
