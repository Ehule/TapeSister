static void test_compressor_controller(const char *directory)
{
    ts_tapehead_stop();ts_note_bank_clear(&a.notes);ts_performance_clear(&a.performance);ts_performance_clear(&a.tile_launchers);
    matrix_close();
    TsMosaic *previous=a.mosaic,*previous_ui=u.mosaic,*m=ts_mosaic_create();assert(m);a.mosaic=u.mosaic=m;
    TsSourceRoute old=pages.tracker.lanes[0].output_route;
    TsMatrixControls saved=a.sister.matrix.controls,c;ts_matrix_default(&c);c.enabled=1;
    c.row[TS_MATRIX_MOSAIC].matrix_gain[TS_MATRIX_MAIN]=0;c.row[TS_MATRIX_MOSAIC].matrix_gain[TS_MATRIX_COMPRESSOR]=1000;
    c.row[TS_MATRIX_COMPRESSOR].matrix_gain[TS_MATRIX_MAIN]=0;c.row[TS_MATRIX_COMPRESSOR].matrix_gain[8]=1000;
    assert(ts_matrix_set(&a.sister.matrix,&c,48000));
    TsCompressorControls comp_saved=a.sister.compressor.controls,comp;ts_compressor_default(&comp);comp.threshold_db=-36;comp.attack_ms=1;comp.release_ms=50;
    ts_compressor_set(&a.sister.compressor,&comp);
    performance_gain(0,&a,1,1);performance_gain(0,&a,2,1);ts_sister_runtime_set_master_output_gain(&a.sister,1);
    pages.tracker.lanes[0].output_route=(TsSourceRoute){.matrix_enabled=1};
    pages.tracker.lanes[0].output_route.matrix_gain[7]=1000;
    ts_tapehead_refresh_routes(&pages,&bank);
    TsMosaicSource *source=ts_mosaic_source(m,&bank.current,error,sizeof(error));assert(source);
    TsMosaicEvent *card=ts_mosaic_add(m,source,0,0);assert(card);card->duration=30;card->looping=1;m->playing=1;
    startPlaying(PLAYMODE_SONG,0);double energy[4];for(int i=0;i<10;++i)render(energy);
    assert(energy[0]>1 && energy[2]>1);double kick=energy[0],dry=energy[2];
    assert(matrix_show(0,&a,&u,&pages,&bank,0,-1));
    matrix_click(1200,34,SDL_BUTTON_LEFT);assert(matrix_window.compressor_open);
    /* Real track row -> the new SC column. No second audible copy. */
    int sc_x=MATRIX_X+(mx_column_position(TS_MATRIX_SIDECHAIN)-matrix_window.column)*MATRIX_CW+12;
    matrix_click(sc_x,MATRIX_Y+10,SDL_BUTTON_LEFT);
    assert(pages.tracker.lanes[0].output_route.matrix_gain[TS_MATRIX_SIDECHAIN]==1000);
    matrix_click(250,708,SDL_BUTTON_LEFT);matrix_click(400,708,SDL_BUTTON_LEFT);
    assert(a.sister.compressor.controls.enabled && a.sister.compressor.controls.external);
    for(int i=0;i<20;++i)render(energy);
    assert(fabs(energy[0]-kick)<.001 && energy[2]<dry*.3 && energy[2]>0);
    assert(a.sister.compressor.reduction_db>10 && m->playing && ts_tapehead_running());
    matrix_shot(directory,"compressor-track-sidechain");
    /* Controls, wheel fine adjustment, undo/redo and Original/Edited. */
    float threshold=a.sister.compressor.controls.threshold_db;
    matrix_wheel(100,751,1,KMOD_SHIFT,0);assert(a.sister.compressor.controls.threshold_db>threshold);
    matrix_click(330,34,SDL_BUTTON_LEFT);assert(a.sister.compressor.controls.threshold_db==threshold);
    matrix_click(430,34,SDL_BUTTON_LEFT);assert(a.sister.compressor.controls.threshold_db>threshold);
    matrix_click(530,34,SDL_BUTTON_LEFT);assert(matrix_window.original_active && !a.sister.compressor.controls.enabled);
    matrix_click(530,34,SDL_BUTTON_LEFT);assert(!matrix_window.original_active && a.sister.compressor.controls.enabled);
    SDL_SetWindowSize(matrix_window.window,960,630);
    matrix_wheel(100,751,-1,KMOD_SHIFT,0);assert(fabsf(a.sister.compressor.controls.threshold_db-threshold)<.001f);
    SDL_SetWindowSize(matrix_window.window,MATRIX_W,MATRIX_H);
    /* Disconnect the key: Mosaic returns to full level while drums stay audible. */
    matrix_click(sc_x,MATRIX_Y+10,SDL_BUTTON_LEFT);
    for(int i=0;i<30;++i)render(energy);
    assert(fabs(energy[0]-kick)<.001 && fabs(energy[2]-dry)<.01);
    /* Bypass is dry even with a live sidechain and is remembered by history. */
    matrix_click(sc_x,MATRIX_Y+10,SDL_BUTTON_LEFT);matrix_click(250,708,SDL_BUTTON_LEFT);
    for(int i=0;i<10;++i)render(energy);assert(fabs(energy[2]-dry)<.001);
    ts_tapehead_stop();matrix_close();pages.tracker.lanes[0].output_route=old;ts_tapehead_refresh_routes(&pages,&bank);
    assert(ts_matrix_set(&a.sister.matrix,&saved,48000));ts_compressor_set(&a.sister.compressor,&comp_saved);
    a.mosaic=previous;u.mosaic=previous_ui;ts_mosaic_free(m);
    puts("Native TrackSister sidechain -> Mosaic compressor, bypass, controls, undo and scaled UI passed");
}
