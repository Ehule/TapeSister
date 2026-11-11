/* Frozen #208 envelope/card renderer; independent of the optimized paths. */
static float reference_event_fade(const TsMosaicEvent *e,const TsMosaicVoice *v,double elapsed)
{
    double in=v->fade_in,out=v->fade_out;
    if(in==0 && out==0)return 1;
    double gain=1;
    if(in>0)gain=fmin(gain,elapsed/in);
    if(out>0)gain=fmin(gain,(e->duration-elapsed)/out);
    return (float)fmax(0,fmin(1,gain));
}
static TsStereoFrame reference_mosaic_event_read(void *pattern_engine,const TsMosaicEvent *e,TsMosaicVoice *v,double time,
    int rate,double speed,float level,int audible,TsSourceRouteMix *routes,uint64_t serial)
{
    if(!v->active || v->id!=e->id || v->revision!=e->revision || v->start!=e->start) {
        voice_begin(v,e,fmax(0,time-e->start),rate);
        v->audible_gain=audible?1.0f:0.0f;
    }
    if(e->source->pattern && v->pattern_source!=e->source && e->looping && time-e->start+1e-9>=v->pattern_switch){
        v->pattern_source=e->source;v->pattern_origin=v->pattern_switch;
    }
    if(v->control_serial!=serial) {
        event_balance(e,&v->target_left,&v->target_right);
        v->base_mode=ts_loop_base_mode(e->mode);v->starts_at_sample=ts_loop_starts_at_sample(e->mode);
        /* Fades are stable within this buffer; editing them retains voice phase. */
        ts_mosaic_event_fades(e,&v->fade_in,&v->fade_out);
        if(routes && (!v->output_route.ready || v->output_route.rate!=(unsigned)rate ||
           memcmp(&v->output_route.route,&e->output_route,sizeof(e->output_route))))
            ts_source_route_set(&v->output_route,e->output_route,(unsigned)rate);
        const TsSourceRouteVoice *r=&v->output_route;
        v->route_passthrough=r->ready && !r->remaining && !r->active_count && r->main==1 &&
            r->main_matrix[0]==1 && r->main_matrix[1]==0 && r->main_matrix[2]==0 && r->main_matrix[3]==1;
        v->control_serial=serial;
    }
    float envelope=v->attack>=v->attack_frames?1:ts_audition_attack_gain(v->attack,v->attack_frames);
    ++v->attack;
    double remaining=e->live && !e->live_finite?INFINITY:e->start+e->duration-time;
    /* The release is 2 ms. Only calculate its division close to the end;
       the conservative 3 ms gate retains the original boundary arithmetic. */
    if(remaining<.003*speed) {
        double tail=remaining*rate/speed;
        if(tail<v->release_frames)envelope*=(float)fmax(0,tail/v->release_frames);
    }
    float target=audible?1.0f:0.0f;
    if(v->audible_gain!=target)
        v->audible_gain=target>v->audible_gain?fminf(target,v->audible_gain+v->audible_ramp):fmaxf(target,v->audible_gain-v->audible_ramp);
    if(v->mix_left!=v->target_left)v->mix_left+=fmaxf(-v->mix_ramp,fminf(v->mix_ramp,v->target_left-v->mix_left));
    if(v->mix_right!=v->target_right)v->mix_right+=fmaxf(-v->mix_ramp,fminf(v->mix_ramp,v->target_right-v->mix_right));
    float gain=level*envelope*v->audible_gain*reference_event_fade(e,v,time-e->start);
    if(e->note_count!=1)gain/=e->note_count;
    TsStereoFrame card={0,0};
    for(int n=0;n<e->note_count;++n) {
        if(e->source->pattern){
            TsStereoFrame f=ts_pattern_read_cached(pattern_engine,v->pattern_source->pattern,v,v->generation,fmax(0,time-e->start-v->pattern_origin),rate,speed,e->notes[n],e->looping,&v->pattern_player[n]);
            card.l+=f.l*gain*v->mix_left;card.r+=f.r*gain*v->mix_right;continue;
        }
        TsStereoFrame f;
        v->position[n]=voice_position(e,v,v->travel[n],&v->direction[n],&v->intro[n]);
        if(!e->looping) {
            if(v->position[n]>=e->last)continue;
            f=mosaic_sample_read(e,v,n);
            float release=(float)fmin(1,(e->last-v->position[n])/(v->step[n]*speed*rate*0.002));
            f.l*=release;f.r*=release;
        } else f=mosaic_sample_read(e,v,n);
        if(e->live) {
            /* Preserve the exact loop period. Each pitched voice gets its own
               short edge fade instead of an overlapping splice that shortens it. */
            double splice=fmin((e->last-e->first)*.25,e->source->sample.sample_rate*.002);
            float edge=splice>0?(float)fmax(0,fmin(1,fmin((v->position[n]-e->first)/splice,(e->last-v->position[n])/splice))):1;
            f.l*=edge;f.r*=edge;
        }
        card.l+=f.l*gain*v->mix_left;card.r+=f.r*gain*v->mix_right;
        v->travel[n]+=v->step[n]*speed;
    }
    if(routes) {
        /* A settled identity route changes no bus or recording tap. Preserve
           its meters and signed-zero arithmetic; all transitions/custom routes
           (and nonfinite input) continue through the general routing code. */
        if(v->route_passthrough && isfinite(card.l) && isfinite(card.r)) {
            float left=fabsf(card.l),right=fabsf(card.r),peak=left>right?left:right;
            if(peak>v->output_route.activity_source)v->output_route.activity_source=peak;
            if(peak>v->output_route.activity_send)v->output_route.activity_send=peak;
            return (TsStereoFrame){card.l+card.r*0,card.l*0+card.r};
        }
        if(!v->output_route.ready)
            ts_source_route_set(&v->output_route,e->output_route,(unsigned)rate);
        card=ts_source_route_frame(&v->output_route,card,routes);
    }
    return card;
}

