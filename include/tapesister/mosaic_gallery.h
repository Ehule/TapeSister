#ifndef TAPESISTER_MOSAIC_GALLERY_H
#define TAPESISTER_MOSAIC_GALLERY_H
#include "tapesister/mosaic.h"

enum { TS_GALLERY_LIMIT=16, TS_GALLERY_NAME=48 };
enum { TS_GALLERY_START, TS_GALLERY_RESUME, TS_GALLERY_MARKER };
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
} TsMosaicGalleryCatalog;
typedef struct TsMosaicGallery {
    TsMosaicCanvas canvas[TS_GALLERY_LIMIT];
    TsMosaic *root; /* Caller-owned original model; all other scenes are owned. */
    int count,active,outgoing;
    uint64_t next_id;
    double fade_seconds,fade_position,fade_length;
} TsMosaicGallery;
TsMosaicGallery *ts_gallery_create(TsMosaic *first);
void ts_gallery_free(TsMosaicGallery *);
TsMosaic *ts_gallery_active(const TsMosaicGallery *);
TsMosaic *ts_gallery_outgoing(const TsMosaicGallery *);
int ts_gallery_add(TsMosaicGallery *,char *,size_t);
int ts_gallery_append_prepared(TsMosaicGallery *,TsMosaic *,char *,size_t);
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
