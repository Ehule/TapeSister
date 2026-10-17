#ifndef TAPESISTER_PERFORMANCE_INPUT_H
#define TAPESISTER_PERFORMANCE_INPUT_H
#include "tapesister/input_monitor.h"
#include "tapesister/performance_transport.h"

enum { TS_TUNER_RING=4096, TS_TUNER_WINDOW=2048, TS_TUNER_LAGS=640 };
typedef struct {
    float left[TS_PERFORMANCE_INPUTS],right[TS_PERFORMANCE_INPUTS];
    float target_left[TS_PERFORMANCE_INPUTS],target_right[TS_PERFORMANCE_INPUTS];
    unsigned active[TS_PERFORMANCE_INPUTS],count,generation;
    float slew;
} TsInputMixVoice;
typedef struct { float sample,rate; unsigned request; } TsTunerSample;
typedef struct {
    TsInputMonitor monitor;
    _Atomic unsigned control[TS_PERFORMANCE_INPUTS],channels,rate,generation;
    TsInputMixVoice capture_voice,duplex_voice;
    _Atomic unsigned tuner_request,tuner_read,tuner_write,tuner_overflows;
    TsTunerSample tuner_ring[TS_TUNER_RING];
    unsigned producer_request,producer_rate,decimation,count;
    float sum;
} TsPerformanceInputs;
typedef struct { int valid,note; float frequency,cents,confidence; } TsTunerReading;
typedef struct {
    float samples[TS_TUNER_WINDOW],rate;
    unsigned request,count,overflows;
    TsTunerReading reading;
} TsInputTuner;

void ts_performance_inputs_init(TsPerformanceInputs *s);
/* Control thread, capture stopped. The output callback may still run. */
void ts_performance_inputs_prepare(TsPerformanceInputs *s,unsigned channels,unsigned rate,unsigned frames);
/* Control thread: publish bounded per-channel controls without an audio lock. */
int ts_performance_inputs_set(TsPerformanceInputs *s,const TsPerformanceMixer *m);
void ts_performance_inputs_tuner(TsPerformanceInputs *s,int enabled,unsigned channel);
/* Capture callback: mixing and decimation only; no pitch analysis/allocations. */
void ts_performance_inputs_capture(TsPerformanceInputs *s,const float *input,size_t frames,unsigned channels,unsigned rate,int duplex);
/* Output callback. Duplex mixes the borrowed frame directly, with no FIFO delay. */
void ts_performance_inputs_begin(TsPerformanceInputs *s,int duplex,unsigned channels,unsigned rate);
TsStereoFrame ts_performance_inputs_read(TsPerformanceInputs *s,int duplex,const float *frame,unsigned channels,unsigned rate);
/* UI only. Returns newly consumed samples; never locks the audio callback. */
unsigned ts_performance_inputs_analyze(TsPerformanceInputs *s,TsInputTuner *t);
#endif
