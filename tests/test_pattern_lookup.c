#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#define TAPEHEAD_EMBEDDED
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "tapesister/tapehead_embed.h"

static int forbidden,fail_realloc;
static void *lookup_calloc(size_t n,size_t size)
{assert(!forbidden);return calloc(n,size);}
static void *lookup_realloc(void *p,size_t size)
{assert(!forbidden);if(fail_realloc){fail_realloc=0;return NULL;}return realloc(p,size);}
/* The actual backend, with allocator checks and access to its private state.
   Its library object is not pulled in because this TU supplies install(). */
#define calloc lookup_calloc
#define realloc lookup_realloc
#include "../src/ts_pattern_tapehead.c"
#undef calloc
#undef realloc

static void equal_read(PatternEngine *cached,PatternEngine *scan,const TsPatternCard *p,
                        const void *owner,uint64_t generation,double time,int rate,int note,int loop,unsigned *hint)
{
    /* A null hint uses the unchanged linear lookup/eviction path. Audio,
       independent voices and exhaustion must agree with that path exactly. */
    forbidden=1;
    TsStereoFrame a=engine_read_cached(cached,p,owner,generation,time,rate,1,note,loop,hint);
    TsStereoFrame b=engine_read(scan,p,owner,generation,time,rate,1,note,loop);
    forbidden=0;
    assert(!memcmp(&a,&b,sizeof(a)));
    assert(cached->overflow==scan->overflow);
    for(unsigned i=0;i<cached->capacity;++i){
        assert(cached->players[i].owner==scan->players[i].owner);
        assert(cached->players[i].note==scan->players[i].note);
        assert(cached->players[i].generation==scan->players[i].generation);
        assert(cached->players[i].stamp==scan->players[i].stamp);
    }
}

int main(void)
{
    SDL_SetMainReady();
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER));
    SDL_Window *window=SDL_CreateWindow("pattern cache",0,0,640,400,SDL_WINDOW_HIDDEN);assert(window);
    char error[256];TsTapeHeadHost host={.window=window};assert(ts_tapehead_init(&host,48000,error,sizeof(error)));
    TsSamplePages *pages=calloc(1,sizeof(*pages));TsInstrument *bank=calloc(1,sizeof(*bank));assert(pages&&bank);
    assert(ts_sample_pages_init(pages,error,sizeof(error)));ts_instrument_init(bank);
    assert(ts_instrument_activate_silence(bank,256,48000,error,sizeof(error)));
    for(size_t i=0;i<bank->current.frames;++i)bank->current.data[i]=.15f*sinf((float)i*.043f);
    assert(ts_instrument_sync_selected(bank,error,sizeof(error)));
    TsPatternId id;assert(ts_sister_tracker_add_pattern(&pages->tracker,16,&id,error,sizeof(error)));
    assert(ts_sister_tracker_insert_order(&pages->tracker,0,id,error,sizeof(error)));
    uint8_t alias;TsTileId tile=bank->bank[0].tile_id;
    assert(ts_sister_tracker_bind_tile(&pages->tracker,tile,&alias,error,sizeof(error)));
    pages->tracker.patterns[0]->cells[0][0]=(TsTrackerCell){.tile_id=tile,.note_kind=TS_TRACKER_NOTE_PITCH,.note=60};
    TsMosaic *m=ts_mosaic_create();assert(m);
    TsMosaicSource *source=ts_pattern_source(m,pages,bank,id,error,sizeof(error));assert(source&&source->pattern->prepared);
    TsPatternId other_id;assert(ts_sister_tracker_add_pattern(&pages->tracker,16,&other_id,error,sizeof(error)));
    pages->tracker.patterns[1]->cells[0][0]=(TsTrackerCell){.tile_id=tile,.note_kind=TS_TRACKER_NOTE_PITCH,.note=72};
    TsMosaicSource *other=ts_pattern_source(m,pages,bank,other_id,error,sizeof(error));assert(other&&other->pattern->prepared);
    PatternEngine *cached=engine_create(),*scan=engine_create();assert(cached&&scan);
    assert(engine_reserve(cached,64)&&engine_reserve(scan,64));
    uintptr_t owners[4096]={0};unsigned hints[16]={0};
    hints[0]=UINT32_MAX;hints[1]=63; /* Out-of-range and wrong-owner hints. */
    for(int frame=0;frame<2048;++frame){
        engine_frame(cached);engine_frame(scan);
        for(int i=0;i<16;++i){
            const void *owner=i<8?&owners[i]:&owners[1000+(frame/79*17+i)%2048];
            double time=(frame%503)/48000.0;
            equal_read(cached,scan,frame>=900&&frame<1200?other->pattern:source->pattern,owner,(uint64_t)(frame/113),time,
                       frame<1400?48000:44100,i<8?60:48+(frame/97+i)%25,frame<1800,&hints[i]);
        }
        if(frame==511){
            PatternPlayer *players=cached->players;
            fail_realloc=1;assert(!engine_reserve(cached,128));
            assert(cached->players==players&&cached->capacity==64);
            assert(engine_reserve(cached,128)&&engine_reserve(scan,128));
        }
    }
    engine_free(cached);engine_free(scan);
    cached=engine_create();scan=engine_create();assert(cached&&scan);
    equal_read(cached,scan,source->pattern,&owners[0],1,0,48000,60,1,&hints[0]);
    assert(cached->overflow);
    assert(engine_reserve(cached,2)&&engine_reserve(scan,2));
    for(int frame=0;frame<12;++frame){
        engine_frame(cached);engine_frame(scan);
        for(int i=0;i<3;++i)
            equal_read(cached,scan,source->pattern,&owners[frame/3*4+i],1,frame/48000.0,48000,60,1,&hints[i]);
        assert(cached->overflow); /* Hints cannot introduce voice stealing. */
    }
    engine_free(cached);engine_free(scan);ts_mosaic_free(m);ts_sample_pages_free(pages);ts_instrument_free(bank);free(pages);free(bank);
    ts_tapehead_close();SDL_DestroyWindow(window);SDL_Quit();
    puts("pattern lookup: exact scan-path audio/state, invalid indices, stale owners, notes, seeks, rates, reserve failure and exhaustion passed");
    return 0;
}
