#ifndef TAPESISTER_EQ_TYPES_H
#define TAPESISTER_EQ_TYPES_H
#define TS_MASTER_EQ_BANDS 5
#define TS_MASTER_EQ_MIN_HZ 20.0f
#define TS_MASTER_EQ_MAX_HZ 20000.0f
#define TS_MASTER_EQ_MIN_Q .3f
#define TS_MASTER_EQ_MAX_Q 8.0f

typedef enum { TS_EQ_BELL, TS_EQ_LOW_SHELF, TS_EQ_HIGH_SHELF,
    TS_EQ_HIGH_PASS, TS_EQ_LOW_PASS, TS_EQ_NOTCH, TS_EQ_TYPE_COUNT } TsEqType;
typedef struct { int enabled, type; float frequency, gain_db, q; } TsEqBand;
typedef struct {
    int enabled;
    TsEqBand band[TS_MASTER_EQ_BANDS];
    int solo_band; /* 0: all enabled bands; 1..5: only that band, retaining bypass states. */
} TsMasterEqControls;
#endif
