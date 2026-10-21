/* Explicit CPU benchmark, not a timing-sensitive CI gate. Optional raw output
   supports exact before/after comparison of the same deterministic stream. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/mosaic.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc,char **argv)
{
    const int totals[]={1,128,128,128,1024,1024};
    const int active[]={1,1,16,128,1,16};
    FILE *capture=argc>1?fopen(argv[1],"wb"):NULL;if(argc>1)assert(capture);
    printf("capacity=%d model=%zu event=%zu voice=%zu bytes\n",TS_MOSAIC_EVENTS,sizeof(TsMosaic),sizeof(TsMosaicEvent),sizeof(TsMosaicVoice));
    TsSample sample={0};sample.frames=4096;sample.sample_rate=48000;sample.channels=2;
    sample.data=malloc(sample.frames*2*sizeof(float));assert(sample.data);
    for(size_t i=0;i<sample.frames;++i){sample.data[2*i]=.2f*sinf(i*.019f);sample.data[2*i+1]=.15f*cosf(i*.031f);}
    for(int scenario=0;scenario<6;++scenario) {
        int total=totals[scenario];if(total>TS_MOSAIC_EVENTS)continue;
        TsMosaic *m=ts_mosaic_create();assert(m);char error[160];
        TsMosaicSource *source=ts_mosaic_source(m,&sample,error,sizeof(error));assert(source);
        for(int i=0;i<total;++i) {
            TsMosaicEvent *e=ts_mosaic_add(m,source,i<active[scenario]?0:10+i,0);assert(e);
            e->duration=4;e->gain=.5f;e->pan=(i%3-1)*.4f;e->mode=(TsLoopMode)(i%TS_LOOP_MODE_COUNT);
            e->first=100;e->crossfade=32;e->notes[0]=48+i%25;
        }
        double best=1e9;float checksum=0;
        for(int trial=0;trial<3;++trial) {
            ts_mosaic_seek(m,0);m->playing=1;m->was_playing=0;m->last_output=(TsStereoFrame){0};m->transition_remaining=0;
            clock_t start=clock();
            for(int block=0;block<48;++block) {
                ts_mosaic_begin_block(m,512);
                for(int frame=0;frame<512;++frame) {
                    TsStereoFrame out=ts_mosaic_read(m,48000);checksum+=out.l+out.r;
                    if(capture && !trial)assert(fwrite(&out,sizeof(out),1,capture)==1);
                }
            }
            double elapsed=(double)(clock()-start)/CLOCKS_PER_SEC;if(elapsed<best)best=elapsed;
        }
        printf("%4d cards / %3d active: %.3f ms/512 frames, %.2f%% of one CPU core, checksum %.7g\n",
            total,active[scenario],best*1000/48,best/(48*512.0/48000)*100,checksum);
        ts_mosaic_free(m);
    }
    if(capture)assert(!fclose(capture));ts_sample_free(&sample);return 0;
}
