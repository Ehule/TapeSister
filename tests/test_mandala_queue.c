#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#include "tapesister/mandala.h"
#include <SDL2/SDL.h>
#include <assert.h>
#include <stdlib.h>

static TsMandalaQueue queue;
static atomic_uint done;
static int produce(void *unused)
{
    (void)unused;
    float out[128*255];
    for(unsigned generation=1;generation<=100000;++generation) {
        unsigned channels=((unsigned[]){16,26,255})[generation%3];
        for(unsigned i=0;i<128*channels;++i)out[i]=(float)generation;
        ts_mandala_push(&queue,out,128,channels);
    }
    atomic_store(&done,1);return 0;
}
static int compare(const void *a,const void *b)
{ Uint64 x=*(const Uint64*)a,y=*(const Uint64*)b;return (x>y)-(x<y); }
int main(int argc,char **argv)
{
    (void)argc;(void)argv;
    assert(!SDL_Init(SDL_INIT_TIMER));ts_mandala_queue_init(&queue);atomic_store(&queue.enabled,1);
    SDL_Thread *thread=SDL_CreateThread(produce,"visual packet producer",NULL);assert(thread);
    TsMandalaPacket packet;unsigned received=0;
    while(!atomic_load(&done)) {
        if(ts_mandala_latest(&queue,&packet)) {
            float value=packet.samples[0];
            assert(packet.channels==((unsigned[]){16,26,255})[(unsigned)value%3]);
            assert(packet.count*packet.channels<=TS_MANDALA_VALUES);
            for(unsigned i=0;i<packet.count;++i)for(unsigned c=0;c<packet.channels;++c)assert(packet.samples[i*packet.channels+c]==value);
            ++received;
        } else SDL_Delay(1);
    }
    SDL_WaitThread(thread,NULL);assert(received);printf("SPSC coherent snapshots=%u drops=%u\n",received,atomic_load(&queue.dropped));
    ts_mandala_latest(&queue,&packet);
    atomic_store(&done,0);thread=SDL_CreateThread(produce,"stalled consumer",NULL);assert(thread);
    SDL_WaitThread(thread,NULL);assert(atomic_load(&done));
    assert(atomic_load(&queue.write)-atomic_load(&queue.read)<=4);
    ts_mandala_latest(&queue,&packet);
    float out[512*16]={0};Uint64 *cost=malloc(100000*sizeof(*cost));assert(cost);
    double frequency=(double)SDL_GetPerformanceFrequency();
    for(int mode=0;mode<3;++mode) {
        atomic_store(&queue.enabled,mode!=0);
        Uint64 total=0;
        for(int i=0;i<100000;++i) {
            if(mode==2)ts_mandala_latest(&queue,&packet);
            Uint64 start=SDL_GetPerformanceCounter();ts_mandala_push(&queue,out,512,16);
            cost[i]=SDL_GetPerformanceCounter()-start;total+=cost[i];
        }
        qsort(cost,100000,sizeof(*cost),compare);
        printf("producer %s mean_ns=%.1f p99_ns=%.1f max_ns=%.1f\n",mode==0?"OFF":mode==1?"FULL":"COPY-16CH",
            total*1e9/frequency/100000,cost[99000]*1e9/frequency,cost[99999]*1e9/frequency);
    }
    free(cost);SDL_Quit();return 0;
}
