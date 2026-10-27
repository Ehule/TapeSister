/* main_sdl.c needs POSIX declarations; expose them before the interposed headers. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
static int recovery_choices[4],recovery_choice_count,recovery_choice_at;
static int recovery_save_button;
static int test_recovery_message(const SDL_MessageBoxData *data,int *button)
{
    if(recovery_choice_at>=recovery_choice_count)return -1;
    if(strcmp(data->title,"TapeSister project recovery"))abort();
    if(!strstr(data->message,"User-added files may be split") ||
       !strstr(data->message,"BACKUP LOCATION") || !strstr(data->message,"Originals will not be deleted"))abort();
    recovery_save_button=0;
    for(int i=0;i<data->numbuttons;++i)if(data->buttons[i].buttonid==1)recovery_save_button=1;
    *button=recovery_choices[recovery_choice_at++];return *button==-99?-1:0;
}
static int recovery_copy_errors;
static int test_recovery_error(Uint32 flags,const char *title,const char *message,SDL_Window *window)
{
    if(!strcmp(title,"Recovery copy incomplete")){++recovery_copy_errors;return 0;}
    return SDL_ShowSimpleMessageBox(flags,title,message,window);
}
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
#define SDL_ShowMessageBox test_recovery_message
#define SDL_ShowSimpleMessageBox test_recovery_error
#include "../src/main_sdl.c"
#undef main
#undef calloc
#undef SDL_ShowMessageBox
#undef SDL_ShowSimpleMessageBox
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
    assert(audio.sister.parameters.fx.slot_count==7 && ui.config.pedalboard_slots==7);
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
#include "test_project_recovery_ui.inc"

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
    ui.config.pedalboard_slots=7;
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
    /* Optional user-project reproduction through the actual Open path. */
    if(argc==3 && (!strcmp(argv[1],"--open-project") || !strcmp(argv[1],"--mosaic-revert-mute"))) {
        uint64_t disk_hash=0;
        for(int pass=0;pass<2;++pass) {
            int ok=load_instrument(0,&audio,&ui,&bank,&pages,&record,0,argv[2]);
            fprintf(stderr,"Project Open: %s\n",ui.status);assert(ok);
            int cards=0,sources=0;double max_x=0;
            for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(ui.mosaic->events[i].id){++cards;max_x=fmax(max_x,ui.mosaic->events[i].x);}
            for(TsMosaicSource *s=ui.mosaic->sources;s;s=s->next)++sources;
            printf("pass=%d cards=%d sources=%d max_x=%.17g hash=%llu\n",pass+1,cards,sources,max_x,(unsigned long long)ts_mosaic_hash(ui.mosaic));
            if(!pass)disk_hash=ts_mosaic_hash(ui.mosaic);
            else assert(ts_mosaic_hash(ui.mosaic)==disk_hash);
            ui.mosaic_open=1;mosaic_fit_all(&ui,&mosaic);ts_ui_render(&frame,&ui,&bank);
            ui.mosaic->playing=1;ts_mosaic_begin_block(ui.mosaic,512);double energy=0;
            for(int i=0;i<512;++i) {
                TsStereoFrame v=ts_mosaic_read(ui.mosaic,48000);assert(isfinite(v.l) && isfinite(v.r));energy+=fabs(v.l)+fabs(v.r);
            }
            assert(!cards || energy>0);
            if(!strcmp(argv[1],"--mosaic-revert-mute")) {
                /* Exercise the actual callback/Matrix/effects after the user's key sequence. */
                ui.mosaic->repeat=1;ts_mosaic_seek(ui.mosaic,0);
                if(audio.sister.matrix.controls.enabled && !audio.sister.matrix.controls.board_insert &&
                   !(audio.sister.matrix.controls.serial_mask&(1u<<TS_MATRIX_BOARD)) &&
                   !audio.sister.parameters.fx.enabled) {
                    assert(audio.sister.post_fx.master_engage.current==0 &&
                           !audio.sister.post_fx.master_engage.remaining);
                    /* The saved OFF/SEND return is intentionally silent. Use
                       the user's confirmed INSERT workaround in memory so
                       mute recovery is tested on an audible route. */
                    TsMatrixControls route=audio.sister.matrix.controls;route.board_insert=1;
                    assert(ts_matrix_set(&audio.sister.matrix,&route,48000));
                    puts("Saved OFF/SEND restored without a fade; mute fixture uses INSERT in memory.");
                }
                mosaic_poll(0,&ui,&bank,&mosaic);mosaic_select_only(&ui,ui.mosaic->events[0].id);
                SDL_Event e;SDL_zero(e);e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_m;e.key.keysym.mod=KMOD_SHIFT;
                for(int stage=0;stage<4;++stage) {
                    if(stage) {
                        if(stage==2) {
                            e.key.keysym.sym=SDLK_a;e.key.keysym.mod=KMOD_CTRL;
                            assert(mosaic_event(&e,window,0,&audio,&ui,&bank,&mosaic,&portal,&sister,&transform,48000));
                            e.key.keysym.sym=SDLK_m;e.key.keysym.mod=KMOD_SHIFT;
                        }
                        assert(mosaic_event(&e,window,0,&audio,&ui,&bank,&mosaic,&portal,&sister,&transform,48000));
                    }
                    double energy=0;float out[1024];
                    int blocks=stage==2?800:64; /* Let effects decay and the arrangement wrap while muted. */
                    for(int block=0;block<blocks;++block) {
                        audio_callback(&audio,(Uint8 *)out,sizeof(out));
                        if(block==blocks-1)for(int i=0;i<1024;++i) {assert(isfinite(out[i]));energy+=fabs(out[i]);}
                    }
                    printf("pass=%d mute-stage=%d callback-energy=%g\n",pass,stage,energy);
                    if(stage==0 || stage==3)assert(energy>0);
                }
                ts_mosaic_checkpoint(ui.mosaic);ui.mosaic->events[0].start=21;ui.mosaic->events[0].muted=1;
            }
        }
        goto cleanup;
    }
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
    char *logs=SDL_GetPrefPath("TapeSisterTests","New Project Health");assert(logs);
    assert(ts_session_log_start(logs,0));SDL_free(logs);
    run_save("/__tapesister_missing_parent__/cannot-save.tsr");
    TsSessionLogStatus log;ts_session_log_status(&log);assert(!log.export_result);
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
    for(int i=0;i<500;++i){ts_session_log_status(&log);if(log.export_result!=1)break;SDL_Delay(10);}
    assert(log.export_result==2);
    FILE *previous_log=fopen("new-project-flow/logs/previous-session.saved.jsonl","wb");assert(previous_log);
    fputs("preserve older session\n",previous_log);fclose(previous_log);
    ui.mosaic_open=1;ui.mosaic_scroll=3;ui.mosaic_xscroll=80;ui.mosaic_scale=12;ui.mosaic_hscale=.5;
    mosaic_select_only(&ui,ui.mosaic->events[0].id);
    uint64_t save_selection=ui.mosaic_selected;
    run_save("new-project-flow.tsr"); /* Complete bundle replacement keeps logs. */
    ui_sync_dialog_workspaces(&ui);
    assert(ui.mosaic_open && ui.mosaic_selected==save_selection && ui.mosaic_scroll==3 && ui.mosaic_xscroll==80);
    assert(ui.mosaic_scale==12 && ui.mosaic_hscale==.5);
    run_save("/__tapesister_missing_parent__/cannot-save.tsr");
    ui_sync_dialog_workspaces(&ui);
    assert(strstr(ui.status,"SAVE FAILED") && ui.mosaic_open && ui.mosaic_selected==save_selection);
    assert(ui.mosaic_scroll==3 && ui.mosaic_xscroll==80 && ui.mosaic_scale==12 && ui.mosaic_hscale==.5);
    run_save("new-project-flow.tsr"); /* A prior export can still be in flight. */
    static TsSamplePages saved;static TsInstrument saved_bank,saved_record;
    assert(ts_sample_pages_init(&saved,error,sizeof(error)));
    ts_instrument_init(&saved_bank);ts_instrument_init(&saved_record);
    assert(ts_sample_pages_load_project(&saved,&saved_bank,&saved_record,"new-project-flow/new-project-flow.tsr",error,sizeof(error)));
    assert(saved.page_count==2 && saved_bank.current.frames==512);
    ts_sample_pages_free(&saved);ts_instrument_free(&saved_bank);ts_instrument_free(&saved_record);
    /* Invalid external project metadata is rejected before touching banks,
       Mosaic transport, tracker editor or Sister runtime state. */
    TsSisterProjectState mismatched;int present=0;
    const char *load_path="new-project-flow/new-project-flow.tsr";
    /* Reopening the active path is a revert, including live Mosaic edits. */
    uint64_t saved_mosaic=ts_mosaic_hash(ui.mosaic);
    for(int pass=0;pass<2;++pass) {
        TsMosaic *same_model=ui.mosaic;uint64_t previous_epoch=ui.mosaic->epoch;
        ts_mosaic_checkpoint(ui.mosaic);
        ui.mosaic->events[0].start=17;ui.mosaic->events[0].muted=1;
        ui.mosaic->gain=.1f;ui.mosaic->playing=1;
        assert(ts_mosaic_copy(ui.mosaic,ui.mosaic->events[0].id,20,0));
        file=(PendingFileOperation){.active=1,.presented=1,.mode=TS_BROWSER_LOAD_WAV};
        snprintf(file.path,sizeof(file.path),"%s",load_path);
        run_pending_file_operation(0,&audio,&ui,&bank,&selection,&importer,&pages,&record,0,&file);
        assert(strstr(ui.status,"OPENED TSR PROJECT"));
        assert(ui.mosaic==same_model && ui.mosaic->epoch!=previous_epoch);
        assert(ts_mosaic_hash(ui.mosaic)==saved_mosaic && !ui.mosaic->playing && !ui.mosaic->history_count);
    }
    assert(ts_sister_project_state_load(&mismatched,load_path,48000,&present,error,sizeof(error)) && present);
    ++mismatched.page_count;
    assert(ts_sister_project_state_save(&mismatched,load_path,error,sizeof(error)));
    before=project_hash();ui.mosaic->playing=1;
    const float *old_audio=bank.current.data;uint64_t old_epoch=ui.mosaic->epoch;
    assert(!load_instrument(0,&audio,&ui,&bank,&pages,&record,0,load_path));
    assert(strstr(ui.status,"does not match") && project_hash()==before);
    assert(bank.current.data==old_audio && ui.mosaic->epoch==old_epoch && ui.mosaic->playing);
    --mismatched.page_count;
    assert(ts_sister_project_state_save(&mismatched,load_path,error,sizeof(error)));
    assert(!load_instrument(0,&audio,&ui,&bank,&pages,&record,0,"missing-project.tsr"));
    assert(project_hash()==before && bank.current.data==old_audio && ui.mosaic->playing);
    assert(project_new(NULL,0,0,&session));assert_empty();
    /* New clears the active path while the accepted save still targets the
       previous project's bundle. Immediate shutdown must finish the copy. */
    ts_session_log_stop();ts_session_log_status(&log);assert(log.export_result==2);
    assert(!strncmp(log.export_path,"new-project-flow/logs/health-session-",36));
    SDL_RWops *session_history=SDL_RWFromFile(log.export_path,"rb");assert(session_history);SDL_RWclose(session_history);
    previous_log=fopen("new-project-flow/logs/previous-session.saved.jsonl","rb");assert(previous_log);
    char previous_text[40];assert(fgets(previous_text,sizeof(previous_text),previous_log));fclose(previous_log);
    assert(!strcmp(previous_text,"preserve older session\n"));remove("new-project-flow/logs/previous-session.saved.jsonl");
    remove(log.path);remove(log.export_path);

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

    recovery_controls();
    assert(ts_tapehead_sync(&pages,&bank,48000,error,sizeof(error)));
    assert(ts_tapehead_export(&pages.tracker,error,sizeof(error)));
    assert(pages.tracker.bpm!=173 && !ts_tapehead_running());
cleanup:
    ts_tapehead_close();SDL_DestroyWindow(window);
    project_audio_free(&audio);ts_mosaic_free(ui.mosaic);ts_sample_pages_free(&pages);
    ts_instrument_free(&bank);ts_instrument_free(&record);fm_bank_history_free(&history);
    ts_performance_recorder_free(&sister.performance_recorder);SDL_Quit();
    puts(argc==3 && !strcmp(argv[1],"--open-project") ?
         "Supplied project: Open, repeated replacement, UI render and playback passed" :
         "New project: confirmation, cancel, save failure/success, allocation failure, workers, banks and silent reset passed");
    return 0;
}
