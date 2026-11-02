#ifndef TAPESISTER_VIDEO_CACHE_H
#define TAPESISTER_VIDEO_CACHE_H
#include <stddef.h>
typedef struct TsVideoCache TsVideoCache;
TsVideoCache *ts_video_cache_create(void);
void ts_video_cache_begin(TsVideoCache *,const char *path,int pool);
/* 1 only for a completed, current proxy. Originals remain untouched. */
int ts_video_cache_path(const char *source,char *path,size_t size);
int ts_video_cache_status(TsVideoCache *,unsigned *done,unsigned *total,unsigned *failed);
void ts_video_cache_free(TsVideoCache *);
#endif
