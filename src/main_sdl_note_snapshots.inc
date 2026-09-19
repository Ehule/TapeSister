/* UI-owned snapshots let existing voices finish independently of the editor.
   One buffer per distinct source, shared by all notes that were playing it. */
static void runtime_note_collect_snapshots(SDL_AudioDeviceID device,AudioState *audio)
{
    TsSample retired[TS_NOTE_BANK_VOICE_CAPACITY]={0};int count=0;
    if(device)SDL_LockAudioDevice(device);
    for(int s=0;s<TS_NOTE_BANK_VOICE_CAPACITY;++s) {
        TsSample *sample=&audio->note_snapshots[s];if(!sample->data)continue;
        int used=0;
        for(int i=0;i<TS_NOTE_BANK_VOICE_CAPACITY;++i)
            used|=audio->notes.voices[i].active && audio->notes.voices[i].sample==sample;
        if(used)continue;
        for(int i=0;i<TS_NOTE_BANK_VOICE_CAPACITY;++i)
            if(audio->notes.voices[i].sample==sample)audio->notes.voices[i].sample=NULL;
        retired[count++]=*sample;ts_sample_init(sample);
    }
    if(device)SDL_UnlockAudioDevice(device);
    for(int i=0;i<count;++i)ts_sample_free(&retired[i]);
}

static int runtime_note_preserve_workspace(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    const TsSample *sources[TS_NOTE_BANK_VOICE_CAPACITY];
    TsSample copies[TS_NOTE_BANK_VOICE_CAPACITY]={0};int count=0,free_count=0;
    runtime_note_collect_snapshots(device,audio);
    if(device)SDL_LockAudioDevice(device);
    for(int i=0;i<TS_NOTE_BANK_VOICE_CAPACITY;++i) {
        free_count+=audio->note_snapshots[i].data==NULL;
        const TsNoteVoice *v=&audio->notes.voices[i];
        if(!v->active || v->synth || v->preview || v->detached || !v->sample)continue;
        int n=0;while(n<count && sources[n]!=v->sample)++n;
        if(n==count)sources[count++]=v->sample;
    }
    if(device)SDL_UnlockAudioDevice(device);
    char error[160]="Too many playing sources";
    int ok=free_count>=count;
    for(int n=0;ok && n<count;++n)ok=ts_sample_clone(&copies[n],sources[n],error,sizeof(error));
    if(!ok) {
        for(int n=0;n<count;++n)ts_sample_free(&copies[n]);
        snprintf(ui->status,sizeof(ui->status),"EDITOR KEPT OPEN: %.135s",error);return 0;
    }
    if(device)SDL_LockAudioDevice(device);
    for(int n=0;n<count;++n) {
        int s=0;while(s<TS_NOTE_BANK_VOICE_CAPACITY && audio->note_snapshots[s].data)++s;
        audio->note_snapshots[s]=copies[n];
        for(int i=0;i<TS_NOTE_BANK_VOICE_CAPACITY;++i) {
            TsNoteVoice *v=&audio->notes.voices[i];
            if(v->active && !v->synth && !v->preview && !v->detached && v->sample==sources[n]) {
                v->sample=&audio->note_snapshots[s];v->detached=1;
            }
        }
    }
    TsPerformanceBank *banks[]={&audio->performance,&audio->tile_launchers,&audio->sister.performance};
    for(int b=0;b<3;++b)for(int i=0;i<TS_PERFORMANCE_VOICE_LIMIT;++i)
        if(banks[b]->voices[i].active)banks[b]->voices[i].detached=audio->output_rate;
    /* Only the editor's standalone audition belongs to the outgoing view. */
    audio->playing=0;audio->sample=NULL;audio->bank_slot=-1;
    if(device)SDL_UnlockAudioDevice(device);
    return 1;
}
