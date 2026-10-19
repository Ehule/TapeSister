#ifndef TAPESISTER_MANDALA_VIDEO_H
#define TAPESISTER_MANDALA_VIDEO_H
#include <SDL2/SDL.h>
#include <stdint.h>
/* Renderer-process only. Never constructed or called by the audio host. */
typedef struct TsVideo TsVideo;
enum { TS_VIDEO_CLOSED, TS_VIDEO_OPENING, TS_VIDEO_PLAYING, TS_VIDEO_ENDED, TS_VIDEO_ERROR };
typedef struct { unsigned state, position_ms, duration_ms, loops; char message[96]; } TsVideoStatus;
TsVideo *ts_video_open(const char *path);
void ts_video_control(TsVideo *v, int paused, int loop, unsigned restart, unsigned fps);
/* Nonblocking consumer; SDL texture operations stay on the renderer thread. */
int ts_video_upload(TsVideo *v, SDL_Renderer *renderer, SDL_Texture **texture, TsVideoStatus *status);
void ts_video_close(TsVideo *v);
#endif
