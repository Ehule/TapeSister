#include "tapesister/loop_capture.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
static void capture_preview_write(void *,const TsStereoFrame *,size_t,uint64_t,uint8_t);
void ts_loop_capture_init(TsLoopCapture *c)
{
    memset(c,0,sizeof(*c));ts_performance_recorder_init(&c->file);
    atomic_init(&c->produced,0);atomic_init(&c->consumed,0);
    atomic_init(&c->active,0);atomic_init(&c->failed,0);
    atomic_init(&c->preview_frames,0);
    for(int i=0;i<TS_LOOP_CAPTURE_PREVIEW_BINS;++i)atomic_init(&c->preview[i],0);
}
void ts_loop_capture_free(TsLoopCapture *c)
{ts_performance_recorder_free(&c->file);
 atomic_store(&c->active,0);atomic_store(&c->failed,0);atomic_store(&c->produced,0);atomic_store(&c->consumed,0);
 c->first=c->frames=c->target=0;c->period=c->period_error=0;c->continuous=0;
 c->preview_stride=0;atomic_store(&c->preview_frames,0);}
int ts_loop_capture_start(TsLoopCapture *c,const char *path,unsigned rate,unsigned channels,
    double target,int continuous,char *error,size_t size)
{
    if(!rate || !isfinite(target) || target<0 || target>(uint64_t)rate*TS_LOOP_CAPTURE_MAX_SECONDS || (target && target<2) ||
       atomic_load(&c->active) || !atomic_is_lock_free(&c->produced) || !atomic_is_lock_free(&c->active)) {
        snprintf(error,size,"Loop length must fit 60 seconds / finish the previous capture");return 0;
    }
    if(!ts_performance_recorder_start(&c->file,path,rate,(uint8_t)channels,(size_t)rate*4,error,size))return 0;
    c->file.live_ranges=1;c->period=target;c->target=(uint64_t)floor(target+.5);c->period_error=target-c->target;
    c->continuous=continuous;c->first=c->frames=0;
    c->preview_stride=(rate+49)/50;
    c->file.writer_observer=capture_preview_write;c->file.writer_userdata=c;
    atomic_store(&c->preview_frames,0);
    for(int i=0;i<TS_LOOP_CAPTURE_PREVIEW_BINS;++i)atomic_store(&c->preview[i],0);
    atomic_store(&c->produced,0);atomic_store(&c->consumed,0);atomic_store(&c->failed,0);
    atomic_store_explicit(&c->active,1,memory_order_release);return 1;
}
void ts_loop_capture_boundary(TsLoopCapture *c,int stop,int discard)
{
    if(!atomic_load_explicit(&c->active,memory_order_acquire))return;
    int full=c->target && c->frames-c->first>=c->target;
    if(!discard && c->frames-c->first>=2) {
        unsigned w=atomic_load_explicit(&c->produced,memory_order_relaxed);
        unsigned r=atomic_load_explicit(&c->consumed,memory_order_acquire);
        if(w-r>=TS_LOOP_CAPTURE_QUEUE) {atomic_store(&c->failed,TS_LOOP_CAPTURE_QUEUE_FULL);stop=1;}
        else {c->takes[w%TS_LOOP_CAPTURE_QUEUE]=(TsLoopTake){c->first,c->frames,full};
            atomic_store_explicit(&c->produced,w+1,memory_order_release);}
    }
    atomic_store_explicit(&c->file.range_checkpoint,c->frames,memory_order_release);
    c->first=c->frames;
    if(c->period>0 && c->continuous && !stop) {
        /* Manual NEXT establishes a fresh capture origin. Full automatic
           layers carry fractional frames, bounding cumulative error to 1/2 frame. */
        double next=c->period+(full?c->period_error:0);
        c->target=(uint64_t)floor(next+.5);c->period_error=next-c->target;
    }
    if(stop || !c->continuous) {
        atomic_store_explicit(&c->active,0,memory_order_release);
        ts_performance_recorder_request_stop(&c->file);
    }
}
void ts_loop_capture_push(TsLoopCapture *c,TsStereoFrame f)
{
    if(!c || !atomic_load_explicit(&c->active,memory_order_relaxed))return;
    if(!ts_performance_recorder_push_frame(&c->file,f)) {
        atomic_store(&c->failed,TS_LOOP_CAPTURE_WRITER_FAILED);ts_loop_capture_boundary(c,1,0);return;
    }
    ++c->frames;
    uint64_t length=c->frames-c->first;
    if(c->target && length>=c->target)ts_loop_capture_boundary(c,0,0);
    else if(length>=(uint64_t)c->file.sample_rate*TS_LOOP_CAPTURE_MAX_SECONDS) {
        atomic_store(&c->failed,TS_LOOP_CAPTURE_LENGTH_LIMIT);ts_loop_capture_boundary(c,1,0);
    } /* Manual take safety cap, archive retained. */
}
int ts_loop_capture_peek(const TsLoopCapture *c,TsLoopTake *take)
{
    unsigned r=atomic_load_explicit(&c->consumed,memory_order_relaxed);
    if(r==atomic_load_explicit(&c->produced,memory_order_acquire))return 0;
    *take=c->takes[r%TS_LOOP_CAPTURE_QUEUE];return 1;
}
void ts_loop_capture_consume(TsLoopCapture *c)
{atomic_fetch_add_explicit(&c->consumed,1,memory_order_release);}

static uint64_t preview_word(uint64_t bin,float peak)
{
    uint32_t bits;memcpy(&bits,&peak,sizeof(bits));
    return ((uint64_t)(uint32_t)(bin+1)<<32)|bits;
}
static float preview_peak(uint64_t word,uint64_t bin)
{
    if((uint32_t)(word>>32)!=(uint32_t)(bin+1))return 0;
    uint32_t bits=(uint32_t)word;float peak;memcpy(&peak,&bits,sizeof(peak));return peak;
}
static void capture_preview_write(void *userdata,const TsStereoFrame *frames,size_t count,uint64_t first,uint8_t channels)
{
    TsLoopCapture *c=userdata;uint64_t end=first+count;
    /* The writer has successfully stored these frames and still owns their
       ring slots. The producer cannot recycle them during peak analysis. */
    for(uint64_t at=first;c->preview_stride && at<end;) {
        uint64_t bin=at/c->preview_stride,next=(bin+1)*c->preview_stride;
        if(next>end)next=end;
        float peak=preview_peak(atomic_load_explicit(&c->preview[bin%TS_LOOP_CAPTURE_PREVIEW_BINS],memory_order_relaxed),bin);
        for(;at<next;++at) {
            TsStereoFrame value=frames[at-first];
            float level=channels==1?fabsf((value.l+value.r)*.5f):fmaxf(fabsf(value.l),fabsf(value.r));
            if(isfinite(level))peak=fmaxf(peak,level);
        }
        atomic_store_explicit(&c->preview[bin%TS_LOOP_CAPTURE_PREVIEW_BINS],preview_word(bin,peak),memory_order_release);
    }
    atomic_store_explicit(&c->preview_frames,end,memory_order_release);
}
int ts_loop_capture_pump(TsLoopCapture *c,size_t max_frames)
{return ts_performance_recorder_pump(&c->file,max_frames);}
void ts_loop_capture_preview(const TsLoopCapture *c,uint64_t first,uint64_t last,float *peaks,size_t count)
{
    if(!peaks || !count)return;
    memset(peaks,0,count*sizeof(*peaks));
    if(!c || !c->preview_stride || last<=first)return;
    uint64_t end=atomic_load_explicit(&c->preview_frames,memory_order_acquire);
    if(end>last)end=last;
    if(end<=first)return;
    uint64_t span=last-first;
    for(size_t i=0;i<count;++i) {
        uint64_t a=first+(uint64_t)((long double)span*i/count),b=first+(uint64_t)((long double)span*(i+1)/count);
        if(a>=end)break;
        if(b<=a)b=a+1;
        if(b>end)b=end;
        for(uint64_t bin=a/c->preview_stride;bin<=(b-1)/c->preview_stride;++bin) {
            uint64_t word=atomic_load_explicit(&c->preview[bin%TS_LOOP_CAPTURE_PREVIEW_BINS],memory_order_acquire);
            peaks[i]=fmaxf(peaks[i],preview_peak(word,bin));
        }
    }
}
