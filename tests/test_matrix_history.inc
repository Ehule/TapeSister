static void matrix_history_bulk(const char *directory)
{
    matrix_history_clear();matrix_window.mosaic=u.mosaic;
    MatrixSnapshot *before=mx_snapshot(0,&a,&pages,&bank);assert(before);
    assert(matrix_show(0,&a,&u,&pages,&bank,0,-1));
    assert(matrix_window.count>=2);
    matrix_click(80,MATRIX_Y+12,SDL_BUTTON_LEFT);
    SDL_SetModState(KMOD_CTRL);matrix_click(80,MATRIX_Y+MATRIX_RH+12,SDL_BUTTON_LEFT);SDL_SetModState(KMOD_NONE);
    assert(matrix_window.selection_count==2);
    SDL_SetModState(KMOD_CTRL);matrix_click(MATRIX_X+TS_MATRIX_BOARD*MATRIX_CW+20,140,SDL_BUTTON_LEFT);SDL_SetModState(KMOD_NONE);
    assert(pages.tracker.lanes[0].output_route.matrix_gain[TS_MATRIX_BOARD]==1000);
    assert(pages.tracker.lanes[1].output_route.matrix_gain[TS_MATRIX_BOARD]==1000);
    assert(matrix_window.history_count==1);
    matrix_click(330,34,SDL_BUTTON_LEFT);
    assert(!pages.tracker.lanes[0].output_route.matrix_gain[TS_MATRIX_BOARD] && !pages.tracker.lanes[1].output_route.matrix_gain[TS_MATRIX_BOARD]);
    matrix_click(430,34,SDL_BUTTON_LEFT);
    assert(pages.tracker.lanes[0].output_route.matrix_gain[TS_MATRIX_BOARD]==1000);
    int count=matrix_window.history_count;
    SDL_Event e;SDL_zero(e);e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=matrix_window.id;e.button.button=SDL_BUTTON_LEFT;e.button.x=914;e.button.y=744;
    assert(matrix_event(&e,window,0,&a,&u,&pages,&bank));
    e.type=SDL_MOUSEMOTION;e.motion.windowID=matrix_window.id;e.motion.x=1044;e.motion.y=744;
    assert(matrix_event(&e,window,0,&a,&u,&pages,&bank));
    e.motion.x=980;assert(matrix_event(&e,window,0,&a,&u,&pages,&bank));
    e.type=SDL_MOUSEBUTTONUP;e.button.windowID=matrix_window.id;assert(matrix_event(&e,window,0,&a,&u,&pages,&bank));
    assert(matrix_window.history_count==count+1 && pages.tracker.lanes[0].output_route.pan==pages.tracker.lanes[1].output_route.pan);
    matrix_click(540,34,SDL_BUTTON_LEFT);assert(matrix_window.original_active);
    assert(!pages.tracker.lanes[0].output_route.matrix_gain[TS_MATRIX_BOARD]);
    TsMatrixControls original_controls=matrix_window.controls;
    matrix_click(800,34,SDL_BUTTON_LEFT); /* Clear cannot alter the original audition. */
    matrix_click(1110,100,SDL_BUTTON_LEFT); /* Neither can feedback enable. */
    assert(!memcmp(&original_controls,&a.sister.matrix.controls,sizeof(original_controls)));
    matrix_click(540,34,SDL_BUTTON_LEFT);assert(!matrix_window.original_active);
    assert(pages.tracker.lanes[0].output_route.matrix_gain[TS_MATRIX_BOARD]==1000 && matrix_window.history_count==count+1);
    matrix_shot(directory,"matrix-bulk-undo");
    matrix_click(330,34,SDL_BUTTON_LEFT);matrix_wheel(900,744,-1,0,0);
    assert(matrix_window.history_cursor==matrix_window.history_count); /* New branch discards redo. */
    pages.tracker.lanes[1].output_route.mode=TS_SOURCE_INHERIT;
    TsSourceRoute first=pages.tracker.lanes[0].output_route;
    SDL_SetModState(KMOD_CTRL);matrix_click(MATRIX_X+TS_MATRIX_PRISM*MATRIX_CW+20,140,SDL_BUTTON_LEFT);SDL_SetModState(KMOD_NONE);
    assert(!memcmp(&first,&pages.tracker.lanes[0].output_route,sizeof(first))); /* Atomic refusal. */
    mx_apply(before,0,&a,&u,&pages,&bank);free(before);matrix_history_clear();
    assert(matrix_show(0,&a,&u,&pages,&bank,-1,-1));
}
static void matrix_import_controls(void)
{
    ts_tapehead_stop();ts_note_bank_clear(&a.notes);ts_performance_clear(&a.performance);ts_performance_clear(&a.tile_launchers);
    ts_audio_mixer_init(&a.mixer);a.external_monitor_enabled=NULL;a.tune_reference_target=a.tune_reference_level=0;
    for(int i=0;i<TS_TRACKER_LANES;++i)pages.tracker.lanes[i].output_route=(TsSourceRoute){0};
    for(size_t p=0;p<ts_sample_pages_count(&pages);++p) {
        TsInstrument *page=ts_sample_pages_page_mut(&pages,&bank,p);assert(page);
        for(int i=0;i<TS_BANK_SLOT_COUNT;++i)page->bank[i].output_route=(TsSourceRoute){0};
    }
    double energy[4];render(energy);matrix_history_clear();assert(matrix_show(0,&a,&u,&pages,&bank,-1,-1));
    bank.bank[0].output_route=(TsSourceRoute){.mode=TS_SOURCE_PAIR,.second=1};
    TsMatrixControls before=a.sister.matrix.controls;
    matrix_click(760,102,SDL_BUTTON_LEFT);
    assert(!memcmp(&before,&a.sister.matrix.controls,sizeof(before)) && !matrix_window.history_count);
    assert(strstr(matrix_window.status,"UNCHANGED"));
    bank.bank[0].output_route=(TsSourceRoute){0};matrix_history_clear();
    matrix_click(760,102,SDL_BUTTON_LEFT);assert(a.sister.matrix.controls.enabled && a.sister.matrix.controls.serial_mask);
    assert(matrix_window.history_count==1);
    matrix_click(760,102,SDL_BUTTON_LEFT);assert(!matrix_window.original_active && matrix_window.history_count==1);
    matrix_click(540,34,SDL_BUTTON_LEFT);assert(matrix_window.original_active && !a.sister.matrix.controls.enabled);
    matrix_click(760,102,SDL_BUTTON_LEFT);assert(matrix_window.original_active && !a.sister.matrix.controls.enabled);
    matrix_click(540,34,SDL_BUTTON_LEFT);assert(!matrix_window.original_active && a.sister.matrix.controls.enabled);
    matrix_click(330,34,SDL_BUTTON_LEFT);assert(!a.sister.matrix.controls.enabled);
    matrix_click(430,34,SDL_BUTTON_LEFT);assert(a.sister.matrix.controls.enabled);
}
