#include "tapesister/pr13.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

static float largest_jump(const TsSample *sample)
{
    float jump=0.0f;
    for(size_t i=1;i<sample->frames;++i){float d=fabsf(sample->data[i]-sample->data[i-1]);if(d>jump)jump=d;}
    return jump;
}

int main(void)
{
    TsInstrument instrument;
    TsInstrument loaded;
    TsSample base, body, edge, drift, neutral;
    TsGeneratorRecipe generator = {0x13572468u, TS_GENERATOR_TONAL, 0.25f, 130.8128f};
    char error[160];
    uint64_t base_hash;
    float base_peak;
    int child = -1;
    int sibling = -1;

    ts_instrument_init(&instrument);
    ts_instrument_init(&loaded);
    ts_sample_init(&base);
    ts_sample_init(&body);
    ts_sample_init(&edge);
    ts_sample_init(&drift);
    ts_sample_init(&neutral);

    CHECK(ts_sample_generate(&base, &generator, error, sizeof(error)));
    CHECK(ts_sample_clone(&body, &base, error, sizeof(error)));
    CHECK(ts_sample_clone(&edge, &base, error, sizeof(error)));
    CHECK(ts_sample_clone(&drift, &base, error, sizeof(error)));
    CHECK(ts_sample_clone(&neutral, &base, error, sizeof(error)));
    base_hash = ts_sample_hash(&base);
    base_peak = ts_sample_peak(&base);

    CHECK(ts_pr13_apply_body_edge_drift(&neutral, 0.0f, 0.0f, 0.5f,
                                        error, sizeof(error)));
    CHECK(ts_sample_hash(&neutral) == base_hash);

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
    CHECK(fabsf(drift.data[0]-base.data[0]) < 0.0001f);
    CHECK(fabsf(drift.data[drift.frames-1]-base.data[base.frames-1]) < 0.0001f);
    CHECK(largest_jump(&drift) < largest_jump(&base) * 3.0f + 0.01f);

    CHECK(fabsf(ts_pr13_family_similarity(TS_FAMILY_CHILD, 1.0f) - 0.50f) < 0.0001f);
    CHECK(fabsf(ts_pr13_family_similarity(TS_FAMILY_COUSIN, 1.0f) - 0.25f) < 0.0001f);
    CHECK(fabsf(ts_pr13_family_similarity(TS_FAMILY_STRANGER, 1.0f) - 0.05f) < 0.0001f);
    CHECK(fabsf(ts_pr13_family_similarity(TS_FAMILY_CHILD, 0.0f) - 1.0f) < 0.0001f);

    CHECK(ts_instrument_generate(&instrument, TS_GENERATOR_TONAL, 0x24681357u,
                                 error, sizeof(error)));
    ts_pr13_neutral_process(&instrument.process);
    instrument.bank[0].process=instrument.process;
    instrument.bank[0].has_process=1;
    instrument.family_relation = TS_FAMILY_CHILD;
    instrument.family_mutation = 0.9f;
    {
        uint64_t root_source_hash=ts_sample_hash(&instrument.bank[0].sample);
        float root_source_peak=ts_sample_peak(&instrument.bank[0].sample);
        TsProcessRecipe root_process=instrument.process;
        root_process.filter_enabled=1;
        root_process.filter_mode=TS_FILTER_LOWPASS;
        root_process.filter_cutoff_hz=900.0f;
        root_process.filter_resonance=0.35f;
        root_process.noise_enabled=1;
        root_process.noise_amount=0.11f;
        CHECK(ts_pr13_set_process(&instrument,0,&root_process,error,sizeof(error)));
        /* Editing the environment must never print it into the genetic source. */
        CHECK(ts_sample_hash(&instrument.bank[0].sample)==root_source_hash);
        CHECK(fabsf(ts_sample_peak(&instrument.bank[0].sample)-root_source_peak)<0.000001f);
    }
    CHECK(ts_pr13_generate_family_candidate(&instrument, 0, 0, &child,
                                             error, sizeof(error)));
    CHECK(child > 0 && instrument.bank[child].parent_slot == 0);
    CHECK(instrument.bank[child].has_process);
    CHECK(fabsf(instrument.bank[child].process.noise_amount-instrument.bank[0].process.noise_amount)<0.0001f);
    CHECK(fabsf(instrument.bank[child].process.filter_cutoff_hz-instrument.bank[0].process.filter_cutoff_hz)<0.001f);
    CHECK(ts_pr13_activate_slot(&instrument,child,error,sizeof(error)));
    {
        uint64_t child_source_hash=ts_sample_hash(&instrument.bank[child].sample);
        TsProcessRecipe child_process=instrument.process;
        child_process.filter_cutoff_hz=2400.0f;
        child_process.delay_enabled=1;child_process.delay_mix=0.41f;
        CHECK(ts_pr13_set_process(&instrument,child,&child_process,error,sizeof(error)));
        CHECK(ts_sample_hash(&instrument.bank[child].sample)==child_source_hash);
    }
    CHECK(ts_pr13_activate_slot(&instrument,0,error,sizeof(error)));
    CHECK(!instrument.process.delay_enabled);
    CHECK(fabsf(instrument.process.filter_cutoff_hz-900.0f)<0.001f);
    CHECK(ts_pr13_activate_slot(&instrument,child,error,sizeof(error)));
    CHECK(instrument.process.delay_enabled && fabsf(instrument.process.delay_mix-0.41f)<0.0001f);
    CHECK(fabsf(instrument.process.filter_cutoff_hz-2400.0f)<0.001f);

    {
        uint64_t root_source_hash=ts_sample_hash(&instrument.bank[0].sample);
        float root_source_peak=ts_sample_peak(&instrument.bank[0].sample);
        int generated=-1;
        /* Repeated sibling generation/activation may change children, but it
           must not progressively process or amplify the parent source. */
        for(int i=0;i<3;++i){
            CHECK(ts_pr13_generate_family_candidate(&instrument,0,0,&generated,error,sizeof(error)));
            CHECK(generated>0);
            CHECK(ts_pr13_activate_slot(&instrument,generated,error,sizeof(error)));
            CHECK(ts_sample_hash(&instrument.bank[0].sample)==root_source_hash);
            CHECK(fabsf(ts_sample_peak(&instrument.bank[0].sample)-root_source_peak)<0.000001f);
            CHECK(ts_pr13_activate_slot(&instrument,0,error,sizeof(error)));
        }
    }

    CHECK(ts_pr13_generate_family_candidate(&instrument, child, 1, &sibling,
                                             error, sizeof(error)));
    CHECK(sibling > child);
    CHECK(instrument.bank[sibling].parent_slot == instrument.bank[child].parent_slot ||
          instrument.bank[sibling].parent_slot == child);
    CHECK(ts_sample_hash(&instrument.bank[sibling].sample) !=
          ts_sample_hash(&instrument.bank[child].sample));

    CHECK(ts_pr13_set_slot_locked(&instrument, child, 1, error, sizeof(error)));
    CHECK(ts_pr13_slot_locked(&instrument, child));
    {
        TsProcessRecipe process = instrument.process;
        process.edge = 0.6f;
        CHECK(!ts_pr13_set_process(&instrument, child, &process, error, sizeof(error)));
    }
    CHECK(ts_pr13_save_project(&instrument, "test-pr13-lock.tsr", error, sizeof(error)));
    CHECK(ts_pr13_load_project(&loaded, "test-pr13-lock.tsr", error, sizeof(error)));
    CHECK(ts_pr13_slot_locked(&loaded, child));
    CHECK(loaded.bank[child].has_process);
    CHECK(fabsf(loaded.bank[child].process.delay_mix-instrument.bank[child].process.delay_mix)<0.0001f);
    CHECK(fabsf(loaded.bank[child].process.filter_cutoff_hz-instrument.bank[child].process.filter_cutoff_hz)<0.001f);
    remove("test-pr13-lock.tsr");

    CHECK(ts_pr13_set_slot_locked(&instrument, child, 0, error, sizeof(error)));
    CHECK(!ts_pr13_slot_locked(&instrument, child));

    {
        uint64_t child_hash=ts_sample_hash(&instrument.bank[child].sample);
        CHECK(ts_pr13_bank_clear(&instrument,0,error,sizeof(error)));
        CHECK(!instrument.bank[0].occupied);
        CHECK(instrument.bank[child].occupied);
        CHECK(ts_sample_hash(&instrument.bank[child].sample)==child_hash);
        CHECK(ts_pr13_generate_family_candidate(&instrument,child,1,&sibling,error,sizeof(error)));
        CHECK(sibling>=0&&instrument.bank[sibling].occupied);
    }

    {
        TsInstrument empty;
        ts_instrument_init(&empty);
        CHECK(ts_instrument_generate(&empty,TS_GENERATOR_TONAL,0x11112222u,error,sizeof(error)));
        CHECK(ts_pr13_bank_clear(&empty,0,error,sizeof(error)));
        CHECK(!empty.bank[0].occupied);
        /* An entirely empty family must be recoverable without a filled anchor. */
        CHECK(ts_pr13_generate_parent_in_slot(&empty,0,0,error,sizeof(error)));
        CHECK(empty.bank[0].occupied);
        ts_instrument_free(&empty);
    }

    {
        TsInstrument fade;
        TsProcessRecipe process;
        ts_instrument_init(&fade);
        CHECK(ts_instrument_generate(&fade,TS_GENERATOR_PULSE,0xface1234u,error,sizeof(error)));
        ts_pr13_neutral_process(&process);
        fade.process=process;fade.bank[0].process=process;fade.bank[0].has_process=1;
        process.body=0.75f;process.edge=0.55f;
        CHECK(ts_pr13_set_process(&fade,0,&process,error,sizeof(error)));
        CHECK(ts_pr13_apply_sample_edit(&fade,0,TS_SAMPLE_EDIT_FADE_IN,1.0f,error,sizeof(error)));
        CHECK(fabsf(fade.current.data[0])<0.00001f);
        CHECK(ts_pr13_apply_sample_edit(&fade,0,TS_SAMPLE_EDIT_FADE_OUT,1.0f,error,sizeof(error)));
        CHECK(fabsf(fade.current.data[fade.current.frames-1])<0.00001f);
        ts_instrument_free(&fade);
    }

    {
        TsInstrument long_edit;
        ts_instrument_init(&long_edit);
        CHECK(ts_instrument_generate(&long_edit,TS_GENERATOR_TONAL,0x51515151u,error,sizeof(error)));
        ts_pr13_neutral_process(&long_edit.process);
        long_edit.bank[0].process=long_edit.process;long_edit.bank[0].has_process=1;
        for(int i=0;i<TS_SAMPLE_EDIT_DEPTH+6;++i)
            CHECK(ts_pr13_apply_sample_edit(&long_edit,0,TS_SAMPLE_EDIT_GAIN,0.999f,error,sizeof(error)));
        CHECK(strstr(error,"Commit before")==NULL);
        ts_instrument_free(&long_edit);
    }

    ts_sample_free(&base);
    ts_sample_free(&body);
    ts_sample_free(&edge);
    ts_sample_free(&drift);
    ts_sample_free(&neutral);
    ts_instrument_free(&loaded);
    ts_instrument_free(&instrument);
    if (failures) return 1;
    puts("PR13 tests passed");
    return 0;
}
