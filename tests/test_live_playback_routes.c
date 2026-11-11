#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/ts_mosaic.c"
#include "tapesister/mosaic_gallery.h"
#include <assert.h>
#include <time.h>
#include "reference/live_routes_207.inc"

typedef TsStereoFrame (*Read)(TsMosaicGallery *,int,TsSourceRouteMix *);
static void same(const void *a,const void *b,size_t n,const char *what,unsigned at)
{
    if(memcmp(a,b,n)){fprintf(stderr,"%s differs at frame %u\n",what,at);abort();}
}
static TsMosaicGallery *fixture(unsigned rate,int cards)
{
    char error[256];TsMosaicGallery *g=ts_gallery_create(ts_mosaic_create());assert(g && g->root);
    float data[2048];for(unsigned i=0;i<2048;++i)data[i]=((int)(i*37%257)-128)*.0007f;
    data[0]=-0.f;data[101]=0;
    TsSample sample={.data=data,.frames=1024,.channels=2,.sample_rate=rate};
    for(int scene=0;scene<3;++scene) {
        if(scene)assert(ts_gallery_add(g,error,sizeof(error)));
        TsMosaic *m=g->canvas[scene].scene;
        TsMosaicSource *source=ts_mosaic_source(m,&sample,error,sizeof(error));assert(source);
        for(int i=0;i<cards;++i) {
            TsMosaicEvent *e=ts_mosaic_add(m,source,i<12?0:4+i,0);assert(e);
            e->duration=20;e->looping=1;e->crossfade=16;e->first=32;
            e->mode=(TsLoopMode)(i%TS_LOOP_MODE_COUNT);e->notes[0]=48+i%25;
            e->fade_in=.01;e->fade_out=.1;e->gain=.25f;e->pan=(i%5-2)*.1f;
            if(i%4==0){e->output_route.matrix_enabled=1;e->output_route.matrix_gain[7+scene]=650;
                e->output_route.matrix_gain[64+scene]=150;e->output_route.matrix_gain[128+scene]=200;
                e->output_route.matrix_gain[TS_MATRIX_DESTINATIONS-1]=100;}
            if(i%4==1)e->output_route=(TsSourceRoute){.mode=TS_SOURCE_PAIR,.speaker=2,.second=5,
                .mix_enabled=1,.clean_level=70,.send_level={20,30,40},.pan=-30,.width=20};
            if(i==2){e->live=1;assert(ts_mosaic_live_launch(m,e->id,0));}
        }
        g->canvas[scene].entry=TS_GALLERY_RESUME;m->playing=1;
    }
    g->fade_seconds=.08;return g;
}
static void destroy(TsMosaicGallery *g)
{TsMosaic *root=g->root;ts_gallery_free(g);ts_mosaic_free(root);}
static void controls(TsMosaicGallery *g,unsigned block)
{
    char error[256];TsMosaic *m=ts_gallery_active(g);
    if(block==3 || block==17 || block==31)assert(ts_gallery_switch(g,(g->active+1)%3,error,sizeof(error)));
    m=ts_gallery_active(g);
    if(block==5){m->events[0].output_route.matrix_gain[7]=0;m->events[0].output_route.matrix_gain[185]=1000;}
    if(block==7)m->events[1].muted=1;
    if(block==8)m->events[1].muted=0;
    if(block==9)m->events[3].solo=1;
    if(block==10)m->events[3].solo=0;
    if(block==11)ts_mosaic_seek(m,.027);
    if(block==12)ts_mosaic_audition(m,m->events[0].id);
    if(block==14)ts_mosaic_audition(m,0);
    if(block==19){m->events[0].output_route=(TsSourceRoute){0};m->events[1].output_route=(TsSourceRoute){0};}
    if(block==20)ts_mosaic_live_transport(m,1);
    if(block==21)ts_mosaic_live_transport(m,0);
    if(block==22)m->playing=0;
    if(block==23)m->playing=1;
    if(block==25){assert(ts_mosaic_set_loop_range(m,.02,.05));ts_mosaic_set_repeat(m,1);}
    if(block==28)ts_mosaic_set_repeat(m,0);
    if(block==34)ts_mosaic_set_speed(m,.75);
}
static void check(unsigned rate,unsigned block_frames,int routed)
{
    TsMosaicGallery *a=fixture(rate,43),*b=fixture(rate,43);unsigned frame=0;
    for(unsigned block=0;block<45;++block) {
        controls(a,block);controls(b,block);
        unsigned count=block_frames?block_frames:512; /* Also exercise standalone reads. */
        if(block_frames){ts_gallery_begin_block(a,count);ts_gallery_begin_block(b,count);}
        int output_rate=block>=36?(int)rate*2:(int)rate;
        for(unsigned j=0;j<count;++j,++frame) {
            TsSourceRouteMix x={0},y={0};x.check_outputs=y.check_outputs=1;x.available=y.available=3;
            x.graph[7].l=y.graph[7].l=.123f;x.graph_mask=y.graph_mask=UINT64_C(1)<<7;
            x.reference.r=y.reference.r=-.25f;
            TsStereoFrame p=ts_gallery_read(a,output_rate,routed?&x:NULL);
            TsStereoFrame q=reference_gallery_read(b,output_rate,routed?&y:NULL);
            same(&p,&q,sizeof(p),"Mosaic PCM",frame);same(&x,&y,sizeof(x),"all route buses/taps",frame);
            assert(a->active==b->active && a->outgoing==b->outgoing && a->fade_position==b->fade_position);
            for(int k=0;k<3;++k) {
                TsMosaic *am=a->canvas[k].scene,*bm=b->canvas[k].scene;
                assert(am->time==bm->time && am->playing==bm->playing && am->live[2].elapsed==bm->live[2].elapsed);
                same(&am->route_handoff,&bm->route_handoff,sizeof(am->route_handoff),"route handoff",frame);
            }
        }
    }
    destroy(a);destroy(b);
}
static void reset_check(void)
{
    TsSourceRouteMix x={0},zero={0};
    for(int d=0;d<TS_MATRIX_DESTINATIONS;++d) {
        x.graph[d]=(TsStereoFrame){.5f,-.7f};
        if(d<64)x.graph_mask|=UINT64_C(1)<<d;
        else if(d<128)x.graph_mask_high|=UINT64_C(1)<<(d-64);
        else x.graph_mask_top|=UINT64_C(1)<<(d-128);
        x.monitor.l=1;x.available=7;x.check_outputs=1;x.send[2].r=1;x.speaker[15]=1;
        ts_source_route_mix_reset(&x);same(&x,&zero,sizeof(x),"reset including upper destinations",d);
    }
}
static volatile double sink;
static double timed(Read read,int fade)
{
    TsMosaicGallery *g=fixture(48000,53);char error[256];
    if(fade){g->fade_seconds=8;assert(ts_gallery_switch(g,1,error,sizeof(error)));}
    const int blocks=96;double sum=0;clock_t start=clock();
    for(int block=0;block<blocks;++block) {
        ts_gallery_begin_block(g,512);
        for(int i=0;i<512;++i){TsSourceRouteMix routes={0};TsStereoFrame out=read(g,48000,&routes);sum+=out.l+routes.graph[7].l;}
    }
    double ms=(double)(clock()-start)*1000/CLOCKS_PER_SEC/blocks;sink=sum;destroy(g);return ms;
}
static double median(double *v)
{for(int i=1;i<5;++i)for(int j=i;j>0 && v[j]<v[j-1];--j){double t=v[j];v[j]=v[j-1];v[j-1]=t;}return v[2];}
int main(void)
{
    reset_check();
    check(8000,512,1);check(48000,512,1);check(44100,257,1);check(8000,0,1);check(8000,512,0);
    puts("PR #207 oracle: exact PCM, routes, dry taps, handoffs and clocks; edits, seeks, live loops, sample-rate change and Gallery fades.");
    for(int fade=0;fade<2;++fade) {
        double before[5],after[5];
        for(int i=0;i<5;++i) {
            if(i%2){after[i]=timed(ts_gallery_read,fade);before[i]=timed(reference_gallery_read,fade);}
            else {before[i]=timed(reference_gallery_read,fade);after[i]=timed(ts_gallery_read,fade);}
        }
        double old=median(before),now=median(after);
        printf("Mosaic %s, 48k/512, 53 cards/12 active per scene, median of 5 alternating trials: old %.6f ms/block, new %.6f ms/block, %.2f%% reduction (not full callback/ASIO)\n",
            fade?"two-scene fade":"steady",old,now,old?100*(old-now)/old:0);
    }
    return 0;
}
