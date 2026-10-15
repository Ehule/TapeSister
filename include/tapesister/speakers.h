#ifndef TAPESISTER_SPEAKERS_H
#define TAPESISTER_SPEAKERS_H
#include <stdio.h>
enum { TS_SPEAKER_COUNT=16, TS_SPEAKER_NAME=24 };
/* Device setup is global; project array membership refers to these speaker IDs. */
typedef struct {
    int count, output[TS_SPEAKER_COUNT];
    char name[TS_SPEAKER_COUNT][TS_SPEAKER_NAME];
} TsSpeakerSetup;
void ts_speakers_default(TsSpeakerSetup *s);
int ts_speakers_valid(const TsSpeakerSetup *s);
int ts_speakers_channel(const TsSpeakerSetup *s,int speaker,unsigned channels);
int ts_speakers_write(FILE *f,const TsSpeakerSetup *s);
int ts_speakers_read(TsSpeakerSetup *s,const char *key,const char *value);
#endif
