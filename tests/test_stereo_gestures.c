#include "tapesister/sample.h"
#include "tapesister/ui.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char error[256];
#define OK(call) do { if (!(call)) { fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, __LINE__, #call, error); abort(); } } while (0)
#define E error, sizeof(error)
static TsInstrument tape, reopened;
static TsSample source;

static void setup(void)
{
    ts_instrument_init(&tape);
    OK(ts_instrument_import_sample(&tape, &source, 0, 0, 0, TS_LOOP_FORWARD, E));
}
static void frame_is(const TsSample *s, size_t at, float l, float r)
{
    TsStereoFrame v = ts_sample_read_frame(s, at);
    assert(fabsf(v.l-l) < 0.000002f && fabsf(v.r-r) < 0.000002f);
}
static void history(uint64_t original)
{
    uint64_t edited = ts_sample_hash(&tape.current);
    assert(edited != original && tape.current.channels == 2);
    OK(ts_instrument_undo(&tape, E));
    assert(ts_sample_hash(&tape.current) == original);
    OK(ts_instrument_redo(&tape, E));
    assert(ts_sample_hash(&tape.current) == edited && tape.current.channels == 2);
}
static void test_placement(void)
{
    /* Constant, different channels make crossfade edges and linked mix gain
       measurable independently of the renderer. */
    for (size_t i=0; i<source.frames; ++i) {
        source.data[2*i] = .4f; source.data[2*i+1] = -.1f;
    }
    for (int kind=TS_POST_COPY_MIX; kind<=TS_POST_MOVE_OVERWRITE; ++kind) {
        const int64_t targets[] = {1024, 320, -300, 4300};
        for (size_t t=0; t<4; ++t) {
            setup(); uint64_t original=ts_sample_hash(&tape.current);
            ts_instrument_set_selection(&tape, 256, 768);
            OK(ts_instrument_apply_tape_drag(&tape, kind, 256, 768, targets[t], E));
            assert(tape.current.channels == 2);
            assert(tape.current.frames == (t==2 ? 4396u : t==3 ? 4812u : 4096u));
            size_t dest=tape.selection_first;
            assert(tape.selection_last-dest == 512);
            for (size_t i=0; i<tape.current.frames; ++i) {
                TsStereoFrame v=ts_sample_read_frame(&tape.current,i);
                assert(isfinite(v.l) && isfinite(v.r));
                assert(fabsf(v.r + v.l*.25f)<.000002f);
            }
            if(t != 1) frame_is(&tape.current, dest+256, .4f, -.1f);
            if(kind==TS_POST_MOVE_MIX || kind==TS_POST_MOVE_OVERWRITE) {
                size_t cleared=256+(t==2 ? 300 : 0);
                if(t!=1) frame_is(&tape.current, cleared+100, 0, 0);
            }
            assert(tape.undo_count==1);
            history(original);
            OK(ts_instrument_save_recipe(&tape,"test-stereo-gestures.tsr",E));
            ts_instrument_init(&reopened);
            OK(ts_instrument_load_recipe(&reopened,"test-stereo-gestures.tsr",E));
            assert(ts_sample_hash(&reopened.current)==ts_sample_hash(&tape.current));
            ts_instrument_free(&reopened); ts_instrument_free(&tape);
        }
    }
    /* Independent impulses must stay in their own channel across overwrite. */
    memset(source.data,0,source.frames*2*sizeof(float));
    source.data[400*2]=.7f; source.data[520*2+1]=-.6f;
    setup(); OK(ts_instrument_apply_tape_drag(&tape,TS_POST_COPY_OVERWRITE,256,768,1200,E));
    frame_is(&tape.current,tape.selection_first+144,.7f,0);
    frame_is(&tape.current,tape.selection_first+264,0,-.6f);
    uint64_t unchanged=ts_sample_hash(&tape.current); int undo=tape.undo_count;
    assert(!ts_instrument_apply_tape_drag(&tape,TS_POST_COPY_MIX,256,768,INT64_MAX,E));
    assert(!ts_instrument_apply_tape_drag(&tape,TS_POST_COPY_MIX,256,768,INT64_MIN,E));
    assert(ts_sample_hash(&tape.current)==unchanged && tape.undo_count==undo);
    ts_instrument_free(&tape); remove("test-stereo-gestures.tsr");
}
static void make_wave(void)
{
    for(size_t i=0;i<source.frames;++i) {
        float v=(i<64 || i>=4032) ? 0 : .3f*sinf((float)(i-64)*.07f);
        source.data[2*i]=v;source.data[2*i+1]=-v;
    }
}
static void test_boundaries(void)
{
    make_wave(); setup();
    OK(ts_instrument_select_wave(&tape));
    assert(tape.selection_first==65 && tape.selection_last==4032);
    size_t crossing=ts_sample_nearest_zero_crossing(&source,100);
    assert(crossing==109); /* anti-phase is not silence at frame 100 */
    for(size_t i=0;i<4096;++i) source.data[2*i]=0;
    assert(ts_sample_nearest_zero_crossing(&source,100)==109);
    make_wave();
    ts_instrument_set_selection_snapped(&tape,100,700);
    assert(tape.selection_first==crossing);
    size_t old=tape.selection_last;
    OK(ts_instrument_resize_selection(&tape,2,1,2)); assert(tape.selection_last>old);
    OK(ts_instrument_resize_selection(&tape,2,0,2)); assert(tape.selection_last==old);
    tape.has_loop=1;tape.loop_first=109;tape.loop_last=700;
    ts_instrument_begin_loop_drag(&tape);
    assert(ts_instrument_move_loop_endpoint(&tape,2,900)==2);
    assert(tape.loop_last==ts_sample_nearest_zero_crossing(&source,900));
    uint64_t original=ts_sample_hash(&tape.current);
    OK(ts_instrument_zoom_view(&tape,1000,.5f,.5f));
    OK(ts_instrument_pan_view(&tape,100));
    assert(tape.current.channels==2 && ts_sample_hash(&tape.current)==original);
    ts_instrument_set_selection(&tape,300,900);
    OK(ts_instrument_rotate_zero_crossing(&tape,1,2,E));
    for(size_t i=0;i<4096;++i)assert(fabsf(tape.current.data[2*i]+tape.current.data[2*i+1])<.000002f);
    history(original); ts_instrument_free(&tape);
    /* No crossing: the minimum must be found inside the selected stereo range. */
    for(size_t i=0;i<4096;++i) {source.data[2*i]=.4f;source.data[2*i+1]=.2f;}
    source.data[2*700]=.05f;source.data[2*700+1]=.025f;
    setup();ts_instrument_set_selection(&tape,600,800);
    OK(ts_instrument_rotate_zero_crossing(&tape,1,1,E));
    frame_is(&tape.current,600,.05f,.025f);ts_instrument_free(&tape);
}
static void test_edit_gestures(void)
{
    make_wave(); setup(); uint64_t original=ts_sample_hash(&tape.current);
    TsAmplitudeGesture draw;ts_amplitude_gesture_init(&draw);
    OK(ts_instrument_amplitude_gesture_begin(&tape,&draw,E));
    OK(ts_instrument_amplitude_gesture_preview(&tape,&draw,400,0,600,1,E));
    frame_is(&tape.current,400,0,0);
    frame_is(&tape.current,500,source.data[1000]*.5f,source.data[1001]*.5f);
    OK(ts_instrument_amplitude_gesture_cancel(&tape,&draw,E));assert(ts_sample_hash(&tape.current)==original && tape.undo_count==0);
    OK(ts_instrument_amplitude_gesture_begin(&tape,&draw,E));
    OK(ts_instrument_amplitude_gesture_preview(&tape,&draw,400,.5f,600,.5f,E));
    OK(ts_instrument_amplitude_gesture_commit(&tape,&draw,E));history(original);
    OK(ts_instrument_undo(&tape,E));
    ts_instrument_set_selection(&tape,500,1500);
    TsStretchGesture stretch;ts_stretch_gesture_init(&stretch);
    OK(ts_instrument_stretch_gesture_begin(&tape,&stretch,1000,E));
    float pitch;
    OK(ts_instrument_stretch_gesture_preview(&tape,&stretch,1.2f,&pitch,E));
    assert(tape.current.channels==2 && tape.current.frames==4096 && pitch<0);
    for(size_t i=0;i<4096;++i)assert(fabsf(tape.current.data[2*i]+tape.current.data[2*i+1])<.000002f);
    OK(ts_instrument_stretch_gesture_cancel(&tape,&stretch,E));assert(ts_sample_hash(&tape.current)==original);
    OK(ts_instrument_stretch_gesture_begin(&tape,&stretch,1000,E));
    OK(ts_instrument_stretch_gesture_preview(&tape,&stretch,.8f,&pitch,E));
    OK(ts_instrument_stretch_gesture_commit(&tape,&stretch,E));history(original);
    OK(ts_instrument_undo(&tape,E));
    for(int edge=1;edge<=2;++edge) {
        TsCanvasGesture canvas;ts_canvas_gesture_init(&canvas);
        OK(ts_instrument_canvas_gesture_begin(&tape,&canvas,edge,E));
        OK(ts_instrument_canvas_gesture_preview(&tape,&canvas,128,E));
        assert(tape.current.frames==4224 && tape.current.channels==2);
        size_t offset=edge==1 ? 128:0;
        for(size_t i=0;i<4096;++i)
            frame_is(&tape.current,i+offset,source.data[2*i],source.data[2*i+1]);
        OK(ts_instrument_canvas_gesture_cancel(&tape,&canvas,E));assert(ts_sample_hash(&tape.current)==original);
        OK(ts_instrument_canvas_gesture_begin(&tape,&canvas,edge,E));
        OK(ts_instrument_canvas_gesture_preview(&tape,&canvas,-128,E));
        OK(ts_instrument_canvas_gesture_commit(&tape,&canvas,E));history(original);
        OK(ts_instrument_undo(&tape,E));
    }
    ts_instrument_free(&tape);
}
static void test_clipboard(void)
{
    make_wave();
    for(int crop=0;crop<=1;++crop) {
        setup();TsSample clip;ts_sample_init(&clip);size_t origin;
        uint64_t original=ts_sample_hash(&tape.current);
        ts_instrument_set_selection(&tape,500,1500);
        OK(ts_instrument_copy_selection(&tape,&clip,&origin,E));
        assert(clip.channels==2 && clip.frames==1000 && origin==500);
        for(size_t i=0;i<clip.frames;++i)frame_is(&clip,i,source.data[2*(500+i)],source.data[2*(500+i)+1]);
        assert(ts_sample_hash(&tape.current)==original && tape.undo_count==0);
        OK(ts_instrument_cut_selection_mode(&tape,&clip,&origin,crop,E));
        assert(tape.current.channels==2 && tape.current.frames==(crop ? 3096u:4096u));
        if(!crop)frame_is(&tape.current,3500,0,0);
        history(original);OK(ts_instrument_undo(&tape,E));
        ts_instrument_set_selection(&tape,2000,2500);
        OK(ts_instrument_paste(&tape,&clip,origin,1,E));
        assert(tape.current.channels==2 && tape.current.frames==4096);
        for(size_t i=0;i<4096;++i)assert(fabsf(tape.current.data[2*i]+tape.current.data[2*i+1])<.000002f);
        history(original);ts_sample_free(&clip);ts_instrument_free(&tape);
    }
}
#define EFFECT_GESTURE(Name, name) do { \
    Ts##Name##Gesture g;ts_##name##_gesture_init(&g); \
    OK(ts_instrument_##name##_gesture_begin(&tape,&g,E)); \
    OK(ts_instrument_##name##_gesture_preview(&tape,&g,.5f,E)); \
    assert(tape.current.channels==2 && tape.current.frames==4096); \
    OK(ts_instrument_##name##_gesture_cancel(&tape,&g,E)); \
    assert(ts_sample_hash(&tape.current)==original); \
    OK(ts_instrument_##name##_gesture_begin(&tape,&g,E)); \
    OK(ts_instrument_##name##_gesture_preview(&tape,&g,.5f,E)); \
    OK(ts_instrument_##name##_gesture_commit(&tape,&g,E)); \
    history(original); OK(ts_instrument_undo(&tape,E)); \
} while(0)
static void test_material(void)
{
    make_wave();setup();uint64_t original=ts_sample_hash(&tape.current);
    ts_instrument_set_selection(&tape,500,3000);
    EFFECT_GESTURE(Warp,warp);EFFECT_GESTURE(Smear,smear);EFFECT_GESTURE(Tear,tear);
    for(int macro=0;macro<TS_MATERIAL_MACRO_COUNT;++macro) {
        TsMaterialMacroGesture g;ts_material_macro_gesture_init(&g);
        OK(ts_instrument_material_macro_gesture_begin(&tape,&g,macro,E));
        OK(ts_instrument_material_macro_gesture_preview(&tape,&g,.5f,E));
        OK(ts_instrument_material_macro_gesture_cancel(&tape,&g,E));assert(ts_sample_hash(&tape.current)==original);
        OK(ts_instrument_material_macro_gesture_begin(&tape,&g,macro,E));
        OK(ts_instrument_material_macro_gesture_preview(&tape,&g,.5f,E));
        OK(ts_instrument_material_macro_gesture_commit(&tape,&g,E));history(original);
        for(size_t i=0;i<4096;++i) {
            assert(isfinite(tape.current.data[2*i]) && isfinite(tape.current.data[2*i+1]));
            if(i<500 || i>=3000)frame_is(&tape.current,i,source.data[2*i],source.data[2*i+1]);
        }
        OK(ts_instrument_undo(&tape,E));
    }
    ts_instrument_free(&tape);
}
int main(void)
{
    ts_sample_init(&source);source.channels=2;source.frames=4096;source.sample_rate=48000;
    source.data=calloc(source.frames*2,sizeof(float));assert(source.data);
    snprintf(source.name,sizeof(source.name),"STEREO GESTURES");
    test_placement();test_boundaries();test_edit_gestures();test_clipboard();test_material();
    ts_sample_free(&source);puts("Stereo canvas gesture tests passed");return 0;
}
