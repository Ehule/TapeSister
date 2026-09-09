/* Exercise the actual canvas note route, output callback, threaded recorder,
   and recording controls with dummy SDL devices. */
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>

static void test_export_tile_name(TsUiState *ui, TsInstrument *instrument)
{
    char error[160], path[TS_BROWSER_PATH_MAX];
    int selected=instrument->selected_slot;assert(selected>=0 && instrument->bank[selected].occupied);
    uint64_t hash=ts_sample_hash(&instrument->current);
    const char *names[]={"Night Garden", "Night Garden.WAV", "Night:Garden/Take?", "CON.wav", ".."};
    const char *expected[]={"Night Garden.wav", "Night Garden.wav", "Night_Garden_Take_.wav", "_CON.wav", "tapesister-export.wav"};
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);++i) {
        assert(ts_instrument_bank_rename(instrument,selected,names[i],error,sizeof(error)));
        browser_open_export_wav(ui,instrument);
        assert(ui->browser.mode==TS_BROWSER_EXPORT_WAV && !strcmp(ui->browser.filename,expected[i]));
        assert(ui->browser.filename_focus && ui->browser.filename_cursor==strlen(expected[i]));
        assert(!ui->browser.overwrite_armed && ts_browser_destination_path(&ui->browser,path,sizeof(path)));
        assert(strlen(path)>=strlen(expected[i]) && !strcmp(path+strlen(path)-strlen(expected[i]),expected[i]));
        assert(ts_sample_hash(&instrument->current)==hash);
        ts_browser_close(&ui->browser);
    }
    /* Use Current only when there is no named occupied tile. */
    instrument->selected_slot=-1;browser_open_export_wav(ui,instrument);
    assert(!strcmp(ui->browser.filename,"CANVAS CHORD.wav"));ts_browser_close(&ui->browser);
    instrument->selected_slot=selected;
    assert(ts_instrument_bank_rename(instrument,selected,"Night Garden",error,sizeof(error)));
    browser_open_export_wav(ui,instrument);
    const char *shot=getenv("TS_TEST_EXPORT_NAME_SCREENSHOT");
    if(shot) {static TsFramebuffer fb;ts_ui_render(&fb,ui,instrument);assert(ts_ui_write_ppm(&fb,shot));}
    ts_browser_close(&ui->browser);SDL_StopTextInput();
}

int main(void)
{
    static AudioState audio;static TsUiState ui;static TsInstrument instrument;
    static SisterWindow sister;TsSample source,saved;char error[160];
    SDL_SetMainReady();SDL_setenv("SDL_AUDIODRIVER","dummy",1);SDL_setenv("SDL_VIDEODRIVER","dummy",1);
    assert(!SDL_Init(SDL_INIT_AUDIO|SDL_INIT_VIDEO|SDL_INIT_TIMER));
    SDL_AudioSpec spec={0};spec.freq=44100;spec.channels=2;spec.format=AUDIO_F32SYS;spec.samples=256;
    SDL_AudioDeviceID device=SDL_OpenAudioDevice(NULL,0,&spec,NULL,0);assert(device);ts_real_output=device;
    SDL_Window *window=SDL_CreateWindow("Canvas recording",0,0,640,400,0);assert(window);
    ts_ui_init(&ui);ts_instrument_init(&instrument);ts_sample_init(&source);ts_sample_init(&saved);
    ts_sister_ui_model_init(&sister.model,&ui.config);
    ts_sister_runtime_init(&audio.sister);ts_note_bank_init(&audio.notes);
    ts_performance_init(&audio.performance);ts_performance_init(&audio.tile_launchers);
    ts_capture_init(&audio.capture);ts_audio_mixer_init(&audio.mixer);
    ts_performance_recorder_init(&sister.performance_recorder);
    audio.sister_file_recorder=&sister.performance_recorder;audio.output_rate=44100;audio.bank_slot=-1;
    atomic_init(&audio.sister_file_tap,TS_SISTER_TAP_H1);
    sister.model.selected_tap=TS_SISTER_TAP_H1;sister.model.destination_mode=TS_SISTER_UI_DEST_CURRENT;
    source.frames=44100;source.sample_rate=44100;source.channels=1;source.data=malloc(source.frames*sizeof(float));assert(source.data);
    for(size_t i=0;i<source.frames;++i)source.data[i]=.18f*sinf((float)i*.05f);
    snprintf(source.name,sizeof(source.name),"CANVAS CHORD");
    assert(ts_instrument_import_sample(&instrument,&source,1,1000,10000,TS_LOOP_FORWARD,error,sizeof(error)));
    test_export_tile_name(&ui,&instrument);
    instrument.has_selection=1;instrument.selection_first=1000;instrument.selection_last=10000;
    uint64_t original=ts_sample_hash(&instrument.current);
    ts_ui_keyboard_set_octave(&ui,4);ui.show_keyboard=ui.show_recipes=ui.show_ingredients=0;
    SDL_Event event={0};event.type=SDL_KEYDOWN;event.key.windowID=SDL_GetWindowID(window);event.key.keysym.sym=SDLK_z;
    assert(canvas_qwerty_event(&event,device,&audio,&ui,&instrument,44100));
    TsNoteVoice *first=&audio.notes.voices[0];assert(first->active && first->looping && first->range_first==1000 && first->range_last==10000);
    float before[512];audio_callback(&audio,(Uint8*)before,sizeof(before));double position=first->position;
    event.key.keysym.sym=SDLK_q;assert(canvas_qwerty_event(&event,device,&audio,&ui,&instrument,44100));
    assert(ts_note_bank_count(&audio.notes)==2 && first->position==position);
    const TsNoteVoice *latest=ts_note_bank_display_voice(&audio.notes);assert(latest==&audio.notes.voices[1] && latest->midi_note==72);
    event.key.repeat=1;assert(canvas_qwerty_event(&event,device,&audio,&ui,&instrument,44100));assert(ts_note_bank_count(&audio.notes)==2);event.key.repeat=0;
    const SDL_Keycode more[]={SDLK_x,SDLK_c,SDLK_v,SDLK_b};
    for(int i=0;i<4;++i) {event.key.keysym.sym=more[i];assert(canvas_qwerty_event(&event,device,&audio,&ui,&instrument,44100));}
    assert(ts_note_bank_count(&audio.notes)==5 && strstr(ui.status,"LIMIT"));
    ts_ui_keyboard_set_octave(&ui,6);release_note(device,&audio,&ui,0);assert(!first->active && ts_note_bank_count(&audio.notes)==4);
    /* Starting OUT capture leaves notes, tile, Sister tap/destination and its disabled state intact. */
    char folder[160];snprintf(folder,sizeof(folder),"/tmp/tapesister-canvas-%lu",(unsigned long)SDL_GetTicks());
    SDL_setenv("TAPESISTER_CAPTURES",folder,1);
    event.type=SDL_MOUSEBUTTONDOWN;event.button.windowID=SDL_GetWindowID(window);event.button.button=SDL_BUTTON_LEFT;event.button.x=270;event.button.y=319;
    if(getenv("TS_TEST_CANVAS_KEYS")) {
        ui.show_keyboard=1;event.button.x=530;event.button.y=300;
    }
    int fx=getenv("TS_TEST_FX_FILE")!=NULL;
    TsSisterUiHit record_hit={0};
    if(fx) {
        sister.model.fx_page=1;
        assert(ts_sister_runtime_reconfigure(&audio.sister,44100,2,error,sizeof(error)));
        TsSisterParameters parameters=audio.sister.parameters;
        parameters.fx.enabled=1;parameters.fx.slot[0].type=TS_SISTER_FX_DISTORTION;
        parameters.fx.slot[0].enabled=1;parameters.fx.slot[0].placement=TS_SISTER_FX_PLACE_POST;
        parameters.fx.slot[0].parameter_a=.7f;parameters.fx.slot[0].mix=.5f;
        ts_sister_runtime_set_parameters(&audio.sister,&parameters);
        ts_sister_runtime_set_master_output_gain(&audio.sister,.5f);
        sister.model.parameters=parameters;
        record_hit=ts_sister_ui_hit_test_model(&sister.model,TS_SISTER_UI_FX_REC_X+10,TS_SISTER_UI_FX_REC_Y+10);
        assert(record_hit.action==TS_SISTER_UI_ACTION_RECORD_FILE);
        sister.model.preset_manage_open=1;
        assert(ts_sister_ui_hit_test_model(&sister.model,548,338).action!=TS_SISTER_UI_ACTION_RECORD_FILE);
        sister.model.preset_manage_open=0;
        for(int page=0;page<3;page+=2) {
            sister.model.fx_page=page;
            assert(ts_sister_ui_hit_test_model(&sister.model,548,338).action!=TS_SISTER_UI_ACTION_RECORD_FILE);
        }
        sister.model.fx_page=1;
        assert(ts_sister_ui_hit_test_model(&sister.model,409,338).action==TS_SISTER_UI_ACTION_PARAMETER);
        const char *shot=getenv("TS_TEST_FX_IDLE_SCREENSHOT");
        if(shot) {static TsFramebuffer fb;ts_sister_runtime_get_snapshot(&audio.sister,&sister.model.routing);ts_sister_ui_render(&fb,&sister.model,&ui.palette);assert(ts_ui_write_ppm(&fb,shot));}
        sister_apply_action(device,&audio,&ui,&instrument,&sister,NULL,NULL,record_hit,44100,2);
    } else assert(main_file_capture_event(&event,window,&audio,&ui,&sister,44100));
    assert(ui.file_record_state==TS_PERFORMANCE_FILE_RECORDING && !audio.sister.enabled);
    assert(sister.model.selected_tap==TS_SISTER_TAP_H1 && sister.model.destination_mode==TS_SISTER_UI_DEST_CURRENT);
    assert(atomic_load(&audio.sister_file_tap)==TS_SISTER_TAP_MIX && ts_note_bank_count(&audio.notes)==4);
    char path[1200];snprintf(path,sizeof(path),"%s",sister.performance_recorder.path);
    float output[4096];
    for(int i=0;i<8;++i)audio_callback(&audio,(Uint8*)(output+i*512),512*sizeof(float));
    poll_file_capture_ui(&ui,&sister);assert(ui.file_record_frames==2048 && ts_sample_hash(&instrument.current)==original);
    if(fx) {
        assert(sister.model.file_capture_state==TS_PERFORMANCE_FILE_RECORDING);
        const char *shot=getenv("TS_TEST_FX_RECORDING_SCREENSHOT");
        if(shot) {static TsFramebuffer fb;sister.model.text_cursor_visible=1;ts_sister_runtime_get_snapshot(&audio.sister,&sister.model.routing);ts_sister_ui_render(&fb,&sister.model,&ui.palette);assert(ts_ui_write_ppm(&fb,shot));}
    }
    /* Every panel gets the recording border and stop strip. */
    static TsFramebuffer frame;
    ui.text_cursor_visible=1;ts_ui_render(&frame,&ui,&instrument);ts_ui_render_file_recording(&frame,&ui);
    uint32_t border=ui.palette.colors[TS_PALETTE_PATTERN_VOLUME];assert(frame.pixels[0]==border && frame.pixels[399*640+639]==border);
    const char *shot=getenv("TS_TEST_CANVAS_RECORDING_SCREENSHOT");if(shot)assert(ts_ui_write_ppm(&frame,shot));
    ui.import_preview_open=1;ui.import_preview_sample=&source;
    ts_ui_render(&frame,&ui,&instrument);ts_ui_render_file_recording(&frame,&ui);assert(frame.pixels[0]==border);
    if(fx)sister_apply_action(device,&audio,&ui,&instrument,&sister,NULL,NULL,record_hit,44100,2);
    else {event.button.x=570;event.button.y=388;assert(main_file_capture_event(&event,window,&audio,&ui,&sister,44100));}
    Uint32 start=SDL_GetTicks();
    while(ts_performance_recorder_state(&sister.performance_recorder)==TS_PERFORMANCE_FILE_STOPPING && SDL_GetTicks()-start<5000)SDL_Delay(1);
    poll_file_capture_ui(&ui,&sister);assert(ui.file_record_state==TS_PERFORMANCE_FILE_IDLE && strstr(ui.status,"SAVED"));
    assert(ts_sample_load_wav(&saved,path,error,sizeof(error)) && saved.channels==2 && saved.frames==2048);
    double energy=0;for(size_t i=0;i<4096;++i) {assert(isfinite(output[i]));assert(fabsf(saved.data[i]-output[i])<.000001f);energy+=fabsf(output[i]);}assert(energy>0);
    assert(ts_note_bank_count(&audio.notes)==4 && ts_sample_hash(&instrument.current)==original);
    if(fx) {
        assert(sister.model.file_capture_state==TS_PERFORMANCE_FILE_IDLE);
        assert(!audio.sister.enabled && sister.model.selected_tap==TS_SISTER_TAP_H1 &&
               sister.model.destination_mode==TS_SISTER_UI_DEST_CURRENT);
        ui.capture_state=TS_CAPTURE_RECORDING;
        sister_apply_action(device,&audio,&ui,&instrument,&sister,NULL,NULL,record_hit,44100,2);
        assert(ui.file_record_state==TS_PERFORMANCE_FILE_IDLE && strstr(sister.model.status,"TILE CAPTURE"));
        ui.capture_state=TS_CAPTURE_IDLE;
    }
    ui.import_preview_open=0;
    /* Failed starts do not leave recording indications or interrupt voices. */
    SDL_AudioDeviceID connected=ts_real_output;ts_real_output=0;main_file_capture_toggle(&audio,&ui,&sister,44100);
    assert(ui.file_record_state==TS_PERFORMANCE_FILE_IDLE && strstr(ui.status,"AVAILABLE"));ts_real_output=connected;
    ui.capture_state=TS_CAPTURE_RECORDING;main_file_capture_toggle(&audio,&ui,&sister,44100);assert(ui.file_record_state==TS_PERFORMANCE_FILE_IDLE);ui.capture_state=TS_CAPTURE_IDLE;
    SDL_setenv("TAPESISTER_CAPTURES","/dev/null/no-directory",1);main_file_capture_toggle(&audio,&ui,&sister,44100);
    assert(ui.file_record_state==TS_PERFORMANCE_FILE_IDLE && strstr(ui.status,"FAILED") && ts_note_bank_count(&audio.notes)==4);
    remove(path);
#ifdef _WIN32
    _rmdir(folder);
#else
    rmdir(folder);
#endif
    ts_performance_recorder_free(&sister.performance_recorder);ts_sample_free(&saved);ts_sample_free(&source);
    ts_instrument_free(&instrument);ts_performance_free(&audio.performance);ts_performance_free(&audio.tile_launchers);
    ts_sister_runtime_free(&audio.sister);ts_capture_free(&audio.capture);
    SDL_DestroyWindow(window);SDL_CloseAudioDevice(device);ts_real_output=0;SDL_Quit();
    puts("Canvas chords and direct output recording tests passed");return 0;
}
