#ifndef TAPESISTER_VISUAL_MEDIA_H
#define TAPESISTER_VISUAL_MEDIA_H
#include <ctype.h>
#include <string.h>

/* Shared by the browser, drop handler and visual child. GIF stays animated. */
static inline int ts_visual_extension(const char *path,const char *extension)
{
    const char *p=path?strrchr(path,'.'):NULL;
    if(!p)return 0;
    while(*p && *extension)if(tolower((unsigned char)*p++)!=*extension++)return 0;
    return !*p && !*extension;
}
static inline int ts_visual_image_path(const char *path)
{
    return ts_visual_extension(path,".png") || ts_visual_extension(path,".jpg") ||
        ts_visual_extension(path,".jpeg") || ts_visual_extension(path,".bmp") || ts_visual_extension(path,".tga");
}
static inline int ts_visual_media_path(const char *path)
{
    const char *types[]={".mp4",".m4v",".mov",".mkv",".webm",".avi",".mpg",".mpeg",".ts",".gif"};
    if(ts_visual_image_path(path))return 1;
    for(unsigned i=0;i<sizeof(types)/sizeof(*types);++i)if(ts_visual_extension(path,types[i]))return 1;
    return 0;
}
#endif
