/* The router owns placement; these adapters retain each processor's state.
   Existing PRE/head inserts are local Sister taps, not extra serial nodes. */
typedef struct {
    TsSisterRuntime *runtime;
    TsSisterRuntimeFrame *frame;
    TsStereoFrame causal, sister_feedback, fallout_wet, dry_monitor;
    int passed_sister, monitor_merged;
} RouterFrameContext;

static void runtime_router_begin(RouterFrameContext *c)
{
    TsSisterRuntime *r=c->runtime;
    float master=ts_sister_post_fx_master_engage(&r->post_fx);
    /* Snapshot both returns before traversing the graph. Their explicit
       one-sample boundary does not depend on the new stage order. */
    c->causal=frame_add(
        frame_scale(r->master_feedback_previous,master>0 && !(r->router.send_mask&(1u<<TS_SEND_PEDALBOARD))?r->router.wet[TS_ROUTER_PEDALBOARD]:0),
        frame_scale(r->fallout_feedback_previous,
            master>0 && !(r->router.send_mask&(1u<<TS_SEND_FALLOUT)) && ts_sister_fallout_engage(&r->fallout)>0?r->router.wet[TS_ROUTER_FALLOUT]:0));
    c->causal=frame_scale(c->causal,r->router.wet[TS_ROUTER_SISTER]);
    c->causal=ts_stereo_frame_sanitize(c->causal);
    float peak=frame_peak(c->causal);
    if(peak>1.5f)c->causal=frame_scale(c->causal,1.5f/peak);
}

static TsStereoFrame runtime_router_stage_impl(void *context,int stage,TsStereoFrame input)
{
    RouterFrameContext *c=context;TsSisterRuntime *r=c->runtime;
    int bus=ts_router_send_bus(stage);
    if(bus>=0 && (r->router.send_mask&(1u<<bus))) {
        float gate=r->router.wet[stage];
        TsStereoFrame source=gate>0?r->send_input[bus]:(TsStereoFrame){0,0},wet={0};
        if(bus==TS_SEND_PRISM)wet=ts_prism_process_send(&r->prism,source);
        else if(bus==TS_SEND_PEDALBOARD) {
            wet=ts_sister_post_fx_process_send(&r->post_fx,source);
            wet=frame_scale(wet,runtime_ramp_advance(&r->ordinary_fx_return_gain));
        } else {
            float master=ts_sister_post_fx_master_engage(&r->post_fx);
            if(gate>0 && master>0)source=frame_add(source,r->fallout_feedback_previous);
            TsSisterFalloutResult f=ts_sister_fallout_process(&r->fallout,source);
            c->fallout_wet=f.wet;
            wet=frame_scale(f.wet,ts_sister_fallout_engage(&r->fallout)*r->fallout.mix_modulated*master);
        }
        wet=frame_scale(wet,gate*r->router.gain);
        TsSourceRoute route=r->router.controls.return_route[bus];
        route.mix_enabled=1;route.clean_level=r->router.controls.return_level[bus];
        ts_source_route_set(&r->return_voice[bus],route,r->router.sample_rate);
        (void)ts_source_route_frame(&r->return_voice[bus],wet,&r->effect_returns);
        r->router.input_peak[stage]=fmaxf(frame_peak(source),r->router.input_peak[stage]*r->router.decay);
        r->router.output_peak[stage]=fmaxf(frame_peak(wet),r->router.output_peak[stage]*r->router.decay);
        return input; /* This processor no longer intercepts the serial bus. */
    }
    switch(stage) {
    case TS_ROUTER_INSERT:
        if(r->router.master_mix)return input; /* One pass after recombination. */
        return ts_insert_process(&r->insert,input,
            r->router.wet[TS_ROUTER_INSERT]*r->router.gain);
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
        if(!c->monitor_merged)c->fallout_wet=f.wet;
        return frame_effect_return(input,f.output,master);
    }
    case TS_ROUTER_PEDALBOARD: {
        TsStereoFrame out=ts_sister_post_fx_process(&r->post_fx,TS_SISTER_HEAD_COUNT,input,0);
        out=frame_effect_return(input,out,runtime_ramp_advance(&r->ordinary_fx_return_gain));
        if(c->passed_sister && !c->monitor_merged)c->sister_feedback=out;
        return out;
    }
    default:return input;
    }
}

static TsStereoFrame runtime_router_stage(void *context,int stage,TsStereoFrame input)
{
    static const TsProfileId ids[TS_ROUTER_COUNT] = {
        TS_PROF_PRISM, TS_PROF_SISTER_STAGE, TS_PROF_FALLOUT, TS_PROF_PEDALBOARD, TS_PROF_INSERT};
    uint64_t started = ts_profile_begin(ids[stage]);
    TsStereoFrame output = runtime_router_stage_impl(context, stage, input);
    ts_profile_end(ids[stage], started);
    return output;
}

static TsStereoFrame runtime_router_prepare_stage(void *context,int stage,TsStereoFrame input)
{
    RouterFrameContext *c=context;TsSisterRuntime *r=c->runtime;
    if(stage==TS_ROUTER_INSERT && !r->router.master_mix && c->passed_sister &&
       (r->insert.controls.send_pair || r->router.wet[TS_ROUTER_INSERT]>0 ||
        ts_router_active(&r->router.controls,TS_ROUTER_INSERT))) {
        float sister=r->router.wet[TS_ROUTER_SISTER];
        /* A serial external loop must also receive the monitor return. Merge
           here, before bypass, so A/B never mixes undelayed dry at Master.
           Keep feedback taps upstream of this merge and the external loop. */
        input=frame_add(frame_scale(input,1+sister*(r->monitor_wet_current-1)),
                        frame_scale(c->dry_monitor,sister));
        c->dry_monitor=(TsStereoFrame){0,0};c->monitor_merged=1;
    }
    return input;
}

static void runtime_router_finish(RouterFrameContext *c)
{
    TsSisterRuntime *r=c->runtime;
    float gate=ts_sister_post_fx_master_engage(&r->post_fx)*
        r->router.wet[TS_ROUTER_PEDALBOARD]*r->router.wet[TS_ROUTER_SISTER];
    if(r->router.send_mask&(1u<<TS_SEND_PEDALBOARD))gate=0;
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
