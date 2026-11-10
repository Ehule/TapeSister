#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/profile.h"
#include "tapesister/ui_schedule.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint64_t now=100, clock_reads;
static uint64_t clock_now(void) {++clock_reads;return now;}
static void effect_blocks(void)
{
    TsProfileEffectIdentity a={.generation=1,.id=21,.type=3};
    TsProfileEffectIdentity b={.generation=2,.id=22,.type=4};
    ts_profile_reset();
    for(unsigned block=0;block<1024;++block) {
        ts_profile_lane_begin(1);uint64_t cb=ts_profile_begin(TS_PROF_AUDIO);
        for(unsigned frame=0;frame<512;++frame) {
            uint64_t t=ts_profile_effect_begin(0);now+=2;
            ts_profile_effect_end(0,&a,t,block==16);
            t=ts_profile_effect_begin(15);now+=frame%256==0?100:1;
            ts_profile_effect_end(15,&b,t,0);
        }
        ts_profile_end(TS_PROF_AUDIO,cb);ts_profile_lane_end(1);
    }
    TsProfileSnapshot s;ts_profile_snapshot(&s);
    assert(s.effect[0].samples==32 && s.effect[15].samples==32);
    assert(s.effect[0].transitions==1 && !s.effect[15].transitions);
    assert(s.effect[0].blocks.calls==1024 && s.effect[0].blocks.peak==1024);
    assert(s.effect[15].blocks.peak==710); /* Two render bursts in one callback. */
    assert(s.effect[0].blocks.ticks==1024*1024);
    assert(s.effect[15].blocks.ticks==1024*710);
    assert(s.value[TS_PROF_MATRIX_FX].ticks==s.effect[0].blocks.ticks+s.effect[15].blocks.ticks);
    assert(s.value[TS_PROF_MATRIX_FX].ticks==s.value[TS_PROF_AUDIO].ticks);
    /* Same saved ID, new physical engine: a fresh row, no old peak/preset.
       The other slot retains its own identity and history. */
    a.generation=3;a.type=2;a.patch=123;
    uint64_t t=0;
    for(int block=0;block<32 && !t;++block) {
        ts_profile_lane_begin(1);t=ts_profile_effect_begin(0);
        now+=9;ts_profile_effect_end(0,&a,t,0);ts_profile_lane_end(1);
    }
    assert(t);
    ts_profile_snapshot(&s);
    assert(s.effect[0].identity.generation==3 && s.effect[0].identity.patch==123);
    assert(s.effect[0].samples==1 && !s.effect[0].transitions && s.effect[0].blocks.peak==9);
    assert(s.effect[15].samples==32 && s.effect[15].identity.id==22);
    uint64_t last=s.effect[15].last;
    now+=500;ts_profile_snapshot(&s);assert(s.effect[15].last==last && s.now>last);
    ts_profile_reset();ts_profile_snapshot(&s);
    assert(!s.effect[0].samples && !s.effect[15].identity.generation);
    /* All 16 slots fit, including the last one; bounded report writes. */
    for(int block=0;block<32;++block) {
        ts_profile_lane_begin(1);
        for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i) {
            a.id=(uint32_t)i+1;a.generation=(uint64_t)i+100;
            t=ts_profile_effect_begin(i);now+=1;
            ts_profile_effect_end(i,&a,t,0);
        }
        ts_profile_lane_end(1);
    }
    char report[16384];ts_profile_report(report,sizeof(report));
    assert(strstr(report,"#16 DELAY") && strstr(report,"peak ms/block"));
    ts_profile_enable(0);ts_profile_lane_begin(1);
    assert(!ts_profile_begin(TS_PROF_MATRIX_FX));
    ts_profile_effect_end(0,&a,0,0);ts_profile_lane_end(1);ts_profile_snapshot(&s);
    assert(s.effect[0].samples==1);
    ts_profile_enable(1);
}
static void staggered_clock_work(void)
{
    ts_profile_reset();
    TsProfileEffectIdentity identity={.generation=777,.id=1,.type=3};
    for(int block=0;block<1024;++block) {
        ts_profile_lane_begin(1);uint64_t before=clock_reads;
        uint64_t callback=ts_profile_begin(TS_PROF_AUDIO);
        for(int frame=0;frame<512;++frame) {
            for(int id=TS_PROF_VOICES;id<TS_PROF_MATRIX_FX;++id) {
                uint64_t start=ts_profile_begin((TsProfileId)id);now+=1;
                ts_profile_end((TsProfileId)id,start);
            }
            for(int slot=0;slot<TS_MATRIX_EFFECT_CAPACITY;++slot) {
                uint64_t start=ts_profile_effect_begin(slot);now+=1;
                ts_profile_effect_end(slot,&identity,start,0);
            }
        }
        ts_profile_end(TS_PROF_AUDIO,callback);ts_profile_lane_end(1);
        // At most one per-frame timer pair, plus the callback timer pair.
        assert(clock_reads-before<=512*2+2);
    }
    TsProfileSnapshot s;ts_profile_snapshot(&s);
    for(int id=TS_PROF_VOICES;id<TS_PROF_MATRIX_FX;++id)
        assert(s.value[id].calls==1024*512);
    for(int slot=0;slot<TS_MATRIX_EFFECT_CAPACITY;++slot) {
        assert(s.effect[slot].samples==32);
        assert(s.effect[slot].blocks.calls==1024);
        assert(s.effect[slot].blocks.peak==512);
    }
}
int main(void)
{
    assert(ts_profile_init(clock_now,1000000));
    assert(!ts_profile_enabled());ts_profile_enable(1);
    ts_profile_lane_begin(0);uint64_t ui=ts_profile_begin(TS_PROF_UI),paint=ts_profile_begin(TS_PROF_RENDER);
    now+=2000;ts_profile_end(TS_PROF_RENDER,paint);now+=1000;ts_profile_end(TS_PROF_UI,ui);
    ts_profile_lane_end(0);
    for(int i=0;i<32;i++) {
        ts_profile_lane_begin(1);uint64_t cb=ts_profile_begin(TS_PROF_AUDIO),dsp=ts_profile_begin(TS_PROF_PRISM);
        now+=100;ts_profile_end(TS_PROF_PRISM,dsp);now+=200;
        ts_profile_end(TS_PROF_AUDIO,cb);ts_profile_lane_end(1);
    }
    TsProfileSnapshot s;ts_profile_snapshot(&s);
    assert(s.value[TS_PROF_UI].ticks==3000 && s.value[TS_PROF_RENDER].ticks==2000);
    assert(s.value[TS_PROF_AUDIO].calls==32 && s.value[TS_PROF_AUDIO].ticks==9600);
    assert(s.value[TS_PROF_PRISM].calls==32 && s.value[TS_PROF_PRISM].ticks==3200);
    assert(s.value[TS_PROF_PRISM].peak==100);
    ts_profile_enable(0);double elapsed=s.seconds;now+=1000000;ts_profile_snapshot(&s);
    assert(s.seconds==elapsed);ts_profile_lane_begin(1);assert(!ts_profile_begin(TS_PROF_PRISM));
    ts_profile_enable(1);ts_profile_snapshot(&s);
    assert(!s.value[TS_PROF_AUDIO].calls && !s.value[TS_PROF_RENDER].calls);
    ts_profile_lane_begin(0);ui=ts_profile_begin(TS_PROF_UI);now+=400;ts_profile_end(TS_PROF_UI,ui);ts_profile_lane_end(0);
    ts_profile_snapshot(&s);assert(s.value[TS_PROF_UI].ticks==400 && !s.value[TS_PROF_AUDIO].calls);
    /* Periodic render work must be represented at every phase, including the
       opposite parity to the first callback. Check weighted totals and peaks. */
    for (unsigned phase=0;phase<32;++phase) {
        ts_profile_reset();
        for (unsigned block=0;block<1024;++block) {
            ts_profile_lane_begin(1);
            uint64_t start=ts_profile_begin(TS_PROF_PRISM);
            now += (block%32==phase) ? 500 : 10;
            ts_profile_end(TS_PROF_PRISM,start);ts_profile_lane_end(1);
        }
        ts_profile_snapshot(&s);
        assert(s.value[TS_PROF_PRISM].calls==1024);
        assert(s.value[TS_PROF_PRISM].ticks==32*(500+31*10));
        assert(s.value[TS_PROF_PRISM].peak==500);
    }
    effect_blocks();
    staggered_clock_work();
    char report[8192];ts_profile_report(report,sizeof(report));assert(strstr(report,"avg ms") && strstr(report,"Present / swap"));
    assert(strstr(report,"Matrix effects DSP"));
    char tiny[8];memset(tiny,'!',sizeof(tiny));ts_profile_report(tiny,sizeof(tiny));assert(tiny[7]==0);
    assert(ts_ui_refresh_ms(0,1,1)==100);assert(ts_ui_refresh_ms(1,0,0)==100);
    assert(ts_ui_refresh_ms(1,0,1)==33);assert(ts_ui_refresh_ms(1,1,1)==16);
    assert(!ts_ui_refresh_due(99,0,100,0));assert(ts_ui_refresh_due(100,0,100,0));
    assert(ts_ui_refresh_due(1,0,100,1));assert(ts_ui_refresh_due(20,UINT32_MAX-10,30,0));
    puts("Profile snapshots, sampling, reset, stop and scheduling passed");return 0;
}
