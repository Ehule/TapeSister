/* Explicit host measurement, no CI timing threshold or hardware claim. */
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
    float data[4800];for(int i=0;i<4800;++i)data[i]=.1f*sinf(i*.03f);
    TsSample sample={.data=data,.frames=4800,.sample_rate=48000,.channels=1};
    puts("step_seconds,sequence_members,background_loops,placed_cards,matrix,median_ms_per_256_frames");
    for(int length=0;length<3;++length)for(int matrix=0;matrix<2;++matrix) {
        double duration=(double[]){.005,.1,4}[length];TsMosaic *m=ts_mosaic_create();assert(m);char error[160];
        TsMosaicSource *source=ts_mosaic_source(m,&sample,error,sizeof(error));assert(source);
        for(int i=0;i<1024;++i) {
            TsMosaicEvent *e=ts_mosaic_add(m,source,100+i,0);assert(e);
            if(i<12) {
                e->live=1;e->duration=duration;e->live_order=i<8?0:-1;e->pan=(i%3-1)*.4f;
                if(matrix && i%3==0){e->output_route.matrix_enabled=1;e->output_route.matrix_gain[7]=750;}
                if(i>=8)assert(ts_mosaic_live_launch(m,e->id,0));
            }
        }
        assert(ts_mosaic_sequence_cycle(m));double times[3],checksum=0;
        for(int trial=0;trial<3;++trial) {
            clock_t start=clock();
            for(int block=0;block<512;++block) {
                ts_mosaic_begin_block(m,256);
                for(int frame=0;frame<256;++frame) {
                    TsSourceRouteMix routes={0};TsStereoFrame f=ts_mosaic_read_routed(m,48000,matrix?&routes:NULL);checksum+=f.l+f.r;
                }
            }
            times[trial]=1000.*(clock()-start)/CLOCKS_PER_SEC/512;
        }
        for(int i=0;i<3;++i)for(int j=i+1;j<3;++j)if(times[j]<times[i]){double swap=times[i];times[i]=times[j];times[j]=swap;}
        assert(isfinite(checksum) && m->sequence.running);printf("%.3f,8,4,1024,%d,%.6f\n",duration,matrix,times[1]);ts_mosaic_free(m);
    }
}
