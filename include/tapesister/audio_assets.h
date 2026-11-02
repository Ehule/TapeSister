#ifndef TAPESISTER_AUDIO_ASSETS_H
#define TAPESISTER_AUDIO_ASSETS_H
#include "tapesister/sample.h"
#include <stdio.h>

/* One thread-local scope per transaction/load. Audio identities describe the
   exact canonical float WAV bytes; names, tuning and loops remain per use. */
typedef struct TsAudioAssets TsAudioAssets;
TsAudioAssets *ts_audio_assets_begin(const char *root, int writing);
void ts_audio_assets_end(TsAudioAssets *);
int ts_audio_assets_writing(void);
int ts_audio_assets_active(void);
int ts_audio_assets_depth(const char *path);
int ts_audio_assets_root(char *out, size_t size, const char *path, unsigned depth);
int ts_audio_assets_put_block(FILE *, const TsSample *);
int ts_audio_assets_get_block(FILE *, TsSample *, int validate_only);
int ts_audio_assets_save_ref(const TsSample *, const char *, char *, size_t);
int ts_audio_assets_load_ref(TsSample *, const char *, char *, size_t);
int ts_audio_assets_known_hash(const char *path, uint64_t *hash);
#endif
