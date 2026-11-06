#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/matrix_effects.h"
#include "tapesister/meta_fx.h"
#include "tapesister/sister_project_state.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void primitives(void) {
    const unsigned rates[] = {8000, 44100, 48000, 96000, 192000};
    for (unsigned r = 0; r < sizeof(rates) / sizeof(*rates); ++r) {
        unsigned rate = rates[r];
        TsSisterSingleFx *d = ts_sister_single_fx_new(TS_SISTER_FX_DISPERSION, rate, 1),
                         *n = ts_sister_single_fx_new(TS_SISTER_FX_RESONATOR, rate, 2);
        assert(d && n);
        TsSisterFxSlotControls c = {.type = TS_SISTER_FX_DISPERSION,
                                    .parameter_a = .45f,
                                    .parameter_b = .3f,
                                    .parameter_c = .5f,
                                    .mix = 1};
        ts_sister_single_fx_set(d, &c);
        c.type = TS_SISTER_FX_RESONATOR;
        c.parameter_a = .25f;
        c.parameter_b = .85f;
        c.parameter_c = .2f;
        ts_sister_single_fx_set(n, &c);
        for (unsigned i = 0; i < rate; ++i) {
            ts_sister_single_fx_process(d, (TsStereoFrame){0}, 1);
            ts_sister_single_fx_process(n, (TsStereoFrame){0}, 1);
        }
        double dispersion_energy = 0, tail = 0;
        for (unsigned i = 0; i < rate; ++i) {
            TsStereoFrame in = {i == 0 ? 1 : 0, 0}, a = ts_sister_single_fx_process(d, in, 1),
                          b = ts_sister_single_fx_process(n, in, 1);
            assert(isfinite(a.l) && isfinite(b.l));
            assert(a.r == 0 && b.r == 0);
            dispersion_energy += a.l * a.l;
            if (i > rate / 100)
                tail += b.l * b.l;
        }
        assert(fabs(dispersion_energy - 1) < .02 && tail > .000001);
        for (unsigned i = 0; i < rate / 10; ++i) {
            TsStereoFrame in = {.2f, -.3f};
            TsStereoFrame out = ts_sister_single_fx_process(n, in, 0);
            assert(out.l == in.l && out.r == in.r);
        }
        c.mix = 0;
        ts_sister_single_fx_set(n, &c);
        TsStereoFrame out = ts_sister_single_fx_process(n, (TsStereoFrame){.3f, .1f}, 1);
        assert(out.l == .3f && out.r == .1f);
        out = ts_sister_single_fx_process(d, (TsStereoFrame){NAN, INFINITY}, 1);
        assert(isfinite(out.l) && isfinite(out.r));
        ts_sister_single_fx_free(d);
        ts_sister_single_fx_free(n);
    }
}
static void racks(void) {
    for (int p = 0; p < TS_META_PRESETS; ++p) {
        TsMetaControls c;
        ts_meta_preset(&c, p);
        assert(ts_meta_valid(&c));
        unsigned assigned = 0;
        for (int j = 0; j < c.map_count; ++j)
            assigned |= 1u << c.map[j].macro;
        assert(assigned == 15);
        TsMetaEngine *a = ts_meta_new(&c, 48000, 42), *b = ts_meta_new(&c, 48000, 42);
        assert(a && b);
        double energy = 0;
        for (int i = 0; i < 96000; ++i) {
            TsStereoFrame in = {.03f * sinf(i * .041f) + (i % 1200 == 0 ? .2f : 0), .02f * cosf(i * .031f)};
            TsStereoFrame x = ts_meta_process(a, in), y = ts_meta_process(b, in);
            assert(x.l == y.l && x.r == y.r && isfinite(x.l) && isfinite(x.r));
            assert(fabsf(x.l) < 1000 && fabsf(x.r) < 1000);
            energy += x.l * x.l + x.r * x.r;
            if (i == 48000) {
                ts_meta_macro_set(&c, 0, .85f);
                ts_meta_set(a, &c);
                ts_meta_set(b, &c);
            }
        }
        assert(energy > 1e-9);
        printf("%s energy %.6g memory %zu\n", c.name, energy, ts_meta_memory(a));
        ts_meta_free(a);
        ts_meta_free(b);
    }
    TsMetaControls c;
    ts_meta_default(&c);
    c.count = 1;
    ts_meta_stage_default(&c.stage[0], TS_SISTER_FX_DELAY);
    c.stage[0].value[3] = 1;
    TsMetaEngine *a = ts_meta_new(&c, 48000, 1), *b = ts_meta_new(&c, 48000, 1);
    assert(a && b);
    double tail = 0;
    for (int i = 0; i < 48000; ++i) {
        TsStereoFrame x = ts_meta_process(a, (TsStereoFrame){i == 0 ? 1 : 0, 0}),
                      y = ts_meta_process(b, (TsStereoFrame){0});
        assert(y.l == 0 && y.r == 0);
        if (i > 0)
            tail += x.l * x.l;
    }
    assert(tail > 0);
    ts_meta_free(a);
    ts_meta_free(b);
    c.macro[0] = .2f;
    ts_meta_capture(&c, 0);
    c.macro[0] = .8f;
    ts_meta_capture(&c, 1);
    c.scene_mode = 1;
    c.morph = .5f;
    ts_meta_capture(&c, 0);
    assert(!c.scene_mode && fabsf(c.macro[0] - .5f) < 1e-6 && fabsf(c.scene[0][0] - .5f) < 1e-6);
    ts_meta_macro_set(&c, 1, .7f);
    assert(fabsf(c.macro[0] - .5f) < 1e-6 && c.macro[1] == .7f && !c.scene_mode);
    c.map_count = 2;
    c.map[0] = (TsMetaMap){0, 0, 0, 0, 1};
    c.map[1] = (TsMetaMap){1, 0, 0, 1, 0};
    assert(!ts_meta_valid(&c));
}
static void serial_equivalence(void) {
    TsMetaControls c;
    ts_meta_default(&c);
    c.count = 3;
    const int types[] = {TS_SISTER_FX_DISTORTION, TS_SISTER_FX_GRAIN, TS_SISTER_FX_TRANSIENT};
    TsSisterSingleFx *child[3];
    for (int i = 0; i < 3; ++i) {
        ts_meta_stage_default(&c.stage[i], types[i]);
        child[i] = ts_sister_single_fx_new(types[i], 48000, 42 + (i + 1) * UINT32_C(0x9e3779b9));
        assert(child[i]);
        TsMetaStage *s = &c.stage[i];
        TsSisterFxSlotControls slot = {.type = types[i],
                                       .enabled = 1,
                                       .parameter_a = s->value[0],
                                       .parameter_b = s->value[1],
                                       .parameter_c = s->value[2],
                                       .mix = s->value[3],
                                       .gain_db = s->value[4] * 24 - 12};
        ts_character_filter_default(&slot.filter);
        ts_sister_single_fx_set(child[i], &slot);
    }
    TsMetaEngine *rack = ts_meta_new(&c, 48000, 42);
    assert(rack);
    for (int n = 0; n < 48000; ++n) {
        TsStereoFrame in = {.05f * sinf(n * .025f), .04f * cosf(n * .015f)}, expected = in;
        for (int i = 0; i < 3; ++i)
            expected = ts_sister_single_fx_process(child[i], expected, 1);
        TsStereoFrame actual = ts_meta_process(rack, in);
        assert(actual.l == expected.l && actual.r == expected.r);
    }
    for (int i = 0; i < 3; ++i)
        ts_sister_single_fx_free(child[i]);
    ts_meta_free(rack);
}
static void persistence(void) {
    char error[256];
    TsMetaControls a, b;
    ts_meta_preset(&a, 3);
    a.stage[0].power = 3;
    a.map[0].low = .9f;
    a.map[0].high = .1f;
    assert(ts_meta_save(&a, "meta-test.tsmfx", error, sizeof(error)));
    ts_meta_default(&b);
    assert(ts_meta_load(&b, "meta-test.tsmfx", error, sizeof(error)));
    assert(!memcmp(&a, &b, sizeof(a)));
    FILE *f = fopen("meta-test.tsmfx", "wb");
    assert(f);
    fputs("TapeSister Meta FX 1\nMeta.Header=1,9,0,-1,0,0,0\nMeta.End=1\n", f);
    fclose(f);
    TsMetaControls saved = b;
    assert(!ts_meta_load(&b, "meta-test.tsmfx", error, sizeof(error)) && !memcmp(&b, &saved, sizeof(b)));
    remove("meta-test.tsmfx");
    static TsSisterProjectState state, loaded;
    ts_sister_project_state_init(&state, 48000);
    int i = ts_matrix_effect_add(&state.matrix, TS_MATRIX_FX_META);
    assert(i >= 0);
    state.matrix.effect[i].meta = a;
    state.parameters.fx.slot[0].type = TS_SISTER_FX_DISPERSION;
    state.parameters.fx.slot[1].type = TS_SISTER_FX_RESONATOR;
    assert(ts_sister_project_state_save_file(&state, "meta-project.tsr", error, sizeof(error)));
    int present = 0;
    assert(ts_sister_project_state_load_file(&loaded, "meta-project.tsr", 48000, &present, error,
                                             sizeof(error)) &&
           present);
    FILE *original = fopen("meta-project.tsr", "rb"), *incomplete = fopen("meta-incomplete.tsr", "wb");
    assert(original && incomplete);
    char line[4096];
    while (fgets(line, sizeof(line), original))
        if (strncmp(line, "Matrix.Meta", 11))
            fputs(line, incomplete);
    fclose(original);
    fclose(incomplete);
    assert(!ts_sister_project_state_load_file(&loaded, "meta-incomplete.tsr", 48000, &present, error,
                                              sizeof(error)));
    remove("meta-incomplete.tsr");
    assert(ts_sister_project_state_load_file(&loaded, "meta-project.tsr", 48000, &present, error,
                                             sizeof(error)));
    assert(!memcmp(&loaded.matrix.effect[i].meta, &a, sizeof(a)));
    assert(loaded.parameters.fx.slot[1].type == TS_SISTER_FX_RESONATOR);
    remove("meta-project.tsr");
    TsSisterFxControls board;
    ts_sister_fx_controls_default(&board);
    board.slot_count = 10;
    ts_meta_from_board(&b, &board, 1, 2);
    assert(b.count == 2 && b.stage[0].type == board.slot[1].type && b.stage[1].type == board.slot[2].type);
}
static void matrix_lifecycle(void) {
    TsMatrixControls c;
    ts_matrix_default(&c);
    int i = ts_matrix_effect_add(&c, TS_MATRIX_FX_META);
    assert(i == 0);
    TsMatrixEffects engine;
    ts_matrix_effects_init(&engine);
    assert(ts_matrix_effects_restore(&engine, &c, 48000));
    TsMatrixEffectPlan plan;
    ts_meta_macro_set(&c.effect[i].meta, 0, .8f);
    assert(ts_matrix_effects_stage(&engine, &c, 48000, &plan) && !plan.changed);
    ts_matrix_effects_commit(&engine, &c, 48000, &plan);
    ts_meta_preset(&c.effect[i].meta, 3);
    assert(ts_matrix_effects_stage(&engine, &c, 48000, &plan) && plan.changed);
    ts_matrix_effects_commit(&engine, &c, 48000, &plan);
    for (int n = 0; n < 1000; ++n) {
        ts_matrix_effects_tick(&engine);
        TsStereoFrame out =
            ts_matrix_effects_process(&engine, i, (TsStereoFrame){.02f, .03f}, (TsStereoFrame){0}, 1);
        assert(isfinite(out.l) && isfinite(out.r));
    }
    ts_matrix_effects_collect(&engine, &plan);
    ts_matrix_effects_discard(&plan);
    c.effect[i].enabled = 0;
    assert(ts_matrix_effects_stage(&engine, &c, 48000, &plan));
    ts_matrix_effects_commit(&engine, &c, 48000, &plan);
    for (int n = 0; n < 1000; ++n)
        ts_matrix_effects_process(&engine, i, (TsStereoFrame){.2f, .3f}, (TsStereoFrame){0}, 1);
    TsStereoFrame out =
        ts_matrix_effects_process(&engine, i, (TsStereoFrame){.2f, .3f}, (TsStereoFrame){0}, 1);
    assert(out.l == .2f && out.r == .3f);
    ts_matrix_effects_free(&engine);
    assert(!ts_matrix_effects_memory(&engine));
}
int main(void) {
    primitives();
    racks();
    serial_equivalence();
    persistence();
    matrix_lifecycle();
    puts("Meta FX audio, stereo, persistence and lifecycle passed.");
    return 0;
}
