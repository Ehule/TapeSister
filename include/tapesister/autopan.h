#ifndef TAPESISTER_AUTOPAN_H
#define TAPESISTER_AUTOPAN_H
#include "tapesister/sample.h"
#include <stdint.h>
#include <stdio.h>
typedef enum {
    TS_AUTOPAN_SINE, TS_AUTOPAN_TRIANGLE, TS_AUTOPAN_SQUARE,
    TS_AUTOPAN_SAW, TS_AUTOPAN_RANDOM_STEP, TS_AUTOPAN_RANDOM_SMOOTH
} TsAutoPanWave;
typedef enum { TS_AUTOPAN_BALANCE, TS_AUTOPAN_DUAL_TREMOLO } TsAutoPanMode;
typedef struct {
    float rate_hz;       /* 0.01..100 Hz */
    float depth;         /* 0..1 */
    float center;        /* -1..1 */
    float phase_offset;  /* 0..1 cycle, dual tremolo only */
    TsAutoPanWave wave;
    TsAutoPanMode mode;
    int sync,reset_on_transport;
    float beats;
} TsAutoPanControls;
typedef struct {
    TsAutoPanControls controls;
    double phase,step;
    float random_a,random_b;
    uint32_t random_state,seed,transport_serial;
    float parameter_cache[5],setting_cache[6];int configured;
    int transport_ready;double quarter;
    float left,right,slew,mix,gain,mix_target,gain_target;
    unsigned sample_rate;
} TsAutoPan;
enum { TS_AP_RATE,TS_AP_DEPTH,TS_AP_CENTER,TS_AP_WAVE,TS_AP_MODE,TS_AP_PHASE,TS_AP_SYNC,TS_AP_DIVISION,TS_AP_RESET,TS_AP_COUNT };
typedef struct { int initialized;float value[TS_AP_COUNT-3]; } TsAutoPanSettings;
void ts_autopan_settings_default(TsAutoPanSettings *);
int ts_autopan_settings_valid(const TsAutoPanSettings *);
int ts_autopan_write(FILE *,const char *,const TsAutoPanSettings *);
int ts_autopan_read(TsAutoPanSettings *,const char *);
float ts_autopan_default_rate(void);
int ts_autopan_steps(int field);
void ts_autopan_label(char *,size_t,int,float);
void ts_autopan_configure(TsAutoPan *,const TsAutoPanSettings *,float,float,float,float,float);
void ts_autopan_transport(TsAutoPan *,double quarter,uint32_t reset_serial);
TsStereoFrame ts_autopan_process(TsAutoPan *,TsStereoFrame,float gate,float *direct);
void ts_autopan_default(TsAutoPanControls *controls);
void ts_autopan_init(TsAutoPan *pan,unsigned sample_rate,uint32_t seed);
void ts_autopan_set(TsAutoPan *pan,const TsAutoPanControls *controls);
void ts_autopan_reset_phase(TsAutoPan *pan);
void ts_autopan_process_span(TsAutoPan *pan,const TsStereoFrame *in,TsStereoFrame *out,unsigned frames);
#endif
