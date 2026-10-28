#include "tapesister/loop_capture.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
void ts_loop_capture_init(TsLoopCapture *c)
{
    memset(c,0,sizeof(*c));ts_performance_recorder_init(&c->file);
    atomic_init(&c->produced,0);atomic_init(&c->consumed,0);
    atomic_init(&c->active,0);atomic_init(&c->failed,0);
}
void ts_loop_capture_free(TsLoopCapture *c)
{ts_performance_recorder_free(&c->file);
 atomic_store(&c->active,0);atomic_store(&c->failed,0);atomic_store(&c->produced,0);atomic_store(&c->consumed,0);
 c->first=c->frames=c->target=0;c->period=c->period_error=0;c->continuous=0;}
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
