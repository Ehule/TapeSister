#include "tapesister/save_io.h"
#include <stdio.h>
#if defined(_MSC_VER)
#define SAVE_LOCAL __declspec(thread)
#else
#define SAVE_LOCAL _Thread_local
#endif
static SAVE_LOCAL TsSaveControl *current;
TsSaveControl *ts_save_io_bind(TsSaveControl *control)
{TsSaveControl *old=current;current=control;return old;}
TsSaveControl *ts_save_io_control(void){return current;}
int ts_save_io_poll(uint64_t bytes)
{
    TsSaveControl *c=current;if(!c)return 1;
    c->bytes+=bytes;
    if(!c->committing && c->cancelled && c->cancelled(c->userdata))c->was_cancelled=1;
    if(c->progress)c->progress(c->userdata,c->stage[0]?c->stage:"Preparing",c->asset,c->bytes,c->completed,c->total);
    return !c->was_cancelled;
}
int ts_save_io_stage(const char *stage,const char *asset)
{if(current){snprintf(current->stage,sizeof(current->stage),"%s",stage?stage:"");snprintf(current->asset,sizeof(current->asset),"%s",asset?asset:"");}return ts_save_io_poll(0);}
int ts_save_io_reuse(const char *path,uint64_t signature)
{return current && current->reuse?current->reuse(current->reuse_userdata,path,signature):0;}
uint64_t ts_save_io_hash(uint64_t h,const void *data,size_t size)
{const unsigned char *p=data;for(size_t i=0;i<size;++i)h=(h^p[i])*UINT64_C(1099511628211);return h;}
