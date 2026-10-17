#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/performance_input.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static TsPerformanceInputs input;
static TsInputTuner tuner;
static TsPerformanceMixer mixer;
static void initialize(unsigned channels,unsigned rate)
{
    ts_performance_inputs_init(&input);ts_performance_mixer_default(&mixer);
    ts_performance_inputs_prepare(&input,channels,rate,128);
    tuner=(TsInputTuner){0};
}
static void tone(double frequency,unsigned channel,unsigned channels,unsigned rate,int harmonic)
{
    float block[256*TS_PERFORMANCE_INPUTS];
    for(unsigned start=0;start<rate/2;start+=256) {
        for(unsigned f=0;f<256;++f)for(unsigned c=0;c<channels;++c) {
            double phase=6.283185307179586*frequency*(start+f)/rate;
            float v=.3f*(float)sin(phase);
            if(harmonic)v+=.4f*(float)sin(2*phase)+.2f*(float)sin(3*phase);
            block[f*channels+c]=c==channel?v:0;
        }
        ts_performance_inputs_capture(&input,block,256,channels,rate,1);
        ts_performance_inputs_analyze(&input,&tuner);
    }
}
static void pitch_tests(void)
{
    const unsigned rates[]={44100,48000,96000};
    const double frequencies[]={27.5,55,110,146.83238396,220,440,880,1760};
    for(unsigned r=0;r<3;++r)for(unsigned f=0;f<8;++f)for(int harmonic=0;harmonic<2;++harmonic) {
        initialize(26,rates[r]);ts_performance_inputs_tuner(&input,1,25);
        double frequency=frequencies[f]*pow(2,17./1200);
        tone(frequency,25,26,rates[r],harmonic);
        double cents=1200*log2(tuner.reading.frequency/frequency);
        if(!tuner.reading.valid || fabs(cents)>2)
            fprintf(stderr,"pitch: %u Hz input %.5f detected %.5f error %.3f cents harmonic %d\n",rates[r],frequency,tuner.reading.frequency,cents,harmonic);
        assert(tuner.reading.valid && fabs(cents)<2);
        assert(fabs(tuner.reading.cents-17)<2);
    }
    /* Channel changes reject old queued audio; off never enqueues samples. */
    initialize(255,48000);ts_performance_inputs_tuner(&input,1,254);
    tone(220*pow(2,-23./1200),254,255,48000,1);
    assert(tuner.reading.valid && tuner.reading.note==57 && fabsf(tuner.reading.cents+23)<2);
    ts_performance_inputs_tuner(&input,1,0);
    ts_performance_inputs_analyze(&input,&tuner);assert(!tuner.reading.valid);
    tone(440,25,26,96000,0);assert(!tuner.reading.valid);
    ts_performance_inputs_tuner(&input,0,0);
    unsigned before=atomic_load(&input.tuner_write);
    tone(440,0,26,96000,0);assert(atomic_load(&input.tuner_write)==before && !tuner.reading.valid);
    /* A stalled UI drops its old analysis window rather than showing old pitch. */
    initialize(1,48000);ts_performance_inputs_tuner(&input,1,0);
    float stale[1024]={0};
    for(int i=0;i<20;++i)ts_performance_inputs_capture(&input,stale,1024,1,48000,1);
    assert(atomic_load(&input.tuner_overflows)>0);
    assert(!ts_performance_inputs_analyze(&input,&tuner) && !tuner.reading.valid);
    tone(220,0,1,48000,1);assert(tuner.reading.valid && tuner.reading.note==57);
    /* Silence, DC and nonperiodic noise have no confident note. */
    ts_performance_inputs_tuner(&input,1,0);
    float block[256];unsigned random=7;
    for(int kind=0;kind<3;++kind) {
        for(int n=0;n<200;++n) {
            for(int f=0;f<256;++f){random=random*1664525u+1013904223u;block[f]=kind==0?0:kind==1?.3f:((int)(random>>16)-32768)*.00001f;}
            ts_performance_inputs_capture(&input,block,256,1,48000,1);
            ts_performance_inputs_analyze(&input,&tuner);
        }
        assert(!tuner.reading.valid);
    }
}
static void mixer_tests(void)
{
    initialize(26,48000);float frame[26]={0};frame[24]=.25f;frame[25]=-.125f;
    assert(!ts_performance_inputs_set(&input,&mixer));
    ts_performance_inputs_begin(&input,1,26,48000);
    TsStereoFrame out=ts_performance_inputs_read(&input,1,frame,26,48000);assert(out.l==0 && out.r==0);
    mixer.input_enabled[24]=1;mixer.input_gain[24]=.5f;
    assert(ts_performance_inputs_set(&input,&mixer));ts_performance_inputs_begin(&input,1,26,48000);
    for(int i=0;i<512;++i)out=ts_performance_inputs_read(&input,1,frame,26,48000);
    assert(out.l==.125f && out.r==.125f); /* Selected mono input centers. */
    mixer.input_enabled[25]=1;ts_performance_inputs_set(&input,&mixer);ts_performance_inputs_begin(&input,1,26,48000);
    for(int i=0;i<512;++i)out=ts_performance_inputs_read(&input,1,frame,26,48000);
    assert(out.l==.125f && out.r==-.125f); /* Selected physical pair preserves L/R. */
    mixer.input_enabled[24]=mixer.input_enabled[25]=0;ts_performance_inputs_set(&input,&mixer);ts_performance_inputs_begin(&input,1,26,48000);
    TsStereoFrame first=ts_performance_inputs_read(&input,1,frame,26,48000);assert(first.l>0 && first.r<0);
    for(int i=0;i<512;++i)out=ts_performance_inputs_read(&input,1,frame,26,48000);
    assert(!out.l && !out.r);
    ts_performance_inputs_prepare(&input,2,44100,128);ts_performance_inputs_begin(&input,1,2,44100);
    out=ts_performance_inputs_read(&input,1,frame,2,44100);assert(!out.l && !out.r);
    /* Independent capture/output clocks take the same mixer through the FIFO. */
    initialize(2,48000);mixer.input_enabled[0]=1;ts_performance_inputs_set(&input,&mixer);
    float block[1024];for(int i=0;i<512;++i){block[i*2]=.25f;block[i*2+1]=-.5f;}
    double sum=0;
    for(int b=0;b<10;++b) {
        ts_performance_inputs_capture(&input,block,512,2,48000,0);ts_performance_inputs_begin(&input,0,2,48000);
        for(int i=0;i<512;++i){out=ts_performance_inputs_read(&input,0,NULL,0,48000);assert(out.l==out.r);sum+=fabs(out.l);}
    }
    assert(sum>100);
    ts_performance_inputs_prepare(&input,0,48000,0);out=ts_performance_inputs_read(&input,0,NULL,0,48000);assert(!out.l && !out.r);
}
int main(void)
{
    clock_t start=clock();pitch_tests();mixer_tests();
    printf("Input mixing, stereo pairs, slew, device changes, tuner accuracy and off/no-signal passed (%.3f CPU s).\n",(double)(clock()-start)/CLOCKS_PER_SEC);
    return 0;
}
