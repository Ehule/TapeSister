/* One shared DSP history per node. The graph owns inter-component routing;
   Sister's tape/head feedback and local PRE/head slots stay inside its node. */
static TsStereoFrame runtime_matrix_node(void *context,int node,TsStereoFrame input)
{
    RouterFrameContext *c=context;TsSisterRuntime *r=c->runtime;
    switch(node) {
    case TS_MATRIX_MAIN:return input;
    case TS_MATRIX_SISTER: {
        if(!r->enabled || r->callback_failed)return (TsStereoFrame){0,0};
        TsStereoFrame out=runtime_router_stage(c,TS_ROUTER_SISTER,input);
        out=ts_sister_machine_finish_router(&r->machine,out);
        c->frame->tap[TS_SISTER_TAP_MIX]=out;
        out=frame_scale(out,runtime_ramp_advance(&r->monitor_route));
        return out; /* Heads only: dry is a separate checkbox in the matrix. */
    }
    case TS_MATRIX_PRISM: {
        uint64_t p=ts_profile_begin(TS_PROF_PRISM);
        TsStereoFrame out=ts_prism_process_send(&r->prism,input);ts_profile_end(TS_PROF_PRISM,p);return out;
    }
    case TS_MATRIX_BOARD: {
        uint64_t p=ts_profile_begin(TS_PROF_PEDALBOARD);
        TsStereoFrame out=ts_sister_post_fx_process_send(&r->post_fx,input);
        out=frame_scale(out,runtime_ramp_advance(&r->ordinary_fx_return_gain));
        ts_profile_end(TS_PROF_PEDALBOARD,p);return out;
    }
    case TS_MATRIX_FALLOUT: {
        uint64_t p=ts_profile_begin(TS_PROF_FALLOUT);
        TsSisterFalloutResult f=ts_sister_fallout_process(&r->fallout,input);
        TsStereoFrame out=frame_scale(f.wet,ts_sister_fallout_engage(&r->fallout)*
            r->fallout.mix_modulated*ts_sister_post_fx_master_engage(&r->post_fx));
        ts_profile_end(TS_PROF_FALLOUT,p);return out;
    }
    case TS_MATRIX_EQ: {
        TsStereoFrame out=ts_master_eq_process(&r->master_eq,input);
        ts_eq_spectrum_push(&r->eq_spectrum,out,r->master_eq.sample_rate);return out;
    }
    case TS_MATRIX_LIMITER:return ts_sister_limiter_process(&r->limiter,input,NULL,&r->limiter_input_peak);
    default:return (TsStereoFrame){0,0};
    }
}
static TsSisterRuntimeFrame runtime_matrix_frame(TsSisterRuntime *r,
    const TsSisterSourceFrames *s,TsStereoFrame tiles,TsSisterRuntimeFrame frame)
{
    TsSourceRouteMix sums={0};
    memcpy(sums.graph,s->matrix_graph,sizeof(sums.graph));
    for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)sums.graph[d]=frame_add(sums.graph[d],frame_scale(r->clean_output.graph[d],s->matrix_program_gain));
    sums.graph[TS_MATRIX_MAIN]=frame_add(sums.graph[TS_MATRIX_MAIN],frame_add(tiles,frame_add(s->preview,s->tracker)));
    sums.graph[TS_MATRIX_PRISM]=frame_add(sums.graph[TS_MATRIX_PRISM],r->send_input[TS_SEND_PRISM]);
    sums.graph[TS_MATRIX_BOARD]=frame_add(sums.graph[TS_MATRIX_BOARD],r->send_input[TS_SEND_PEDALBOARD]);
    sums.graph[TS_MATRIX_FALLOUT]=frame_add(sums.graph[TS_MATRIX_FALLOUT],r->send_input[TS_SEND_FALLOUT]);
    TsStereoFrame live[TS_MATRIX_ROWS-TS_MATRIX_NODES]={0};
    for(int i=0;i<TS_MATRIX_INPUTS;++i)live[i]=s->matrix_input[i];
    live[TS_MATRIX_FM-TS_MATRIX_NODES]=s->fm;live[TS_MATRIX_LINK-TS_MATRIX_NODES]=s->tapehead;
    RouterFrameContext context={.runtime=r,.frame=&frame};
    /* Matrix controls supersede legacy chain placement and source switches. */
    r->router.wet[TS_ROUTER_SISTER]=r->router.wet[TS_ROUTER_PEDALBOARD]=1;
    uint64_t p=ts_profile_begin(TS_PROF_ROUTER);
    ts_matrix_process(&r->matrix,&sums,live,runtime_matrix_node,&context);
    ts_profile_end(TS_PROF_ROUTER,p);
    frame.monitor_return=r->matrix.output[0];
    if(atomic_load_explicit(&r->capture.state,memory_order_relaxed)==TS_CAPTURE_RECORDING)
        (void)ts_capture_write_frame(&r->capture,selected_tap(&frame,r->selected_tap));
    r->last_frame=frame;++r->processed_frames;publish_frame_snapshot(r);return frame;
}
