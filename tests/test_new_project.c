#ifdef NDEBUG
#undef NDEBUG
#endif
#include <stdlib.h>
/* Fail the staging allocation, before any destructive action. */
static int fail_new_allocation;
static void *test_calloc(size_t n, size_t size)
{
    if (fail_new_allocation) { fail_new_allocation = 0; return NULL; }
    return calloc(n, size);
}
#define calloc test_calloc
#define SDL_MAIN_HANDLED
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#undef calloc
#include <assert.h>

static AudioState audio;
static TsUiState ui;
static TsInstrument bank, record;
static TsSamplePages pages;
static SisterWindow sister;
static ExternalInputState input;
static MosaicController mosaic;
static PortalController portal;
static TransformController transform;
static ImportController importer;
static BrowserAudition audition;
static PendingFileOperation file;
static TsSample clipboard, selection, drone, fm;
static FmBankHistory history;
static TsFmSeedSequence seeds;
static TsExchangeOffer exchange;
static int record_active;
static TsFramebuffer frame;
static ProjectSession session = {
    &audio, &ui, &bank, &record, &pages, &record_active, &sister, &input,
    &mosaic, &portal, &transform, &importer, &audition, &file,
    &clipboard, &selection, &drone, &fm, &history, &seeds, &exchange
};
static char error[160];

static void populate(void)
{
    float samples[512];
    for (int i=0;i<512;++i) samples[i]=.2f*sinf((float)i*.12f);
    TsSample sample={.data=samples,.frames=512,.sample_rate=48000,.channels=1};
    assert(ts_instrument_import_sample(&bank,&sample,0,0,0,TS_LOOP_FORWARD,error,sizeof(error)));
    assert(ts_instrument_commit_current(&bank,error,sizeof(error)));
    assert(ts_sample_clone(&clipboard,&sample,error,sizeof(error)));
    TsMosaicSource *source=ts_mosaic_source(ui.mosaic,&sample,error,sizeof(error));
    assert(source && ts_mosaic_add(ui.mosaic,source,0,0));
    ui.mosaic->playing=1;
    assert(ts_sample_pages_append(&pages,error,sizeof(error)));
    int slot=ts_matrix_effect_add(&audio.sister.matrix.controls,TS_SISTER_FX_REVERB);
    assert(slot>=0);
    assert(ts_matrix_effects_restore(&audio.sister.matrix_effects,&audio.sister.matrix.controls,48000));
    audio.tune_reference_enabled=1;audio.tune_reference_target=.2f;
    snprintf(ui.project_path,sizeof(ui.project_path),"old-project.tsr");
}
static uint64_t project_hash(void)
{
    return runtime_project_state_hash(&pages,&bank,&record,record_active,&audio.sister,&audio.keyboard_sequence);
}
static void run_save(const char *path)
{
    file=(PendingFileOperation){.active=1,.presented=1,.mode=TS_BROWSER_SAVE_RECIPE};
    assert(ts_sample_pages_bundle_project_path(path,file.path,sizeof(file.path),error,sizeof(error)));
    run_pending_file_operation(0,&audio,&ui,&bank,&selection,&importer,&pages,&record,record_active,&file);
}
static int cancelled_import(void *data)
{
    ImportWorker *worker=data;
    while (!SDL_AtomicGet(&worker->cancel)) SDL_Delay(1);
    return 0;
}
static void assert_empty(void)
{
    assert(pages.page_count==1 && pages.active_page==0 && pages.active_live);
    assert(!record_active && !bank.current.data && !record.current.data);
    assert(!ts_instrument_bank_count(&bank) && !ts_instrument_bank_count(&record));
    assert(!clipboard.data && !selection.data && !drone.data && !fm.data);
    assert(!audio.playing && !audio.tune_reference_enabled && !audio.workspace_bank);
    assert(!audio.sister.enabled && !ts_matrix_effects_memory(&audio.sister.matrix_effects));
    assert(!ui.mosaic->playing && !ui.mosaic->events[0].id && !ui.mosaic->history_count);
    assert(!ui.project_path[0] && !ui.project_modified && !ui.new_project_pending && !ui.exit_for_new);
    assert(ui.tracker==&pages.tracker && audio.mosaic==ui.mosaic && pages.mosaic==ui.mosaic);
    assert(portal.pages==&pages && mosaic.pages==&pages && portal.mosaic==&mosaic);
    assert(ui.config.master_output_percent==63 && !strcmp(ui.config.midi_input_device,"PRESERVE MIDI"));
    assert(ui.config.audio_buffer_frames==512 && ui.palette.colors[0]==0xff123456);
    assert(ui.saved_state_hash==project_hash());
    /* The suspended embedded editor must not export its old score into New. */
    assert(pages.tracker.bpm!=173 && !pages.tracker.embedded_size);
    assert(ts_tapehead_export(&pages.tracker,error,sizeof(error)));
    assert(!pages.tracker.embedded_size && ui.saved_state_hash==project_hash());
    float output[2048];
    for(int pass=0;pass<4;++pass) {
        for(int i=0;i<2048;++i)output[i]=1;
        audio_callback(&audio,(Uint8 *)output,sizeof(output));
        for(int i=0;i<2048;++i)assert(isfinite(output[i]) && fabsf(output[i])<1e-7f);
    }
}
int main(int argc,char **argv)
{
    SDL_SetMainReady();SDL_setenv("SDL_VIDEODRIVER","dummy",1);
    assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER));
    ts_ui_init(&ui);ts_instrument_init(&bank);ts_instrument_init(&record);
    assert(ts_sample_pages_init(&pages,error,sizeof(error)));
    ts_sister_runtime_init(&audio.sister);audio.output_rate=48000;audio.output_device_channels=2;
    ts_note_bank_init(&audio.notes);ts_tracker_playback_init(&audio.tracker);
    ts_keyboard_sequence_init(&audio.keyboard_sequence);ts_audio_mixer_init(&audio.mixer);
    ts_capture_init(&audio.capture);ts_performance_init(&audio.performance);ts_performance_init(&audio.tile_launchers);
    ts_realtime_diagnostics_init(&audio.realtime_diagnostics);tapeLinkReaderInit(&audio.live_link);
    ts_external_recorder_init(&input.recorder);ts_input_monitor_init(&input.monitor);ts_input_ownership_init(&input.ownership);
    audio.input_monitor=&input.monitor;audio.external_monitor_enabled=&input.direct_monitor;
    audio.record_bank_recorder=&input.recorder;audio.record_source=&input.record_source;
    audio.sister_file_recorder=&sister.performance_recorder;input.insert=&audio.sister.insert;
    ts_performance_recorder_init(&sister.performance_recorder);
    transform_controller_init(&transform);import_controller_init(&importer);fm_bank_history_init(&history);
    ui.mosaic=audio.mosaic=pages.mosaic=ts_mosaic_create();assert(ui.mosaic);
    ui.tracker=&pages.tracker;mosaic.pages=&pages;
    ui.config.master_output_percent=63;ui.config.audio_buffer_frames=512;
    snprintf(ui.config.midi_input_device,sizeof(ui.config.midi_input_device),"PRESERVE MIDI");
    ui.palette.colors[0]=0xff123456;
    assert(ts_sister_runtime_reconfigure(&audio.sister,48000,2,error,sizeof(error)));
    ui.saved_state_hash=project_hash();
    populate();
    SDL_Window *window=SDL_CreateWindow("New project fixture",0,0,640,400,SDL_WINDOW_HIDDEN);assert(window);
    TsTapeHeadHost host={.window=window};
    assert(ts_tapehead_init(&host,48000,error,sizeof(error)));
    pages.tracker.bpm=173;
    assert(ts_tapehead_sync(&pages,&bank,48000,error,sizeof(error)));
    assert(ts_tapehead_export(&pages.tracker,error,sizeof(error)));
    uint64_t before=project_hash();
    begin_new_confirmation(&session);
    assert(ui.exit_confirm_open && ui.exit_for_new && ui.exit_has_unsaved && ui.exit_choice==2);
    if(argc>1) { ts_ui_render(&frame,&ui,&bank);assert(ts_ui_write_ppm(&frame,argv[1])); }
    assert(!exit_confirmation_activate(&ui)); /* Default Enter is Cancel. */
    assert(!ui.exit_confirm_open && !ui.new_project_pending && project_hash()==before);

    begin_new_confirmation(&session);ui.exit_choice=0;
    assert(!exit_confirmation_activate(&ui) && ui.exit_after_save==1);
    browser_cancel(&ui);
    assert(!ui.exit_after_save && !ui.exit_for_new && project_hash()==before);
    begin_new_confirmation(&session);ui.exit_choice=0;exit_confirmation_activate(&ui);
    run_save("/__tapesister_missing_parent__/cannot-save.tsr");
    assert(!ui.exit_after_save && !ui.new_project_pending && !ui.exit_for_new && project_hash()==before);
    browser_cancel(&ui);

    /* Allocation failure must keep data and audio ownership unchanged. */
    TsMosaic *old_mosaic=ui.mosaic;
    fail_new_allocation=1;
    assert(!project_new(NULL,0,0,&session));
    assert(project_hash()==before && ui.mosaic==old_mosaic && bank.current.data);

    begin_new_confirmation(&session);ui.exit_choice=0;exit_confirmation_activate(&ui);
    run_save("new-project-flow.tsr");
    assert(ui.exit_after_save==2 && ui.exit_for_new && !ui.new_project_pending);
    static TsSamplePages saved;static TsInstrument saved_bank,saved_record;
    assert(ts_sample_pages_init(&saved,error,sizeof(error)));
    ts_instrument_init(&saved_bank);ts_instrument_init(&saved_record);
    assert(ts_sample_pages_load_project(&saved,&saved_bank,&saved_record,"new-project-flow/new-project-flow.tsr",error,sizeof(error)));
    assert(saved.page_count==2 && saved_bank.current.frames==512);
    ts_sample_pages_free(&saved);ts_instrument_free(&saved_bank);ts_instrument_free(&saved_record);
    assert(project_new(NULL,0,0,&session));assert_empty();

    /* Cancel and join a late decode; its result cannot cross the reset. */
    populate();importer.worker=calloc(1,sizeof(*importer.worker));assert(importer.worker);
    importer.worker->thread=SDL_CreateThread(cancelled_import,"new-project-test",importer.worker);
    assert(importer.worker->thread);
    begin_new_confirmation(&session);ui.exit_choice=1;exit_confirmation_activate(&ui);
    assert(ui.new_project_pending);
    assert(project_new(NULL,0,0,&session));assert(!importer.worker);assert_empty();

    /* Clear while the Record bank owns the canvas. */
    populate();TsInstrument *record_ptr=&record;
    assert(set_record_bank(NULL,0,0,&input,&audio,&ui,&bank,&pages,&record_ptr,&record_active,1,&transform));
    assert(record_active);
    assert(project_new(NULL,0,0,&session));assert_empty();

    /* Retire a Mosaic editor's parked Sample bank and document history too. */
    populate();uint64_t id=ui.mosaic->events[0].id;
    assert(mosaic_enter(0,&audio,&ui,&bank,&mosaic,id));assert(mosaic.active);
    assert(project_new(NULL,0,0,&session));assert_empty();
    begin_new_confirmation(&session);assert(ui.exit_confirm_open && ui.exit_choice==2 && !ui.exit_has_unsaved);
    exit_confirmation_activate(&ui);

    assert(ts_tapehead_sync(&pages,&bank,48000,error,sizeof(error)));
    assert(ts_tapehead_export(&pages.tracker,error,sizeof(error)));
    assert(pages.tracker.bpm!=173 && !ts_tapehead_running());
    ts_tapehead_close();SDL_DestroyWindow(window);
    project_audio_free(&audio);ts_mosaic_free(ui.mosaic);ts_sample_pages_free(&pages);
    ts_instrument_free(&bank);ts_instrument_free(&record);fm_bank_history_free(&history);
    ts_performance_recorder_free(&sister.performance_recorder);SDL_Quit();
    puts("New project: confirmation, cancel, save failure/success, allocation failure, workers, banks and silent reset passed");
    return 0;
}
