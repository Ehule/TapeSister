#include "tapesister/sample.h"
#include "tapesister/ui.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char error[160];
static TsSample render(const TsFmPatch *p, uint32_t seed, float seconds)
{
    TsSample s = {0};
    assert(ts_fm_render_sample(&s, p, seconds, 261.625565f, 12000, seed, error, sizeof(error)));
    assert(ts_fm_sample_is_usable(&s));
    for (size_t i = 0; i < s.frames; ++i) assert(isfinite(s.data[i]) && fabsf(s.data[i]) <= .981f);
    return s;
}

static double energy(const TsSample *s, size_t first, size_t last)
{
    double e = 0;
    for (size_t i = first; i < last; ++i) e += (double)s->data[i] * s->data[i];
    return e / (double)(last - first);
}

static void test_palette(void)
{
    unsigned families = 0;
    for (uint32_t seed = 1; seed <= 24; ++seed) {
        TsFmPatch legacy, none;
        ts_fm_patch_fresh(&legacy, seed);
        ts_fm_patch_directed(&none, seed, 0);
        assert(!memcmp(&legacy, &none, sizeof(none)));
        for (uint32_t mask = 1; mask <= TS_FM_DIRECTION_ALL; ++mask) {
            TsFmPatch p, again, v;
            ts_fm_patch_directed(&p, seed, mask);
            ts_fm_patch_directed(&again, seed, mask);
            assert(!memcmp(&p, &again, sizeof(p)) && p.directions == mask);
            assert(p.drone_mode == ((mask & TS_FM_DIRECTION_DRONE) != 0));
            if (mask == TS_FM_DIRECTION_PERC) families |= 1u << p.percussion;
            TsSample s = render(&p, seed, .8f);
            TsSample same = render(&again, seed + 9000, .8f);
            assert(ts_sample_hash(&s) == ts_sample_hash(&same));
            ts_sample_free(&same);
            ts_fm_patch_vary(&p, seed + 400, 0, &v);
            assert(!memcmp(&p, &v, sizeof(p)));
            ts_fm_patch_vary(&p, seed + 400, .15f, &v);
            assert(v.directions == mask && v.percussion == p.percussion);
            assert(v.drone_mode == p.drone_mode && v.ratios[0] == p.ratios[0]);
            TsSample relative = render(&v, seed + 400, .8f);
            ts_sample_free(&relative);
            ts_fm_patch_vary(&p, seed + 700, 1, &v);
            assert(v.directions == mask && v.percussion == p.percussion);
            relative = render(&v, seed + 700, .8f);
            ts_sample_free(&relative);
            p.mutation_mask = 0;
            ts_fm_patch_vary(&p, seed + 500, 1, &v);
            assert(!memcmp(&p, &v, sizeof(p)));
            ts_sample_free(&s);
        }
    }
    assert(families == (1u << TS_FM_PERC_COUNT) - 1u);
}

static void test_envelopes_and_seam(void)
{
    TsFmPatch p;
    ts_fm_patch_directed(&p, 3, TS_FM_DIRECTION_PERC);
    p.active_mask = 1; p.structure = 3; p.waveforms[0] = TS_FM_WAVE_SINE;
    p.filter_mode = TS_FM_FILTER_CLEAN; p.transient_mix = 0;
    p.pitch_sweep = 0; p.attack_seconds = .001f; p.decay_seconds = .05f;
    p.lfo_types[0] = TS_FM_LFO_OFF;
    TsSample hit = render(&p, 3, 2);
    assert(hit.frames <= 5000 && fabsf(hit.data[0]) < 1e-6f);
    assert(energy(&hit, 0, hit.frames / 4) > 1000 * energy(&hit, hit.frames * 3 / 4, hit.frames));
    ts_sample_free(&hit);
    p.directions |= TS_FM_DIRECTION_DRONE; p.drone_mode = 1; p.pulse_rate = 2;
    TsSample pulse = render(&p, 3, 1);
    assert(pulse.frames == 12000);
    assert(energy(&pulse, 6000, 6600) > 20 * energy(&pulse, 10800, 11400));
    assert(energy(&pulse, 6000, 6600) > .001);
    ts_sample_free(&pulse);
    ts_fm_patch_basic(&p, TS_FM_WAVE_SINE);
    p.directions = TS_FM_DIRECTION_DRONE;
    TsSample drone = render(&p, 3, 1);
    float ordinary_step = 0;
    for (size_t i = 1; i < drone.frames; ++i)
        ordinary_step = fmaxf(ordinary_step, fabsf(drone.data[i] - drone.data[i-1]));
    assert(fabsf(drone.data[0] - drone.data[drone.frames-1]) <= ordinary_step + 1e-4f);
    ts_sample_free(&drone);
}

static void test_incremental_directions(void)
{
    TsFmPatch p;
    ts_fm_patch_directed(&p, 43, TS_FM_DIRECTION_EXPERIMENTAL);
    ts_fm_patch_set_directions(&p, TS_FM_DIRECTION_EXPERIMENTAL | TS_FM_DIRECTION_PERC, 43);
    assert(p.lfo_types[1] == TS_FM_LFO_INDEX_RANDOM && p.lfo_depths[1] >= .2f);
    assert(p.attack_seconds <= .003f && p.interaction_mix >= .3f);
    ts_fm_patch_set_directions(&p, TS_FM_DIRECTION_ALL, 43);
    assert(p.drone_mode && p.ratios[0] == 1 && p.pitch_sweep == 0);
    assert(p.lfo_types[1] == TS_FM_LFO_INDEX_RANDOM && p.lfo_depths[1] >= .08f);
    TsSample s = render(&p, 43, .8f);
    ts_sample_free(&s);
}

static TsInstrument *instrument(void)
{
    TsInstrument *p = calloc(1, sizeof(*p)); assert(p); ts_instrument_init(p); return p;
}
static void release(TsInstrument *p) { ts_instrument_free(p); free(p); }

static void test_workflow_and_save(void)
{
    TsInstrument *a = instrument(), *b = instrument();
    TsFmSeedSequence sequence; ts_fm_seed_sequence_init(&sequence, 7799);
    uint32_t seed;
    uint32_t mask = TS_FM_DIRECTION_DRONE | TS_FM_DIRECTION_PERC | TS_FM_DIRECTION_MELODIC;
    assert(ts_instrument_create_directed(a, &sequence, mask, &seed, error, sizeof(error)));
    assert(a->has_loop && a->loop_first == 0 && a->loop_last == a->current.frames);
    assert(a->generator.fm_patch.directions == mask);
    uint64_t hash = ts_sample_hash(&a->current);
    assert(ts_instrument_save_recipe(a, "directions.tsr", error, sizeof(error)));
    assert(ts_instrument_load_recipe(b, "directions.tsr", error, sizeof(error)));
    assert(ts_sample_hash(&b->current) == hash && b->has_loop);
    assert(!memcmp(&a->generator.fm_patch, &b->generator.fm_patch, sizeof(TsFmPatch)));
    a->family_mutation = .2f;
    int destination;
    assert(ts_instrument_vary_selected(a, 1, &destination, error, sizeof(error)));
    assert(a->generator.fm_patch.directions == mask && a->has_loop);
    /* Count never clears sounds beyond its destination, including locked ones. */
    assert(ts_instrument_select_bank(a, 15, error, sizeof(error)));
    assert(ts_instrument_create_directed(a, &sequence, 0, &seed, error, sizeof(error)));
    hash = ts_sample_hash(&a->current); a->bank[15].locked = 1;
    TsTileId id = a->bank[15].tile_id;
    TsFmPatch p; ts_fm_patch_directed(&p, 21, mask);
    assert(ts_instrument_make_fm_bank_count(a, &p, 21, 3, error, sizeof(error)));
    assert(ts_instrument_bank_count(a) == 4 && a->bank[15].locked && a->bank[15].tile_id == id);
    assert(ts_sample_hash(&a->bank[15].sample) == hash);
    for (int i = 0; i < 3; ++i) assert(a->bank[i].has_loop && a->bank[i].generator.fm_patch.directions == mask);
    a->bank[1].locked = 1;
    hash = ts_sample_hash(&a->bank[0].sample);
    assert(!ts_instrument_make_fm_bank_count(a, &p, 21, 3, error, sizeof(error)));
    assert(hash == ts_sample_hash(&a->bank[0].sample));
    assert(!ts_instrument_make_fm_bank_count(a, &p, 21, 17, error, sizeof(error)));
    assert(ts_instrument_make_fm_bank_count(a, &p, 21, 1, error, sizeof(error)));
    /* Selection Create keeps its boundaries and stores the selected directions. */
    assert(ts_instrument_select_bank(b, 3, error, sizeof(error)));
    assert(ts_instrument_activate_silence(b, 12000, 12000, error, sizeof(error)));
    ts_instrument_set_selection(b, 2000, 6000);
    assert(ts_instrument_stamp_directed(b, &sequence, TS_FM_DIRECTION_PERC, &seed, error, sizeof(error)));
    assert(b->current.frames == 12000 && b->selection_first == 2000 && b->selection_last == 6000);
    TsBankSlot *slot = &b->bank[b->selected_slot];
    assert(slot->patches[slot->patch_count-1].generator.fm_patch.directions == TS_FM_DIRECTION_PERC);
    a->bank[1].locked = 0; a->family_mutation = 0;
    assert(ts_instrument_make_fm_bank_count(a, &p, 21, 3, error, sizeof(error)));
    assert(ts_sample_hash(&a->bank[0].sample) == ts_sample_hash(&a->bank[2].sample));
    TsFmPatch original = p;
    ts_fm_toggle_unison(&p);
    assert(ts_instrument_apply_fm_patch(a, &p, error, sizeof(error)));
    assert(ts_instrument_save_recipe(a, "directions.tsr", error, sizeof(error)));
    assert(ts_instrument_load_recipe(b, "directions.tsr", error, sizeof(error)));
    p = b->generator.fm_patch;
    assert(ts_fm_toggle_unison(&p) == 0);
    assert(!memcmp(&p, &original, sizeof(p)));
    remove("directions.tsr"); release(a); release(b);
}

static void test_controls(void)
{
    TsUiState *u = calloc(1, sizeof(*u)); assert(u); ts_ui_init(u);
    assert(!u->config.create_directions && u->config.fm_bank_count == 16);
    for (int i = 0; i < 4; ++i) {
        const int offsets[] = {0,68,126,206};
        assert(ts_ui_direction_from_point(0, 113 + offsets[i], TS_WAVE_Y + TS_WAVE_H - 18) == (1u << i));
        assert(ts_ui_direction_from_point(1, 21 + offsets[i], 99) == (1u << i));
    }
    assert(!ts_ui_direction_from_point(0, 456, TS_WAVE_Y + TS_WAVE_H - 18));
    assert(ts_ui_fm_action_from_point(40, 286) == TS_UI_FM_ACTION_COUNT);
    u->config.create_directions = 15; u->config.fm_bank_count = 5;
    assert(ts_config_save(&u->config, "directions.ini", error, sizeof(error)));
    TsConfig config; ts_config_init(&config);
    assert(ts_config_load(&config, "directions.ini", error, sizeof(error)));
    assert(config.create_directions == 15 && config.fm_bank_count == 5);
    remove("directions.ini"); free(u);
}

int main(void)
{
    test_palette(); test_envelopes_and_seam(); test_incremental_directions(); test_workflow_and_save(); test_controls();
    puts("FM directions: palette, envelopes, loops, mutations, project roundtrip, partial banks and controls passed");
    return 0;
}
