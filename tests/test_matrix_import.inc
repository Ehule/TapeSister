/* Optional real-project regression. Supply a portable .tsr through
   TAPESISTER_MATRIX_IMPORT_PROJECT; no user project is modified or saved. */
static void matrix_import_project(void)
{
    const char *path=getenv("TAPESISTER_MATRIX_IMPORT_PROJECT");if(!path || !*path)return;
    enum { BLOCKS=1500, FRAMES=512, VALUES=FRAMES*2 };
    float *reference=malloc((size_t)BLOCKS*VALUES*sizeof(float));assert(reference);
    TsInstrument *record=calloc(1,sizeof(*record));TsSisterProjectState *state=malloc(sizeof(*state));assert(record && state);
    ts_instrument_init(record);ts_tapehead_stop();ts_tapehead_close();
    TsMosaic *saved=pages.mosaic;pages.mosaic=ts_mosaic_create();assert(pages.mosaic);
    double peak_error=0,energy=0;
    for(int pass=0;pass<2;++pass) {
        matrix_close();ts_note_bank_clear(&a.notes);ts_performance_clear(&a.performance);ts_performance_clear(&a.tile_launchers);
        ts_sister_runtime_free(&a.sister);ts_sister_runtime_init(&a.sister);
        assert(ts_sister_runtime_reconfigure(&a.sister,48000,2,error,sizeof(error)));
        ts_audio_mixer_init(&a.mixer);a.output_rate=48000;a.output_device_channels=2;
        a.playing=a.tile_launcher_active=0;a.sample=NULL;
        assert(ts_sample_pages_load_project(&pages,&bank,record,path,error,sizeof(error)));
        int present=0;assert(ts_sister_project_state_load(state,path,48000,&present,error,sizeof(error)) && present);
        assert(ts_sister_project_state_apply(state,&a.sister,&bank));
        a.mosaic=u.mosaic=pages.mosaic;ts_mosaic_seek(a.mosaic,330);a.mosaic->playing=1;
        assert(matrix_show(0,&a,&u,&pages,&bank,-1,-1));SDL_SetWindowSize(matrix_window.window,MATRIX_W,MATRIX_H);
        for(int block=0;block<BLOCKS;++block) {
            if(pass && block==300) {
                matrix_click(760,102,SDL_BUTTON_LEFT);
                if(!a.sister.matrix.controls.enabled)fprintf(stderr,"Import: %s\n",matrix_window.status);
                assert(a.sister.matrix.controls.enabled && a.sister.matrix.controls.serial_mask);
                assert(matrix_window.history_count==1);
                /* Live is status, and cannot switch audio or add history. */
                matrix_click(760,102,SDL_BUTTON_LEFT);assert(!matrix_window.original_active && matrix_window.history_count==1);
            }
            if(pass && (block==600 || block==900)) {
                matrix_click(540,34,SDL_BUTTON_LEFT);
                assert(matrix_window.original_active==(block==600));
            }
            float out[VALUES];audio_callback(&a,(Uint8*)out,sizeof(out));
            for(int i=0;i<VALUES;++i) {
                assert(isfinite(out[i]));size_t at=(size_t)block*VALUES+i;
                if(!pass)reference[at]=out[i];
                else {double delta=fabs((double)out[i]-reference[at]);peak_error=fmax(peak_error,delta);energy+=(double)out[i]*out[i];}
            }
        }
        if(pass) {
            TsMatrixControls imported=a.sister.matrix.controls;
            matrix_click(330,34,SDL_BUTTON_LEFT);assert(!a.sister.matrix.controls.enabled);
            matrix_click(430,34,SDL_BUTTON_LEFT);assert(!memcmp(&imported,&a.sister.matrix.controls,sizeof(imported)));
            ts_sister_project_state_capture(state,&a.sister,pages.page_count,NULL);
            assert(ts_sister_project_state_save_file(state,"matrix-import-state.ini",error,sizeof(error)));
            assert(ts_sister_project_state_load_file(state,"matrix-import-state.ini",48000,&present,error,sizeof(error)));
            assert(!memcmp(&imported,&state->matrix,sizeof(imported)));remove("matrix-import-state.ini");
        }
    }
    printf("Real Mosaic project: 768000 frames, live import and A/B peak difference %.9g; energy %.9g\n",peak_error,energy);
    assert(energy>1 && peak_error<.000002);
    matrix_close();ts_mosaic_free(pages.mosaic);pages.mosaic=saved;a.mosaic=u.mosaic=saved;
    ts_instrument_free(record);free(record);free(state);free(reference);
}
