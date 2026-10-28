#ifndef TAPESISTER_LOOP_CAPTURE_H
#define TAPESISTER_LOOP_CAPTURE_H
#include "tapesister/performance_recorder.h"
#define TS_LOOP_CAPTURE_QUEUE 32
#define TS_LOOP_CAPTURE_MAX_SECONDS 60
/* Writer-owned, ~20 ms peak bins. Covers more than the longest take while
   keeping preview memory fixed and all analysis off the audio producer. */
#define TS_LOOP_CAPTURE_PREVIEW_BINS 4096
enum { TS_LOOP_CAPTURE_OK,TS_LOOP_CAPTURE_QUEUE_FULL,TS_LOOP_CAPTURE_WRITER_FAILED,TS_LOOP_CAPTURE_LENGTH_LIMIT };
typedef struct { uint64_t first,last;int full_length; } TsLoopTake;
typedef struct {
    TsPerformanceRecorder file;
    TsLoopTake takes[TS_LOOP_CAPTURE_QUEUE];
    _Atomic unsigned produced,consumed;
    _Atomic int active,failed;
    uint64_t first,frames,target;
    double period,period_error; /* Fractional capture frames, carried between full takes. */
    int continuous;
    unsigned preview_stride;
    _Atomic uint64_t preview[TS_LOOP_CAPTURE_PREVIEW_BINS];
    _Atomic uint64_t preview_frames;
} TsLoopCapture;
void ts_loop_capture_init(TsLoopCapture *);
void ts_loop_capture_free(TsLoopCapture *); /* Producer stopped, writer joined. */
int ts_loop_capture_start(TsLoopCapture *,const char *path,unsigned rate,unsigned channels,
    double target_frames,int continuous,char *error,size_t size);
/* Single producer. boundary() is also a control command under producer exclusion.
   stop keeps the current partial take; discard drops only that partial range.
   Every input frame still goes to the session's recoverable archive. */
void ts_loop_capture_push(TsLoopCapture *,TsStereoFrame);
void ts_loop_capture_boundary(TsLoopCapture *,int stop,int discard);
int ts_loop_capture_peek(const TsLoopCapture *,TsLoopTake *);
void ts_loop_capture_consume(TsLoopCapture *);
/* Single disk-writer thread, in place of performance_recorder_pump(). */
int ts_loop_capture_pump(TsLoopCapture *,size_t max_frames);
/* UI reader; overview of [first,last) at ~20 ms resolution. It may trail the
   producer while disk work is queued. Does not allocate, read disk or lock. */
void ts_loop_capture_preview(const TsLoopCapture *,uint64_t first,uint64_t last,float *peaks,size_t count);
#endif
