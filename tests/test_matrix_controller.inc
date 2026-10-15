static void matrix_click(int x,int y,int button)
{
    SDL_Event event;SDL_zero(event);event.type=SDL_MOUSEBUTTONDOWN;event.button.windowID=matrix_window.id;
    event.button.x=x;event.button.y=y;event.button.button=button;
    assert(matrix_event(&event,window,0,&a,&u,&pages,&bank));
    event.type=SDL_MOUSEBUTTONUP;assert(matrix_event(&event,window,0,&a,&u,&pages,&bank));
}
static void matrix_wheel(int x,int y,int steps,int modifiers,int flipped)
{
    SDL_Event e;SDL_zero(e);e.type=SDL_MOUSEWHEEL;e.wheel.windowID=matrix_window.id;e.wheel.y=steps;
    e.wheel.direction=flipped?SDL_MOUSEWHEEL_FLIPPED:SDL_MOUSEWHEEL_NORMAL;
    int width,height;SDL_GetWindowSize(matrix_window.window,&width,&height);
#if SDL_VERSION_ATLEAST(2,26,0)
    e.wheel.mouseX=x*width/MATRIX_W;e.wheel.mouseY=y*height/MATRIX_H;
#else
    SDL_WarpMouseInWindow(matrix_window.window,x*width/MATRIX_W,y*height/MATRIX_H);
#endif
    SDL_SetModState(modifiers);assert(matrix_event(&e,window,0,&a,&u,&pages,&bank));SDL_SetModState(KMOD_NONE);
}
static void matrix_feedback_controls(void)
{
    int scroll=matrix_window.scroll;TsSourceRoute *route=&pages.tracker.lanes[0].output_route;
    matrix_wheel(400,744,1,0,0);assert(route->matrix_gain[8]==1122 && matrix_window.scroll==scroll);
    matrix_click(100,744,SDL_BUTTON_RIGHT);assert(route->matrix_gain[8]==1000 && !matrix_window.drag);
    matrix_wheel(400,744,1,KMOD_SHIFT,0);assert(route->matrix_gain[8]==1012);
    matrix_wheel(400,744,1,KMOD_SHIFT,1);assert(route->matrix_gain[8]==1000);
    matrix_wheel(400,744,128,0,0);assert(route->matrix_gain[8]==2000);
    matrix_click(500,744,SDL_BUTTON_RIGHT);assert(route->matrix_gain[8]==1000);
    matrix_wheel(650,744,1,0,0);assert(route->pan==5);
    matrix_wheel(900,744,1,KMOD_SHIFT,0);assert(route->pan==6);
    matrix_wheel(900,744,1,0,1);assert(route->pan==1);
    matrix_wheel(900,744,128,0,0);assert(route->pan==100);
    matrix_click(640,744,SDL_BUTTON_RIGHT);assert(route->pan==0);
    SDL_SetWindowSize(matrix_window.window,960,630);
    matrix_wheel(400,744,1,0,0);assert(route->matrix_gain[8]==1122);
    matrix_wheel(900,744,-1,0,0);assert(route->pan==-5);
    SDL_SetWindowSize(matrix_window.window,MATRIX_W,MATRIX_H);
    matrix_click(400,744,SDL_BUTTON_RIGHT);matrix_click(900,744,SDL_BUTTON_RIGHT);
    assert(route->matrix_gain[8]==1000 && !route->pan && ts_tapehead_running());
}
static void matrix_shot(const char *directory,const char *name)
{
    if(!directory)return;
    mx_capture_activity(0,&a,&pages,&bank,0);
    matrix_window.outputs=a.output_device_channels;matrix_window.inputs=6;
    mx_rows(&pages,&bank);mx_paint(&a,&pages,&bank);
    char path[1024];snprintf(path,sizeof(path),"%s/%s.bmp",directory,name);
    SDL_Surface *s=SDL_CreateRGBSurfaceFrom(matrix_window.pixels,MATRIX_W,MATRIX_H,32,MATRIX_W*4,0xff0000,0xff00,0xff,0xff000000);
    assert(s&&!SDL_SaveBMP(s,path));SDL_FreeSurface(s);
}
static void matrix_clear_reset(const char *directory)
{
    /* Include off-page tiles, inherited tracks and a hidden lane. */
    assert(ts_sample_pages_append(&pages,error,sizeof(error)));
    TsInstrument *other=ts_sample_pages_page_mut(&pages,&bank,ts_sample_pages_count(&pages)-1);
    assert(other && ts_instrument_activate_silence(other,32,48000,error,sizeof(error)));
    TsSourceRoute direct={.matrix_enabled=1,.pan=30,.width=-100};
    direct.matrix_gain[TS_MATRIX_NODES+1]=1000;
    other->bank[0].output_route=bank.bank[0].output_route=direct;
    pages.tracker.lanes[0].output_route=(TsSourceRoute){.mode=TS_SOURCE_INHERIT};
    pages.tracker.lanes[TS_TRACKER_LANES-1].output_route=direct;
    ts_tapehead_routes_changed(bank.bank[0].tile_id,direct);
    TsMatrixControls named=a.sister.matrix.controls;named.feedback=1;
    assert(ts_matrix_connect(&named,TS_MATRIX_MAIN,TS_MATRIX_SISTER,500)==1);
    assert(named.delayed[TS_MATRIX_MAIN]);
    assert(ts_matrix_set(&a.sister.matrix,&named,48000));
    assert(matrix_show(0,&a,&u,&pages,&bank,-1,-1));
    matrix_click(455,102,SDL_BUTTON_LEFT); /* Filtered INPUTS must still clear everything. */
    assert(ts_note_bank_start(&a.notes,&bank,TS_AUDITION_CURRENT,12,0,48000)==TS_NOTE_STARTED);
    startPlaying(PLAYMODE_SONG,0);double energy[4];render(energy);render(energy);
    assert(energy[2]>1 && energy[3]>1);
    matrix_click(800,34,SDL_BUTTON_LEFT);
    assert(ts_tapehead_running() && a.sister.matrix.controls.enabled && !a.sister.matrix.controls.feedback);
    for(int row=0;row<TS_MATRIX_ROWS;++row)for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)
        assert(!a.sister.matrix.controls.row[row].matrix_gain[d]);
    for(int lane=0;lane<TS_TRACKER_LANES;++lane) {
        TsSourceRoute route=pages.tracker.lanes[lane].output_route;
        assert(route.matrix_enabled && route.mode==TS_SOURCE_MAIN && !route.pan && !route.width);
        for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)assert(!route.matrix_gain[d]);
    }
    assert(!memcmp(&bank.bank[0].output_route,&other->bank[0].output_route,sizeof(TsSourceRoute)));
    for(int n=0;n<TS_MATRIX_NODES;++n) {
        assert(!a.sister.matrix.controls.delayed[n]);
        for(int f=0;f<TS_MATRIX_DELAY;++f)assert(a.sister.matrix.history[n][f].l==0 && a.sister.matrix.history[n][f].r==0);
    }
    assert(!memcmp(named.input_name,a.sister.matrix.controls.input_name,sizeof(named.input_name)));
    assert(!memcmp(named.output_name,a.sister.matrix.controls.output_name,sizeof(named.output_name)));
    render(energy);render(energy);for(int ch=0;ch<4;++ch)assert(energy[ch]==0);
    matrix_shot(directory,"matrix-cleared");
    /* Restoring only the graph cannot unmute cleared source routes, including
       cached tiles on inherited tracks and already-held keyboard voices. */
    TsMatrixControls defaults;ts_matrix_default(&defaults);defaults.enabled=1;
    memcpy(defaults.input_name,named.input_name,sizeof(named.input_name));
    memcpy(defaults.output_name,named.output_name,sizeof(named.output_name));
    assert(ts_matrix_set(&a.sister.matrix,&defaults,48000));
    pages.tracker.lanes[0].output_route.mode=TS_SOURCE_INHERIT;
    ts_tapehead_routes_changed(0,(TsSourceRoute){0});
    render(energy);render(energy);for(int ch=0;ch<4;++ch)assert(energy[ch]==0);
    matrix_click(1000,34,SDL_BUTTON_LEFT);
    assert(ts_tapehead_running());
    assert(!memcmp(&defaults,&a.sister.matrix.controls,sizeof(defaults)));
    assert(!memcmp(&defaults,&u.config.matrix,sizeof(defaults)));
    TsSourceRoute main={.matrix_enabled=1};main.matrix_gain[TS_MATRIX_MAIN]=1000;
    TsSourceRoute track_bus={0};
    for(int lane=0;lane<TS_TRACKER_LANES;++lane)assert(!memcmp(&track_bus,&pages.tracker.lanes[lane].output_route,sizeof(track_bus)));
    assert(!memcmp(&main,&bank.bank[0].output_route,sizeof(main)));
    assert(!memcmp(&main,&other->bank[0].output_route,sizeof(main)));
    render(energy);render(energy);assert(energy[0]>1 && energy[1]>1 && energy[2]==0 && energy[3]==0);
    ts_tapehead_stop();render(energy);render(energy);
    assert(energy[0]>1 && energy[1]>1 && energy[2]==0 && energy[3]==0); /* The held tile recovers too. */
    ts_note_bank_clear(&a.notes);
    matrix_click(600,102,SDL_BUTTON_LEFT);matrix_shot(directory,"matrix-default");
    /* Either explicit action also activates the matrix from legacy mode. */
    defaults.enabled=0;assert(ts_matrix_set(&a.sister.matrix,&defaults,48000));
    matrix_click(800,34,SDL_BUTTON_LEFT);assert(a.sister.matrix.controls.enabled);
    defaults.enabled=0;assert(ts_matrix_set(&a.sister.matrix,&defaults,48000));
    matrix_click(1000,34,SDL_BUTTON_LEFT);assert(a.sister.matrix.controls.enabled);
}
#include "test_matrix_history.inc"
#include "test_performance_transport.inc"
#include "test_matrix_buses.inc"
#include "test_compressor_controller.inc"
#include "test_matrix_layout_controller.inc"
#include "test_matrix_instances_controller.inc"
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
    matrix_click(MATRIX_X+8*MATRIX_CW+8,MATRIX_Y+12,SDL_BUTTON_LEFT);
    assert(pages.tracker.lanes[0].output_route.matrix_enabled && pages.tracker.lanes[0].output_route.matrix_gain[8]==1000);
    assert(!bank.bank[0].output_route.matrix_enabled);
    startPlaying(PLAYMODE_SONG,0);double energy[4];render(energy);render(energy);
    assert(energy[0]==0&&energy[1]==0&&energy[2]>1&&energy[3]>1);
    matrix_feedback_controls();
    mx_capture_activity(0,&a,&pages,&bank,50);render(energy);mx_capture_activity(0,&a,&pages,&bank,50);
    assert(matrix_window.row_source[0]>.001f && matrix_window.row_send[0]>.001f);
    assert(matrix_window.receiving[8]>.001f && matrix_window.receiving[7]==0);
    matrix_shot(directory,"matrix-track-activity");
    /* Actual output metering is after OUT gain, not just graph connectivity. */
    ts_sister_runtime_set_master_output_gain(&a.sister,0);render(energy);
    mx_capture_activity(0,&a,&pages,&bank,50);
    for(int i=0;i<10;++i){render(energy);mx_capture_activity(0,&a,&pages,&bank,150);}
    assert(matrix_window.row_source[0]>.001f && matrix_window.receiving[8]<.0001f);
    ts_sister_runtime_set_master_output_gain(&a.sister,1);render(energy);
    matrix_click(1044,744,SDL_BUTTON_LEFT);render(energy);render(energy);
    assert(energy[2]==0&&energy[3]>1);assert(ts_tapehead_scope_pan(0,128)==255);
    matrix_click(1100,744,SDL_BUTTON_LEFT);render(energy);render(energy);
    assert(pages.tracker.lanes[0].output_route.mode==TS_SOURCE_INHERIT);
    matrix_wheel(400,744,-1,0,0);matrix_wheel(900,744,1,0,0);
    matrix_click(400,744,SDL_BUTTON_RIGHT);matrix_click(900,744,SDL_BUTTON_RIGHT);
    assert(pages.tracker.lanes[0].output_route.mode==TS_SOURCE_INHERIT);
    assert(energy[0]>1&&energy[1]>1&&energy[2]==0&&energy[3]==0);
    matrix_click(1100,744,SDL_BUTTON_LEFT);
    /* Track matrix routing survives the real embedded score codec. */
    matrix_click(MATRIX_X+8*MATRIX_CW+8,MATRIX_Y+12,SDL_BUTTON_LEFT);
    assert(ts_tapehead_export(&pages.tracker,error,sizeof(error)));
    assert(ts_sister_tracker_save_file(&pages.tracker,"matrix-score.tst",error,sizeof(error)));
    TsSisterTracker *copy=calloc(1,sizeof(*copy));assert(copy);ts_sister_tracker_init(copy);
    assert(ts_sister_tracker_load_file(copy,"matrix-score.tst",error,sizeof(error)));
    assert(copy->lanes[0].output_route.matrix_gain[8]==1000);ts_sister_tracker_free(copy);free(copy);remove("matrix-score.tst");
    ts_tapehead_stop();
    /* Tile keyboard voices use the same path, independent of track overrides. */
    assert(matrix_show(0,&a,&u,&pages,&bank,-1,0));
    /* First destination replaces the inherited live tile bus. */
    matrix_click(MATRIX_X+8*MATRIX_CW+8,MATRIX_Y+12,SDL_BUTTON_LEFT);
    assert(ts_note_bank_start(&a.notes,&bank,TS_AUDITION_CURRENT,12,0,48000)==TS_NOTE_STARTED);
    render(energy);render(energy);assert(energy[0]==0&&energy[1]==0&&energy[2]>1&&energy[3]>1);
    mx_capture_activity(0,&a,&pages,&bank,50);render(energy);mx_capture_activity(0,&a,&pages,&bank,50);
    assert(matrix_window.row_source[0]>.001f && matrix_window.row_send[0]>.001f);
    /* No new callbacks must let meters fade instead of retaining old peaks. */
    for(int i=0;i<12;++i)mx_capture_activity(0,&a,&pages,&bank,150);
    assert(matrix_window.row_source[0]<.0001f && matrix_window.receiving[8]<.0001f);
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
    strcpy(matrix_window.name,"Terra live");
    matrix_window.rename_blink_started=SDL_GetTicks();
    matrix_shot(directory,"matrix-rename-caret");
    mx_paint(&a,&pages,&bank);
    int caret=736*MATRIX_W+266+(6+(int)strlen(matrix_window.name))*12;
    assert(matrix_window.pixels[caret]==0xffffce69u);
    matrix_window.rename_blink_started=SDL_GetTicks()-600u;
    mx_paint(&a,&pages,&bank);assert(matrix_window.pixels[caret]!=0xffffce69u);
    SDL_Event e;SDL_zero(e);e.type=SDL_KEYDOWN;e.key.windowID=matrix_window.id;e.key.keysym.sym=SDLK_RETURN;
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
    matrix_clear_reset(directory);matrix_history_bulk(directory);test_performance_transport(directory);
    /* An input can reach an OFF processor without an output from that node.
       The screenshot is a native render driven by actual callback samples. */
    assert(matrix_show(0,&a,&u,&pages,&bank,-1,-1));
    c=a.sister.matrix.controls;c.row[TS_MATRIX_NODES].matrix_gain[TS_MATRIX_MAIN]=1000;
    c.row[TS_MATRIX_NODES].matrix_gain[TS_MATRIX_PRISM]=500;
    assert(ts_matrix_set(&a.sister.matrix,&c,48000));
    a.sister.parameters.prism.enabled=0;
    for(int f=0;f<2048;++f){input[f*6]=.12f;input[f*6+1]=.18f;input[f*6+4]=input[f*6+5]=0;}
    mx_capture_activity(0,&a,&pages,&bank,50);
    for(int i=0;i<3;++i){ts_insert_duplex_block(&a.sister.insert,1,input,2048,6);audio_callback(&a,(Uint8*)out,sizeof(out));}
    mx_capture_activity(0,&a,&pages,&bank,50);
    assert(matrix_window.receiving[TS_MATRIX_PRISM]>.01f && matrix_window.peaks[TS_MATRIX_PRISM]<.0001f);
    assert(matrix_window.receiving[TS_MATRIX_NODES]>.01f && matrix_window.receiving[TS_MATRIX_NODES+2]==0);
    matrix_window.filter=0;matrix_window.selected=-1;matrix_window.destination=TS_MATRIX_MAIN;mx_rows(&pages,&bank);
    /* Put the external inputs and processor returns together in view. */
    for(int i=0;i<matrix_window.count;++i)if(matrix_window.rows[i].kind==2 && matrix_window.rows[i].index==TS_MATRIX_NODES) {
        matrix_window.scroll=SDL_max(0,SDL_min(i,matrix_window.count-MATRIX_VISIBLE));matrix_window.selected=i;break;
    }
    ts_insert_duplex_block(&a.sister.insert,1,input,2048,6);audio_callback(&a,(Uint8*)out,sizeof(out));
    mx_capture_activity(0,&a,&pages,&bank,50);matrix_shot(directory,"matrix-signal-activity");
    ts_insert_duplex_block(&a.sister.insert,0,NULL,0,0);
    ts_sister_project_state_init(state,48000);assert(ts_sister_project_state_apply(state,&a.sister,&bank));free(state);
    assert(!a.sister.matrix.controls.enabled && !a.sister.matrix.controls.input_name[0][0]);
    matrix_import_controls();
    test_matrix_buses(directory);
    test_tracker_bus(directory);
    test_compressor_controller(directory);
    matrix_layout_controller(directory);
    matrix_instances_controller(directory);
    matrix_close();puts("Matrix UI, native track/tile audio, pan, inheritance, physical return, naming and project reset passed");
}
