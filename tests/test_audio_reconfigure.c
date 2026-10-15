#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#define TAPEHEAD_EMBEDDED
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>
extern unsigned fixture_asio_opens(void);
extern unsigned fixture_asio_closes(void);
extern void fixture_asio_fail_start(int);
static AudioState audio;
static ExternalInputState input;
static TsUiState ui;
static void silence(void *userdata, Uint8 *stream, int bytes)
{ (void)userdata; memset(stream, 0, (size_t)bytes); }

int main(void)
{
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    assert(!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER));
    SDL_Window *window = SDL_CreateWindow("Audio switch fixture", 0, 0, 640, 400, 0);
    assert(window);
    ts_ui_init(&ui); ts_live_ui = &ui; ts_live_config = &ui.config;
    ui.config.audio_backend = TS_AUDIO_BACKEND_AUTO;
    ui.config.audio_buffer_frames = 256;
    ui.config.audio_output_device[0] = ui.config.record_input_device[0] = 0;
    ts_sister_runtime_init(&audio.sister); ts_capture_init(&audio.capture);
    ts_note_bank_init(&audio.notes); ts_performance_init(&audio.performance);
    ts_performance_init(&audio.tile_launchers); ts_tracker_playback_init(&audio.tracker);
    ts_external_recorder_init(&input.recorder); ts_input_monitor_init(&input.monitor);
    ts_input_activity_init(&input.activity); input.insert = &audio.sister.insert;
    SDL_AudioSpec desired = {0}, obtained, captured;
    desired.freq = 48000; desired.format = AUDIO_F32SYS; desired.channels = 2;
    desired.samples = 256; desired.callback = silence; desired.userdata = &audio;
    assert(ts_audio_open_device(NULL, 0, &desired, &obtained, 0) == TS_AUDIO_LOGICAL_OUTPUT);
    assert(ts_real_output); audio.output_rate = obtained.freq;
    desired.userdata = &input;
    assert(ts_audio_open_device(NULL, 1, &desired, &captured, SDL_AUDIO_ALLOW_CHANNELS_CHANGE) == TS_AUDIO_LOGICAL_CAPTURE);
    assert(ts_real_capture);
    TsMatrixControls matrix = audio.sister.matrix.controls;
    snprintf(matrix.input_name[0], TS_MATRIX_NAME, "MOTU instrument");
    matrix.enabled = 1;
    assert(ts_matrix_set(&audio.sister.matrix, &matrix, 48000));
    char error[256]; const char *path = "audio-reconfigure-test.ini";
    remove(path);

    /* The CFG selector exposes ASIO names before the active backend changes. */
    ui.config_open = 1;
    ts_cycle_audio_backend(-1); /* Auto -> ASIO. */
    assert(ui.config.audio_backend == TS_AUDIO_BACKEND_ASIO && !ts_asio_selected);
    assert(ts_device_catalog.output_count == 2 && ts_device_catalog.input_count == 2);
    assert(ts_audio_config_field_from_point(432, TS_AUDIO_CONFIG_OUTPUT_Y + 5) == -1);
    assert(!strcmp(ui.config.audio_output_device, "Test MOTU ASIO"));
    assert(!strcmp(ui.config.record_input_device, ui.config.audio_output_device));
    assert(ts_audio_main_config_save(&ui.config, path, error, sizeof(error)));
    assert(ts_asio_selected && ts_started_backend == TS_AUDIO_BACKEND_ASIO && ts_asio_running());
    assert(ts_real_output && ts_real_capture && obtained.freq == 48000 && obtained.samples == 128 && obtained.channels == 16);
    assert(input.channels == 16 && input.sample_rate == 48000);
    assert(fixture_asio_opens() == 1 && fixture_asio_closes() == 0);
    assert(!memcmp(&matrix, &audio.sister.matrix.controls, sizeof(matrix)));
    assert(SDL_GetWindowID(window)); /* Video remains alive across audio reinit. */
    TsConfig saved; assert(ts_audio_config_load(&saved, path, error, sizeof(error)));
    assert(saved.audio_backend == TS_AUDIO_BACKEND_ASIO && !strcmp(saved.audio_output_device, "Test MOTU ASIO"));

    /* Change the driver from the INPUT row, then change its buffer. Each
       operation closes the entire old duplex stream before opening one new one. */
    assert(ts_audio_config_field_from_point(432, TS_AUDIO_CONFIG_INPUT_Y + 5) == -1);
    assert(!strcmp(ui.config.audio_output_device, "Test Matrix ASIO"));
    assert(ts_audio_main_config_save(&ui.config, path, error, sizeof(error)));
    assert(fixture_asio_opens() == 2 && fixture_asio_closes() == 1 && ts_asio_running());
    ui.config.audio_buffer_frames = 512;
    assert(ts_audio_main_config_save(&ui.config, path, error, sizeof(error)));
    assert(fixture_asio_opens() == 3 && fixture_asio_closes() == 2);
    assert(!ts_audio_reopen_required(&ui.config)); /* Negotiated != requested buffer is not pending. */
    assert(ts_audio_main_config_save(&ui.config, path, error, sizeof(error)));
    assert(fixture_asio_opens() == 3); /* Saving unrelated settings does not reopen. */

    /* Recording blocks a switch without closing or persisting anything. */
    input.recorder.state = TS_EXTERNAL_CAPTURE_ARMED;
    ui.config.audio_buffer_frames = 1024;
    assert(!ts_audio_main_config_save(&ui.config, path, error, sizeof(error)) && strstr(error, "recording"));
    assert(fixture_asio_opens() == 3 && fixture_asio_closes() == 2);
    input.recorder.state = TS_EXTERNAL_CAPTURE_IDLE;
    assert(ts_audio_config_load(&saved, path, error, sizeof(error)) && saved.audio_buffer_frames == 512);
    ui.config.audio_buffer_frames = 512;

    /* Failure restores the prior driver and both logical endpoint views. */
    snprintf(ui.config.audio_output_device, sizeof(ui.config.audio_output_device), "Missing ASIO");
    ts_audio_link_asio_selection(0);
    assert(!ts_audio_main_config_save(&ui.config, path, error, sizeof(error)));
    assert(strstr(error, "Previous audio restored") && ts_asio_running() && ts_real_capture);
    assert(!strcmp(ts_active_output, "Test Matrix ASIO"));
    assert(!memcmp(&matrix, &audio.sister.matrix.controls, sizeof(matrix)));
    assert(ts_audio_config_load(&saved, path, error, sizeof(error)) && !strcmp(saved.audio_output_device, "Test Matrix ASIO"));
    ui.config = saved;

    /* Auto must clear the ASIO dummy override and return to SDL. A failure to
       start ASIO subsequently restores that working SDL session. */
    ui.config.audio_backend = TS_AUDIO_BACKEND_AUTO;
    ui.config.audio_output_device[0] = ui.config.record_input_device[0] = 0;
    assert(ts_audio_main_config_save(&ui.config, path, error, sizeof(error)));
    assert(!ts_asio_selected && !strcmp(ts_native_current_driver(), "dummy") && ts_real_capture);
    snprintf(ui.config.record_input_device, sizeof(ui.config.record_input_device), "Missing SDL input");
    assert(!ts_audio_main_config_save(&ui.config, path, error, sizeof(error)));
    assert(strstr(error, "Previous audio restored") && ts_real_capture && ts_real_output);
    ui.config.record_input_device[0] = 0;
    fixture_asio_fail_start(1);
    ui.config.audio_backend = TS_AUDIO_BACKEND_ASIO;
    assert(!ts_audio_main_config_save(&ui.config, path, error, sizeof(error)));
    assert(strstr(error, "Previous audio restored") && !ts_asio_selected && ts_real_output && ts_real_capture);
    fixture_asio_fail_start(0);
    assert(ts_audio_main_config_save(&ui.config, path, error, sizeof(error)));
    assert(ts_asio_running());
    /* If the hardware remains unavailable even during rollback, the next save
       recovers both endpoints rather than leaving the input silently closed. */
    fixture_asio_fail_start(1); ui.config.audio_buffer_frames = 1024;
    assert(!ts_audio_main_config_save(&ui.config, path, error, sizeof(error)));
    assert(strstr(error, "Audio unavailable") && !ts_real_output && !ts_real_capture);
    fixture_asio_fail_start(0);
    assert(ts_audio_main_config_save(&ui.config, path, error, sizeof(error)));
    assert(ts_asio_running() && ts_real_capture);

    /* The same CFG ranges apply to Windows ASIO and SDL, without moving routes. */
    ui.config.audio_input_range=(TsAudioChannelRange){3,6};
    ui.config.audio_output_range=(TsAudioChannelRange){3,10};
    assert(ts_audio_main_config_save(&ui.config,path,error,sizeof(error)));
    assert(obtained.channels==10 && input.channels==6 && audio.output_range.first==3);
    assert(!ts_audio_reopen_required(&ui.config));
    assert(!memcmp(&matrix,&audio.sister.matrix.controls,sizeof(matrix)));
    assert(ts_audio_config_load(&saved,path,error,sizeof(error)) && saved.audio_output_range.last==10);
    ui.config.audio_output_range.last=17;
    unsigned opens=fixture_asio_opens();
    assert(!ts_audio_main_config_save(&ui.config,path,error,sizeof(error)) && strstr(error,"range"));
    assert(fixture_asio_opens()==opens && ts_asio_running());
    ui.config=saved;
    snprintf(ui.config.audio_output_device,sizeof(ui.config.audio_output_device),"Missing ASIO");
    ts_audio_link_asio_selection(0);ui.config.audio_output_range=(TsAudioChannelRange){1,16};
    assert(!ts_audio_main_config_save(&ui.config,path,error,sizeof(error)) && strstr(error,"restored"));
    assert(audio.output_range.first==3 && audio.output_range.last==10 && input.range.first==3);
    ui.config=saved;
    ui.config.audio_backend=TS_AUDIO_BACKEND_AUTO;
    ui.config.audio_output_device[0]=ui.config.record_input_device[0]=0;
    ui.config.audio_output_range=(TsAudioChannelRange){2,2};ui.config.audio_input_range=(TsAudioChannelRange){2,2};
    assert(ts_audio_main_config_save(&ui.config,path,error,sizeof(error)));
    assert(!ts_asio_selected && obtained.channels==2 && input.range.first==2 && audio.output_range.last==2);
    float capture_data[512];assert(input.channels<=255);
    for(unsigned i=0;i<input.channels*2u;++i)capture_data[i]=(float)(i+1);
    external_input_callback(&input,(Uint8 *)capture_data,input.channels*2*sizeof(float));
    for(unsigned i=0;i<input.channels*2u;++i)assert(capture_data[i]==(i%input.channels==1?(float)(i+1):0));
    /* Arrows use physical numbers; touching the center leaves a range alone. */
    ts_audio_config_field_from_point(425,TS_AUDIO_CONFIG_OUTPUT_RANGE_Y+5);assert(!ui.config.audio_output_range.last);
    ts_audio_config_field_from_point(290,TS_AUDIO_CONFIG_OUTPUT_RANGE_Y+5);assert(ui.config.audio_output_range.last==2);
    ts_audio_config_field_from_point(135,TS_AUDIO_CONFIG_OUTPUT_RANGE_Y+5);assert(ui.config.audio_output_range.first==1);
    ts_audio_config_field_from_point(210,TS_AUDIO_CONFIG_OUTPUT_RANGE_Y+5);assert(ui.config.audio_output_range.first==1);

    ts_audio_session_close(); ts_audio_catalog_free();
    ts_sister_runtime_free(&audio.sister); ts_external_recorder_free(&input.recorder);
    ts_capture_free(&audio.capture); ts_performance_free(&audio.performance);
    ts_performance_free(&audio.tile_launchers); ts_tracker_playback_free(&audio.tracker);
    remove(path); SDL_DestroyWindow(window); SDL_Quit();
    puts("Audio CFG: backend/duplex/buffer switches, linked catalogs, recording guard, rollback, start failure, persistence and routing preservation passed");
    return 0;
}
