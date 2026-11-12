unsigned ts_sister_runtime_matrix_span_limit(const TsSisterRuntime *r,unsigned frames)
{
    /* The tape stage and imported serial router share mutable state with
       sources/capture. Keep those graphs chronological. Pedalboard master
       fades also feed Fallout's return law, so split those transitions. */
    if(!r || !r->matrix.controls.enabled || r->matrix.controls.serial_mask ||
       r->enabled || r->callback_failed || r->post_fx.master_engage.remaining ||
       r->performance.render_limit || r->performance.clean_handoff.last.mask ||
       !ts_voice_handoff_idle(&r->performance.raw_handoff) ||
       !ts_voice_handoff_idle(&r->performance.output_handoff))return 1;
    frames=ts_matrix_span_limit(&r->matrix,frames);
    return ts_matrix_effects_span_limit(&r->matrix_effects,frames);
}
typedef struct {
    TsSisterRuntime *runtime;
    TsSisterRuntimeFrame *frames;
    const TsSisterSourceFrames *sources;
    float *limiter_peaks;
} RuntimeMatrixSpanContext;
static void runtime_matrix_node_span(void *opaque,int node,const TsStereoFrame *in,
    const TsStereoFrame *key,TsStereoFrame *out,unsigned frames)
{
    RuntimeMatrixSpanContext *c=opaque;TsSisterRuntime *r=c->runtime;
    int effect=ts_matrix_effect_index(node);
    if(effect>=0) {
        ts_matrix_effects_process_span(&r->matrix_effects,effect,in,key,out,frames,
            r->matrix.voice[node].active_count!=0 || r->matrix.controls.delayed[node]!=0);
        return;
    }
    for(unsigned j=0;j<frames;++j) {
        RouterFrameContext frame={.runtime=r,.frame=&c->frames[j],
            .matrix_program_gain=c->sources[j].matrix_program_gain};
        r->matrix.sidechain=key[j];
        out[j]=runtime_matrix_node(&frame,node,in[j]);
        if(node==TS_MATRIX_LIMITER)c->limiter_peaks[j]=r->limiter_input_peak;
    }
}
void ts_sister_runtime_matrix_span(TsSisterRuntime *r,const TsSisterSourceFrames *sources,
    TsSisterRuntimeFrame *out,float *limiter_peaks,TsMatrixSpan *scratch,unsigned frames)
{
    if(!r || !sources || !out || !limiter_peaks || !scratch || !frames || frames>TS_MATRIX_DELAY)return;
    for(unsigned j=0;j<frames;++j) {
        const TsSisterSourceFrames *s=&sources[j];
        TsSisterRuntimeFrame frame={.dry_monitor_gain=1};
        frame.tap[TS_SISTER_TAP_TAPEHEAD]=ts_stereo_frame_sanitize_inline(s->tapehead);
        ts_source_route_mix_reuse(&r->clean_output,&r->clean_output);
        ts_source_route_mix_reuse(&r->effect_returns,&r->clean_output);
        TsSourceRouteMix *manual=&r->manual_routes;
        ts_source_route_mix_reuse(manual,&r->clean_output);
        TsStereoFrame raw;
        frame.keyboard_dry=frame_scale(ts_performance_read_routed(&r->performance,&raw,manual),r->performance_gain_current[3]);
        ts_source_route_add(&r->clean_output,manual,r->performance_gain_current[3]);
        for(int bus=0;bus<TS_SOURCE_SENDS;++bus)
            r->send_input[bus]=ts_stereo_frame_sanitize_inline(frame_add(s->send[bus],r->clean_output.send[bus]));
        runtime_matrix_prepare(r,s,ts_stereo_frame_sanitize_inline(s->tiles),frame,
            &scratch->sums[j],scratch->live[j]);
        out[j]=frame;
    }
    r->router.wet[TS_ROUTER_SISTER]=r->router.wet[TS_ROUTER_PEDALBOARD]=1;
    RuntimeMatrixSpanContext context={r,out,sources,limiter_peaks};
    uint64_t p=ts_profile_begin(TS_PROF_ROUTER);
    ts_matrix_effects_begin_frame(&r->matrix_effects);
    ts_matrix_process_span(&r->matrix,scratch,frames,runtime_matrix_node_span,&context);
    ts_matrix_effects_end_frame(&r->matrix_effects);
    ts_profile_end(TS_PROF_ROUTER,p);
    for(unsigned j=0;j<frames;++j) {
        out[j].monitor_return=scratch->result[j].output[0];
        if(atomic_load_explicit(&r->capture.state,memory_order_relaxed)==TS_CAPTURE_RECORDING)
            (void)ts_capture_write_frame(&r->capture,selected_tap(&out[j],r->selected_tap));
    }
    r->last_frame=out[frames-1];r->processed_frames+=frames;publish_frame_snapshot(r);
}
