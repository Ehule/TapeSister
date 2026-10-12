static void test_matrix_buses(const char *directory)
{
    ts_tapehead_stop();ts_note_bank_clear(&a.notes);ts_performance_clear(&a.performance);ts_performance_clear(&a.tile_launchers);
    matrix_close();
    TsMosaic *saved=a.mosaic,*saved_ui=u.mosaic,*m=ts_mosaic_create();assert(m);a.mosaic=u.mosaic=m;
    TsSourceRoute tile=bank.bank[0].output_route;bank.bank[0].output_route=(TsSourceRoute){0};
    TsMatrixControls previous=a.sister.matrix.controls,c;ts_matrix_default(&c);c.enabled=1;
    c.row[TS_MATRIX_TILES].matrix_gain[TS_MATRIX_MAIN]=0;c.row[TS_MATRIX_TILES].matrix_gain[TS_MATRIX_NODES+1]=1000;
    assert(ts_matrix_set(&a.sister.matrix,&c,48000));
    TsMosaicSource *source=ts_mosaic_source(m,&bank.current,error,sizeof(error));assert(source);
    TsMosaicEvent *one=ts_mosaic_add(m,source,0,0);assert(one);one->duration=20;
    m->playing=1;
    assert(ts_note_bank_start(&a.notes,&bank,TS_AUDITION_CURRENT,12,0,48000)==TS_NOTE_STARTED);
    double energy[4];render(energy);render(energy);
    assert(energy[0]>1 && energy[2]>1);double backing=energy[0],live=energy[2];
    performance_gain(0,&a,3,.5f);render(energy);
    assert(fabs(energy[0]-backing)<.001 && fabs(energy[2]-live*.5)<.001);
    performance_gain(0,&a,3,0);render(energy);
    assert(fabs(energy[0]-backing)<.001 && energy[2]==0 && energy[3]==0 && m->playing);
    performance_gain(0,&a,3,1);render(energy);assert(fabs(energy[2]-live)<.001);
    assert(source->hash==ts_sample_hash(&bank.current)); /* Shared level never rewrites tile/card audio. */
    ts_note_bank_clear(&a.notes);
    TsMosaicEvent *two=ts_mosaic_add(m,source,0,1);assert(two);two->duration=20;
    one->output_route=two->output_route=(TsSourceRoute){.matrix_enabled=1};
    one->output_route.matrix_gain[TS_MATRIX_NODES+1]=two->output_route.matrix_gain[TS_MATRIX_NODES+1]=1000;
    assert(matrix_show(0,&a,&u,&pages,&bank,-1,-1));matrix_click(600,102,SDL_BUTTON_LEFT);
    assert(matrix_window.filter==5 && matrix_window.rows[0].kind==2 && matrix_window.rows[0].index==TS_MATRIX_MOSAIC);
    mx_select(1,0);mx_select(2,KMOD_CTRL);assert(matrix_window.selection_count==2);
    matrix_click(1130,744,SDL_BUTTON_LEFT);
    assert(!one->output_route.matrix_enabled && !two->output_route.matrix_enabled && matrix_window.history_count==1);
    render(energy);render(energy);assert(energy[0]>1 && energy[2]==0 && energy[3]==0);
    matrix_click(330,34,SDL_BUTTON_LEFT);assert(one->output_route.matrix_enabled && two->output_route.matrix_enabled);
    matrix_click(430,34,SDL_BUTTON_LEFT);assert(!one->output_route.matrix_enabled && !two->output_route.matrix_enabled);
    matrix_shot(directory,"matrix-mosaic-bus");
    /* Saved indicator follows edits, reversal, playback and unexported tracker changes. */
    strcpy(u.project_path,"C:\\Music\\A-Pale-Weight-Mosaic.tsr");
    project_mark_saved(&u,&pages,&bank,NULL,&a.sister,&a.keyboard_sequence);
    project_refresh_title(window,&u,&pages,&bank,NULL,&a.sister,&a.keyboard_sequence,1000);
    assert(!u.project_modified && !strcmp(SDL_GetWindowTitle(window),"TapeSister - A-Pale-Weight-Mosaic.tsr"));
    render(energy);project_refresh_title(window,&u,&pages,&bank,NULL,&a.sister,&a.keyboard_sequence,1300);assert(!u.project_modified);
    performance_gain(0,&a,3,.5f);project_refresh_title(window,&u,&pages,&bank,NULL,&a.sister,&a.keyboard_sequence,1600);
    assert(u.project_modified && strstr(SDL_GetWindowTitle(window)," *"));
    performance_gain(0,&a,3,1);project_refresh_title(window,&u,&pages,&bank,NULL,&a.sister,&a.keyboard_sequence,1900);assert(!u.project_modified);
    ts_sample_touch(&bank.current);project_refresh_title(window,&u,&pages,&bank,NULL,&a.sister,&a.keyboard_sequence,2200);assert(u.project_modified);
    project_mark_saved(&u,&pages,&bank,NULL,&a.sister,&a.keyboard_sequence);
    setSongModifiedFlag();project_refresh_title(window,&u,&pages,&bank,NULL,&a.sister,&a.keyboard_sequence,2500);assert(u.project_modified);
    project_mark_saved(&u,&pages,&bank,NULL,&a.sister,&a.keyboard_sequence);assert(!u.project_modified && !ts_tapehead_project_dirty(&pages.tracker));
    if(directory) {
        TsFramebuffer *fb=malloc(sizeof(*fb));assert(fb);int tracker=u.tracker_open,mosaic=u.mosaic_open;u.tracker_open=u.mosaic_open=0;u.project_modified=1;
        ts_ui_render(fb,&u,&bank);char path[1024];snprintf(path,sizeof(path),"%s/project-filename.bmp",directory);
        SDL_Surface *s=SDL_CreateRGBSurfaceFrom(fb->pixels,640,400,32,640*4,0xff0000,0xff00,0xff,0xff000000);
        assert(s&&!SDL_SaveBMP(s,path));SDL_FreeSurface(s);free(fb);u.tracker_open=tracker;u.mosaic_open=mosaic;
    }
    matrix_close();bank.bank[0].output_route=tile;assert(ts_matrix_set(&a.sister.matrix,&previous,48000));
    a.mosaic=saved;u.mosaic=saved_ui;ts_mosaic_free(m);u.project_path[0]=0;u.project_modified=0;
    puts("Independent live tile bus, Mosaic bus return/undo and filename dirty state passed");
}
