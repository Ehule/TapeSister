#include "tapesister/ui.h"
#include "tapesister/render_damage.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fill_sample(TsSample *sample, size_t frames, float phase)
{
    sample->data = malloc(frames * sizeof(*sample->data));
    assert(sample->data != NULL);
    sample->frames = frames;
    sample->sample_rate = 48000u;
    for (size_t i = 0; i < frames; ++i)
        sample->data[i] = sinf((float)i * 0.013f + phase);
    ts_sample_touch(sample);
}

static void check_native_cache(TsUiState *ui, TsInstrument *instrument,
                               TsUiWaveformDetail *warm, int width, int height)
{
    static TsFramebuffer frame, reference;
    TsUiWaveformDetail cold={0};
    assert(ts_ui_waveform_detail_resize(warm,width,height));
    assert(ts_ui_waveform_detail_resize(&cold,width,height));
    size_t bytes=(size_t)width*height*sizeof(uint32_t);
    uint32_t *previous=malloc(bytes);assert(previous);
    int had_pixels=warm->valid;
    if(had_pixels)memcpy(previous,warm->pixels,bytes);
    ts_ui_waveform_detail_begin(warm);ts_ui_render(&frame,ui,instrument);
    assert(ts_ui_waveform_detail_finish(warm,&frame));
    ts_ui_waveform_detail_begin(&cold);ts_ui_render(&reference,ui,instrument);
    assert(ts_ui_waveform_detail_finish(&cold,&reference));
    assert(!memcmp(frame.pixels,reference.pixels,sizeof(frame.pixels)));
    assert(!memcmp(warm->pixels,cold.pixels,bytes));
    if(had_pixels)for(int y=0;y<height;++y)for(int x=0;x<width;++x)
        if(x<warm->dirty_x || x>=warm->dirty_x+warm->dirty_w ||
           y<warm->dirty_y || y>=warm->dirty_y+warm->dirty_h)
            assert(previous[(size_t)y*width+x]==warm->pixels[(size_t)y*width+x]);
    free(previous);ts_ui_waveform_detail_free(&cold);
}

static void test_native_raster_cache(TsUiState *ui, TsInstrument *instrument)
{
    TsUiWaveformDetail warm={0};
    ui->playback_active=1;ui->playhead_source=TS_AUDITION_CURRENT;
    ui->playhead_frames=instrument->current.frames;
    const int dimensions[][2]={{600,134},{1200,343},{997,211},{2400,536},{399,89}};
    for(unsigned size=0;size<sizeof(dimensions)/sizeof(dimensions[0]);++size) {
        if(size==1) {
            TsSample *s=&instrument->current;
            float *stereo=malloc(s->frames*2*sizeof(float));assert(stereo);
            for(size_t i=0;i<s->frames;++i) {
                stereo[2*i]=s->data[i];stereo[2*i+1]=.4f*cosf(i*.07f);
            }
            free(s->data);s->data=stereo;s->channels=2;ts_sample_touch(s);
        }
        int w=dimensions[size][0],h=dimensions[size][1];
        check_native_cache(ui,instrument,&warm,w,h);
        uint64_t rasters=warm.raster_count;
        for(int i=0;i<6;++i) {
            ui->playhead_frame=(i*1733u)%instrument->current.frames;
            check_native_cache(ui,instrument,&warm,w,h);
            assert(warm.raster_count==rasters);
        }
        check_native_cache(ui,instrument,&warm,w,h);
        assert(!warm.dirty_w && !warm.dirty_h);
        instrument->has_selection=1;instrument->selection_first=1037;instrument->selection_last=4117;
        check_native_cache(ui,instrument,&warm,w,h);
        instrument->has_loop=1;instrument->loop_first=1539;instrument->loop_last=8153;
        check_native_cache(ui,instrument,&warm,w,h);
        instrument->has_selection=instrument->has_loop=0;
        check_native_cache(ui,instrument,&warm,w,h);
        uint64_t analyses=warm.analysis_count;
        instrument->current.data[0]=-instrument->current.data[0]+.1f;
        ts_sample_touch(&instrument->current);
        check_native_cache(ui,instrument,&warm,w,h);
        assert(warm.analysis_count==analyses+1);
        for(int mode=0;mode<TS_WAVEFORM_DISPLAY_COUNT;++mode) {
            ui->config.waveform_display_mode=mode;
            check_native_cache(ui,instrument,&warm,w,h);
        }
        ui->palette.colors[TS_PALETTE_STEREO_WAVE_LEFT]^=0x00202020u;
        ui->palette.colors[TS_PALETTE_STEREO_WAVE_SUM]^=0x00202020u;
        check_native_cache(ui,instrument,&warm,w,h);
        /* Below one source frame per pixel, then return to the full sample. */
        instrument->view_first=100;instrument->view_last=200;
        /* A render may be skipped by the presenter; its pending full upload
           must survive a subsequent cache hit. */
        static TsFramebuffer skipped;
        ts_ui_waveform_detail_begin(&warm);ts_ui_render(&skipped,ui,instrument);
        check_native_cache(ui,instrument,&warm,w,h);
        instrument->view_first=0;instrument->view_last=instrument->current.frames;
        check_native_cache(ui,instrument,&warm,w,h);
    }
    ts_ui_waveform_detail_free(&warm);
}

int main(void)
{
    TsInstrument instrument;
    TsUiState ui;
    TsSample preview;
    TsSample drone;
    TsFramebuffer *framebuffer = malloc(sizeof(*framebuffer));
    TsFramebuffer *previous = malloc(sizeof(*previous));
    TsRenderDamagePlan damage;
    uint64_t main_rebuilds;
    uint64_t transform_rebuilds;
    uint64_t drone_rebuilds;
    assert(framebuffer != NULL && previous != NULL);
    ts_instrument_init(&instrument);
    ts_ui_init(&ui);
    ts_sample_init(&preview);
    ts_sample_init(&drone);
    fill_sample(&instrument.current, 12000u, 0.0f);
    instrument.view_first = 0u;
    instrument.view_last = instrument.current.frames;
    test_native_raster_cache(&ui,&instrument);

    ts_ui_waveform_cache_reset_counters();
    ts_ui_render(framebuffer, &ui, &instrument);
    main_rebuilds = ts_ui_waveform_cache_rebuild_count(TS_UI_WAVEFORM_MAIN);
    assert(main_rebuilds == 1u);

    ui.playback_active = 1;
    ui.playhead_source = TS_AUDITION_CURRENT;
    ui.playhead_frames = instrument.current.frames;
    ui.playhead_frame = 3000u;
    ts_ui_render(framebuffer, &ui, &instrument);
    assert(ts_ui_waveform_cache_rebuild_count(TS_UI_WAVEFORM_MAIN) ==
           main_rebuilds);
    memcpy(previous, framebuffer, sizeof(*previous));
    ui.playhead_frame = 4000u;
    ts_ui_render(framebuffer, &ui, &instrument);
    assert(ts_render_damage_plan(framebuffer->pixels, previous->pixels,
                                 TS_UI_WIDTH, TS_UI_HEIGHT, &damage));
    assert(!damage.full_frame);
    assert(damage.damaged_pixels <
           (size_t)TS_UI_WIDTH * TS_UI_HEIGHT / 10u);
    printf("Actual UI playhead damage: %.2f%% of full frame\n",
           100.0 * (double)damage.damaged_pixels /
           (double)((size_t)TS_UI_WIDTH * TS_UI_HEIGHT));
    assert(ts_ui_waveform_cache_rebuild_count(TS_UI_WAVEFORM_MAIN) ==
           main_rebuilds);

    instrument.has_selection = 1;
    instrument.selection_first = 1000u;
    instrument.selection_last = 3000u;
    instrument.has_loop = 1;
    instrument.loop_first = 2000u;
    instrument.loop_last = 5000u;
    ts_ui_render(framebuffer, &ui, &instrument);
    assert(ts_ui_waveform_cache_rebuild_count(TS_UI_WAVEFORM_MAIN) ==
           main_rebuilds);

    instrument.current.data[0] = 0.75f;
    ts_sample_touch(&instrument.current);
    ts_ui_render(framebuffer, &ui, &instrument);
    assert(ts_ui_waveform_cache_rebuild_count(TS_UI_WAVEFORM_MAIN) ==
           ++main_rebuilds);
    instrument.view_first = 500u;
    instrument.view_last = 7500u;
    ts_ui_render(framebuffer, &ui, &instrument);
    assert(ts_ui_waveform_cache_rebuild_count(TS_UI_WAVEFORM_MAIN) ==
           ++main_rebuilds);
    assert(ts_instrument_pan_view(&instrument, 250));
    ts_ui_render(framebuffer, &ui, &instrument);
    assert(ts_ui_waveform_cache_rebuild_count(TS_UI_WAVEFORM_MAIN) ==
           ++main_rebuilds);

    fill_sample(&instrument.parent, 16000u, 0.2f);
    ui.audition_source = TS_AUDITION_PARENT;
    ts_ui_reset_parent_view(&ui, instrument.parent.frames);
    ts_ui_render(framebuffer, &ui, &instrument);
    assert(ts_ui_waveform_cache_rebuild_count(TS_UI_WAVEFORM_MAIN) ==
           ++main_rebuilds);
    fill_sample(&instrument.bank[3].sample, 6000u, 0.6f);
    instrument.bank[3].occupied = 1;
    ui.bank_view_slot = 3;
    ts_ui_render(framebuffer, &ui, &instrument);
    assert(ts_ui_waveform_cache_rebuild_count(TS_UI_WAVEFORM_MAIN) ==
           ++main_rebuilds);
    ui.bank_view_slot = -1;
    ui.audition_source = TS_AUDITION_CURRENT;
    ts_ui_render(framebuffer, &ui, &instrument);
    assert(ts_ui_waveform_cache_rebuild_count(TS_UI_WAVEFORM_MAIN) ==
           ++main_rebuilds);

    fill_sample(&preview, 2000u, 0.4f);
    ui.transform_open = 1;
    ui.transform_preview_sample = &preview;
    ui.transform_preview_first = 1500u;
    ui.transform_preview_last = 3500u;
    ui.transform_preview_available = 1;
    ts_ui_waveform_cache_invalidate(&ui, TS_UI_WAVEFORM_TRANSFORM);
    ts_ui_render(framebuffer, &ui, &instrument);
    transform_rebuilds =
        ts_ui_waveform_cache_rebuild_count(TS_UI_WAVEFORM_TRANSFORM);
    assert(transform_rebuilds == 1u);
    ui.playhead_frame += 100u;
    ts_ui_render(framebuffer, &ui, &instrument);
    assert(ts_ui_waveform_cache_rebuild_count(TS_UI_WAVEFORM_TRANSFORM) ==
           transform_rebuilds);
    instrument.selection_first += 10u;
    instrument.selection_last += 20u;
    ts_ui_render(framebuffer, &ui, &instrument);
    assert(ts_ui_waveform_cache_rebuild_count(TS_UI_WAVEFORM_TRANSFORM) ==
           transform_rebuilds);
    ui.transform_preview_sample = NULL;
    ui.transform_preview_available = 0;
    ts_ui_waveform_cache_invalidate(&ui, TS_UI_WAVEFORM_TRANSFORM);
    ts_ui_render(framebuffer, &ui, &instrument);
    assert(ts_ui_waveform_cache_rebuild_count(TS_UI_WAVEFORM_TRANSFORM) ==
           ++transform_rebuilds);

    fill_sample(&drone, 4096u, 0.8f);
    ui.transform_open = 0;
    ui.drone_open = 1;
    ui.drone_preview_sample = &drone;
    ui.drone_output_frames = drone.frames;
    ts_ui_waveform_cache_invalidate(&ui, TS_UI_WAVEFORM_DRONE);
    ts_ui_render(framebuffer, &ui, &instrument);
    drone_rebuilds = ts_ui_waveform_cache_rebuild_count(TS_UI_WAVEFORM_DRONE);
    assert(drone_rebuilds == 1u);
    ui.drone_preview_active = 1;
    ts_ui_render(framebuffer, &ui, &instrument);
    assert(ts_ui_waveform_cache_rebuild_count(TS_UI_WAVEFORM_DRONE) ==
           drone_rebuilds);
    drone.data[0] = -0.5f;
    ts_sample_touch(&drone);
    ts_ui_render(framebuffer, &ui, &instrument);
    assert(ts_ui_waveform_cache_rebuild_count(TS_UI_WAVEFORM_DRONE) ==
           ++drone_rebuilds);

    ts_sample_free(&drone);
    ts_sample_free(&preview);
    ts_instrument_free(&instrument);
    free(previous);
    free(framebuffer);
    puts("UI waveform cache integration tests passed.");
    return 0;
}
