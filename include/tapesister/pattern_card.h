#ifndef TAPESISTER_PATTERN_CARD_H
#define TAPESISTER_PATTERN_CARD_H
#include "tapesister/audition.h"
#include "tapesister/sister_tracker.h"
struct TsSamplePages;
struct TsMosaic;
struct TsMosaicSource;
/* Immutable generation. Cards and undo share it; PCM assets are shared with
   the ordinary Mosaic source pool. Only runtime state belongs to instances. */
typedef struct {
    TsTileId tile;
    struct TsMosaicSource *audio;
    TsTuning tuning;
    size_t first,last,crossfade;
    int looping;
    TsLoopMode mode;
} TsPatternAsset;
typedef struct TsPatternCard {
    TsSisterTracker score;
    TsPatternId id;
    int asset_count, root;
    TsPatternAsset *assets;
    /* Exact FT2 cells, including volume-column effects, indexed row/lane. */
    unsigned char cells[256][32][7];
    int linear, enabled, global_volume;
    uint32_t selected;
    double duration;
    void *prepared;
} TsPatternCard;
typedef struct {
    void *(*prepare)(const TsPatternCard *);
    void (*free_source)(void *);
    void *(*create)(void);
    int (*reserve)(void *,unsigned);
    void (*free_engine)(void *);
    void (*frame)(void *);
    int (*overflow)(const void *);
    TsStereoFrame (*read)(void *,const TsPatternCard *,const void *,uint64_t,double,int,double,int,int);
} TsPatternBackend;
void ts_pattern_backend(const TsPatternBackend *);
void *ts_pattern_engine_create(void);
int ts_pattern_engine_reserve(void *,unsigned);
uint64_t ts_pattern_hash(const TsPatternCard *);
void ts_pattern_engine_free(void *);
void ts_pattern_engine_frame(void *);
int ts_pattern_engine_overflow(const void *);
TsStereoFrame ts_pattern_read(void *,const TsPatternCard *,const void *,uint64_t,double,int,double,int,int);
struct TsMosaicSource *ts_pattern_source(struct TsMosaic *,const struct TsSamplePages *,const TsInstrument *,TsPatternId,char *,size_t);
void ts_pattern_free(TsPatternCard *);
int ts_pattern_prepare(TsPatternCard *);
int ts_pattern_save(const TsPatternCard *,const char *,int,char *,size_t);
TsPatternCard *ts_pattern_load(struct TsMosaic *,const char *,int,char *,size_t);
#endif
