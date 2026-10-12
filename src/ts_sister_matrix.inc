/* One shared DSP history per node. The graph owns inter-component routing;
   Sister's tape/head feedback and local PRE/head slots stay inside its node. */
int ts_sister_runtime_matrix_import(const TsSisterRuntime *r,
    TsMatrixControls *out,char *error,size_t size)
{
    if(!r || !out)return 0;
    const char *reason=NULL;
    if(r->enabled || r->callback_failed)reason="LIVE SISTER ROUTING";
    else if(r->router.controls.send_mask)reason="PARALLEL EFFECT RETURNS";
    else if(ts_router_active(&r->router.controls,TS_ROUTER_INSERT) || r->router.wet[TS_ROUTER_INSERT]>0)reason="EXTERNAL INSERT";
    else if(r->spatial.controls.enabled)reason="SPEAKER ARRAY";
    else if(r->router.handoff || r->router.transport.running || r->router.transport.timer_mask)reason="ROUTER TRANSITION / AUTOMATION";
    for(int i=0;!reason && i<TS_ROUTER_COUNT;++i)
        if(r->router.wet[i]!=(float)ts_router_active(&r->router.controls,i))reason="ROUTER TRANSITION";
    if(reason) {
        if(error && size)snprintf(error,size,"UNCHANGED: %s CANNOT YET BE IMPORTED WITHOUT CHANGING THE SOUND.",reason);
        return 0;
    }
    TsMatrixControls next;ts_matrix_default(&next);next.enabled=1;next.serial_mask=1u<<TS_MATRIX_MAIN;
    memcpy(next.input_name,r->matrix.controls.input_name,sizeof(next.input_name));
    memcpy(next.output_name,r->matrix.controls.output_name,sizeof(next.output_name));
    for(int n=0;n<TS_MATRIX_NODES;++n)next.row[n]=(TsSourceRoute){.matrix_enabled=1};
    int from=TS_MATRIX_MAIN;
    for(int i=0;i<TS_ROUTER_COUNT;++i) {
        int stage=r->router.controls.order[i];
        if(stage==TS_ROUTER_INSERT || stage==TS_ROUTER_SISTER || !ts_router_active(&r->router.controls,stage))continue;
        int node=stage==TS_ROUTER_PRISM?TS_MATRIX_PRISM:stage==TS_ROUTER_PEDALBOARD?TS_MATRIX_BOARD:TS_MATRIX_FALLOUT;
        next.row[from].matrix_gain[node]=1000;next.serial_mask|=1u<<node;from=node;
    }
    next.row[from].matrix_gain[TS_MATRIX_EQ]=1000;
    next.row[TS_MATRIX_EQ].matrix_gain[TS_MATRIX_LIMITER]=1000;
    next.row[TS_MATRIX_LIMITER].matrix_gain[TS_MATRIX_NODES]=1000;
    if(!ts_matrix_valid(&next))return 0;
    *out=next;runtime_error(error,size,"");return 1;
}

static TsStereoFrame runtime_matrix_node(void *context,int node,TsStereoFrame input)
{
    RouterFrameContext *c=context;TsSisterRuntime *r=c->runtime;
    if(r->matrix.controls.serial_mask&(1u<<node)) {
        if(node==TS_MATRIX_MAIN) {
            float ceiling=fmaxf(0,c->matrix_program_gain);
            return (TsStereoFrame){fmaxf(-ceiling,fminf(ceiling,input.l)),fmaxf(-ceiling,fminf(ceiling,input.r))};
        }
        int stage=node==TS_MATRIX_PRISM?TS_ROUTER_PRISM:node==TS_MATRIX_BOARD?TS_ROUTER_PEDALBOARD:TS_ROUTER_FALLOUT;
        /* Same processor, history, dry/wet law and return trim as before the
           import. Graph connections now own its position. */
        return runtime_router_stage(c,stage,input);
    }
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
    if(!r->matrix.controls.serial_mask) {
        sums.graph[TS_MATRIX_PRISM]=frame_add(sums.graph[TS_MATRIX_PRISM],r->send_input[TS_SEND_PRISM]);
        sums.graph[TS_MATRIX_BOARD]=frame_add(sums.graph[TS_MATRIX_BOARD],r->send_input[TS_SEND_PEDALBOARD]);
        sums.graph[TS_MATRIX_FALLOUT]=frame_add(sums.graph[TS_MATRIX_FALLOUT],r->send_input[TS_SEND_FALLOUT]);
    }
    TsStereoFrame live[TS_MATRIX_ROWS-TS_MATRIX_NODES]={0};
    for(int i=0;i<TS_MATRIX_INPUTS;++i)live[i]=s->matrix_input[i];
    live[TS_MATRIX_FM-TS_MATRIX_NODES]=s->fm;live[TS_MATRIX_LINK-TS_MATRIX_NODES]=s->tapehead;
    live[TS_MATRIX_MOSAIC-TS_MATRIX_NODES]=s->mosaic;
    live[TS_MATRIX_TILES-TS_MATRIX_NODES]=s->live_tiles;
    RouterFrameContext context={.runtime=r,.frame=&frame,.matrix_program_gain=s->matrix_program_gain};
    /* Matrix controls supersede legacy chain placement and source switches. */
    if(!r->matrix.controls.serial_mask)r->router.wet[TS_ROUTER_SISTER]=r->router.wet[TS_ROUTER_PEDALBOARD]=1;
    else runtime_router_begin(&context);
    uint64_t p=ts_profile_begin(TS_PROF_ROUTER);
    ts_matrix_process(&r->matrix,&sums,live,runtime_matrix_node,&context);
    if(r->matrix.controls.serial_mask)runtime_router_finish(&context);
    ts_profile_end(TS_PROF_ROUTER,p);
    frame.monitor_return=r->matrix.output[0];
    if(atomic_load_explicit(&r->capture.state,memory_order_relaxed)==TS_CAPTURE_RECORDING)
        (void)ts_capture_write_frame(&r->capture,selected_tap(&frame,r->selected_tap));
    r->last_frame=frame;++r->processed_frames;publish_frame_snapshot(r);return frame;
}
