#ifndef TAPESISTER_SAVE_IO_H
#define TAPESISTER_SAVE_IO_H
#include <stddef.h>
#include <stdint.h>
/* Synchronous, caller-owned control. Callbacks execute on the saving thread.
   A thread-local binding carries cancellation through the existing codecs;
   unrelated audio, loading and exporting threads are unaffected. */
typedef struct {
    void *userdata;
    int (*cancelled)(void *);
    void (*progress)(void *, const char *stage, const char *asset,
                     uint64_t bytes, size_t completed, size_t total);
    int (*reuse)(void *, const char *path, uint64_t signature);
    /* Optional bounded cache owned by an immutable document snapshot. Never
       carry these entries across saves/edits. store=0 lookup, store=1 insert. */
    int (*sample_hash)(void *,const void *,size_t,unsigned,unsigned,uint64_t *,int store);
    void *reuse_userdata;
    uint64_t bytes;
    size_t completed, total;
    char stage[64], asset[2048];
    int committing, was_cancelled;
    char recovery[2048];
} TsSaveControl;
TsSaveControl *ts_save_io_bind(TsSaveControl *);
TsSaveControl *ts_save_io_control(void);
int ts_save_io_poll(uint64_t bytes);
int ts_save_io_stage(const char *, const char *);
int ts_save_io_reuse(const char *, uint64_t);
uint64_t ts_save_io_hash(uint64_t, const void *, size_t);
#endif
