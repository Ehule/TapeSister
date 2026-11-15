#ifndef TAPESISTER_MOSAIC_GALLERY_H
#define TAPESISTER_MOSAIC_GALLERY_H
#include "tapesister/mosaic.h"

enum { TS_GALLERY_LIMIT=16, TS_GALLERY_NAME=48 };
enum { TS_GALLERY_START, TS_GALLERY_RESUME, TS_GALLERY_MARKER };
enum { TS_GALLERY_SEQUENCE_STEPS=64, TS_GALLERY_POINT_END=-2, TS_GALLERY_POINT_TIME=-1 };
enum { TS_GALLERY_FADE_LINEAR, TS_GALLERY_FADE_SMOOTH, TS_GALLERY_FADE_POWER, TS_GALLERY_FADE_CURVES };
typedef struct {
    uint64_t canvas_id; /* Stable identity, independent of Gallery order. */
    int in_marker,out_marker; /* TIME/END or a zero-based marker slot. */
    double in_seconds,out_seconds,fade_seconds;
    int repeats,curve;
} TsGalleryStep;
typedef struct {
    TsGalleryStep step[TS_GALLERY_SEQUENCE_STEPS];
    int length,loop,first,last;
} TsGallerySequence;
typedef struct {
    int running,paused,step,visit,queued,next,next_visit;
    int continuation; /* When step == -1, finish the removed visit, then enter this slot (length means end). */
    double start,end,fade_start,fade_end,next_fade;
    TsGalleryStep current; /* Frozen until the next visit. */
    char error[128];
} TsGallerySequencePlayer;
typedef struct {
    TsMosaic *scene;
    uint64_t id;
    char name[TS_GALLERY_NAME];
    int entry,marker;
    int parked,paused;
    double scroll,xscroll,scale,hscale;
} TsMosaicCanvas;
typedef struct {
    uint64_t id;
    char name[TS_GALLERY_NAME];
    int entry,marker;
} TsMosaicCanvasInfo;
typedef struct TsMosaicGalleryCatalog {
    TsMosaicCanvasInfo canvas[TS_GALLERY_LIMIT];
    int count,active,present;
    uint64_t next_id;
    double fade_seconds;
    TsGallerySequence sequence;
} TsMosaicGalleryCatalog;
typedef struct TsMosaicGallery {
    TsMosaicCanvas canvas[TS_GALLERY_LIMIT];
    TsMosaic *root; /* Caller-owned original model; all other scenes are owned. */
    int count,active,outgoing;
    uint64_t next_id;
    double fade_seconds,fade_position,fade_length;
    TsGallerySequence sequence;
    TsGallerySequencePlayer player;
    TsMosaic *spare; /* Preallocated playback-only outgoing copy for same-canvas fades. */
    int spare_active,sequence_fade,fade_curve;
    unsigned block_frames;
} TsMosaicGallery;
void ts_gallery_sequence_default(TsGallerySequence *,uint64_t);
int ts_gallery_sequence_valid(const TsGallerySequence *);
int ts_gallery_step_bounds(const TsMosaicGallery *,const TsGalleryStep *,int *,double *,double *,char *,size_t);
/* Prepare on the control thread under audio exclusion (like pattern edits).
   Other commands require the same exclusion. No allocation or I/O in playback. */
int ts_gallery_sequence_prepare(TsMosaicGallery *,char *,size_t);
int ts_gallery_sequence_play(TsMosaicGallery *,int,char *,size_t);
void ts_gallery_sequence_pause(TsMosaicGallery *,int);
void ts_gallery_sequence_stop(TsMosaicGallery *);
int ts_gallery_sequence_next(TsMosaicGallery *,int,char *,size_t);
/* Live arrangement edits preserve the sounding visit and in-flight fade. */
int ts_gallery_sequence_insert(TsMosaicGallery *,int,const TsGalleryStep *);
int ts_gallery_sequence_remove(TsMosaicGallery *,int);
int ts_gallery_sequence_move(TsMosaicGallery *,int,int);
void ts_gallery_sequence_tick(TsMosaicGallery *,int);
unsigned ts_gallery_sequence_span(const TsMosaicGallery *,int,unsigned);
TsMosaicGallery *ts_gallery_create(TsMosaic *first);
void ts_gallery_free(TsMosaicGallery *);
TsMosaic *ts_gallery_active(const TsMosaicGallery *);
TsMosaic *ts_gallery_outgoing(const TsMosaicGallery *);
int ts_gallery_add(TsMosaicGallery *,char *,size_t);
int ts_gallery_append_prepared(TsMosaicGallery *,TsMosaic *,char *,size_t);
/* Reorder without restarting transport, including both sides of a fade. */
int ts_gallery_move(TsMosaicGallery *,int,int,char *,size_t);
/* Replace in place with a fresh identity. Active playback must be stopped.
   Publish under exclusion; release retired outside it. The caller-owned root
   stays owned by the caller even after replacement, like detach. */
int ts_gallery_replace_prepared(TsMosaicGallery *,int,TsMosaic *,TsMosaic **,char *,size_t);
int ts_gallery_detach(TsMosaicGallery *,int,TsMosaic **,char *,size_t);
/* Remove an inactive/non-fading canvas; destruction stays on the control thread. */
int ts_gallery_remove(TsMosaicGallery *,int,char *,size_t);
int ts_gallery_rename(TsMosaicGallery *,int,const char *,char *,size_t);
/* Control-thread command under the output exclusion. At most two renderers.
   A second request during a fade returns busy without changing either page. */
int ts_gallery_switch(TsMosaicGallery *,int,char *,size_t);
void ts_gallery_begin_block(TsMosaicGallery *,unsigned);
void ts_gallery_clock(TsMosaicGallery *,int,int,double,double);
TsStereoFrame ts_gallery_read(TsMosaicGallery *,int,TsSourceRouteMix *);
uint64_t ts_gallery_hash(const TsMosaicGallery *);
int ts_gallery_save(const TsMosaicGallery *,const char *,char *,size_t);
/* Load only into a fresh staging gallery; live replacement is a project transaction. */
int ts_gallery_load(TsMosaicGallery *,const char *,char *,size_t);
/* Metadata only: no audio allocation or decoding. */
int ts_gallery_catalog(const char *,TsMosaicGalleryCatalog *,char *,size_t);
/* Map the two original model addresses after their contents have been swapped. */
void ts_gallery_rebind_root(TsMosaicGallery *,TsMosaic *);
#endif
