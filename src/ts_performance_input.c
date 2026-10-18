#include "tapesister/performance_input.h"
#include <math.h>
#include <string.h>

void ts_performance_inputs_init(TsPerformanceInputs *s)
{
    memset(s,0,sizeof(*s));ts_input_monitor_init(&s->monitor);
    for(int i=0;i<TS_PERFORMANCE_INPUTS;++i)atomic_init(&s->control[i],0);
    atomic_init(&s->channels,0);atomic_init(&s->rate,0);atomic_init(&s->generation,1);
    atomic_init(&s->tuner_request,0);atomic_init(&s->tuner_read,0);atomic_init(&s->tuner_write,0);
    atomic_init(&s->tuner_overflows,0);
}
void ts_performance_inputs_prepare(TsPerformanceInputs *s,unsigned channels,unsigned rate,unsigned frames)
{
    if(!s)return;
    if(channels>TS_PERFORMANCE_INPUTS)channels=0;
    atomic_store(&s->channels,channels);atomic_store(&s->rate,rate);
    atomic_fetch_add(&s->generation,1);
    ts_input_monitor_set_enabled(&s->monitor,0,rate);
    ts_input_monitor_set_prime_frames(&s->monitor,frames>4096?8192:frames*2);
    if(channels)ts_input_monitor_set_enabled(&s->monitor,1,rate);
    /* Invalidate queued tuner samples on device/rate changes. */
    unsigned request=atomic_load(&s->tuner_request);
    atomic_store(&s->tuner_request,request+256u);
}
int ts_performance_inputs_set(TsPerformanceInputs *s,const TsPerformanceMixer *m)
{
    if(!s || !m)return 0;
    int selected=0;
    for(int i=0;i<TS_PERFORMANCE_INPUTS;++i) {
        float gain=isfinite(m->input_gain[i])?fmaxf(0,fminf(2,m->input_gain[i])):0;
        unsigned v=m->input_enabled[i]?0x80000000u|(unsigned)lrintf(gain*1000000):0;
        selected|=m->input_enabled[i]!=0;
        if(atomic_load_explicit(&s->control[i],memory_order_relaxed)!=v)
            atomic_store_explicit(&s->control[i],v,memory_order_release);
    }
    return selected;
}
void ts_performance_inputs_tuner(TsPerformanceInputs *s,int enabled,unsigned channel)
{
    if(!s)return;
    unsigned old=atomic_load(&s->tuner_request),source=enabled && channel<TS_PERFORMANCE_INPUTS?channel+1:0;
    if((old&255u)!=source)atomic_store(&s->tuner_request,((old&~255u)+256u)|source);
}
static void mix_begin(TsPerformanceInputs *s,TsInputMixVoice *v,unsigned channels,unsigned rate)
{
    unsigned generation=atomic_load(&s->generation);
    if(v->generation!=generation){memset(v,0,sizeof(*v));v->generation=generation;}
    unsigned controls[TS_PERFORMANCE_INPUTS];
    channels=channels>TS_PERFORMANCE_INPUTS?TS_PERFORMANCE_INPUTS:channels;
    for(unsigned i=0;i<channels;++i)controls[i]=atomic_load_explicit(&s->control[i],memory_order_acquire);
    v->count=0;v->slew=rate?2.f/(.005f*rate):2;
    for(unsigned i=0;i<channels;++i) {
        unsigned partner=i^1u;
        int paired=partner<channels && (controls[partner]&0x80000000u);
        float gain=(controls[i]&0x7fffffffu)/1000000.f;
        v->target_left[i]=paired && (i&1u)?0:gain;
        v->target_right[i]=paired && !(i&1u)?0:gain;
        if(gain || v->left[i] || v->right[i])v->active[v->count++]=i;
    }
}
static TsStereoFrame mix_frame(TsInputMixVoice *v,const float *frame)
{
    TsStereoFrame out={0,0};
    for(unsigned n=0;n<v->count;++n) {
        unsigned i=v->active[n];
        v->left[i]+=fmaxf(-v->slew,fminf(v->slew,v->target_left[i]-v->left[i]));
        v->right[i]+=fmaxf(-v->slew,fminf(v->slew,v->target_right[i]-v->right[i]));
        float sample=isfinite(frame[i])?frame[i]:0;
        out.l+=sample*v->left[i];out.r+=sample*v->right[i];
    }
    return ts_stereo_frame_sanitize(out);
}
void ts_performance_inputs_capture(TsPerformanceInputs *s,const float *input,size_t frames,unsigned channels,unsigned rate,int duplex)
{
    if(!s || !input || !channels || channels>TS_PERFORMANCE_INPUTS || !rate)return;
    if(!duplex) {
        mix_begin(s,&s->capture_voice,channels,rate);
        if(s->capture_voice.count) {
            ts_input_monitor_note_capture_block(&s->monitor,(uint32_t)frames);
            for(size_t i=0;i<frames;++i)ts_input_monitor_push_frame(&s->monitor,mix_frame(&s->capture_voice,input+i*channels));
        }
    }
    unsigned request=atomic_load_explicit(&s->tuner_request,memory_order_acquire),source=request&255u;
    if(!source || source>channels)return;
    if(request!=s->producer_request || rate!=s->producer_rate) {
        s->producer_request=request;s->producer_rate=rate;s->count=0;s->sum=0;
        s->decimation=(rate+15999)/16000;if(!s->decimation)s->decimation=1;
    }
    unsigned w=atomic_load_explicit(&s->tuner_write,memory_order_relaxed);
    unsigned r=atomic_load_explicit(&s->tuner_read,memory_order_acquire);
    int dropped=0;
    for(size_t i=0;i<frames;++i) {
        float sample=input[i*channels+source-1];s->sum+=isfinite(sample)?sample:0;
        if(++s->count==s->decimation) {
            if(w-r<TS_TUNER_RING) {
                s->tuner_ring[w++%TS_TUNER_RING]=(TsTunerSample){s->sum/s->count,(float)rate/s->decimation,request};
            } else dropped=1;
            s->count=0;s->sum=0;
        }
    }
    atomic_store_explicit(&s->tuner_write,w,memory_order_release);
    if(dropped)atomic_fetch_add_explicit(&s->tuner_overflows,1,memory_order_release);
}
void ts_performance_inputs_begin(TsPerformanceInputs *s,int duplex,unsigned channels,unsigned rate)
{
    if(!s)return;
    if(duplex)mix_begin(s,&s->duplex_voice,channels,rate);
    else ts_input_monitor_recover_backlog(&s->monitor);
}
TsStereoFrame ts_performance_inputs_read(TsPerformanceInputs *s,int duplex,const float *frame,unsigned channels,unsigned rate)
{
    if(!s || !atomic_load_explicit(&s->channels,memory_order_acquire))return (TsStereoFrame){0,0};
    if(duplex)return frame && channels?mix_frame(&s->duplex_voice,frame):(TsStereoFrame){0,0};
    return ts_input_monitor_read_frame(&s->monitor,rate);
}
static TsTunerReading detect_pitch(const float *x,float rate)
{
    TsTunerReading result={0};float difference[TS_TUNER_LAGS+1],raw[TS_TUNER_LAGS+1];
    float mean=0,energy=0;for(int i=0;i<1024;++i)mean+=x[i];mean/=1024;
    for(int i=0;i<1024;++i){float v=x[i]-mean;energy+=v*v;}
    if(energy/1024<1e-7f)return result;
    unsigned maximum=(unsigned)(rate/25.f),minimum=(unsigned)(rate/2100.f);
    if(maximum>TS_TUNER_LAGS)maximum=TS_TUNER_LAGS;
    if(minimum<2)minimum=2;
    float cumulative=0;difference[0]=1;
    for(unsigned lag=1;lag<=maximum;++lag) {
        float sum=0;for(int j=0;j<1024;++j){float d=x[j]-x[j+lag];sum+=d*d;}
        raw[lag]=sum;cumulative+=sum;difference[lag]=cumulative>1e-20f?sum*lag/cumulative:1;
    }
    unsigned period=0;
    for(unsigned lag=minimum;lag<maximum;++lag)if(difference[lag]<.12f) {
        while(lag+1<=maximum && difference[lag+1]<difference[lag])++lag;
        period=lag;break;
    }
    if(!period || period>=maximum)return result;
    float a=difference[period-1],b=difference[period],c=difference[period+1];
    float denominator=a-2*b+c,offset=fabsf(denominator)>1e-12f?.5f*(a-c)/denominator:0;
    float refined=period+fmaxf(-1,fminf(1,offset));
    /* Refine over several cycles: sub-sample interpolation of a short high
       note's single period otherwise introduces several cents of bias. */
    unsigned multiple=(unsigned)((maximum-3)/refined);
    if(multiple>8)multiple=8;
    if(multiple>1) {
        unsigned center=(unsigned)lrintf(refined*multiple),best=center;
        for(unsigned lag=center-2;lag<=center+2 && lag<maximum;++lag)
            if(raw[lag]<raw[best])best=lag;
        a=raw[best-1];b=raw[best];c=raw[best+1];denominator=a-2*b+c;
        offset=fabsf(denominator)>1e-12f?.5f*(a-c)/denominator:0;
        refined=(best+fmaxf(-1,fminf(1,offset)))/multiple;
    }
    float frequency=rate/refined;
    float note=69+12*log2f(frequency/440.f);
    result=(TsTunerReading){1,(int)lrintf(note),frequency,0,1-difference[period]};
    result.cents=100*(note-result.note);return result;
}
unsigned ts_performance_inputs_analyze(TsPerformanceInputs *s,TsInputTuner *t)
{
    unsigned request=atomic_load(&s->tuner_request);
    if(t->request!=request){memset(t,0,sizeof(*t));t->request=request;}
    unsigned r=atomic_load_explicit(&s->tuner_read,memory_order_relaxed);
    unsigned w=atomic_load_explicit(&s->tuner_write,memory_order_acquire),received=0;
    unsigned overflows=atomic_load_explicit(&s->tuner_overflows,memory_order_acquire);
    if(t->overflows!=overflows) {
        t->overflows=overflows;t->count=0;t->reading.valid=0;
        atomic_store_explicit(&s->tuner_read,w,memory_order_release);return 0;
    }
    for(;r!=w;++r) {
        TsTunerSample v=s->tuner_ring[r%TS_TUNER_RING];
        if(v.request!=request || !(request&255u))continue;
        if(t->rate!=v.rate){t->count=0;t->rate=v.rate;t->reading.valid=0;}
        t->samples[t->count++%TS_TUNER_WINDOW]=v.sample;++received;
    }
    atomic_store_explicit(&s->tuner_read,r,memory_order_release);
    if(received && t->count>=TS_TUNER_WINDOW) {
        float ordered[TS_TUNER_WINDOW];
        for(unsigned i=0;i<TS_TUNER_WINDOW;++i)ordered[i]=t->samples[(t->count+i)%TS_TUNER_WINDOW];
        t->reading=detect_pitch(ordered,t->rate);
        /* Keep the circular index bounded for multi-day sessions. */
        t->count=TS_TUNER_WINDOW+t->count%TS_TUNER_WINDOW;
    }
    return received;
}
