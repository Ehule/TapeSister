/* Frozen sample path from a6df710. Independent parity oracle for compiled plans. */
static float meter_max(float a,float b){return a>b || isnan(b)?a:b;}
static float peak(TsStereoFrame f){return meter_max(fabsf(f.l),fabsf(f.r));}
static void reference_process(TsRoutingMatrix *m,TsSourceRouteMix *sums,
    const TsStereoFrame live[TS_MATRIX_ROWS-TS_MATRIX_NODES],TsMatrixProcess fn,void *context)
{
    /* Compile routed sources on edits; unused interface capacity costs no DSP. */
    for(unsigned p=0;p<m->input_pairs && p<TS_MATRIX_INPUTS;++p) {
        int row=ts_matrix_input_row(p);
        m->output_peak[row]=meter_max(peak(live[row-TS_MATRIX_NODES]),m->output_peak[row]*m->decay);
    }
    for(unsigned at=0;at<m->source_count;++at) {
        int s=m->sources[at];
        TsStereoFrame input=ts_stereo_frame_sanitize_inline(live[s-TS_MATRIX_NODES]);
        m->output_peak[s]=meter_max(peak(input),m->output_peak[s]*m->decay);
        ts_source_route_frame(&m->voice[s],input,sums);
    }
    for(int k=0;k<TS_MATRIX_PROCESSORS;++k) {
        int s=ts_matrix_processor(k);if(!m->controls.delayed[s])continue;
        TsStereoFrame old=m->history[s][m->position];
        const TsSourceRoute *r=&m->controls.row[s];
        float l=r->pan>0?1-r->pan*.01f:1,rh=r->pan<0?1+r->pan*.01f:1;
        for(int d=0;d<TS_MATRIX_INSTANCE_DESTINATIONS;++d)if(m->controls.delayed[s]&(UINT64_C(1)<<d)) {
            float g=r->matrix_gain[d]*.001f;
            sums->graph[d].l+=old.l*g*l;sums->graph[d].r+=old.r*g*rh;
            float activity=meter_max(fabsf(old.l*g*l),fabsf(old.r*g*rh));
            if(activity>m->voice[s].activity_send)m->voice[s].activity_send=activity;
        }
    }
    for(int i=0;i<TS_MATRIX_PROCESSORS;++i) {
        int n=m->order[i];
        if(n>=TS_MATRIX_EFFECT_BASE && !m->controls.effect[n-TS_MATRIX_EFFECT_BASE].id && !m->voice[n].remaining)continue;
        TsStereoFrame input=ts_stereo_frame_sanitize_inline(sums->graph[n]);
        m->input_peak[n]=meter_max(peak(input),m->input_peak[n]*m->decay);
        int sc=ts_matrix_sidechain_for(n);
        if(sc>=0) {
            m->sidechain=ts_stereo_frame_sanitize_inline(sums->graph[sc]);
            m->input_peak[sc]=meter_max(peak(m->sidechain),m->input_peak[sc]*m->decay);
        }
        TsStereoFrame output=ts_stereo_frame_sanitize_inline(fn(context,n,input));
        m->output_peak[n]=meter_max(peak(output),m->output_peak[n]*m->decay);
        /* Only feedback edges consume this history. Enabling/changing delayed
           edges clears history in ts_matrix_set, so dormant rows need no tanh
           or stores. Forward audio and effect tails still run every sample. */
        if(m->controls.delayed[n])
            m->history[n][m->position]=(TsStereoFrame){tanhf(output.l),tanhf(output.r)};
        ts_source_route_frame(&m->voice[n],output,sums);
    }
    for(int p=0;p<TS_MATRIX_OUTPUTS;++p)m->output[p]=ts_stereo_frame_sanitize_inline(sums->graph[ts_matrix_output_destination(p)]);
    m->spatial_input=ts_stereo_frame_sanitize_inline(sums->graph[TS_MATRIX_AMBISONICS]);
    m->input_peak[TS_MATRIX_AMBISONICS]=meter_max(peak(m->spatial_input),m->input_peak[TS_MATRIX_AMBISONICS]*m->decay);
    for(int i=0;i<TS_SOURCE_SPEAKERS;++i) {
        TsStereoFrame f=ts_stereo_frame_sanitize_inline(sums->graph[TS_MATRIX_SPEAKER_BASE+i]);
        m->speaker_output[i]=.5f*(f.l+f.r);
        m->input_peak[TS_MATRIX_SPEAKER_BASE+i]=meter_max(fabsf(m->speaker_output[i]),m->input_peak[TS_MATRIX_SPEAKER_BASE+i]*m->decay);
    }
    m->position=(m->position+1)%TS_MATRIX_DELAY;
}
