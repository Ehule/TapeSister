#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/profile.h"
#include "tapesister/matrix_effects.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t tick=100, reads;
static uint64_t clock_now(void) { ++reads; return ++tick; }
static TsMatrixControls controls;
static TsMatrixEffects bank;

int main(void)
{
    /* Deterministic SunVox processor with a known, retained display name. */
    static const char patch[]="SSYN\0\0\0\0STYP\x0a\0\0\0Amplifier\0"
        "SNAM\x0e\0\0\0Profile probe\0SEND\0\0\0\0";
    FILE *f=fopen("profile-matrix.sunsynth","wb");assert(f);
    assert(fwrite(patch,1,sizeof(patch)-1,f)==sizeof(patch)-1);assert(!fclose(f));
    ts_matrix_default(&controls);controls.enabled=1;
    assert(ts_matrix_effect_add(&controls,TS_SISTER_FX_DISTORTION)==0);
    assert(ts_matrix_effect_add(&controls,TS_SISTER_FX_GRAIN)==1);
    assert(ts_matrix_effect_add(&controls,TS_SISTER_FX_REVERB)==2);
    assert(ts_matrix_effect_add(&controls,TS_MATRIX_FX_SUNVOX)==3);
    controls.effect[15]=controls.effect[3];memset(&controls.effect[3],0,sizeof(controls.effect[3]));
    assert(ts_sunvox_import(&controls.effect[15].sunvox,"profile-matrix.sunsynth"));
    remove("profile-matrix.sunsynth");
    assert(ts_profile_init(clock_now,1000000));
    enum {BLOCKS=128, FRAMES=512};
    TsStereoFrame *reference=malloc(BLOCKS*FRAMES*sizeof(*reference));assert(reference);
    const int slots[]={0,1,2,15};
    uint64_t original_generation=0;
    for(int profiled=0;profiled<2;++profiled) {
        controls.effect[15].sunvox.quantum=256;
        ts_matrix_effects_init(&bank);assert(ts_matrix_effects_restore(&bank,&controls,48000));
        ts_profile_enable(profiled);
        uint64_t before=reads;
        for(int block=0;block<BLOCKS;++block) {
            if(block==64) {
                if(profiled) {
                    TsProfileSnapshot s;ts_profile_snapshot(&s);
                    assert(s.effect[15].samples==2 && s.effect[15].identity.quantum==256);
                    original_generation=s.effect[15].identity.generation;
                }
                /* Same saved ID, replacement engine and a lifecycle fade. */
                controls.effect[15].sunvox.quantum=1024;
                TsMatrixEffectPlan plan;
                assert(ts_matrix_effects_stage(&bank,&controls,48000,&plan));
                ts_matrix_effects_commit(&bank,&controls,48000,&plan);
                ts_matrix_effects_discard(&plan);
            }
            ts_profile_lane_begin(1);uint64_t cb=ts_profile_begin(TS_PROF_AUDIO);
            for(int frame=0;frame<FRAMES;++frame) {
                int n=block*FRAMES+frame;
                TsStereoFrame out={.2f*sinf(n*.02f),.15f*cosf(n*.023f)};
                ts_matrix_effects_tick(&bank);
                for(int i=0;i<4;++i)out=ts_matrix_effects_process(&bank,slots[i],out,(TsStereoFrame){0,0},1);
                if(profiled)assert(!memcmp(&out,&reference[n],sizeof(out)));
                else reference[n]=out;
            }
            ts_profile_end(TS_PROF_AUDIO,cb);ts_profile_lane_end(1);
        }
        if(!profiled)assert(reads==before); /* No performance-clock reads when OFF. */
        else {
            TsProfileSnapshot s;ts_profile_snapshot(&s);
            assert(s.effect[15].samples==2 && s.effect[15].transitions==1);
            assert(s.effect[15].identity.generation!=original_generation);
            assert(s.effect[15].identity.id==controls.effect[15].id && s.effect[15].identity.quantum==1024);
            for(int i=0;i<3;++i)assert(s.effect[i].samples==4);
            /* 4 sampled callbacks, 4 effects, 512 frames: exactly the existing
               two reads per frame, plus callback pairs and two snapshots. */
            assert(reads-before==4*4*FRAMES*2+BLOCKS*2+2);
            char report[16384];ts_profile_report(report,sizeof(report));
            assert(strstr(report,"#4 SUNVOX Profile probe (1024 frames)"));
            assert(strstr(report,"crossfade samples"));
        }
        ts_profile_enable(0);ts_matrix_effects_free(&bank);
    }
    free(reference);
    puts("Matrix profiling: 65536 stereo frames identical OFF/ON, including SunVox replacement; clock count and identities passed");
    return 0;
}
