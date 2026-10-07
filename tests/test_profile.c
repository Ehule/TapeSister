#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/profile.h"
#include "tapesister/ui_schedule.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint64_t now=100;
static uint64_t clock_now(void) {return now;}
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
    char report[8192];ts_profile_report(report,sizeof(report));assert(strstr(report,"avg ms") && strstr(report,"Present / swap"));
    char tiny[8];memset(tiny,'!',sizeof(tiny));ts_profile_report(tiny,sizeof(tiny));assert(tiny[7]==0);
    assert(ts_ui_refresh_ms(0,1,1)==100);assert(ts_ui_refresh_ms(1,0,0)==100);
    assert(ts_ui_refresh_ms(1,0,1)==33);assert(ts_ui_refresh_ms(1,1,1)==16);
    assert(!ts_ui_refresh_due(99,0,100,0));assert(ts_ui_refresh_due(100,0,100,0));
    assert(ts_ui_refresh_due(1,0,100,1));assert(ts_ui_refresh_due(20,UINT32_MAX-10,30,0));
    puts("Profile snapshots, sampling, reset, stop and scheduling passed");return 0;
}
