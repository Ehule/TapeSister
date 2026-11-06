/* Simulated driver layouts, not a listening/clock/latency test. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/routing_matrix.h"
#include "tapesister/insert.h"
#include "tapesister/sister_tracker.h"
#include "tapesister/mosaic.h"
#include "tapesister/audio_assets.h"
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define MKDIR(p) _mkdir(p)
#define CHDIR(p) _chdir(p)
#else
#include <unistd.h>
#define MKDIR(p) mkdir(p,0700)
#define CHDIR(p) chdir(p)
#endif
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
static void sample_near(float a,float b){assert(fabsf(a-b)<.0001f);}
static TsStereoFrame identity(void *u,int node,TsStereoFrame f){(void)u;(void)node;return f;}
static void render(TsRoutingMatrix *m,TsInsert *io,const float *data,unsigned channels)
{
    TsStereoFrame pairs[TS_AUDIO_PAIR_CAPACITY],live[TS_MATRIX_ROWS-TS_MATRIX_NODES]={0};
    ts_insert_duplex_block(io,1,data,1,channels);ts_insert_matrix_read(io,pairs);
    for(int p=0;p<TS_MATRIX_INPUTS;++p)live[ts_matrix_input_row(p)-TS_MATRIX_NODES]=pairs[p];
    live[TS_MATRIX_FM-TS_MATRIX_NODES]=(TsStereoFrame){.2f,.3f};
    for(int i=0;i<300;++i){TsSourceRouteMix sums={0};ts_matrix_process(m,&sums,live,identity,NULL);}
}
static void channel_layout(void)
{
    assert(TS_MATRIX_FM==11 && TS_MATRIX_COMPRESSOR==15 && TS_MATRIX_TRACKER==16);
    assert(TS_MATRIX_EFFECT_BASE==17 && TS_MATRIX_EFFECT_SC_BASE==33 && TS_MATRIX_AMBISONICS==49 && TS_MATRIX_SPEAKER_BASE==50);
    for(int p=0;p<TS_AUDIO_PAIR_CAPACITY;++p){
        assert(ts_matrix_input_pair(ts_matrix_input_row(p))==p);
        assert(!ts_matrix_is_processor(ts_matrix_input_row(p)));
        assert(ts_matrix_output_pair(ts_matrix_output_destination(p))==p);
    }
    TsRoutingMatrix *m=calloc(1,sizeof(*m));TsInsert *io=malloc(sizeof(*io));assert(m&&io);
    ts_matrix_init(m);ts_insert_init(io);TsMatrixControls c=m->controls;c.enabled=1;
    const int send=ts_matrix_output_destination(9),ret=ts_matrix_input_row(5);
    assert(ts_matrix_connect(&c,TS_MATRIX_FM,send,250)==1);
    assert(ts_matrix_connect(&c,ret,TS_MATRIX_EQ,500)==1);
    assert(ts_matrix_connect(&c,ts_matrix_input_row(12),ts_matrix_output_destination(13),1000)==1);
    snprintf(c.input_name[5],TS_MATRIX_NAME,"SUNVOX REVERB RETURN");
    snprintf(c.output_name[9],TS_MATRIX_NAME,"S/PDIF SEND");
    assert(ts_matrix_set(m,&c,48000));
    float frame[255]={0};frame[10]=.1f;frame[11]=-.15f;frame[24]=.4f;frame[25]=-.45f;
    render(m,io,frame,26);
    sample_near(m->output[0].l,.25f);sample_near(m->output[0].r,.225f); /* dry + selected wet */
    sample_near(m->output[9].l,.05f);sample_near(m->output[9].r,.075f); /* no return fed back to send */
    sample_near(m->output[13].l,.4f);sample_near(m->output[13].r,-.45f); /* high ADAT -> output 27/28 */
    TsStereoFrame selected=ts_input_channel_select(frame,26,TS_INPUT_PAIR_BASE+5);
    sample_near(selected.l,.1f);sample_near(selected.r,-.15f);
    selected=ts_input_channel_select(frame,26,TS_INPUT_MONO_BASE+25);sample_near(selected.l,-.45f);sample_near(selected.r,-.45f);
    /* Muting wet preserves dry; serial removes dry, while the send stays live. */
    assert(ts_matrix_connect(&c,ret,TS_MATRIX_EQ,0)==1);assert(ts_matrix_set(m,&c,48000));render(m,io,frame,26);
    sample_near(m->output[0].l,.2f);sample_near(m->output[0].r,.3f);
    assert(ts_matrix_connect(&c,ret,TS_MATRIX_EQ,1000)==1);
    assert(ts_matrix_connect(&c,TS_MATRIX_FM,TS_MATRIX_MAIN,0)==1);
    assert(ts_matrix_set(m,&c,48000));render(m,io,frame,26);
    sample_near(m->output[0].l,.1f);sample_near(m->output[0].r,-.15f);sample_near(m->output[9].l,.05f);
    /* Disabled range retains absolute positions and never substitutes 1/2. */
    frame[0]=.9f;frame[1]=-.9f;ts_audio_range_mask((TsAudioChannelRange){11,12},frame,1,26);
    render(m,io,frame,26);sample_near(m->output[0].l,.1f);sample_near(m->output[13].l,0);
    frame[0]=.73f;frame[1]=-.61f; /* A missing return must not substitute these live inputs. */
    render(m,io,frame,2);sample_near(m->output[0].l,0);sample_near(m->output[0].r,0);
    selected=ts_input_channel_select(frame,2,TS_INPUT_PAIR_BASE+5);sample_near(selected.l,0);sample_near(selected.r,0);
    /* Last representable odd channel and third destination mask word. */
    c.row[ts_matrix_input_row(127)].matrix_gain[ts_matrix_output_destination(127)]=1000;
    assert(ts_matrix_set(m,&c,48000));memset(frame,0,sizeof(frame));frame[254]=.37f;
    render(m,io,frame,255);sample_near(m->output[127].l,.37f);sample_near(m->output[127].r,0);
    TsSourceRouteVoice voice={0};TsSourceRoute r={.matrix_enabled=1};r.matrix_gain[ts_matrix_output_destination(127)]=1000;
    ts_source_route_set(&voice,r,48000);TsSourceRouteMix a={0},b={0};
    ts_source_route_frame(&voice,(TsStereoFrame){.2f,-.3f},&a);ts_source_route_add(&b,&a,1);
    sample_near(b.graph[ts_matrix_output_destination(127)].r,-.3f);
    /* Matrix names and routes round-trip while their device is unavailable. */
    FILE *f=tmpfile();assert(f&&ts_matrix_write(f,&c));rewind(f);TsMatrixControls saved;ts_matrix_default(&saved);
    char line[2048];while(fgets(line,sizeof(line),f)){char *v=strchr(line,'=');assert(v);*v++=0;v[strcspn(v,"\r\n")]=0;assert(ts_matrix_read(&saved,line,v)==1);}fclose(f);
    assert(!memcmp(&c,&saved,sizeof(c)) && ts_matrix_valid(&saved));
    /* Pre-I/O text rows keep all original offsets, with appended routes zero. */
    ts_matrix_default(&saved);char old[2048];int at=snprintf(old,sizeof(old),"0,0,0");
    for(int d=0;d<66;++d)at+=snprintf(old+at,sizeof(old)-at,",%d",d==15?700:d==33?300:d==50?200:0);
    assert(ts_matrix_read(&saved,"Matrix.Row11",old)==1);
    assert(saved.row[11].matrix_gain[15]==700 && saved.row[11].matrix_gain[33]==300 && saved.row[11].matrix_gain[50]==200);
    assert(!saved.row[11].matrix_gain[66] && ts_matrix_valid(&saved));
    ts_insert_free(io);free(io);free(m);
}
static void legacy_insert(void)
{
    TsInsert *io=malloc(sizeof(*io));assert(io);ts_insert_init(io);
    TsInsertControls c={.send_pair=9,.return_pair=5};assert(ts_insert_valid(&c));ts_insert_set(io,&c);
    io->output_channels=28;ts_insert_capture_prepare(io,26,48000,128);
    float input[26]={0},output[28];input[10]=.2f;input[11]=-.4f;
    ts_insert_duplex_block(io,1,input,1,26);
    TsStereoFrame returned=ts_insert_process(io,(TsStereoFrame){.3f,.5f},1);
    sample_near(returned.l,.2f);sample_near(returned.r,-.4f);
    ts_insert_write_output(io,output,28,(TsStereoFrame){.1f,.15f});
    sample_near(output[0],.1f);sample_near(output[18],.3f);sample_near(output[19],.5f);sample_near(output[20],0);
    assert(ts_insert_reserved_port(io)==6);
    /* Generation and pair encoding do not alias high pairs to the low ports. */
    c.return_pair=100;c.send_pair=100;ts_insert_set(io,&c);assert(ts_insert_reserved_port(io)==101);
    ts_insert_duplex_block(io,1,input,1,26);returned=ts_insert_process(io,(TsStereoFrame){.3f,.5f},1);
    sample_near(returned.l,0);sample_near(returned.r,0);
    ts_insert_free(io);free(io);
}
static void persistence(void)
{
    char error[256];const int destination=ts_matrix_output_destination(13);
    static TsInstrument bank,loaded;ts_instrument_init(&bank);ts_instrument_init(&loaded);
    assert(ts_instrument_activate_silence(&bank,32,48000,error,sizeof(error)));
    bank.bank[0].output_route=(TsSourceRoute){.matrix_enabled=1};bank.bank[0].output_route.matrix_gain[destination]=725;
    assert(ts_instrument_save_recipe(&bank,"io-extended.tsr",error,sizeof(error)));
    assert(ts_instrument_validate_recipe("io-extended.tsr",error,sizeof(error)));
    assert(ts_instrument_load_recipe(&loaded,"io-extended.tsr",error,sizeof(error)));
    assert(loaded.bank[0].output_route.matrix_gain[destination]==725);remove("io-extended.tsr");
    MKDIR("io-shared");MKDIR("io-shared/samples");MKDIR("io-shared/samples/audio");TsAudioAssets *assets=ts_audio_assets_begin("io-shared",1);assert(assets);
    assert(ts_instrument_save_recipe(&bank,"io-shared/bank.tsr",error,sizeof(error)));ts_audio_assets_end(assets);
    assert(ts_instrument_validate_recipe("io-shared/bank.tsr",error,sizeof(error)));
    assert(ts_instrument_load_recipe(&loaded,"io-shared/bank.tsr",error,sizeof(error)));
    assert(loaded.bank[0].output_route.matrix_gain[destination]==725);remove("io-shared/bank.tsr");
    ts_instrument_free(&bank);ts_instrument_free(&loaded);
    static TsSisterTracker t,copy;ts_sister_tracker_init(&t);ts_sister_tracker_init(&copy);
    t.lanes[0].output_route=(TsSourceRoute){.matrix_enabled=1};t.lanes[0].output_route.matrix_gain[destination]=650;
    assert(ts_sister_tracker_save_file(&t,"io-extended.trk",error,sizeof(error)));
    assert(ts_sister_tracker_load_file(&copy,"io-extended.trk",error,sizeof(error)));
    assert(copy.lanes[0].output_route.matrix_gain[destination]==650);remove("io-extended.trk");
    ts_sister_tracker_free(&t);ts_sister_tracker_free(&copy);
    TsMosaic *m=ts_mosaic_create(),*other=ts_mosaic_create();assert(m&&other);
    TsSample sample={.data=calloc(64,sizeof(float)),.frames=32,.sample_rate=48000,.channels=2};assert(sample.data);
    TsMosaicSource *source=ts_mosaic_source(m,&sample,error,sizeof(error));assert(source);ts_sample_free(&sample);
    TsMosaicEvent *e=ts_mosaic_add(m,source,0,0);assert(e);e->output_route=(TsSourceRoute){.matrix_enabled=1};e->output_route.matrix_gain[destination]=800;
    MKDIR("io-mosaic");assert(ts_mosaic_save(m,"io-mosaic",error,sizeof(error)));
    if(!ts_mosaic_load(other,"io-mosaic",error,sizeof(error))){fprintf(stderr,"Mosaic: %s\n",error);assert(0);}
    assert(other->events[0].output_route.matrix_gain[destination]==800);
    MKDIR("io-mosaic-shared");MKDIR("io-mosaic-shared/samples");MKDIR("io-mosaic-shared/samples/audio");remove("io-mosaic-shared/mosaic-000.tsa");
    assets=ts_audio_assets_begin("io-mosaic-shared",1);assert(assets);
    assert(ts_mosaic_save(m,"io-mosaic-shared",error,sizeof(error)));ts_audio_assets_end(assets);
    assert(ts_mosaic_load(other,"io-mosaic-shared",error,sizeof(error)));
    assert(other->events[0].output_route.matrix_gain[destination]==800);
    remove("io-mosaic-shared/mosaic.tsm");remove("io-mosaic-shared/mosaic-000.tsa");
    ts_mosaic_free(m);ts_mosaic_free(other);remove("io-mosaic/mosaic.tsm");remove("io-mosaic/mosaic-000.wav");
}
int main(void)
{
    /* Shared-asset ownership is scoped to a save transaction. Each invocation
       needs its own fixture root, including repeated release-suite runs. */
    char work[128];
#ifdef _WIN32
    snprintf(work,sizeof(work),"interface-io-%lu-%lu",(unsigned long)GetCurrentProcessId(),(unsigned long)GetTickCount());
    assert(!MKDIR(work));
#else
    strcpy(work,"interface-io-XXXXXX");assert(mkdtemp(work));
#endif
    assert(!CHDIR(work));channel_layout();legacy_insert();persistence();
    puts("Complete interface I/O: physical identity, stereo stride, parallel/serial, migration and unavailable ports passed (simulated)");return 0;
}
