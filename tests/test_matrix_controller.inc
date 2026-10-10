static void matrix_click(int x,int y,int button)
{
    SDL_Event event={0};event.type=SDL_MOUSEBUTTONDOWN;event.button.windowID=matrix_window.id;
    event.button.x=x;event.button.y=y;event.button.button=button;
    assert(matrix_event(&event,window,0,&a,&u,&pages,&bank));
    event.type=SDL_MOUSEBUTTONUP;assert(matrix_event(&event,window,0,&a,&u,&pages,&bank));
}
static void matrix_shot(const char *directory,const char *name)
{
    if(!directory)return;
    matrix_window.outputs=a.output_device_channels;matrix_window.inputs=6;
    mx_rows(&pages,&bank);mx_paint(&a,&pages,&bank);
    char path[1024];snprintf(path,sizeof(path),"%s/%s.bmp",directory,name);
    SDL_Surface *s=SDL_CreateRGBSurfaceFrom(matrix_window.pixels,MATRIX_W,MATRIX_H,32,MATRIX_W*4,0xff0000,0xff00,0xff,0xff000000);
    assert(s&&!SDL_SaveBMP(s,path));SDL_FreeSurface(s);
}
static void matrix_controller(const char *directory)
{
    ts_tapehead_stop();ts_note_bank_clear(&a.notes);ts_performance_clear(&a.performance);ts_performance_clear(&a.tile_launchers);
    ts_sister_runtime_free(&a.sister);ts_sister_runtime_init(&a.sister);
    assert(ts_sister_runtime_reconfigure(&a.sister,48000,2,error,sizeof(error)));
    ts_sister_runtime_configure_limiter(&a.sister,0,-1,1,50);
    a.output_device_channels=4;TsMatrixControls c;a.mixer.program_gain=1;
    ts_matrix_default(&c);c.enabled=1;assert(ts_matrix_set(&a.sister.matrix,&c,48000));
    bank.bank[0].output_route=(TsSourceRoute){0};pages.tracker.lanes[0].output_route=(TsSourceRoute){0};
    ts_tapehead_routes_changed(bank.bank[0].tile_id,bank.bank[0].output_route);
    assert(matrix_show(0,&a,&u,&pages,&bank,0,-1));
    SDL_SetWindowSize(matrix_window.window,MATRIX_W,MATRIX_H);
    TsSourceRoute legacy={.mode=TS_SOURCE_PAIR,.second=1,.mix_enabled=1,.clean_level=75,.send_level={50,0,0}};
    matrix_window.legacy.master_mix=1;TsSourceRoute shown=mx_display_route(legacy);
    assert(shown.matrix_gain[TS_MATRIX_MAIN]==750 && !shown.matrix_gain[TS_MATRIX_NODES] && shown.matrix_gain[TS_MATRIX_PRISM]==500);
    legacy.mix_enabled=0;legacy.second=3;assert(mx_legacy_direct(legacy));
    matrix_window.legacy=a.sister.router.controls;
    /* The real checkbox edits the track, with no tile write and no transport stop. */
    matrix_click(MATRIX_X+8,MATRIX_Y+12,SDL_BUTTON_LEFT);
    matrix_click(MATRIX_X+8*MATRIX_CW+8,MATRIX_Y+12,SDL_BUTTON_LEFT);
    assert(pages.tracker.lanes[0].output_route.matrix_enabled && pages.tracker.lanes[0].output_route.matrix_gain[8]==1000);
    assert(!bank.bank[0].output_route.matrix_enabled);
    startPlaying(PLAYMODE_SONG,0);double energy[4];render(energy);render(energy);
    assert(energy[0]==0&&energy[1]==0&&energy[2]>1&&energy[3]>1);
    matrix_click(1044,744,SDL_BUTTON_LEFT);render(energy);render(energy);
    assert(energy[2]==0&&energy[3]>1);assert(ts_tapehead_scope_pan(0,128)==255);
    matrix_click(1100,744,SDL_BUTTON_LEFT);render(energy);render(energy);
    assert(pages.tracker.lanes[0].output_route.mode==TS_SOURCE_INHERIT);
    assert(energy[0]>1&&energy[1]>1&&energy[2]==0&&energy[3]==0);
    matrix_click(1100,744,SDL_BUTTON_LEFT);
    /* Track matrix routing survives the real embedded score codec. */
    matrix_click(MATRIX_X+8,MATRIX_Y+12,SDL_BUTTON_LEFT);
    matrix_click(MATRIX_X+8*MATRIX_CW+8,MATRIX_Y+12,SDL_BUTTON_LEFT);
    assert(ts_tapehead_export(&pages.tracker,error,sizeof(error)));
    assert(ts_sister_tracker_save_file(&pages.tracker,"matrix-score.tst",error,sizeof(error)));
    TsSisterTracker *copy=calloc(1,sizeof(*copy));assert(copy);ts_sister_tracker_init(copy);
    assert(ts_sister_tracker_load_file(copy,"matrix-score.tst",error,sizeof(error)));
    assert(copy->lanes[0].output_route.matrix_gain[8]==1000);ts_sister_tracker_free(copy);free(copy);remove("matrix-score.tst");
    ts_tapehead_stop();
    /* Tile keyboard voices use the same path, independent of track overrides. */
    assert(matrix_show(0,&a,&u,&pages,&bank,-1,0));
    matrix_click(MATRIX_X+8,MATRIX_Y+12,SDL_BUTTON_LEFT);
    matrix_click(MATRIX_X+8*MATRIX_CW+8,MATRIX_Y+12,SDL_BUTTON_LEFT);
    assert(ts_note_bank_start(&a.notes,&bank,TS_AUDITION_CURRENT,12,0,48000)==TS_NOTE_STARTED);
    render(energy);render(energy);assert(energy[0]==0&&energy[1]==0&&energy[2]>1&&energy[3]>1);
    ts_note_bank_clear(&a.notes);render(energy);render(energy);
    assert(ts_instrument_save_recipe(&bank,"matrix-tile.tsr",error,sizeof(error)));
    TsInstrument *tile_copy=malloc(sizeof(*tile_copy));assert(tile_copy);ts_instrument_init(tile_copy);
    assert(ts_instrument_load_recipe(tile_copy,"matrix-tile.tsr",error,sizeof(error)));
    assert(!memcmp(&bank.bank[0].output_route,&tile_copy->bank[0].output_route,sizeof(TsSourceRoute)));
    ts_instrument_free(tile_copy);free(tile_copy);remove("matrix-tile.tsr");
    /* Physical IN 5/6 -> EQ -> limiter -> OUT 1/2, no legacy Insert setup. */
    c=a.sister.matrix.controls;c.row[TS_MATRIX_NODES+2].matrix_gain[TS_MATRIX_EQ]=1000;
    c.row[TS_MATRIX_MAIN].matrix_gain[TS_MATRIX_EQ]=0;c.row[TS_MATRIX_MAIN].matrix_gain[TS_MATRIX_NODES+1]=1000;
    snprintf(c.input_name[0],TS_MATRIX_NAME,"Terra");snprintf(c.input_name[2],TS_MATRIX_NAME,"Vulture Return");snprintf(c.output_name[1],TS_MATRIX_NAME,"Vulture Send");
    assert(ts_matrix_set(&a.sister.matrix,&c,48000));
    static float input[2048*6],out[2048*4];for(int f=0;f<2048;++f){input[f*6+4]=.2f;input[f*6+5]=.3f;}
    ts_insert_duplex_block(&a.sister.insert,1,input,2048,6);audio_callback(&a,(Uint8*)out,sizeof(out));
    ts_insert_duplex_block(&a.sister.insert,1,input,2048,6);audio_callback(&a,(Uint8*)out,sizeof(out));
    assert(fabsf(out[2044*4]-.2f)<.001f&&fabsf(out[2044*4+1]-.3f)<.001f);
    assert(out[2044*4+2]==0&&out[2044*4+3]==0);
    assert(a.sister.matrix.input_peak[TS_MATRIX_EQ]>.2f);
    ts_insert_duplex_block(&a.sister.insert,0,NULL,0,0);
    assert(matrix_show(0,&a,&u,&pages,&bank,-1,-1));
    matrix_click(455,102,SDL_BUTTON_LEFT); /* INPUTS */
    matrix_click(80,MATRIX_Y+12,SDL_BUTTON_MIDDLE);assert(matrix_window.rename==0);
    strcpy(matrix_window.name,"Terra live");SDL_Event e={0};e.type=SDL_KEYDOWN;e.key.windowID=matrix_window.id;e.key.keysym.sym=SDLK_RETURN;
    assert(matrix_event(&e,window,0,&a,&u,&pages,&bank));assert(!strcmp(a.sister.matrix.controls.input_name[0],"Terra live"));
    matrix_shot(directory,"matrix-hardware-inputs");
    /* Project state owns names and graph, and an old project clears both. */
    TsSisterProjectState *state=malloc(sizeof(*state));assert(state);ts_sister_project_state_capture(state,&a.sister,1,NULL);
    assert(ts_sister_project_state_save_file(state,"matrix-state.ini",error,sizeof(error)));
    ts_sister_project_state_init(state,48000);int present=0;assert(ts_sister_project_state_load_file(state,"matrix-state.ini",48000,&present,error,sizeof(error)));
    assert(present&&state->matrix.enabled&&!strcmp(state->matrix.input_name[0],"Terra live"));
    assert(ts_sister_project_state_apply(state,&a.sister,&bank));remove("matrix-state.ini");
    matrix_click(600,102,SDL_BUTTON_LEFT);matrix_shot(directory,"matrix-master-vulture");
    /* Default overview screenshot shows source routes and the selected pan. */
    matrix_click(80,102,SDL_BUTTON_LEFT);matrix_window.selected=0;matrix_window.destination=TS_MATRIX_NODES+1;
    matrix_click(966,744,SDL_BUTTON_LEFT);matrix_shot(directory,"matrix-overview");
    c=a.sister.matrix.controls;c.row[TS_MATRIX_NODES].matrix_gain[TS_MATRIX_MAIN]=1000;
    c.row[TS_MATRIX_NODES].matrix_gain[TS_MATRIX_SISTER]=500;c.row[TS_MATRIX_NODES].matrix_gain[TS_MATRIX_PRISM]=500;
    assert(ts_matrix_set(&a.sister.matrix,&c,48000));matrix_window.controls=c;
    matrix_click(455,102,SDL_BUTTON_LEFT);matrix_window.selected=0;matrix_window.destination=TS_MATRIX_PRISM;
    matrix_shot(directory,"matrix-terra-multiple");
    ts_sister_project_state_init(state,48000);assert(ts_sister_project_state_apply(state,&a.sister,&bank));free(state);
    assert(!a.sister.matrix.controls.enabled && !a.sister.matrix.controls.input_name[0][0]);
    matrix_close();puts("Matrix UI, native track/tile audio, pan, inheritance, physical return, naming and project reset passed");
}
