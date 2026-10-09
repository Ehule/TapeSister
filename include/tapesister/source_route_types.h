#ifndef TAPESISTER_SOURCE_ROUTE_TYPES_H
#define TAPESISTER_SOURCE_ROUTE_TYPES_H

#include <stdint.h>

enum { TS_SOURCE_MAIN, TS_SOURCE_PAIR, TS_SOURCE_SPEAKER, TS_SOURCE_INHERIT };
enum { TS_SOURCE_SPEAKERS = 16 };
/* Zero is the legacy Main route. Width is an offset from the original 100%.
   Speaker numbers are logical array positions, never hardware channel IDs. */
typedef struct {
    int32_t mode, speaker, second, pan, width;
} TsSourceRoute;

/* Audio-owned state. Coefficients are prepared only on an actual edit and
   interpolated for 5 ms. Main's settled path is an identity operation. */
typedef struct {
    TsSourceRoute route;
    float main, target_main;
    float matrix[TS_SOURCE_SPEAKERS+2][2], target[TS_SOURCE_SPEAKERS+2][2];
    unsigned remaining, rate, mask;
    int ready;
} TsSourceRouteVoice;

#endif
