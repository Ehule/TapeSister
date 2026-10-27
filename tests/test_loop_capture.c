#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/loop_capture.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>
static void drain(TsLoopCapture *c)
{while(ts_performance_recorder_pump(&c->file,8192)) {if(atomic_load(&c->active))break;}}
static void check_take(TsLoopCapture *c,uint64_t first,uint64_t last)
{
    TsLoopTake take;TsSample sample={0};char error[160];
    assert(ts_loop_capture_peek(c,&take) && take.first==first && take.last==last);
    assert(ts_performance_recorder_load_range(&c->file,first,last,&sample,error,sizeof(error)));
    assert(sample.frames==last-first && sample.channels==2);
    for(size_t i=0;i<sample.frames;++i) {
        assert(sample.data[i*2]==(float)(first+i)/1000);
        assert(sample.data[i*2+1]==-(float)(first+i)/2000);
    }
    ts_sample_free(&sample);ts_loop_capture_consume(c);
}
int main(void)
{
    TsLoopCapture c;ts_loop_capture_init(&c);char error[160];
    assert(!ts_loop_capture_start(&c,"unusable/directory/take.wav",1000,2,50,1,error,sizeof(error)));
    assert(!ts_loop_capture_start(&c,"loop-capture-test.wav",1000,2,60001,1,error,sizeof(error)));
    assert(ts_loop_capture_start(&c,"loop-capture-test.wav",1000,2,100,1,error,sizeof(error)));
    for(int i=0;i<350;++i) {
        ts_loop_capture_push(&c,(TsStereoFrame){i/1000.f,-i/2000.f});
        if(i==149) {drain(&c);check_take(&c,0,100);} /* Read a finished layer while next still records. */
    }
    assert(atomic_load(&c.active));ts_loop_capture_boundary(&c,1,0);drain(&c);
    check_take(&c,100,200);check_take(&c,200,300);check_take(&c,300,350);
    TsLoopTake take;assert(!ts_loop_capture_peek(&c,&take));assert(ts_performance_recorder_frames(&c.file)==350);
    TsSample full={0};assert(ts_performance_recorder_load(&c.file,&full,error,sizeof(error)) && full.frames==350);ts_sample_free(&full);
    ts_loop_capture_free(&c);
    /* Manual next is frame exact, and stopping/discarding affects only current partial. */
    assert(ts_loop_capture_start(&c,"loop-capture-test.wav",1000,2,0,1,error,sizeof(error)));
    for(int i=0;i<150;++i){ts_loop_capture_push(&c,(TsStereoFrame){i/1000.f,-i/2000.f});if(i==49)ts_loop_capture_boundary(&c,0,0);}
    ts_loop_capture_boundary(&c,1,1);drain(&c);check_take(&c,0,50);assert(!ts_loop_capture_peek(&c,&take));
    assert(ts_performance_recorder_frames(&c.file)==150);ts_loop_capture_free(&c);
    /* Single mode does not unexpectedly arm another layer. */
    assert(ts_loop_capture_start(&c,"loop-capture-test.wav",1000,1,25,0,error,sizeof(error)));
    for(int i=0;i<50;++i)ts_loop_capture_push(&c,(TsStereoFrame){.1f,.1f});
    assert(!atomic_load(&c.active) && c.frames==25);drain(&c);ts_loop_capture_free(&c);
    /* Slow UI: never overwrite an unconsumed descriptor; the full audio prefix
       remains archived even when creation must stop at the bounded queue. */
    assert(ts_loop_capture_start(&c,"loop-capture-test.wav",1000,2,10,1,error,sizeof(error)));
    for(int i=0;i<400;++i)ts_loop_capture_push(&c,(TsStereoFrame){.1f,-.1f});
    assert(!atomic_load(&c.active) && atomic_load(&c.failed));drain(&c);
    int count=0;while(ts_loop_capture_peek(&c,&take)){assert(take.first==(uint64_t)count*10);++count;ts_loop_capture_consume(&c);}assert(count==TS_LOOP_CAPTURE_QUEUE);
    assert(ts_performance_recorder_load(&c.file,&full,error,sizeof(error)) && full.frames==330);ts_sample_free(&full);ts_loop_capture_free(&c);
    /* A stalled disk writer stops capture at the first queue overrun; it
       retains complete accepted frames and does not manufacture a gap. */
    assert(ts_loop_capture_start(&c,"loop-capture-test.wav",1000,2,0,1,error,sizeof(error)));
    for(int i=0;i<4500;++i)ts_loop_capture_push(&c,(TsStereoFrame){.1f,-.1f});
    assert(atomic_load(&c.failed) && c.frames==4000 && !atomic_load(&c.active));drain(&c);
    assert(ts_performance_recorder_load(&c.file,&full,error,sizeof(error)) && full.frames==4000);ts_sample_free(&full);ts_loop_capture_free(&c);
    assert(ts_loop_capture_start(&c,"loop-capture-test.wav",10,2,0,1,error,sizeof(error)));
    for(int i=0;i<601;++i){ts_loop_capture_push(&c,(TsStereoFrame){.1f,-.1f});if(i%10==0)drain(&c);}
    assert(!atomic_load(&c.active) && atomic_load(&c.failed)==TS_LOOP_CAPTURE_LENGTH_LIMIT && c.frames==600);drain(&c);
    assert(ts_performance_recorder_load(&c.file,&full,error,sizeof(error)) && full.frames==600);ts_sample_free(&full);ts_loop_capture_free(&c);
    remove("loop-capture-test.wav");puts("Live capture: contiguous layers, concurrent range reads, partial stop/discard, queue/disk recovery passed");return 0;
}
