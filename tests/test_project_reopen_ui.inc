/* Exercise the native save/open controller, including an overwrite after
   editing a dense arrangement. File preservation alone is not a round trip. */
static int project_save_completed(void)
{
    return strstr(ui.status,"SAVED TSR PROJECT") || strstr(ui.status,"PROJECT SAVED; SESSION EXPORT UNAVAILABLE");
}
static int project_same_saved_file(const char *a,const char *b)
{
#ifdef _WIN32
    /* The browser emits native separators; the saver also accepts '/'. File
       identity verifies the destination instead of comparing path spelling. */
    HANDLE first=CreateFileA(a,0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,0,NULL);
    HANDLE second=CreateFileA(b,0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,0,NULL);
    BY_HANDLE_FILE_INFORMATION x,y;
    int same=first!=INVALID_HANDLE_VALUE && second!=INVALID_HANDLE_VALUE &&
        GetFileInformationByHandle(first,&x) && GetFileInformationByHandle(second,&y) &&
        x.dwVolumeSerialNumber==y.dwVolumeSerialNumber && x.nFileIndexHigh==y.nFileIndexHigh && x.nFileIndexLow==y.nFileIndexLow;
    if(first!=INVALID_HANDLE_VALUE)CloseHandle(first);
    if(second!=INVALID_HANDLE_VALUE)CloseHandle(second);
    return same;
#else
    struct stat x,y;
    return !stat(a,&x) && !stat(b,&y) && x.st_dev==y.st_dev && x.st_ino==y.st_ino;
#endif
}
static void project_legacy_reopen_controls(void)
{
    char original[96],saved[TS_BROWSER_PATH_MAX],selected[TS_BROWSER_PATH_MAX];
    snprintf(original,sizeof(original),"Legacy saved %llu.tsr",(unsigned long long)SDL_GetPerformanceCounter());
    assert(ts_instrument_save_recipe(&bank,original,error,sizeof(error)));
    audio.sister.clock_controls.enabled=1;audio.sister.clock_controls.tempo_mask=3;
    audio.sister.clock_controls.position_custom=1;audio.sister.clock_controls.home_mask=2;
    audio.sister.master_clock=(TsMasterClock){.running=1,.seconds=43.25};
    assert(load_instrument(0,&audio,&ui,&bank,&pages,&record,0,original));
    assert(!audio.sister.clock_controls.enabled && !audio.sister.clock_controls.tempo_mask &&
        !audio.sister.clock_controls.position_custom && !audio.sister.master_clock.running && !audio.sister.master_clock.seconds);
    uint64_t old_audio=ts_sample_hash(&bank.current);
    TsMosaicSource *source=ts_mosaic_source(ui.mosaic,&bank.current,error,sizeof(error));assert(source);
    TsMosaicEvent *card=ts_mosaic_add(ui.mosaic,source,3,50);assert(card);
    card->muted=card->solo=1;card->gain=.37f;ui.mosaic_open=1;
    uint64_t expected=ts_mosaic_hash(ui.mosaic);
    snprintf(ui.browser.directory,sizeof(ui.browser.directory),".");browser_open(&ui,TS_BROWSER_LOAD_WAV);
    for(int i=0;i<ui.browser.entry_count;++i)if(!strcmp(ui.browser.entries[i].name,original))ts_browser_select(&ui.browser,i);
    ts_browser_close(&ui.browser);begin_active_project_save(&ui);
    assert(ui.project_overwrite_confirm_open && ui.project_save_new_bundle);
    queue_active_project_save(&ui,&file);
    run_pending_file_operation(0,&audio,&ui,&bank,&selection,&importer,&pages,&record,0,&file);
    assert(project_save_completed() && strcmp(ui.project_path,original));
    snprintf(saved,sizeof(saved),"%s",ui.project_path);
    browser_open(&ui,TS_BROWSER_LOAD_WAV);
    assert(ts_browser_selected_path(&ui.browser,selected,sizeof(selected)) && project_same_saved_file(selected,saved));
    browser_action(0,&audio,&ui,&bank,&selection,&pages,&record,0,&file,0);
    assert(file.active && project_same_saved_file(file.path,saved));
    run_pending_file_operation(0,&audio,&ui,&bank,&selection,&importer,&pages,&record,0,&file);
    mosaic_poll(0,&ui,&bank,&mosaic);
    assert(ts_mosaic_hash(ui.mosaic)==expected && ui.mosaic_open);
    /* Migration leaves the original flat TSR intact and independently readable. */
    TsInstrument *old=malloc(sizeof(*old));assert(old);ts_instrument_init(old);
    assert(ts_instrument_load_recipe(old,original,error,sizeof(error)) && ts_sample_hash(&old->current)==old_audio);
    ts_instrument_free(old);free(old);
    begin_active_project_save(&ui);assert(ui.project_overwrite_confirm_open && !ui.project_save_new_bundle);
    ui.project_overwrite_confirm_open=0;
}
static void project_reopen_controls(SDL_Window *window)
{
    char requested[96],path[TS_BROWSER_PATH_MAX],extra[TS_BROWSER_PATH_MAX];
    snprintf(requested,sizeof(requested),"reopen-ui-%llu.tsr",(unsigned long long)SDL_GetPerformanceCounter());
    run_save(requested);
    assert(project_save_completed());
    snprintf(path,sizeof(path),"%s",ui.project_path);
    const char *slash=strrchr(path,'/');assert(slash);
    snprintf(extra,sizeof(extra),"%.*s/notes.txt",(int)(slash-path),path);
    FILE *f=fopen(extra,"wb");assert(f);fputs("keep my notes\n",f);assert(!fclose(f));
    snprintf(extra,sizeof(extra),"%.*s/extras",(int)(slash-path),path);
#ifdef _WIN32
    assert(!_mkdir(extra));
#else
    assert(!mkdir(extra,0700));
#endif
    snprintf(extra,sizeof(extra),"%.*s/extras/keep.txt",(int)(slash-path),path);
    f=fopen(extra,"wb");assert(f);fputs("keep this too\n",f);assert(!fclose(f));
    while(!ui.mosaic->events[95].id)
        assert(ts_mosaic_copy(ui.mosaic,ui.mosaic->events[0].id,1,100));
    ui.mosaic_open=1;ui.tracker_open=0;
    mosaic_poll(0,&ui,&bank,&mosaic);
    SDL_Event key={.type=SDL_KEYDOWN};key.key.windowID=SDL_GetWindowID(window);
    mosaic_select_only(&ui,ui.mosaic->events[0].id);key.key.keysym.sym=SDLK_m;
    assert(mosaic_event(&key,window,0,&audio,&ui,&bank,&mosaic,&portal,&sister,&transform,48000));
    mosaic_select_only(&ui,ui.mosaic->events[1].id);key.key.keysym.sym=SDLK_s;
    assert(mosaic_event(&key,window,0,&audio,&ui,&bank,&mosaic,&portal,&sister,&transform,48000));
    assert(ui.mosaic->events[0].muted && ui.mosaic->events[1].solo);
    TsMosaicEvent *card=&ui.mosaic->events[2];
    card->start=17.25;card->duration=9.5;card->x=42.125;card->width=160;
    card->gain=.31f;card->pan=-.25f;card->fade_in=.125;card->fade_out=.25;
    snprintf(card->name,sizeof(card->name),"edited card");
    mosaic_select_add(&ui,ui.mosaic->events[2].id);
    ui.mosaic_scroll=15.75;ui.mosaic_xscroll=80.5;ui.mosaic_scale=12.25;ui.mosaic_hscale=.5;
    ui.mosaic_follow=1;ui.mosaic_source_page=1;
    ts_mosaic_seek(ui.mosaic,17.375);
    audio.sister.clock_controls=(TsMasterClockControls){.enabled=1,.bpm=137,.tempo_mask=3,
        .arp_division=2,.position_custom=1,.home_mask=2};
    audio.sister.master_clock=(TsMasterClock){.running=1,.seconds=43.25,.beats=98.75,.phase=.75};
    uint64_t primary=ui.mosaic_selected;
    uint64_t expected=ts_mosaic_hash(ui.mosaic);
    ts_browser_close(&ui.browser);ui.file_busy=0;
    queue_active_project_save(&ui,&file);assert(file.active && !strcmp(file.path,path));
    run_pending_file_operation(0,&audio,&ui,&bank,&selection,&importer,&pages,&record,0,&file);
    assert(project_save_completed() && ts_mosaic_hash(ui.mosaic)==expected);
    ui.mosaic->events[0].muted=0;ui.mosaic->events[1].solo=0;card->start=1;
    ui.mosaic_open=0;ui.mosaic_scroll=ui.mosaic_xscroll=0;ui.mosaic_scale=24;ui.mosaic_hscale=1;
    file=(PendingFileOperation){.active=1,.mode=TS_BROWSER_LOAD_WAV};
    snprintf(file.path,sizeof(file.path),"%s",path);
    run_pending_file_operation(0,&audio,&ui,&bank,&selection,&importer,&pages,&record,0,&file);
    assert(strstr(ui.status,"OPENED TSR PROJECT"));
    mosaic_poll(0,&ui,&bank,&mosaic);
    assert(ts_mosaic_hash(ui.mosaic)==expected);
    assert(ui.mosaic->events[0].muted && ui.mosaic->events[1].solo);
    assert(ui.mosaic->events[2].start==17.25 && !strcmp(ui.mosaic->events[2].name,"edited card"));
    snprintf(extra,sizeof(extra),"%.*s/notes.txt",(int)(slash-path),path);
    f=fopen(extra,"rb");assert(f);char text[32]={0};assert(fgets(text,sizeof(text),f));assert(!strcmp(text,"keep my notes\n"));fclose(f);
    snprintf(extra,sizeof(extra),"%.*s/extras/keep.txt",(int)(slash-path),path);
    f=fopen(extra,"rb");assert(f);fclose(f);
    fprintf(stderr,"Native overwrite/open retained 96 cards, mute/solo, card edits and extra files\n");
    assert(ui.mosaic_open && ui.mosaic_scroll==15.75 && ui.mosaic_xscroll==80.5);
    assert(ui.mosaic_scale==12.25 && ui.mosaic_hscale==.5);
    assert(ui.mosaic_selected==primary && mosaic_chosen(&ui,1) && mosaic_chosen(&ui,2));
    assert(ui.mosaic_follow && ui.mosaic_source_page==1 && ui.mosaic->time==17.375 && !ui.mosaic->playing);
    assert(audio.sister.clock_controls.enabled && audio.sister.clock_controls.bpm==137 &&
        audio.sister.clock_controls.tempo_mask==3 && audio.sister.clock_controls.home_mask==2);
    assert(!audio.sister.master_clock.running && audio.sister.master_clock.seconds==43.25 &&
        audio.sister.master_clock.beats==98.75 && audio.sister.master_clock.phase==.75);
    /* Reopen from an empty session, not just over the already populated UI. */
    assert(project_new(NULL,0,0,&session));assert_empty();
    assert(load_instrument(0,&audio,&ui,&bank,&pages,&record,0,path));
    mosaic_poll(0,&ui,&bank,&mosaic);
    assert(ts_mosaic_hash(ui.mosaic)==expected && ui.mosaic_open && ui.mosaic_selected==primary);
    assert(ui.mosaic_scroll==15.75 && ui.mosaic_xscroll==80.5 && ui.mosaic_scale==12.25 && ui.mosaic_hscale==.5);
    assert(ui.mosaic->time==17.375 && !ui.mosaic->playing);
    assert(audio.sister.clock_controls.bpm==137 && audio.sister.clock_controls.home_mask==2 &&
        !audio.sister.master_clock.running && audio.sister.master_clock.beats==98.75);
    /* Main canvas range/panel and the TrackSister workspace are project views too. */
    for(int workspace=0;workspace<2;++workspace) {
        ui.mosaic_open=0;ui.tracker_open=workspace;
        ui.show_keyboard=0;ui.show_ingredients=1;ui.show_recipes=0;
        ui.parent_view_first=12;ui.parent_view_last=333;
        ui.tracker_scroll=7;ui.tracker_expanded=1;ui.tracker_field=3;
        snprintf(requested,sizeof(requested),"reopen-view-%d-%llu.tsr",workspace,(unsigned long long)SDL_GetPerformanceCounter());
        run_save(requested);assert(project_save_completed());
        snprintf(path,sizeof(path),"%s",ui.project_path);
        assert(project_new(NULL,0,0,&session));assert_empty();
        assert(load_instrument(0,&audio,&ui,&bank,&pages,&record,0,path));
        if(workspace)tracker_refresh(0,&audio,&ui,&pages,&bank,48000,&sister);
        assert(!ui.mosaic_open && ui.tracker_open==workspace && ui.show_ingredients && !ui.show_keyboard);
        assert(ui.parent_view_first==12 && ui.parent_view_last==333);
        assert(ui.tracker_scroll==7 && ui.tracker_expanded && ui.tracker_field==3);
    }
    /* Invalid editor state is rejected before publishing any project data. */
    snprintf(extra,sizeof(extra),"%.*s/sister-state.ini",(int)(strrchr(path,'/')-path),path);
    f=fopen(extra,"ab");assert(f);fputs("ProjectView.Version=2\n",f);assert(!fclose(f));
    uint64_t before=project_hash(),epoch=ui.mosaic->epoch;
    assert(!load_instrument(0,&audio,&ui,&bank,&pages,&record,0,path));
    assert(project_hash()==before && ui.mosaic->epoch==epoch && ui.tracker_open);
    assert(ui.parent_view_first==12 && ui.parent_view_last==333);
}
