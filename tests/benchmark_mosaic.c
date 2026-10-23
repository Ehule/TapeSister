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
#include <string.h>
#include <time.h>

/* Read-only external-project benchmark. Scatter is deterministic and changes
   only this in-memory copy; no files in the supplied project are written. */
static int benchmark_project(const char *directory,const char *capture_path)
{
    TsMosaic *m=ts_mosaic_create();assert(m);char error[256];
    if(!ts_mosaic_load(m,directory,error,sizeof(error))) {
        fprintf(stderr,"%s\n",error);ts_mosaic_free(m);return 1;
    }
    FILE *capture=capture_path?fopen(capture_path,"wb"):NULL;if(capture_path)assert(capture);
    int cards=0;for(int i=0;i<TS_MOSAIC_EVENTS;++i)cards+=m->events[i].id!=0;
    for(int scatter=0;scatter<2;++scatter) {
        if(scatter)for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id)m->events[i].start=(i*613%1024)*.025;
        int blocks=scatter?192:48;double best=1e9,checksum=0,active=0;
        for(int trial=0;trial<3;++trial) {
            ts_mosaic_seek(m,scatter?10:0);m->playing=1;m->was_playing=0;
            m->last_output=(TsStereoFrame){0};m->transition_remaining=0;
            m->route_handoff=(TsSourceRouteHandoff){0};checksum=0;active=0;
            clock_t start=clock();
            for(int block=0;block<blocks;++block) {
                ts_mosaic_begin_block(m,512);
                for(int frame=0;frame<512;++frame) {
                    TsSourceRouteMix routes={0};TsStereoFrame out=ts_mosaic_read_routed(m,48000,&routes);
                    checksum+=out.l+out.r;
                    if(capture && !trial){assert(fwrite(&out,sizeof(out),1,capture)==1);assert(fwrite(&routes,sizeof(routes),1,capture)==1);}
                }
                active+=m->schedule.active_count;
            }
            double elapsed=(double)(clock()-start)/CLOCKS_PER_SEC;if(elapsed<best)best=elapsed;
        }
        printf("project %s: %d cards, %.1f mean active, %.3f ms/512 frames, checksum %.17g\n",
            scatter?"deterministic scatter":"saved layout",cards,active/blocks,best*1000/blocks,checksum);
    }
    if(capture)assert(!fclose(capture));ts_mosaic_free(m);return 0;
}

int main(int argc,char **argv)
{
    if(argc>2 && !strcmp(argv[1],"--project"))return benchmark_project(argv[2],argc>3?argv[3]:NULL);
    const int totals[]={1,128,128,128,1024,1024,1024,128,1024,1024};
    const int active[]={1,1,16,128,1,16,1024,128,160,160};
    int routed=argc>1 && !strcmp(argv[1],"--routed"),path=routed?2:1;
    FILE *capture=argc>path?fopen(argv[path],"wb"):NULL;if(argc>path)assert(capture);
    printf("capacity=%d model=%zu event=%zu voice=%zu bytes\n",TS_MOSAIC_EVENTS,sizeof(TsMosaic),sizeof(TsMosaicEvent),sizeof(TsMosaicVoice));
    TsSample sample={0};sample.frames=4096;sample.sample_rate=48000;sample.channels=2;
    sample.data=malloc(sample.frames*2*sizeof(float));assert(sample.data);
    for(size_t i=0;i<sample.frames;++i){sample.data[2*i]=.2f*sinf(i*.019f);sample.data[2*i+1]=.15f*cosf(i*.031f);}
    for(int scenario=0;scenario<10;++scenario) {
        int total=totals[scenario];if(total>TS_MOSAIC_EVENTS)continue;
        TsMosaic *m=ts_mosaic_create();assert(m);char error[160];
        TsMosaicSource *source=ts_mosaic_source(m,&sample,error,sizeof(error));assert(source);
        for(int i=0;i<total;++i) {
            TsMosaicEvent *e=ts_mosaic_add(m,source,i<active[scenario]?0:10+i,0);assert(e);
            e->duration=4;e->gain=.5f;e->pan=(i%3-1)*.4f;e->mode=(TsLoopMode)(i%TS_LOOP_MODE_COUNT);
            e->first=100;e->crossfade=32;e->notes[0]=48+i%25;
            if(scenario==9)e->start=(i*613%1024)*.025; /* Staggered starts/ends, about 160 crossing cards. */
            if(scenario==7) {
                e->note_count=5;for(int n=1;n<5;++n)e->notes[n]=e->notes[0]+n*3;
                e->fade_in=.01;e->fade_out=5; /* Proportional overlapping fades. */
                if(i%3==0){e->output_route.matrix_enabled=1;e->output_route.matrix_gain[7]=750;}
            }
        }
        double best=1e9;float checksum=0;
        for(int trial=0;trial<3;++trial) {
            ts_mosaic_seek(m,scenario==9?10:0);m->playing=1;m->was_playing=0;m->last_output=(TsStereoFrame){0};m->transition_remaining=0;
            clock_t start=clock();
            for(int block=0;block<48;++block) {
                if(scenario==7) {
                    TsMosaicEvent *e=&m->events[0];
                    e->gain=block%2?.2f:.8f;e->pan=block%3==0?-.7f:.4f;
                    e->muted=block%4==0;e->fade_in=block%2?.02:0;e->fade_out=block%2?5:0;
                }
                ts_mosaic_begin_block(m,512);
                for(int frame=0;frame<512;++frame) {
                    TsSourceRouteMix routes={0};
                    TsStereoFrame out=ts_mosaic_read_routed(m,48000,routed || scenario==7?&routes:NULL);checksum+=out.l+out.r;
                    if(capture && !trial)assert(fwrite(&out,sizeof(out),1,capture)==1);
                    if(capture && !trial && (routed || scenario==7))assert(fwrite(&routes,sizeof(routes),1,capture)==1);
                }
            }
            double elapsed=(double)(clock()-start)/CLOCKS_PER_SEC;if(elapsed<best)best=elapsed;
        }
        printf("%4d cards / %4d active%s: %.3f ms/512 frames, %.2f%% of one CPU core, checksum %.7g\n",
            total,active[scenario],scenario==7?" / chords, routes, edits":scenario==9?" / staggered":routed?" / Matrix enabled":"",best*1000/48,best/(48*512.0/48000)*100,checksum);
        ts_mosaic_free(m);
    }
    if(capture)assert(!fclose(capture));ts_sample_free(&sample);return 0;
}
