/* Caller owns audio, or holds output exclusion. No UI fields are accessed. */
static void mx_processor_snapshot(AudioState *audio,FxProcessorState *state,
    TsMasterEqControls *live_eq,int *morph_owned,unsigned *sample_rate)
{
    *sample_rate=audio->output_rate>0?(unsigned)audio->output_rate:48000;
    memset(state,0,sizeof(*state));
    state->parameters=audio->sister.parameters;state->eq=audio->sister.master_eq.controls;
    state->morph=audio->sister.master_eq.morph;
    /* The changing curve is display-only: navigation must not record the
       audio clock as an undoable edit. Stop adopts that curve explicitly. */
    *live_eq=audio->sister.master_eq.live;
    *morph_owned=audio->sister.master_eq.morph_running ||
        !ts_master_eq_matches(&state->eq,live_eq);
    const TsSisterLimiter *l=&audio->sister.limiter;
    state->limiter_enabled=l->enabled;state->ceiling=l->ceiling_db;
    state->lookahead=l->lookahead_ms;state->release=l->release_ms;
}
static void mx_processor_read(AudioState *audio,FxProcessorState *state)
{
    mx_processor_snapshot(audio,state,&matrix_window.live_eq,&matrix_window.eq_morph_owned,&matrix_window.sample_rate);
}
static void mx_processor_apply(AudioState *audio,TsUiState *ui,const FxProcessorState *state)
{
    if(memcmp(&audio->sister.parameters,&state->parameters,sizeof(state->parameters))) {
        ts_sister_runtime_set_parameters(&audio->sister,&state->parameters);
        ts_sister_runtime_mark_selected_preset_modified(&audio->sister);
    }
    if(memcmp(&audio->sister.master_eq.controls,&state->eq,sizeof(state->eq)))
        ts_master_eq_set(&audio->sister.master_eq,&state->eq);
    if(memcmp(&audio->sister.master_eq.morph,&state->morph,sizeof(state->morph)))
        ts_master_eq_morph_set(&audio->sister.master_eq,&state->morph);
    TsSisterLimiter *l=&audio->sister.limiter;
    if(l->enabled!=state->limiter_enabled || l->ceiling_db!=state->ceiling ||
       l->lookahead_ms!=state->lookahead || l->release_ms!=state->release)
        ts_sister_limiter_set_live_controls(l,state->limiter_enabled,state->ceiling,state->lookahead,state->release);
    ui->config.master_eq=audio->sister.master_eq.controls;ui->config.eq_morph=audio->sister.master_eq.morph;
    ui->config.sister_limiter_enabled=l->enabled;ui->config.sister_limiter_ceiling_db=l->ceiling_db;
    ui->config.sister_limiter_lookahead_ms=l->lookahead_ms;ui->config.sister_limiter_release_ms=l->release_ms;
    ui->master_output.limiter_enabled=l->enabled;
    mx_processor_read(audio,&matrix_window.fixed);
}
