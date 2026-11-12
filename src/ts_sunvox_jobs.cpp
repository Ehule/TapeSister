#include "tapesister/sunvox_fx.h"
#include "sundog.h"
#include <atomic>
#include <thread>
#include <cfenv>
#include <new>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif

static std::atomic<unsigned> requested_workers{0};
struct TsSunVoxBatch {
    ssemaphore wake{};
    std::thread worker;
    std::atomic<int> state{0}; // idle, pending, claimed, complete
    std::atomic<bool> stopping{false},notified{false};
    std::atomic<uint64_t> dispatched{0},stolen{0};
    TsSunVoxJob *job=nullptr;
    uint64_t (*clock)(void)=nullptr;
    fenv_t environment{};
#if defined(__SSE__)
    unsigned mxcsr=0;
#endif
};
static void run_job(TsSunVoxJob &job,uint64_t (*clock)(void))
{
    uint64_t start=job.timed && clock?clock():0;
    ts_sunvox_render_pending(job.engine);
    uint64_t end=start?clock():0;
    job.ticks=end>=start?end-start:0;
}
void ts_sunvox_workers_configure(unsigned workers)
{requested_workers.store(workers==1?1:0,std::memory_order_relaxed);}
unsigned ts_sunvox_workers_configured(void)
{return requested_workers.load(std::memory_order_relaxed);}
TsSunVoxBatch *ts_sunvox_batch_new(unsigned workers)
{
    if(workers!=1 || std::thread::hardware_concurrency()<2)return nullptr;
    TsSunVoxBatch *b=new(std::nothrow) TsSunVoxBatch;
    if(!b)return nullptr;
    if(!b->state.is_lock_free() || !b->notified.is_lock_free() || !b->dispatched.is_lock_free() ||
       !b->stolen.is_lock_free() || ssemaphore_create(&b->wake,nullptr,0,0)) {delete b;return nullptr;}
    try {
        b->worker=std::thread([b] {
            for(;;) {
                if(ssemaphore_wait(&b->wake,STHREAD_TIMEOUT_INFINITE))continue;
                b->notified.store(false,std::memory_order_relaxed);
                if(b->stopping.load(std::memory_order_acquire))break;
                int pending=1;
                if(!b->state.compare_exchange_strong(pending,2,std::memory_order_acquire,std::memory_order_relaxed))continue;
                fenv_t previous;std::fegetenv(&previous);
#if defined(__SSE__)
                unsigned saved=_mm_getcsr();
#endif
                std::fesetenv(&b->environment);
#if defined(__SSE__)
                _mm_setcsr(b->mxcsr);
#endif
                run_job(*b->job,b->clock);
#if defined(__SSE__)
                _mm_setcsr(saved);
#endif
                std::fesetenv(&previous);
                b->state.store(3,std::memory_order_release);
            }
        });
    } catch(...) {ssemaphore_destroy(&b->wake);delete b;return nullptr;}
    return b;
}
void ts_sunvox_batch_free(TsSunVoxBatch *b)
{
    if(!b)return;
    // Caller has stopped/excluded audio; no submitted job can outlive render().
    b->stopping.store(true,std::memory_order_release);ssemaphore_release(&b->wake);
    b->worker.join();ssemaphore_destroy(&b->wake);delete b;
}
void ts_sunvox_batch_render(TsSunVoxBatch *b,TsSunVoxJob *jobs,unsigned count,uint64_t (*clock)(void))
{
    int chosen=-1;unsigned largest=0;
    if(b && count>1)for(unsigned i=0;i<count;++i) {
        unsigned cost=ts_sunvox_parallel_cost(jobs[i].engine);
        // Do not dispatch trivial graphs or the low-latency 64-frame path.
        if(cost>=16 && cost>largest){largest=cost;chosen=(int)i;}
    }
    if(chosen<0){for(unsigned i=0;i<count;++i)run_job(jobs[i],clock);return;}
    b->job=&jobs[chosen];b->clock=clock;std::fegetenv(&b->environment);
#if defined(__SSE__)
    b->mxcsr=_mm_getcsr();
#endif
    // Publish the job and FP context before either thread claims it. READY
    // completion is the matching release for output/timing read by audio.
    b->state.store(1,std::memory_order_release);
    b->dispatched.fetch_add(1,std::memory_order_relaxed);
    if(!b->notified.exchange(true,std::memory_order_relaxed) && ssemaphore_release(&b->wake))
        b->notified.store(false,std::memory_order_relaxed);
    // Noneligible engines keep their relative order on the audio thread,
    // including modules using the SDK's process-global random generator.
    for(unsigned i=0;i<count;++i)if((int)i!=chosen)run_job(jobs[i],clock);
    int pending=1;
    if(b->state.compare_exchange_strong(pending,2,std::memory_order_acquire,std::memory_order_relaxed)) {
        // A late worker cannot hold up a job that it has not started.
        b->stolen.fetch_add(1,std::memory_order_relaxed);run_job(jobs[chosen],clock);
    } else {
        while(b->state.load(std::memory_order_acquire)!=3)std::this_thread::yield();
    }
    b->state.store(0,std::memory_order_release);
}
uint64_t ts_sunvox_batch_dispatched(const TsSunVoxBatch *b)
{return b?b->dispatched.load(std::memory_order_relaxed):0;}
uint64_t ts_sunvox_batch_stolen(const TsSunVoxBatch *b)
{return b?b->stolen.load(std::memory_order_relaxed):0;}
