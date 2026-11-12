#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/sister_runtime.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TsSisterRuntime scalar,span;
static int workers;
static TsSisterSourceFrames sources[TS_MATRIX_DELAY];
static TsSisterRuntimeFrame actual[TS_MATRIX_DELAY],expected[TS_MATRIX_DELAY];
static TsMatrixSpan scratch;
static TsMatrixOutput reference[TS_MATRIX_DELAY];
static float peaks[TS_MATRIX_DELAY],reference_peaks[TS_MATRIX_DELAY];
static void configure(TsMatrixControls *c)
{
    TsSisterRuntime *lanes[]={&scalar,&span};
    for(int i=0;i<2;++i) {
        ts_sunvox_workers_configure(i?workers:0);
        TsMatrixEffectPlan plan;
        ts_matrix_effects_collect(&lanes[i]->matrix_effects,&plan);ts_matrix_effects_discard(&plan);
        assert(ts_matrix_effects_stage(&lanes[i]->matrix_effects,c,48000,&plan));
        ts_matrix_effects_commit(&lanes[i]->matrix_effects,c,48000,&plan);ts_matrix_effects_discard(&plan);
        assert(ts_matrix_set(&lanes[i]->matrix,c,48000));
    }
}
static void equal_graph(void)
{
    assert(span.matrix.position==scalar.matrix.position);
    assert(!memcmp(span.matrix.output,scalar.matrix.output,sizeof(span.matrix.output)));
    assert(!memcmp(span.matrix.input_peak,scalar.matrix.input_peak,sizeof(span.matrix.input_peak)));
    assert(!memcmp(span.matrix.output_peak,scalar.matrix.output_peak,sizeof(span.matrix.output_peak)));
    assert(!memcmp(span.matrix.history,scalar.matrix.history,sizeof(span.matrix.history)));
    assert(!memcmp(span.matrix.voice,scalar.matrix.voice,sizeof(span.matrix.voice)));
    assert(!memcmp(&span.matrix.sidechain,&scalar.matrix.sidechain,sizeof(span.matrix.sidechain)));
}
int main(void)
{
    for(workers=0;workers<2;++workers) {
    ts_sister_runtime_init(&scalar);ts_sister_runtime_init(&span);char error[256];
    assert(ts_sister_runtime_reconfigure(&scalar,48000,2,error,sizeof(error)));
    assert(ts_sister_runtime_reconfigure(&span,48000,2,error,sizeof(error)));
    static const char amp[]="SSYN\0\0\0\0STYP\x0a\0\0\0Amplifier\0SEND\0\0\0\0";
    static const char reverb[]="SSYN\0\0\0\0STYP\x07\0\0\0Reverb\0SEND\0\0\0\0";
    const char *patch=workers?reverb:amp;size_t patch_size=workers?sizeof(reverb)-1:sizeof(amp)-1;
    FILE *f=fopen("span.sunsynth","wb");assert(f);assert(fwrite(patch,1,patch_size,f)==patch_size);assert(!fclose(f));
    TsSunVoxControls sv;assert(ts_sunvox_import(&sv,"span.sunsynth"));remove("span.sunsynth");
    TsMatrixControls c;ts_matrix_default(&c);c.enabled=1;c.feedback=1;
    for(int i=0;i<TS_MATRIX_ROWS;++i)c.row[i]=(TsSourceRoute){.matrix_enabled=1};
    for(int i=0;i<12;++i) {
        int type=i<10?i+1:TS_MATRIX_FX_SUNVOX;
        assert(ts_matrix_effect_add(&c,type)==i);
        if(type==TS_MATRIX_FX_SUNVOX){c.effect[i].sunvox=sv;c.effect[i].sunvox.quantum=workers?(i==10?256:1024):(i==10?64:256);}
        int n=TS_MATRIX_EFFECT_BASE+i;
        c.row[TS_MATRIX_MOSAIC].matrix_gain[n]=700;
        c.row[n].matrix_gain[TS_MATRIX_EQ]=200;
        c.row[n].matrix_gain[ts_matrix_output_destination(127-i)]=100;
        if(i)c.row[n-1].matrix_gain[n]=400;
    }
    c.row[TS_MATRIX_EQ].matrix_gain[TS_MATRIX_LIMITER]=1000;
    c.row[TS_MATRIX_LIMITER].matrix_gain[ts_matrix_output_destination(0)]=1000;
    c.row[ts_matrix_input_row(127)].matrix_gain[TS_MATRIX_COMPRESSOR]=300;
    c.row[TS_MATRIX_MOSAIC].matrix_gain[TS_MATRIX_SIDECHAIN]=500;
    c.row[TS_MATRIX_COMPRESSOR].matrix_gain[TS_MATRIX_AMBISONICS]=200;
    c.row[TS_MATRIX_EQ].matrix_gain[TS_MATRIX_SPEAKER_BASE+15]=500;
    assert(ts_matrix_connect(&c,TS_MATRIX_EFFECT_BASE+3,TS_MATRIX_EFFECT_BASE,120)==1);
    atomic_store(&scalar.insert.matrix_channels,255);atomic_store(&span.insert.matrix_channels,255);
    configure(&c);unsigned block_count=0,fallback_count=0;
    ts_sister_runtime_begin_audio_block(&scalar);ts_sister_runtime_begin_audio_block(&span);
    for(unsigned at=0;at<32768;) {
        if(at==8192){c.row[TS_MATRIX_MOSAIC].pan=31;configure(&c);}
        if(at==16384){c.effect[2].enabled=0;c.effect[10].sunvox.quantum=1024;configure(&c);}
        if(at==24576){ts_matrix_effect_remove(&c,1);configure(&c);}
        unsigned limit=8192-at%8192,request=(unsigned[]){1,7,17,63,64}[at%5];
        if(request>limit)request=limit;
        unsigned count=ts_sister_runtime_matrix_span_limit(&span,request);
        for(unsigned j=0;j<count;++j) {
            TsSisterSourceFrames *s=&sources[j];memset(s,0,sizeof(*s));s->matrix_program_gain=.8f;
            unsigned t=at+j;
            if(t<20000) {
                s->mosaic=(TsStereoFrame){.12f*sinf(t*.031f),.1f*cosf(t*.017f)};
                s->matrix_input[127]=(TsStereoFrame){.02f,-.03f};
                s->matrix_graph[TS_MATRIX_MAIN]=(TsStereoFrame){.002f,-.001f};
            }
            if(t%331==0)s->matrix_input[0]=(TsStereoFrame){NAN,INFINITY};
            expected[j]=ts_sister_runtime_process_frame(&scalar,s);
            memcpy(reference[j].output,scalar.matrix.output,sizeof(reference[j].output));
            memcpy(reference[j].speaker_output,scalar.matrix.speaker_output,sizeof(reference[j].speaker_output));
            reference[j].spatial_input=scalar.matrix.spatial_input;
            reference_peaks[j]=scalar.limiter_input_peak;
        }
        if(count>1) {
            ++block_count;ts_sister_runtime_matrix_span(&span,sources,actual,peaks,&scratch,count);
            assert(!memcmp(reference,scratch.result,count*sizeof(reference[0])));
            assert(!memcmp(reference_peaks,peaks,count*sizeof(peaks[0])));
        } else {++fallback_count;actual[0]=ts_sister_runtime_process_frame(&span,&sources[0]);}
        assert(!memcmp(expected,actual,count*sizeof(actual[0])));equal_graph();at+=count;
    }
    assert(block_count>100 && fallback_count>100);
    if(workers)assert(span.matrix_effects.worker && ts_sunvox_batch_dispatched(span.matrix_effects.worker)>0);
    span.enabled=1;assert(ts_sister_runtime_matrix_span_limit(&span,64)==1);span.enabled=0;
    span.matrix.controls.serial_mask=1;assert(ts_sister_runtime_matrix_span_limit(&span,64)==1);span.matrix.controls.serial_mask=0;
    span.post_fx.master_engage.remaining=32;assert(ts_sister_runtime_matrix_span_limit(&span,64)==1);
    ts_sister_runtime_end_audio_block(&scalar);ts_sister_runtime_end_audio_block(&span);
    ts_sister_runtime_free(&scalar);ts_sister_runtime_free(&span);
    printf("Exact matrix spans: %u spans, %u chronological transitions; all FX types, mixed quanta, feedback, sidechain, high ports and tails\n",block_count,fallback_count);
    }
    ts_sunvox_workers_configure(0);
}
