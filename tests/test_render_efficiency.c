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
