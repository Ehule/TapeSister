#ifndef TAPESISTER_SOURCE_ROUTE_TYPES_H
#define TAPESISTER_SOURCE_ROUTE_TYPES_H

#include <stdint.h>

enum { TS_SOURCE_MAIN, TS_SOURCE_PAIR, TS_SOURCE_SPEAKER, TS_SOURCE_INHERIT };
enum { TS_SOURCE_SPEAKERS = 16 };
enum { TS_SEND_PRISM, TS_SEND_PEDALBOARD, TS_SEND_FALLOUT, TS_SOURCE_SENDS };
enum { TS_SOURCE_COEFFICIENTS = TS_SOURCE_SPEAKERS + 2 + TS_SOURCE_SENDS * 2 };
/* Zero is the legacy Main route. Width is an offset from the original 100%.
   Speaker numbers are logical array positions, never hardware channel IDs. */
typedef struct {
    int32_t mode, speaker, second, pan, width;
    /* Opt-in parallel mix. A zero-initialized/legacy route remains unchanged.
       Sends tap the source before clean level, pan and width. */
    int32_t mix_enabled, clean_level, send_level[TS_SOURCE_SENDS];
} TsSourceRoute;

/* Audio-owned state. Coefficients are prepared only on an actual edit and
   interpolated for 5 ms. Main's settled path is an identity operation. */
typedef struct {
    TsSourceRoute route;
    float main, target_main;
    float main_matrix[4], target_main_matrix[4];
    float matrix[TS_SOURCE_COEFFICIENTS][2], target[TS_SOURCE_COEFFICIENTS][2];
    unsigned remaining, rate, mask;
    int ready;
} TsSourceRouteVoice;

#endif
