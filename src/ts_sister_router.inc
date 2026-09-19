/* The router owns placement; these adapters retain each processor's state.
   Existing PRE/head inserts are local Sister taps, not extra serial nodes. */
typedef struct {
    TsSisterRuntime *runtime;
    TsSisterRuntimeFrame *frame;
    TsStereoFrame causal, sister_feedback, fallout_wet, dry_monitor;
    int passed_sister;
} RouterFrameContext;

static void runtime_router_begin(RouterFrameContext *c)
{
    TsSisterRuntime *r=c->runtime;
    float master=ts_sister_post_fx_master_engage(&r->post_fx);
    /* Snapshot both returns before traversing the graph. Their explicit
       one-sample boundary does not depend on the new stage order. */
    c->causal=frame_add(
        frame_scale(r->master_feedback_previous,master>0?r->router.wet[TS_ROUTER_PEDALBOARD]:0),
        frame_scale(r->fallout_feedback_previous,
            master>0 && ts_sister_fallout_engage(&r->fallout)>0?r->router.wet[TS_ROUTER_FALLOUT]:0));
    c->causal=frame_scale(c->causal,r->router.wet[TS_ROUTER_SISTER]);
    c->causal=ts_stereo_frame_sanitize(c->causal);
    float peak=frame_peak(c->causal);
    if(peak>1.5f)c->causal=frame_scale(c->causal,1.5f/peak);
}

static TsStereoFrame runtime_router_stage(void *context,int stage,TsStereoFrame input)
{
    RouterFrameContext *c=context;TsSisterRuntime *r=c->runtime;
    switch(stage) {
    case TS_ROUTER_PRISM:
        return ts_prism_process(&r->prism,input);
    case TS_ROUTER_SISTER: {
        if(!r->enabled || r->callback_failed || !c->frame)return input;
        float board=r->router.wet[TS_ROUTER_PEDALBOARD];
        TsStereoFrame pre=ts_sister_post_fx_process_pre(&r->post_fx,board>0?input:(TsStereoFrame){0,0},0);
        input=frame_effect_return(input,pre,board);
        TsSisterOutput out=ts_sister_machine_process_router(&r->machine,&r->post_fx,board,input,c->causal);
        if(r->waveform_capacity_frames!=r->machine.buffer.capacity_frames) {
            ts_sister_wave_publisher_resize(&r->waveform,r->waveform_capacity_frames,
                r->machine.buffer.capacity_frames,r->machine.master_clock?r->machine.master_clock-1:0);
            r->waveform_capacity_frames=r->machine.buffer.capacity_frames;
        }
        ts_sister_wave_publisher_push(&r->waveform,out.write,out.write_position,
            r->machine.buffer.capacity_frames,r->machine.buffer.channels,out.wrote);
        c->frame->input=ts_stereo_frame_sanitize(out.input);
        c->frame->duck_sidechain=c->frame->input;
        c->sister_feedback=out.post_fx;
        c->passed_sister=1;
        c->dry_monitor=frame_scale(c->frame->input,r->monitor_dry_current);
        for(int i=0;i<TS_SISTER_HEAD_COUNT;++i)
            c->frame->tap[TS_SISTER_TAP_H1+i]=ts_stereo_frame_sanitize(out.head[i]);
        return out.mix;
    }
    case TS_ROUTER_FALLOUT: {
        if(!r->fallout.ready)return input;
        float master=ts_sister_post_fx_master_engage(&r->post_fx);
        TsStereoFrame incoming=input;
        if(!r->enabled && master>0 && r->router.wet[stage]>0 && ts_sister_fallout_engage(&r->fallout)>0)
            incoming=frame_add(incoming,r->fallout_feedback_previous);
        TsSisterFalloutResult f=ts_sister_fallout_process(&r->fallout,incoming);
        c->fallout_wet=f.wet;
        return frame_effect_return(input,f.output,master);
    }
    case TS_ROUTER_PEDALBOARD: {
        TsStereoFrame out=ts_sister_post_fx_process(&r->post_fx,TS_SISTER_HEAD_COUNT,input,0);
        out=frame_effect_return(input,out,runtime_ramp_advance(&r->ordinary_fx_return_gain));
        if(c->passed_sister)c->sister_feedback=out;
        return out;
    }
    default:return input;
    }
}

static void runtime_router_finish(RouterFrameContext *c)
{
    TsSisterRuntime *r=c->runtime;
    float gate=ts_sister_post_fx_master_engage(&r->post_fx)*
        r->router.wet[TS_ROUTER_PEDALBOARD]*r->router.wet[TS_ROUTER_SISTER];
    if(!r->enabled || gate<=0) {
        r->master_feedback_current=0;r->master_feedback_previous=(TsStereoFrame){0,0};
    } else {
        r->master_feedback_current=monitor_approach(r->master_feedback_current,
            r->parameters.fx.master_feedback*1.35f*gate,r->machine.buffer.sample_rate);
        if(r->parameters.fx.master_feedback<=0 && r->master_feedback_current<.000001f)
            r->master_feedback_current=0;
        /* Only a completed wet Sister contribution can recirculate. When
           Pedalboard is downstream, retain its established post-effect tap;
           upstream effects are already printed into the tape input. The dry
           monitor never becomes a feedback send after a routing change. */
        TsStereoFrame f=frame_scale(ts_stereo_frame_sanitize(c->sister_feedback),r->master_feedback_current);
        float peak=frame_peak(f);if(peak>1.5f)f=frame_scale(f,1.5f/peak);
        r->master_feedback_previous=ts_stereo_frame_sanitize((TsStereoFrame){tanhf(f.l),tanhf(f.r)});
    }
    runtime_fallout_feedback(r,c->fallout_wet);
    if(r->router.wet[TS_ROUTER_FALLOUT]<=0) {
        r->fallout_feedback_current=0;r->fallout_feedback_previous=(TsStereoFrame){0,0};
    }
}
