#ifndef TAPESISTER_LOOP_CAPTURE_H
#define TAPESISTER_LOOP_CAPTURE_H
#include "tapesister/performance_recorder.h"
#define TS_LOOP_CAPTURE_QUEUE 32
#define TS_LOOP_CAPTURE_MAX_SECONDS 60
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
#endif
