#include "tapesister/sample.h"
#include <math.h>
#include <stdio.h>

int main(void)
{
    TsInstrument instrument;
    TsProcessRecipe process;
    TsSample neutral, changed;
    char error[160];
    int made = -1;
    ts_instrument_init(&instrument);
    ts_sample_init(&neutral);
    ts_sample_init(&changed);
    if (!ts_instrument_generate(&instrument, TS_GENERATOR_TONAL, 0x50523133u, error, sizeof(error))) return 1;
    ts_process_recipe_reset(&process);
    if (fabsf(process.drift - 0.5f) > 0.0001f) return 2;
    if (!ts_sample_process(&neutral, &instrument.parent, 0, instrument.parent.frames, &process, error, sizeof(error))) return 3;
    process.body = 1.0f;
    if (!ts_sample_process(&changed, &instrument.parent, 0, instrument.parent.frames, &process, error, sizeof(error))) return 4;
    if (ts_sample_hash(&neutral) == ts_sample_hash(&changed)) return 5;
    instrument.family_relation = TS_FAMILY_CHILD;
    instrument.family_mutation = 1.0f;
    if (!ts_instrument_generate_family_candidate(&instrument, 0, 0, &made, error, sizeof(error))) return 6;
    if (made <= 0 || instrument.active_bank_slot != made) return 7;
    if (!ts_instrument_bank_toggle_lock(&instrument, made, error, sizeof(error))) return 8;
    process = instrument.process;
    process.edge = 1.0f;
    if (ts_instrument_set_process(&instrument, &process, error, sizeof(error))) return 9;
    ts_sample_free(&neutral);
    ts_sample_free(&changed);
    ts_instrument_free(&instrument);
    puts("PR13 smoke checks passed");
    return 0;
}
