#include "tapesister/pr13.h"

#include <math.h>
#include <stdio.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

int main(void)
{
    TsInstrument instrument;
    TsSample base, body, edge, drift;
    TsGeneratorRecipe generator = {0x13572468u, TS_GENERATOR_TONAL, 0.25f, 130.8128f};
    char error[160];
    uint64_t base_hash;
    float base_peak;
    int child = -1;
    int sibling = -1;

    ts_instrument_init(&instrument);
    ts_sample_init(&base);
    ts_sample_init(&body);
    ts_sample_init(&edge);
    ts_sample_init(&drift);

    CHECK(ts_sample_generate(&base, &generator, error, sizeof(error)));
    CHECK(ts_sample_clone(&body, &base, error, sizeof(error)));
    CHECK(ts_sample_clone(&edge, &base, error, sizeof(error)));
    CHECK(ts_sample_clone(&drift, &base, error, sizeof(error)));
    base_hash = ts_sample_hash(&base);
    base_peak = ts_sample_peak(&base);

    CHECK(ts_pr13_apply_body_edge_drift(&body, 0.8f, 0.0f, 0.5f,
                                        error, sizeof(error)));
    CHECK(ts_sample_hash(&body) != base_hash);
    CHECK(fabsf(ts_sample_peak(&body) - base_peak) < 0.01f);

    CHECK(ts_pr13_apply_body_edge_drift(&edge, 0.0f, 0.8f, 0.5f,
                                        error, sizeof(error)));
    CHECK(ts_sample_hash(&edge) != base_hash);
    CHECK(fabsf(ts_sample_peak(&edge) - base_peak) < 0.01f);

    CHECK(ts_pr13_apply_body_edge_drift(&drift, 0.0f, 0.0f, 0.75f,
                                        error, sizeof(error)));
    CHECK(drift.frames == base.frames);
    CHECK(ts_sample_hash(&drift) != base_hash);
    CHECK(fabsf(ts_sample_peak(&drift) - base_peak) < 0.0001f);

    CHECK(fabsf(ts_pr13_family_similarity(TS_FAMILY_CHILD, 1.0f) - 0.50f) < 0.0001f);
    CHECK(fabsf(ts_pr13_family_similarity(TS_FAMILY_COUSIN, 1.0f) - 0.25f) < 0.0001f);
    CHECK(fabsf(ts_pr13_family_similarity(TS_FAMILY_STRANGER, 1.0f) - 0.05f) < 0.0001f);
    CHECK(fabsf(ts_pr13_family_similarity(TS_FAMILY_CHILD, 0.0f) - 1.0f) < 0.0001f);

    CHECK(ts_instrument_generate(&instrument, TS_GENERATOR_TONAL, 0x24681357u,
                                 error, sizeof(error)));
    instrument.family_relation = TS_FAMILY_CHILD;
    instrument.family_mutation = 0.9f;
    CHECK(ts_pr13_generate_family_candidate(&instrument, 0, 0, &child,
                                             error, sizeof(error)));
    CHECK(child > 0 && instrument.bank[child].parent_slot == 0);
    CHECK(ts_pr13_generate_family_candidate(&instrument, child, 1, &sibling,
                                             error, sizeof(error)));
    CHECK(sibling > child);
    CHECK(instrument.bank[sibling].parent_slot == instrument.bank[child].parent_slot);
    CHECK(ts_sample_hash(&instrument.bank[sibling].sample) !=
          ts_sample_hash(&instrument.bank[child].sample));

    CHECK(ts_pr13_set_slot_locked(&instrument, child, 1, error, sizeof(error)));
    CHECK(ts_pr13_slot_locked(&instrument, child));
    {
        TsProcessRecipe process = instrument.process;
        process.edge = 0.6f;
        CHECK(!ts_pr13_set_process(&instrument, child, &process, error, sizeof(error)));
    }
    CHECK(ts_pr13_set_slot_locked(&instrument, child, 0, error, sizeof(error)));
    CHECK(!ts_pr13_slot_locked(&instrument, child));

    ts_sample_free(&base);
    ts_sample_free(&body);
    ts_sample_free(&edge);
    ts_sample_free(&drift);
    ts_instrument_free(&instrument);
    if (failures) return 1;
    puts("PR13 tests passed");
    return 0;
}
