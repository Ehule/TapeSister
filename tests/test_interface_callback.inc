/* Same native application callback used by ASIO, SDL and JACK workers. */
static void interface_callback(const char *directory)
{
    TsMatrixControls saved=a.sister.matrix.controls,c;ts_matrix_default(&c);c.enabled=1;
    c.row[TS_MATRIX_MAIN].matrix_gain[TS_MATRIX_EQ]=0;
    c.row[ts_matrix_input_row(5)].matrix_gain[TS_MATRIX_EQ]=1000;
    c.row[ts_matrix_input_row(0)].matrix_gain[TS_MATRIX_EQ]=1000;
    c.row[ts_matrix_input_row(0)].matrix_gain[ts_matrix_output_destination(9)]=500;
    c.row[ts_matrix_input_row(12)].matrix_gain[ts_matrix_output_destination(13)]=1000;
    strcpy(c.input_name[0],"DRY INSTRUMENT");strcpy(c.input_name[5],"SUNVOX REVERB RETURN");strcpy(c.output_name[9],"S/PDIF SEND");
    assert(ts_matrix_set(&a.sister.matrix,&c,48000));
    unsigned old_channels=a.output_device_channels;a.output_device_channels=28;
    /* The previous fixture intentionally drove the linked hardware guard hard. */
    float old_guard=a.clean_output.limit;a.clean_output.limit=1;
    ts_insert_capture_prepare(&a.sister.insert,26,48000,128);
    static float input[2048*26],output[2048*28];
    for(int f=0;f<2048;++f){input[f*26]=.15f;input[f*26+1]=.2f;input[f*26+10]=.1f;input[f*26+11]=-.15f;input[f*26+24]=.2f;input[f*26+25]=-.25f;}
    /* Check parallel, muted-wet and serial replacement at the native outputs.
       Only the dry source feeds the send; the return never feeds itself. */
    for(int mode=0;mode<3;++mode) {
        if(mode==1)c.row[ts_matrix_input_row(5)].matrix_gain[TS_MATRIX_EQ]=0;
        if(mode==2){c.row[ts_matrix_input_row(5)].matrix_gain[TS_MATRIX_EQ]=1000;c.row[ts_matrix_input_row(0)].matrix_gain[TS_MATRIX_EQ]=0;}
        assert(ts_matrix_set(&a.sister.matrix,&c,48000));
        /* Settle the prior torture patch's route fade and guard release. */
        for(int block=0;block<(mode?3:16);++block){ts_insert_duplex_block(&a.sister.insert,1,input,2048,26);audio_callback(&a,(Uint8 *)output,sizeof(output));}
        for(int f=1024;f<2048;++f)for(int ch=0;ch<28;++ch) {
            float left=mode==0?.25f:mode==1?.15f:.1f,right=mode==0?.05f:mode==1?.2f:-.15f;
            float expected=ch==0?left:ch==1?right:ch==18?.075f:ch==19?.1f:ch==26?.2f:ch==27?-.25f:0;
            if(fabsf(output[f*28+ch]-expected)>=.001f)fprintf(stderr,"I/O callback mode=%d frame=%d channel=%d got=%g expected=%g\n",mode,f,ch+1,output[f*28+ch],expected);
            assert(fabsf(output[f*28+ch]-expected)<.001f);
        }
    }
    assert(matrix_show(0,&a,&u,&pages,&bank,-1,-1));matrix_window.input_ports=26;
    matrix_window.controls=c;matrix_window.filter=3;mx_rows(&pages,&bank);
    int found=0;for(int i=0;i<matrix_window.count;++i)if(matrix_window.rows[i].index==ts_matrix_input_row(0)) {
        matrix_window.selected=i;matrix_window.scroll=SDL_max(0,i-2);found=1;
    }
    assert(found);matrix_window.destination=ts_matrix_output_destination(9);mx_reveal_column(matrix_window.destination);
    mx_capture_activity(0,&a,&pages,&bank,50);mx_paint(&a,&pages,&bank);
    if(directory){char path[1024];snprintf(path,sizeof(path),"%s/matrix-digital-reverb.bmp",directory);
        SDL_Surface *surface=SDL_CreateRGBSurfaceFrom(matrix_window.pixels,MATRIX_W,MATRIX_H,32,MATRIX_W*4,0xff0000,0xff00,0xff,0xff000000);
        assert(surface && !SDL_SaveBMP(surface,path));SDL_FreeSurface(surface);}
    /* A stereo layout hides unused capacity but keeps saved unavailable routes. */
    a.output_device_channels=2;ts_insert_capture_prepare(&a.sister.insert,2,48000,128);
    ts_insert_duplex_block(&a.sister.insert,1,input,2048,2);matrix_window.input_ports=2;
    mx_capture_activity(0,&a,&pages,&bank,50);
    assert(!mx_destination_available(ts_matrix_output_destination(9)));
    int unavailable=0;for(int i=0;i<matrix_window.column_count;++i)unavailable|=mx_column(i)==ts_matrix_output_destination(9);
    assert(unavailable && !strcmp(matrix_window.controls.input_name[5],"SUNVOX REVERB RETURN"));
    ts_insert_duplex_block(&a.sister.insert,0,NULL,0,0);ts_insert_capture_prepare(&a.sister.insert,0,0,0);
    a.clean_output.limit=old_guard;
    a.output_device_channels=old_channels;assert(ts_matrix_set(&a.sister.matrix,&saved,48000));matrix_hide();
}
