/* Graph-major rendering. Accumulation order within each sample is unchanged;
   only independent histories are advanced in contiguous spans. */
unsigned ts_matrix_span_limit(const TsRoutingMatrix *m,unsigned requested)
{
    if(requested>TS_MATRIX_DELAY)requested=TS_MATRIX_DELAY;
    for(unsigned i=0;i<m->processor_count;++i)
        if(m->voice[m->processors[i]].remaining)return 1;
    for(unsigned i=0;i<m->source_count;++i)
        if(m->voice[m->sources[i]].remaining)return 1;
    return requested;
}
/* Matrix rows contain only graph destinations. Stable coefficients let us
   route a whole span per connection instead of decoding its destination and
   ownership on every sample. Preserve separate products and additions. */
static void matrix_route_span(TsSourceRouteVoice *v,const TsStereoFrame *in,
    TsSourceRouteMix *sums,unsigned frames)
{
    for(unsigned j=0;j<frames;++j) {
        float source=peak(in[j]);
        if(isfinite(source) && source>v->activity_source)v->activity_source=source;
        sums[j].graph_mask|=v->graph_mask;sums[j].graph_mask_high|=v->graph_mask_high;
        sums[j].graph_mask_top|=v->graph_mask_top;
        if(!in[j].l && !in[j].r) {if(v->activity_send<0)v->activity_send=0;continue;}
        TsStereoFrame main={in[j].l*v->main_matrix[0]+in[j].r*v->main_matrix[1],
            in[j].l*v->main_matrix[2]+in[j].r*v->main_matrix[3]};
        float sent=peak(main);if(isfinite(sent) && sent>v->activity_send)v->activity_send=sent;
        sums[j].reference.l+=in[j].l-main.l;sums[j].reference.r+=in[j].r-main.r;
        sums[j].tape_input.l+=in[j].l*(1-v->main);sums[j].tape_input.r+=in[j].r*(1-v->main);
    }
    for(unsigned at=0;at<v->active_count;++at) {
        unsigned coefficient=v->active[at],d=(coefficient-TS_SOURCE_GRAPH_OFFSET)/2;
        float left=v->matrix[coefficient][0],right=v->matrix[coefficient][1];
        if(coefficient&1)for(unsigned j=0;j<frames;++j) {
            if(!in[j].l && !in[j].r)continue;
            float value=in[j].l*left+in[j].r*right;
            if(isfinite(value) && fabsf(value)>v->activity_send)v->activity_send=fabsf(value);
            sums[j].graph[d].r+=value;
        } else for(unsigned j=0;j<frames;++j) {
            if(!in[j].l && !in[j].r)continue;
            float value=in[j].l*left+in[j].r*right;
            if(isfinite(value) && fabsf(value)>v->activity_send)v->activity_send=fabsf(value);
            sums[j].graph[d].l+=value;
        }
    }
}
void ts_matrix_process_span(TsRoutingMatrix *m,TsMatrixSpan *span,unsigned frames,
    TsMatrixProcessSpan fn,void *context)
{
    if(!m || !span || !fn || !frames || frames>TS_MATRIX_DELAY)return;
    TsStereoFrame input[TS_MATRIX_DELAY],key[TS_MATRIX_DELAY],output[TS_MATRIX_DELAY];
    /* A routed physical input is metered twice in the frame renderer. Keep
       those two updates interleaved for that row, including NaN suppression. */
    for(unsigned p=0;p<m->input_pairs && p<TS_MATRIX_INPUTS;++p) {
        int row=ts_matrix_input_row(p),routed=0;
        for(unsigned at=0;at<m->source_count;++at)routed|=m->sources[at]==row;
        if(!routed)for(unsigned j=0;j<frames;++j)
            m->output_peak[row]=meter_max(peak(span->live[j][row-TS_MATRIX_NODES]),m->output_peak[row]*m->decay);
    }
    for(unsigned at=0;at<m->source_count;++at) {
        int s=m->sources[at],pair=ts_matrix_input_pair(s);
        for(unsigned j=0;j<frames;++j) {
            TsStereoFrame raw=span->live[j][s-TS_MATRIX_NODES];
            if(pair>=0 && (unsigned)pair<m->input_pairs)
                m->output_peak[s]=meter_max(peak(raw),m->output_peak[s]*m->decay);
            TsStereoFrame in=ts_stereo_frame_sanitize_inline(raw);
            m->output_peak[s]=meter_max(peak(in),m->output_peak[s]*m->decay);
            input[j]=in;
        }
        matrix_route_span(&m->voice[s],input,span->sums,frames);
    }
    for(unsigned e=0;e<m->edge_count;++e) {
        int s=m->edges[e].source,d=m->edges[e].destination;
        float l=m->edges[e].left,r=m->edges[e].right,g=m->edges[e].gain;
        for(unsigned j=0;j<frames;++j) {
            TsStereoFrame old=m->history[s][(m->position+j)%TS_MATRIX_DELAY];
            span->sums[j].graph[d].l+=old.l*g*l;span->sums[j].graph[d].r+=old.r*g*r;
            ts_source_route_graph_mark(&span->sums[j],(unsigned)d);
            float activity=meter_max(fabsf(old.l*g*l),fabsf(old.r*g*r));
            if(activity>m->voice[s].activity_send)m->voice[s].activity_send=activity;
        }
    }
    for(unsigned at=0;at<m->processor_count;++at) {
        int n=m->processors[at],sc=ts_matrix_sidechain_for(n);
        if(n>=TS_MATRIX_EFFECT_BASE && !m->controls.effect[n-TS_MATRIX_EFFECT_BASE].id)continue;
        for(unsigned j=0;j<frames;++j) {
            input[j]=ts_stereo_frame_sanitize_inline(span->sums[j].graph[n]);
            m->input_peak[n]=meter_max(peak(input[j]),m->input_peak[n]*m->decay);
            key[j]=sc>=0?ts_stereo_frame_sanitize_inline(span->sums[j].graph[sc]):m->sidechain;
            if(sc>=0)m->input_peak[sc]=meter_max(peak(key[j]),m->input_peak[sc]*m->decay);
        }
        if(sc>=0)m->sidechain=key[frames-1];
        fn(context,n,input,key,output,frames);
        for(unsigned j=0;j<frames;++j) {
            TsStereoFrame out=ts_stereo_frame_sanitize_inline(output[j]);
            m->output_peak[n]=meter_max(peak(out),m->output_peak[n]*m->decay);
            if(m->controls.delayed[n])m->history[n][(m->position+j)%TS_MATRIX_DELAY]=
                (TsStereoFrame){tanhf(out.l),tanhf(out.r)};
            output[j]=out;
        }
        matrix_route_span(&m->voice[n],output,span->sums,frames);
    }
    for(unsigned j=0;j<frames;++j) {
        TsSourceRouteMix *s=&span->sums[j];TsMatrixOutput *out=&span->result[j];
        matrix_outputs(m,s,1);
        memcpy(out->output,m->output,sizeof(out->output));
        out->spatial_input=m->spatial_input=ts_stereo_frame_sanitize_inline(s->graph[TS_MATRIX_AMBISONICS]);
        m->input_peak[TS_MATRIX_AMBISONICS]=meter_max(peak(m->spatial_input),m->input_peak[TS_MATRIX_AMBISONICS]*m->decay);
        for(int i=0;i<TS_SOURCE_SPEAKERS;++i) {
            TsStereoFrame f=ts_stereo_frame_sanitize_inline(s->graph[TS_MATRIX_SPEAKER_BASE+i]);
            out->speaker_output[i]=m->speaker_output[i]=.5f*(f.l+f.r);
            m->input_peak[TS_MATRIX_SPEAKER_BASE+i]=meter_max(fabsf(m->speaker_output[i]),m->input_peak[TS_MATRIX_SPEAKER_BASE+i]*m->decay);
        }
    }
    m->position=(m->position+frames)%TS_MATRIX_DELAY;
}
