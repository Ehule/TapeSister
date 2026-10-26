#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/mosaic.h"
#include "tapesister/routing_matrix.h"
#include "tapesister/sample_pages.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#define RMDIR(p) _rmdir(p)
#else
#include <unistd.h>
#define MKDIR(p) mkdir(p,0700)
#define RMDIR(p) rmdir(p)
#endif

static TsMosaic *fixture(void)
{
    TsMosaic *m=ts_mosaic_create();assert(m);m->auto_space=1;
    TsSample s={0};char error[160];s.frames=4096;s.sample_rate=48000;s.channels=2;
    s.data=calloc(s.frames*2,sizeof(float));assert(s.data);strcpy(s.name,"PHASE BLOOM");
    for(size_t i=0;i<s.frames;++i){s.data[i*2]=.4f*sinf((float)i*.019f);s.data[i*2+1]=.15f*cosf((float)i*.031f);}
    TsMosaicSource *source=ts_mosaic_source(m,&s,error,sizeof(error));assert(source);
    assert(ts_mosaic_source(m,&s,error,sizeof(error))==source);ts_sample_free(&s);
    TsMosaicEvent *e=ts_mosaic_add(m,source,0,0);assert(e);e->duration=5;e->note_count=5;
    int notes[]={48,60,64,67,72};memcpy(e->notes,notes,sizeof(notes));m->playing=1;return m;
}
static void storage_line_endings(const char *path,int crlf)
{
    FILE *f=fopen(path,"rb");assert(f);
    assert(!fseek(f,0,SEEK_END));long length=ftell(f);assert(length>=0);rewind(f);
    char *data=malloc((size_t)length+1);assert(data);
    assert(fread(data,1,(size_t)length,f)==(size_t)length);assert(!fclose(f));
    f=fopen(path,"wb");assert(f);
    for(long i=0;i<length;++i) {
        if(data[i]=='\r' && i+1<length && data[i+1]=='\n')continue;
        if(data[i]=='\n' && crlf)assert(fputc('\r',f)!=EOF);
        assert(fputc(data[i],f)!=EOF);
    }
    assert(!fclose(f));free(data);
}
static void storage_portable_load(TsMosaic *loaded,const char *dir)
{
    char path[1024],error[160];snprintf(path,sizeof(path),"%s/mosaic.tsm",dir);
    assert(ts_mosaic_load(loaded,dir,error,sizeof(error)));
    uint64_t hash=ts_mosaic_hash(loaded);
    for(int crlf=1;crlf>=0;--crlf) {
        storage_line_endings(path,crlf);
        assert(ts_mosaic_load(loaded,dir,error,sizeof(error)));
        assert(ts_mosaic_hash(loaded)==hash);
    }
}
static void test_ownership(void)
{
    TsMosaic *m=fixture();TsMosaicEvent *a=&m->events[0];
    TsMosaicEvent *b=ts_mosaic_copy(m,a->id,.25,0);assert(b && b->source==a->source && b->id!=a->id);
    uint64_t id=b->id;
    assert(b->x>=a->x+a->width+TS_MOSAIC_GUTTER);
    for(int i=0;i<24000;++i)ts_mosaic_read(m,48000);
    assert(m->voices[0].active && m->voices[1].active);
    assert(m->voices[0].position[1]!=m->voices[1].position[1]);
    double position=m->voices[1].position[1];
    ts_mosaic_checkpoint(m);ts_mosaic_delete(m,a->id);ts_mosaic_read(m,48000);
    assert(m->voices[1].active && m->voices[1].position[1]>position);
    assert(ts_mosaic_undo(m,0));assert(m->events[0].id && ts_mosaic_find(m,id));
    assert(ts_mosaic_undo(m,1));assert(!m->events[0].id && ts_mosaic_find(m,id));
    ts_mosaic_free(m);
}
static void test_seek(void)
{
    for(int rate=44100;rate<=48000;rate+=3900)for(int mode=0;mode<TS_LOOP_MODE_COUNT;++mode)for(int fade=0;fade<=32;fade+=32) {
        TsMosaic *a=fixture(),*b=fixture();
        a->events[0].mode=b->events[0].mode=(TsLoopMode)mode;
        a->events[0].first=b->events[0].first=100;
        a->events[0].crossfade=b->events[0].crossfade=(size_t)fade;
        int frames=rate*2+137;
        for(int i=0;i<frames;++i)ts_mosaic_read(a,rate);
        ts_mosaic_seek(b,(double)frames/rate);
        /* The resumed envelope is intentional; compare phase after it settles. */
        for(int i=0;i<120;++i){ts_mosaic_read(a,rate);ts_mosaic_read(b,rate);}
        for(int n=0;n<5;++n) {
            double difference=fabs(a->voices[0].position[n]-b->voices[0].position[n]);
            if(difference>1e-5)fprintf(stderr,"seek rate=%d mode=%d fade=%d note=%d difference=%g\n",rate,mode,fade,n,difference);
            assert(difference<1e-5);
        }
        TsStereoFrame x=ts_mosaic_read(a,rate),y=ts_mosaic_read(b,rate);
        assert(fabsf(x.l-y.l)<1e-5f && fabsf(x.r-y.r)<1e-5f);
        ts_mosaic_free(a);ts_mosaic_free(b);
    }
}
static void test_one_shot_and_resize(void)
{
    TsMosaic *m=fixture();TsMosaicEvent *e=&m->events[0];e->looping=0;
    for(int i=0;i<18000;++i)ts_mosaic_read(m,48000);
    TsStereoFrame f=ts_mosaic_read(m,48000);assert(f.l==0 && f.r==0);
    e->looping=1;++e->revision;ts_mosaic_seek(m,0);
    for(int i=0;i<1000;++i)ts_mosaic_read(m,48000);
    double position=m->voices[0].position[1];e->duration=8;
    ts_mosaic_read(m,48000);assert(fabs(m->voices[0].position[1]-position-1)<1e-9);
    assert(fabs(m->voices[0].step[4]-2)<1e-9);
    assert(fabs(m->voices[0].step[0]-.5)<1e-9);
    ts_mosaic_free(m);
}
static void test_spacing_and_history(void)
{
    TsMosaic *m=fixture();TsMosaicEvent *a=&m->events[0];
    TsMosaicEvent *b=ts_mosaic_copy(m,a->id,a->duration,0);assert(b && b->x==0);
    b->start-=.01;ts_mosaic_space(m,b);assert(b->x>=a->width+8);
    assert(ts_mosaic_snap(m,b->id,2.51,.03)==2.5);
    ts_mosaic_checkpoint(m);double old=b->start;b->start=12;
    ts_mosaic_checkpoint(m);b->duration=3;
    assert(ts_mosaic_undo(m,0));assert(b->duration==5);
    assert(ts_mosaic_undo(m,0));assert(b->start==old);
    assert(ts_mosaic_undo(m,1));assert(b->start==12 && b->duration==5);
    assert(ts_mosaic_undo(m,1));assert(b->duration==3);
    ts_mosaic_free(m);
}
static void test_mute_solo_phase(void)
{
    TsMosaic *m=fixture(),*reference=fixture();
    TsMosaicEvent *a=&m->events[0],*b=ts_mosaic_copy(m,a->id,0,110);
    ts_mosaic_copy(reference,reference->events[0].id,0,110);
    for(int i=0;i<1000;++i){ts_mosaic_read(m,48000);ts_mosaic_read(reference,48000);}
    uint64_t revision=a->revision,hash=ts_mosaic_hash(m);
    ts_mosaic_checkpoint(m);a->muted=1;b->solo=1;
    assert(ts_mosaic_hash(m)!=hash);
    float previous=m->voices[0].audible_gain;
    for(int i=0;i<500;++i) {
        ts_mosaic_read(m,48000);ts_mosaic_read(reference,48000);
        assert(m->voices[0].audible_gain<=previous);previous=m->voices[0].audible_gain;
        assert(a->revision==revision);
        for(int n=0;n<5;++n)assert(m->voices[0].travel[n]==reference->voices[0].travel[n]);
    }
    assert(previous==0 && m->voices[1].audible_gain==1);
    a->muted=0; /* Still excluded by B's solo. */
    ts_mosaic_read(m,48000);ts_mosaic_read(reference,48000);assert(m->voices[0].audible_gain==0);
    b->solo=0;
    for(int i=0;i<500;++i){ts_mosaic_read(m,48000);ts_mosaic_read(reference,48000);}
    assert(m->voices[0].audible_gain==1);
    assert(fabsf(m->last_output.l-reference->last_output.l)<1e-6f);
    assert(ts_mosaic_undo(m,0));assert(!m->events[0].muted && !m->events[1].solo);
    ts_mosaic_free(m);ts_mosaic_free(reference);
}

#include "test_mosaic_performance.inc"

static void test_card_routes(void)
{
    TsMosaic *m=fixture(),*plain=fixture();
    m->events[0].output_route.matrix_enabled=1;m->events[0].output_route.matrix_gain[3]=1000;
    for(int i=0;i<500;++i) {
        TsSourceRouteMix mix={0};TsStereoFrame bus=ts_mosaic_read_routed(m,48000,&mix),expected=ts_mosaic_read(plain,48000);
        assert(bus.l==0 && bus.r==0);
        assert(fabsf(mix.graph[3].l-expected.l)<1e-6f && fabsf(mix.graph[3].r-expected.r)<1e-6f);
    }
    m->events[0].output_route=(TsSourceRoute){0};
    for(int i=0;i<300;++i) {
        TsSourceRouteMix mix={0};TsStereoFrame bus=ts_mosaic_read_routed(m,48000,&mix),expected=ts_mosaic_read(plain,48000);
        assert(fabsf(bus.l+mix.graph[3].l-expected.l)<1e-5f);
        if(i==299)assert(mix.graph[3].l==0 && fabsf(bus.l-expected.l)<1e-6f);
    }
    m->events[0].output_route.matrix_enabled=1;m->events[0].output_route.matrix_gain[3]=1000;
    for(int i=0;i<300;++i){TsSourceRouteMix mix={0};ts_mosaic_read_routed(m,48000,&mix);}
    m->playing=0;
    for(int i=0;i<100;++i){TsSourceRouteMix mix={0};ts_mosaic_read_routed(m,48000,&mix);if(i==99)assert(mix.graph[3].l==0 && mix.graph[3].r==0);}
    ts_mosaic_free(m);ts_mosaic_free(plain);
}
static void test_storage(void)
{
    const char *dir="mosaic-storage-test";MKDIR(dir);
    TsMosaic *m=fixture(),*loaded=ts_mosaic_create();char error[160];m->auto_space=0;
    ts_mosaic_copy(m,m->events[0].id,6,0);
    m->events[0].muted=1;m->events[1].solo=1;
    m->events[0].pan=-.35f;m->events[0].fade_in=1.25;m->events[0].fade_out=2.5;ts_mosaic_set_speed(m,1.5);
    ts_mosaic_volume_draw(m,0,.2f,1,.8f);
    m->events[0].output_route.matrix_enabled=1;
    m->events[0].output_route.matrix_gain[7]=500;
    m->events[0].output_route.matrix_gain[TS_MATRIX_COMPRESSOR]=1000;
    m->events[0].output_route.matrix_gain[TS_MATRIX_SIDECHAIN]=600;
    m->events[0].output_route.matrix_gain[TS_MATRIX_INSTANCE_DESTINATIONS-1]=777;
    m->events[0].output_route.matrix_gain[TS_MATRIX_AMBISONICS]=1000;
    m->events[0].output_route.matrix_gain[TS_MATRIX_DESTINATIONS-1]=1234;
    assert(ts_mosaic_set_loop_range(m,1.23456789,4.56789123));
    m->views[0]=(TsMosaicView){1,725.25,90,.6,1.5};
    m->views[4]=(TsMosaicView){1,0,0,.125,.5};
    assert(ts_mosaic_save(m,dir,error,sizeof(error)));
    FILE *saved=fopen("mosaic-storage-test/mosaic.tsm","rb");assert(saved);
    int byte;while((byte=fgetc(saved))!=EOF)assert(byte!='\r');
    assert(!ferror(saved));assert(!fclose(saved));
    assert(ts_mosaic_load(loaded,dir,error,sizeof(error)));
    assert(ts_mosaic_hash(m)==ts_mosaic_hash(loaded));
    assert(loaded->views[0].scroll==725.25 && loaded->views[4].scale==.125 && !loaded->views[1].stored);
    assert(loaded->loop_range.start==m->loop_range.start && loaded->loop_range.end==m->loop_range.end);
    storage_portable_load(loaded,dir);
    assert(loaded->events[0].muted && loaded->events[1].solo);
    assert(loaded->events[0].source==loaded->events[1].source);
    assert(!memcmp(m->events[0].source->sample.data,loaded->events[0].source->sample.data,4096*2*sizeof(float)));
    /* Invalid view data must fail transactionally, preserving the live project. */
    const char *bad_views[]={"VIEW 1 0 0 0 1\n","VIEW 1 nan 0 1 1\n","VIEW 1 0 0 1 inf\n",
        "LOOP nan 3\n","LOOP 2 1\n","LOOP -1 2\n","LOOP 1 1\n","LOOP 0 1000001\n","LOOP 0 inf\n"};
    for(int i=0;i<9;++i) {
        FILE *current=fopen("mosaic-storage-test/mosaic.tsm","r"),*bad=fopen("mosaic-storage-test/bad.tsm","w");assert(current && bad);
        char record[1024];int replaced=0;
        while(fgets(record,sizeof(record),current)) {
            if(!replaced && !strncmp(record,i<3?"VIEW ":"LOOP ",5)){fputs(bad_views[i],bad);replaced=1;}
            else fputs(record,bad);
        }
        fclose(current);fclose(bad);assert(replaced);remove("mosaic-storage-test/mosaic.tsm");
        assert(!rename("mosaic-storage-test/bad.tsm","mosaic-storage-test/mosaic.tsm"));
        uint64_t before=ts_mosaic_hash(loaded);assert(!ts_mosaic_load(loaded,dir,error,sizeof(error)));
        assert(ts_mosaic_hash(loaded)==before);assert(ts_mosaic_save(m,dir,error,sizeof(error)));
    }
    /* Older route widths retain their destinations and zero the added buses. */
    for(int version=6;version<=11;++version) {
        int destinations=version>=9?TS_MATRIX_DESTINATIONS:version==8?TS_MATRIX_INSTANCE_DESTINATIONS:version==7?TS_MATRIX_SHARED_DESTINATIONS:TS_MATRIX_LEGACY_DESTINATIONS;
        FILE *current=fopen("mosaic-storage-test/mosaic.tsm","r"),*old=fopen("mosaic-storage-test/old.tsm","w");assert(current && old);
        char record[1024];assert(fgets(record,sizeof(record),current));fprintf(old,"TAPESISTER_MOSAIC %d\n",version);
        while(fgets(record,sizeof(record),current)) {
            if(!strncmp(record,"MARKER ",7) || !strncmp(record,"PATTERN ",8) || !strncmp(record,"LOOP ",5) || (version<11 && !strncmp(record,"VIEW ",5)) || (version<10 && !strncmp(record,"SPACING ",8)))continue;
            if(!strncmp(record,"ROUTE ",6)) {
                record[strcspn(record,"\r\n")]=0;
                for(int i=destinations;i<TS_MATRIX_DESTINATIONS;++i){char *last=strrchr(record,' ');assert(last);*last=0;}
                fprintf(old,"%s\n",record);
            } else fputs(record,old);
        }
        fclose(current);fclose(old);remove("mosaic-storage-test/mosaic.tsm");
        assert(!rename("mosaic-storage-test/old.tsm","mosaic-storage-test/mosaic.tsm"));
        assert(ts_mosaic_load(loaded,dir,error,sizeof(error)));
        assert(loaded->loop_range.start==0 && loaded->loop_range.end==0);
        for(int i=0;i<TS_MOSAIC_VIEWS;++i)assert(loaded->views[i].stored==(version>=11?m->views[i].stored:0));
        assert(loaded->auto_space==(version<10));
        storage_portable_load(loaded,dir);
        for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)
            assert(loaded->events[0].output_route.matrix_gain[d]==(d<destinations?m->events[0].output_route.matrix_gain[d]:0));
        assert(ts_mosaic_save(m,dir,error,sizeof(error)));
    }
    /* Both previous formats load with neutral new controls, preserving the
       second format's mute/solo state. */
    FILE *old=fopen("mosaic-storage-test/mosaic.tsm","r");assert(old);
    char lines[4][1024];
    for(int i=0;i<2;++i)assert(fgets(lines[i],sizeof(lines[i]),old));
    assert(fgets(lines[2],sizeof(lines[2]),old) && !strncmp(lines[2],"SPACING ",8));
    for(int i=0;i<TS_MOSAIC_VIEWS;++i)assert(fgets(lines[2],sizeof(lines[2]),old) && !strncmp(lines[2],"VIEW ",5));
    assert(fgets(lines[2],sizeof(lines[2]),old) && !strncmp(lines[2],"LOOP ",5));
    for(int i=0;i<TS_MOSAIC_MARKERS;++i)assert(fgets(lines[2],sizeof(lines[2]),old) && !strncmp(lines[2],"MARKER ",7));
    for(int i=0;i<=TS_MOSAIC_ENVELOPE_POINTS;++i)assert(fgets(lines[2],sizeof(lines[2]),old));
    for(int i=2;i<4;++i){assert(fgets(lines[i],sizeof(lines[i]),old) && !strncmp(lines[i],"PATTERN ",8));assert(fgets(lines[i],sizeof(lines[i]),old));char route[1024];assert(fgets(route,sizeof(route),old));assert(fgets(route,sizeof(route),old));}fclose(old);
    for(int version=3;version>=1;--version) {
        old=fopen("mosaic-storage-test/mosaic.tsm","w");assert(old);
        fprintf(old,"TAPESISTER_MOSAIC %d\nSETTINGS 0 0.7%s\n",version,version==3?" 1.5":"");
        for(int i=2;i<4;++i) {
            char line[1024];snprintf(line,sizeof(line),"%s",lines[i]);line[strcspn(line,"\r\n")]=0;
            for(int k=0;k<(version==3?0:version==2?3:5);++k){char *end=strrchr(line,' ');assert(end);*end=0;}
            fprintf(old,"%s\n",line);
        }
        fclose(old);assert(ts_mosaic_load(loaded,dir,error,sizeof(error)));
        storage_portable_load(loaded,dir);
        for(int i=0;i<TS_MOSAIC_ENVELOPE_POINTS;++i)assert(loaded->volume[i]==1);
        if(version==3)assert(loaded->speed==1.5 && loaded->events[0].pan==-.35f);
        else assert(loaded->speed==1 && loaded->speed_current==1 && loaded->events[0].pan==0 && loaded->events[0].fade_in==0 && loaded->events[0].fade_out==0);
        assert(loaded->events[0].muted==(version>=2) && loaded->events[1].solo==(version>=2));
    }
    uint64_t before=ts_mosaic_hash(loaded);
    FILE *f=fopen("mosaic-storage-test/mosaic.tsm","w");assert(f);fputs("TAPESISTER_MOSAIC 1\ncorrupt\n",f);fclose(f);
    assert(!ts_mosaic_load(loaded,dir,error,sizeof(error)));assert(ts_mosaic_hash(loaded)==before);
    f=fopen("mosaic-storage-test/mosaic.tsm","w");assert(f);
    fputs("TAPESISTER_MOSAIC 4\nSETTINGS 0 0.7 1\nVOLUME 257\nnan\n",f);fclose(f);
    assert(!ts_mosaic_load(loaded,dir,error,sizeof(error)) && ts_mosaic_hash(loaded)==before);
    const char *bad_headers[]={"TAPESISTER_MOSAIC 1\rjunk\n","TAPESISTER_MOSAIC 10\r\n"};
    for(size_t i=0;i<sizeof(bad_headers)/sizeof(bad_headers[0]);++i) {
        f=fopen("mosaic-storage-test/mosaic.tsm","wb");assert(f);
        fputs(bad_headers[i],f);fputs("SETTINGS 0 0.7\r\n",f);assert(!fclose(f));
        assert(!ts_mosaic_load(loaded,dir,error,sizeof(error)) && ts_mosaic_hash(loaded)==before);
    }
    remove("mosaic-storage-test/mosaic.tsm");remove("mosaic-storage-test/mosaic-000.wav");RMDIR(dir);
    ts_mosaic_free(m);ts_mosaic_free(loaded);
}
static void test_project_transaction(void)
{
    TsMosaic *m=fixture();TsSamplePages pages;char error[160],path[1024];
    TsInstrument *bank=malloc(sizeof(*bank)),*record=malloc(sizeof(*record));assert(bank && record);
    ts_instrument_init(bank);ts_instrument_init(record);assert(ts_sample_pages_init(&pages,error,sizeof(error)));
    pages.mosaic=m;
    assert(ts_instrument_import_sample(bank,&m->events[0].source->sample,0,0,0,TS_LOOP_FORWARD,error,sizeof(error)));
    assert(ts_sample_pages_bundle_project_path("mosaic-project-test.tsr",path,sizeof(path),error,sizeof(error)));
    assert(ts_sample_pages_save_project(&pages,bank,record,NULL,path,error,sizeof(error)));
    uint64_t hash=ts_mosaic_hash(m),sample_hash=ts_sample_hash(&bank->current),epoch=m->epoch;
    for(int i=0;i<TS_MOSAIC_HISTORY;++i)ts_mosaic_checkpoint(m);
    ts_mosaic_audition(m,m->events[0].id);ts_mosaic_begin_block(m,32);
    for(int i=0;i<32;++i)ts_mosaic_read(m,48000);
    assert(m->voices[0].active && m->audition_id);
    m->events[0].start=100;
    /* A Windows-saved bundle must open intact on Linux, including its audio. */
    storage_line_endings("mosaic-project-test/project-data/mosaic.tsm",1);
    TsSampleProjectLoad *prepared=ts_sample_pages_prepare_project(path,1,error,sizeof(error));assert(prepared);
    uint64_t edited=ts_mosaic_hash(m);TsMosaicSource *old_source=m->sources;
    const float *old_data=bank->current.data;
    assert(m->epoch==epoch && m->events[0].start==100 && m->playing);
    assert(ts_sample_pages_prepared_pages(prepared)->page_count==1);
    /* Cancelling a fully decoded project must not touch the running one. */
    ts_sample_pages_dispose_project(prepared);
    assert(ts_mosaic_hash(m)==edited && m->sources==old_source && bank->current.data==old_data);
    prepared=ts_sample_pages_prepare_project(path,1,error,sizeof(error));assert(prepared);
    /* Publication does not need the files any more, and retirement happens
       only after the caller releases its audio lock. */
    assert(rename("mosaic-project-test","mosaic-project-staged")==0);
    assert(ts_sample_pages_publish_project(&pages,bank,record,prepared,error,sizeof(error)));
    assert(!ts_sample_pages_prepared_pages(prepared));
    assert(old_source->sample.data && old_data[0]==old_source->sample.data[0]);
    assert(!ts_sample_pages_publish_project(&pages,bank,record,prepared,error,sizeof(error)));
    ts_sample_pages_dispose_project(prepared);
    assert(rename("mosaic-project-staged","mosaic-project-test")==0);
    assert(pages.mosaic==m && ts_mosaic_hash(m)==hash && m->epoch!=epoch);
    assert(ts_sample_hash(&bank->current)==sample_hash);
    assert(!m->history_count && !m->history_cursor && !m->history_first && !m->playing && !m->audition_id);
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)assert(!m->voices[i].active);
    /* Retired history/voice backing memory must never retain old ownership or
       affect the first render after replacement. */
    ts_mosaic_collect(m);assert(m->sources && !m->sources->next);
    TsMosaic *expected=ts_mosaic_create();assert(expected);
    assert(ts_mosaic_load(expected,"mosaic-project-test/project-data",error,sizeof(error)));
    m->playing=expected->playing=1;ts_mosaic_begin_block(m,512);ts_mosaic_begin_block(expected,512);
    for(int i=0;i<512;++i) {
        TsSourceRouteMix a={0},b={0};
        TsStereoFrame x=ts_mosaic_read_routed(m,48000,&a),y=ts_mosaic_read_routed(expected,48000,&b);
        assert(!memcmp(&x,&y,sizeof(x)) && !memcmp(&a,&b,sizeof(a)));
    }
    ts_mosaic_free(expected);
    ts_mosaic_checkpoint(m);m->events[0].start=8;
    assert(ts_mosaic_undo(m,0) && ts_mosaic_hash(m)==hash && !ts_mosaic_undo(m,0));
    /* Missing arrangement must fail, not silently open an empty Mosaic. */
    assert(remove("mosaic-project-test/project-data/mosaic.tsm")==0);
    assert(!ts_sample_pages_load_project(&pages,bank,record,path,error,sizeof(error)));
    assert(ts_mosaic_hash(m)==hash && ts_sample_hash(&bank->current)==sample_hash);
    assert(ts_sample_pages_save_project(&pages,bank,record,NULL,path,error,sizeof(error)));
    ts_sample_pages_free(&pages);ts_mosaic_free(m);ts_instrument_free(bank);ts_instrument_free(record);free(bank);free(record);
    remove("mosaic-project-test/project-data/mosaic.tsm");remove("mosaic-project-test/project-data/mosaic-000.wav");
    remove("mosaic-project-test/mosaic-project-test.tsr");remove("mosaic-project-test/manifest.txt");
    remove("mosaic-project-test/samples/page-001/01_PHASE-BLOOM.wav");
    RMDIR("mosaic-project-test/samples/page-001");RMDIR("mosaic-project-test/samples");
    remove("mosaic-project-test/project-data/sister-tracker.tst");
    RMDIR("mosaic-project-test/project-data");RMDIR("mosaic-project-test");
}
#include "test_mosaic_volume.inc"
#include "test_mosaic_loop.inc"
#include "test_mosaic_schedule.inc"
#include "test_mosaic_read_paths.inc"
static void test_source_preview_lifetime(void)
{
    TsMosaic *m=fixture();TsMosaicEvent source=m->events[0];
    memset(m->events,0,sizeof(m->events));m->playing=0;m->time=19;
    uint64_t hash=ts_mosaic_hash(m);int history=m->history_count;
    ts_mosaic_audition_source(m,&source);ts_mosaic_collect(m);
    assert(m->sources==source.source && m->audition_source.source==source.source);
    double energy=0;
    for(int i=0;i<512;++i) {TsStereoFrame f=ts_mosaic_read(m,48000);energy+=fabs(f.l)+fabs(f.r);}
    assert(energy>0 && m->time==19 && !m->playing && ts_mosaic_hash(m)==hash && m->history_count==history);
    ts_mosaic_audition(m,0);
    for(int i=0;i<512;++i)ts_mosaic_read(m,48000);
    assert(!m->audition_id);ts_mosaic_collect(m);assert(m->sources==source.source);
    ts_mosaic_audition(m,0);assert(!m->audition_source.source);
    ts_mosaic_collect(m);assert(!m->sources);ts_mosaic_free(m);
    /* Natural completion and loading a fresh arrangement retire preview ownership. */
    m=fixture();source=m->events[0];source.duration=.002;m->playing=0;
    ts_mosaic_audition_source(m,&source);
    for(int i=0;i<512;++i)ts_mosaic_read(m,48000);
    assert(!m->audition_id);ts_mosaic_audition(m,0);assert(!m->audition_source.source);
    ts_mosaic_audition_source(m,&source);TsMosaic *loaded=ts_mosaic_create();assert(loaded);
    ts_mosaic_publish_loaded(m,loaded);ts_mosaic_free(loaded);
    assert(!m->audition_id && !m->sources && !m->audition_source.source);ts_mosaic_free(m);
}
#include "test_mosaic_marker_storage.inc"

int main(void)
{
    test_source_preview_lifetime();
    test_mosaic_volume();
    test_mosaic_read_paths();
    test_mosaic_schedule();test_mosaic_large_storage();
    test_mosaic_loop_audio();test_mosaic_loop_history();
    test_card_routes();test_tape_speed();test_event_mix_and_fades();test_mute_solo_phase();test_ownership();test_marker_storage();test_seek();test_one_shot_and_resize();test_spacing_and_history();test_storage();test_project_transaction();
    puts("Mosaic ownership, independent clocks, seeking, geometry and storage passed");return 0;
}
