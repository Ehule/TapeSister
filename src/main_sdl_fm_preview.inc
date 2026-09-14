/* One bounded render in flight. Edits coalesce, and only the newest revision
   may replace the audio-owned preview; workers never access UI/audio state. */
typedef struct {
    SDL_Thread *thread;
    SDL_atomic_t done;
    TsFmPatch patch;
    TsSample sample;
    uint32_t serial, seed;
    int ok, quiet;
    char error[160];
} FmPreviewJob;

static int fm_preview_worker(void *opaque)
{
    FmPreviewJob *job=opaque;
    const TsTuning unity={TS_KEYBOARD_BASE_NOTE,0};
    job->ok=ts_fm_render_preview(&job->sample,&job->patch,TS_FM_LOGIC_SECONDS,
        (float)ts_tuning_frequency(&unity),44100u,job->seed,job->error,sizeof(job->error));
    job->quiet=job->ok && !ts_fm_sample_is_usable(&job->sample);
    SDL_AtomicSet(&job->done,1);
    return 0;
}

static void discard_fm_preview_job(TsUiState *ui)
{
    FmPreviewJob *job=ui->fm_preview_job;
    if(job) {
        SDL_WaitThread(job->thread,NULL);
        ts_sample_free(&job->sample);free(job);
    }
    ui->fm_preview_job=NULL;ui->fm_preview_pending=0;
    ++ui->fm_preview_serial;
}

static void publish_fm_preview(SDL_AudioDeviceID device,AudioState *audio,
                              TsUiState *ui,TsSample *preview,FmPreviewJob *job)
{
    /* A failed render publishes empty audio, never a stale, unrelated sound. */
    if(device)SDL_LockAudioDevice(device);
    TsSample old=*preview;*preview=job->sample;job->sample=old;
    ts_note_bank_replace_sample(&audio->notes,preview,preview,audio->output_rate);
    ui->fm_preview_sample=preview;
    if(device)SDL_UnlockAudioDevice(device);
    ts_ui_waveform_cache_invalidate(ui,TS_UI_WAVEFORM_FM);
    if(!job->ok)snprintf(ui->fm_message,sizeof(ui->fm_message),"FM PREVIEW FAILED: %.72s",job->error);
    else if(job->quiet)snprintf(ui->fm_message,sizeof(ui->fm_message),"FM PREVIEW QUIET - CURRENT SETTINGS RETAINED");
    else snprintf(ui->fm_message,sizeof(ui->fm_message),"%s / %s  %d VOICE MASK",
        ts_fm_structure_name(ui->fm_patch.structure),ts_fm_interaction_name(ui->fm_patch.interaction),
        (int)ui->fm_patch.active_mask);
}

/* Initial opening is synchronous so immediately played keys have a sample.
   Subsequent sound edits use the worker and never block event processing. */
static int render_fm_workspace(SDL_AudioDeviceID device,AudioState *audio,
                              TsUiState *ui,const TsInstrument *instrument,TsSample *preview)
{
    discard_fm_preview_job(ui);
    FmPreviewJob job={0};job.patch=ui->fm_patch;job.seed=instrument->generator.seed^0x50524556u;
    fm_preview_worker(&job);
    publish_fm_preview(device,audio,ui,preview,&job);
    ts_sample_free(&job.sample);
    return job.ok;
}

static void request_fm_preview(TsUiState *ui,const TsInstrument *instrument)
{
    ++ui->fm_preview_serial;ui->fm_preview_seed=instrument->generator.seed^0x50524556u;
    ui->fm_preview_pending=1;ui->fm_preview_due=SDL_GetTicks()+50u;
    snprintf(ui->fm_message,sizeof(ui->fm_message),"FM PREVIEW UPDATING");
}

static void poll_fm_preview(SDL_AudioDeviceID device,AudioState *audio,
                            TsUiState *ui,TsSample *preview)
{
    FmPreviewJob *job=ui->fm_preview_job;
    if(job && SDL_AtomicGet(&job->done)) {
        SDL_WaitThread(job->thread,NULL);
        if(job->serial==ui->fm_preview_serial)
            publish_fm_preview(device,audio,ui,preview,job);
        ts_sample_free(&job->sample);free(job);ui->fm_preview_job=NULL;
    }
    if(ui->fm_preview_job || !ui->fm_preview_pending ||
       !SDL_TICKS_PASSED(SDL_GetTicks(),ui->fm_preview_due))return;
    job=calloc(1,sizeof(*job));
    if(!job) {
        ui->fm_preview_pending=0;
        FmPreviewJob failed={0};snprintf(failed.error,sizeof(failed.error),"Out of memory starting preview");
        publish_fm_preview(device,audio,ui,preview,&failed);ts_sample_free(&failed.sample);return;
    }
    job->patch=ui->fm_patch;job->seed=ui->fm_preview_seed;job->serial=ui->fm_preview_serial;
    job->thread=SDL_CreateThread(fm_preview_worker,"FM preview",job);
    ui->fm_preview_pending=0;
    if(!job->thread) {
        snprintf(job->error,sizeof(job->error),"Could not start preview worker");
        publish_fm_preview(device,audio,ui,preview,job);ts_sample_free(&job->sample);free(job);return;
    }
    ui->fm_preview_job=job;
}

static void toggle_fm_unison_workspace(TsUiState *ui,const TsInstrument *instrument)
{
    int had_source=ui->fm_patch.has_unison_source;
    int enabled=ts_fm_toggle_unison(&ui->fm_patch);
    ui->fm_voice_bank=0;
    request_fm_preview(ui,instrument);
    snprintf(ui->fm_message,sizeof(ui->fm_message),
        enabled ? "UNISON ON - NINE VOICES + THREE LOWER VOICES" :
        had_source ? "UNISON OFF - ORIGINAL SOUND RESTORED" :
        "UNISON OFF - V1 SOURCE; OLDER TILE HAS NO ORIGINAL");
}

static void toggle_fm_voice_workspace(TsUiState *ui,const TsInstrument *instrument,int voice)
{
    if(voice<0 || voice>=ts_fm_voice_count(&ui->fm_patch))return;
    ui->fm_patch.active_mask^=1u<<voice;
    request_fm_preview(ui,instrument);
}
