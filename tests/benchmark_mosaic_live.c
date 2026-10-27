/* Explicit development-host CPU measurement, never a CI deadline gate. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/mosaic.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <time.h>
int main(void)
{
    float samples[4800];for(int i=0;i<4800;++i)samples[i]=.1f*sinf(i*.03f);
    TsSample sample={.data=samples,.frames=4800,.sample_rate=48000,.channels=1};
    puts("live_cards,placed_cards,matrix,median_ms_per_256_frames");
    for(int n=0;n<4;++n)for(int matrix=0;matrix<2;++matrix) {
        int count=(int[]){0,1,8,32}[n];TsMosaic *m=ts_mosaic_create();assert(m);char error[160];
        TsMosaicSource *source=ts_mosaic_source(m,&sample,error,sizeof(error));assert(source);
        for(int i=0;i<1024;++i) {
            TsMosaicEvent *e=ts_mosaic_add(m,source,100+i,0);assert(e);
            if(i<count){e->live=1;e->live_tempo=1;e->live_beats=.2;e->pan=(i%3-1)*.4f;ts_mosaic_live_launch(m,e->id,0);
                if(matrix && i%3==0){e->output_route.matrix_enabled=1;e->output_route.matrix_gain[7]=750;}}
        }
        double times[3],checksum=0;ts_mosaic_live_clock(m,1,0,.5,.25);
        for(int trial=0;trial<3;++trial) {
            clock_t started=clock();
            for(int block=0;block<512;++block) {
                if(block%32==0 && count){m->events[0].muted^=1;m->events[0].pan=-m->events[0].pan;}
                ts_mosaic_begin_block(m,256);
                for(int frame=0;frame<256;++frame) {
                    TsSourceRouteMix routes={0};TsStereoFrame f=ts_mosaic_read_routed(m,48000,matrix?&routes:NULL);checksum+=f.l+f.r;
                }
            }
            times[trial]=1000.0*(clock()-started)/CLOCKS_PER_SEC/512;
        }
        for(int i=0;i<3;++i)for(int j=i+1;j<3;++j)if(times[j]<times[i]){double t=times[i];times[i]=times[j];times[j]=t;}
        assert(isfinite(checksum));printf("%d,1024,%d,%.6f\n",count,matrix,times[1]);ts_mosaic_free(m);
    }
}
