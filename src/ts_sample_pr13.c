#define ts_sample_process ts_sample_process_legacy
#define ts_process_recipe_reset ts_process_recipe_reset_legacy
#define ts_instrument_init ts_instrument_init_legacy
#define ts_instrument_generate ts_instrument_generate_legacy
#define ts_instrument_load_wav ts_instrument_load_wav_legacy
#define ts_instrument_set_process ts_instrument_set_process_legacy
#define ts_instrument_set_process_and_tuning ts_instrument_set_process_and_tuning_legacy
#define ts_instrument_set_process_and_tunings ts_instrument_set_process_and_tunings_legacy
#define ts_instrument_set_tuning ts_instrument_set_tuning_legacy
#define ts_instrument_set_audible_tuning ts_instrument_set_audible_tuning_legacy
#define ts_instrument_reset_current ts_instrument_reset_current_legacy
#define ts_instrument_commit_current ts_instrument_commit_current_legacy
#define ts_instrument_set_loop_from_selection ts_instrument_set_loop_from_selection_legacy
#define ts_instrument_clear_loop ts_instrument_clear_loop_legacy
#define ts_instrument_set_loop_crossfade ts_instrument_set_loop_crossfade_legacy
#define ts_instrument_set_loop_mode ts_instrument_set_loop_mode_legacy
#define ts_instrument_crop_selection ts_instrument_crop_selection_legacy
#define ts_instrument_apply_sample_edit ts_instrument_apply_sample_edit_legacy
#define ts_instrument_undo ts_instrument_undo_legacy
#define ts_instrument_redo ts_instrument_redo_legacy
#define ts_instrument_apply_tape_drag ts_instrument_apply_tape_drag_legacy
#define ts_instrument_bank_clear ts_instrument_bank_clear_legacy
#define ts_instrument_bank_rename ts_instrument_bank_rename_legacy
#define ts_instrument_bank_set_loop_full ts_instrument_bank_set_loop_full_legacy
#define ts_instrument_bank_clear_loop ts_instrument_bank_clear_loop_legacy
#define ts_instrument_bank_set_loop_crossfade ts_instrument_bank_set_loop_crossfade_legacy
#define ts_instrument_bank_set_loop_mode ts_instrument_bank_set_loop_mode_legacy
#define ts_instrument_bank_move_loop_endpoint ts_instrument_bank_move_loop_endpoint_legacy
#define ts_instrument_set_bank_as_current ts_instrument_set_bank_as_current_legacy
#define ts_instrument_generate_family_candidate ts_instrument_generate_family_candidate_legacy
#define ts_instrument_save_recipe ts_instrument_save_recipe_legacy
#define ts_instrument_load_recipe ts_instrument_load_recipe_legacy
#include "ts_sample.c"
#undef ts_sample_process
#undef ts_process_recipe_reset
#undef ts_instrument_init
#undef ts_instrument_generate
#undef ts_instrument_load_wav
#undef ts_instrument_set_process
#undef ts_instrument_set_process_and_tuning
#undef ts_instrument_set_process_and_tunings
#undef ts_instrument_set_tuning
#undef ts_instrument_set_audible_tuning
#undef ts_instrument_reset_current
#undef ts_instrument_commit_current
#undef ts_instrument_set_loop_from_selection
#undef ts_instrument_clear_loop
#undef ts_instrument_set_loop_crossfade
#undef ts_instrument_set_loop_mode
#undef ts_instrument_crop_selection
#undef ts_instrument_apply_sample_edit
#undef ts_instrument_undo
#undef ts_instrument_redo
#undef ts_instrument_apply_tape_drag
#undef ts_instrument_bank_clear
#undef ts_instrument_bank_rename
#undef ts_instrument_bank_set_loop_full
#undef ts_instrument_bank_clear_loop
#undef ts_instrument_bank_set_loop_crossfade
#undef ts_instrument_bank_set_loop_mode
#undef ts_instrument_bank_move_loop_endpoint
#undef ts_instrument_set_bank_as_current
#undef ts_instrument_generate_family_candidate
#undef ts_instrument_save_recipe
#undef ts_instrument_load_recipe

#include "tapesister/pr13.h"

#define PR13_LOCK_BIT 0x80000000u
#define PR13_SEQUENCE_MASK 0x00ffffffu
#define PR13_ACTIVE_SHIFT 24u

int ts_pr13_lock_request = 0;
void ts_pr13_set_lock_request(int enabled) { ts_pr13_lock_request = enabled ? 1 : 0; }

static int pr13_active_locked(const TsInstrument *instrument)
{
    int slot = instrument != NULL ? instrument->active_bank_slot : -1;
    return slot >= 0 && slot < TS_BANK_SLOT_COUNT && instrument->bank[slot].occupied &&
           instrument->bank[slot].locked;
}

static int pr13_guard_edit(const TsInstrument *instrument, char *error, size_t error_size)
{
    if (!pr13_active_locked(instrument)) return 1;
    set_error(error, error_size, "Active family slot is locked - unlock it before editing");
    return 0;
}

static void pr13_sync_active(TsInstrument *instrument)
{
    int slot;
    TsBankSlot *bank;
    char saved_name[sizeof(instrument->bank[0].sample.name)];
    char ignored[8];
    if (instrument == NULL) return;
    slot = instrument->active_bank_slot;
    if (slot < 0 || slot >= TS_BANK_SLOT_COUNT) return;
    bank = &instrument->bank[slot];
    if (!bank->occupied || bank->locked || instrument->current.data == NULL) return;
    snprintf(saved_name, sizeof(saved_name), "%s", bank->sample.name);
    if (!ts_sample_clone(&bank->sample, &instrument->current, ignored, sizeof(ignored))) return;
    if (saved_name[0] != '\0') snprintf(bank->sample.name, sizeof(bank->sample.name), "%s", saved_name);
    bank->tuning = instrument->tuning;
    bank->audible_tuning = instrument->audible_tuning;
    bank->has_loop = instrument->has_loop;
    bank->loop_first = instrument->loop_first;
    bank->loop_last = instrument->loop_last;
    bank->loop_crossfade_ms = instrument->loop_crossfade_ms;
    bank->loop_mode = instrument->loop_mode;
}

static void pr13_match_peak(float *data, size_t frames, float target)
{
    float peak = 0.0f;
    if (data == NULL || frames == 0 || target <= 0.0000001f) return;
    for (size_t i = 0; i < frames; ++i) if (fabsf(data[i]) > peak) peak = fabsf(data[i]);
    if (peak <= 0.0000001f) return;
    {
        float gain = target / peak;
        for (size_t i = 0; i < frames; ++i) data[i] = clampf(data[i] * gain, -1.0f, 1.0f);
    }
}

static void pr13_body_edge(float *data, size_t frames, float body, float edge)
{
    float original_peak = 0.0f;
    float low = 0.0f;
    float envelope = 0.0f;
    float body_amount = clampf((body - 0.5f) * 2.0f, -1.0f, 1.0f);
    float edge_amount = clampf(edge, 0.0f, 1.0f);
    if (data == NULL || frames == 0) return;
    for (size_t i = 0; i < frames; ++i) if (fabsf(data[i]) > original_peak) original_peak = fabsf(data[i]);
    for (size_t i = 0; i < frames; ++i) {
        float x = data[i];
        float absolute = fabsf(x);
        low += (x - low) * 0.018f;
        envelope += (absolute - envelope) * (absolute > envelope ? 0.18f : 0.012f);
        if (body_amount > 0.0f) {
            float dense = copysignf(powf(absolute + 0.000001f, 1.0f - body_amount * 0.32f), x);
            float third = 4.0f * x * x * x - 3.0f * x;
            x = x * (1.0f - body_amount * 0.34f) + dense * body_amount * 0.24f +
                low * body_amount * 0.42f + third * body_amount * 0.10f;
        } else if (body_amount < 0.0f) {
            x -= low * (-body_amount) * 0.72f;
        }
        if (edge_amount > 0.0f) {
            float transient = absolute > envelope ? absolute - envelope : 0.0f;
            float high = x - low;
            x += high * edge_amount * 1.15f + copysignf(transient, x) * edge_amount * 2.4f;
        }
        data[i] = x;
    }
    if (fabsf(body_amount) > 0.0001f || edge_amount > 0.0001f)
        pr13_match_peak(data, frames, original_peak);
}

static void pr13_shaper(float *data, size_t frames, const TsProcessRecipe *recipe)
{
    float mix;
    float drive;
    if (data == NULL || recipe == NULL || !recipe->shaper_enabled || recipe->shaper_mix <= 0.0f) return;
    mix = clampf(recipe->shaper_mix, 0.0f, 1.0f);
    drive = 1.0f + (clampf(recipe->shaper_drive, 1.0f, 16.0f) - 1.0f) * 1.75f;
    for (size_t i = 0; i < frames; ++i) {
        float dry = data[i];
        float wet = dry * drive;
        if (recipe->shaper_mode == TS_SHAPER_CLIP) {
            wet = clampf(wet, -1.0f, 1.0f);
        } else if (recipe->shaper_mode == TS_SHAPER_FOLD) {
            for (int fold = 0; fold < 32 && (wet > 1.0f || wet < -1.0f); ++fold)
                wet = wet > 1.0f ? 2.0f - wet : -2.0f - wet;
            wet = clampf(wet, -1.0f, 1.0f);
        } else wet = tanhf(wet);
        data[i] = clampf(dry * (1.0f - mix) + wet * mix, -1.0f, 1.0f);
    }
}

static void pr13_drift(float *data, size_t frames, float drift)
{
    float *copy;
    long shift;
    if (data == NULL || frames < 2u) return;
    shift = lroundf((clampf(drift, 0.0f, 1.0f) - 0.5f) * (float)frames);
    if (shift == 0) return;
    copy = (float *)malloc(frames * sizeof(float));
    if (copy == NULL) return;
    memcpy(copy, data, frames * sizeof(float));
    for (size_t i = 0; i < frames; ++i) {
        long source = (long)i - shift;
        while (source < 0) source += (long)frames;
        while (source >= (long)frames) source -= (long)frames;
        data[i] = copy[(size_t)source];
    }
    free(copy);
}

void ts_process_recipe_reset(TsProcessRecipe *process)
{
    ts_process_recipe_reset_legacy(process);
    if (process != NULL) process->drift = 0.5f;
}

int ts_sample_process(TsSample *sample, const TsSample *parent, size_t first, size_t last,
                      const TsProcessRecipe *recipe, char *error, size_t error_size)
{
    TsProcessRecipe base;
    int ok;
    if (recipe == NULL) { set_error(error, error_size, "Invalid processing recipe"); return 0; }
    base = *recipe;
    base.body = 0.5f;
    base.edge = 0.0f;
    base.drift = 0.0f;
    base.shaper_enabled = 0;
    ok = ts_sample_process_legacy(sample, parent, first, last, &base, error, error_size);
    if (!ok) return 0;
    pr13_body_edge(sample->data, sample->frames, recipe->body, recipe->edge);
    pr13_shaper(sample->data, sample->frames, recipe);
    pr13_drift(sample->data, sample->frames, recipe->drift);
    set_error(error, error_size, "");
    return 1;
}

void ts_instrument_init(TsInstrument *instrument)
{
    ts_instrument_init_legacy(instrument);
    if (instrument != NULL) instrument->active_bank_slot = -1;
}

int ts_instrument_generate(TsInstrument *instrument, TsGeneratorKind kind, uint32_t seed,
                           char *error, size_t error_size)
{
    int ok = ts_instrument_generate_legacy(instrument, kind, seed, error, error_size);
    if (ok) instrument->active_bank_slot = 0;
    return ok;
}

int ts_instrument_load_wav(TsInstrument *instrument, const char *path,
                           char *error, size_t error_size)
{
    int ok = ts_instrument_load_wav_legacy(instrument, path, error, error_size);
    if (ok) instrument->active_bank_slot = 0;
    return ok;
}

#define PR13_EDIT_WRAPPER(name, legacy, args, callargs) \
int name args { int ok; if (!pr13_guard_edit(instrument, error, error_size)) return 0; ok = legacy callargs; if (ok) pr13_sync_active(instrument); return ok; }
PR13_EDIT_WRAPPER(ts_instrument_set_process, ts_instrument_set_process_legacy,
    (TsInstrument *instrument, const TsProcessRecipe *process, char *error, size_t error_size),
    (instrument, process, error, error_size))
PR13_EDIT_WRAPPER(ts_instrument_set_process_and_tuning, ts_instrument_set_process_and_tuning_legacy,
    (TsInstrument *instrument, const TsProcessRecipe *process, const TsTuning *tuning, char *error, size_t error_size),
    (instrument, process, tuning, error, error_size))
PR13_EDIT_WRAPPER(ts_instrument_set_process_and_tunings, ts_instrument_set_process_and_tunings_legacy,
    (TsInstrument *instrument, const TsProcessRecipe *process, const TsTuning *tuning, const TsTuning *audible_tuning, char *error, size_t error_size),
    (instrument, process, tuning, audible_tuning, error, error_size))
PR13_EDIT_WRAPPER(ts_instrument_set_tuning, ts_instrument_set_tuning_legacy,
    (TsInstrument *instrument, int root_note, float fine_tune_cents, char *error, size_t error_size),
    (instrument, root_note, fine_tune_cents, error, error_size))
PR13_EDIT_WRAPPER(ts_instrument_set_audible_tuning, ts_instrument_set_audible_tuning_legacy,
    (TsInstrument *instrument, int root_note, float fine_tune_cents, char *error, size_t error_size),
    (instrument, root_note, fine_tune_cents, error, error_size))
PR13_EDIT_WRAPPER(ts_instrument_reset_current, ts_instrument_reset_current_legacy,
    (TsInstrument *instrument, char *error, size_t error_size), (instrument, error, error_size))
PR13_EDIT_WRAPPER(ts_instrument_commit_current, ts_instrument_commit_current_legacy,
    (TsInstrument *instrument, char *error, size_t error_size), (instrument, error, error_size))
PR13_EDIT_WRAPPER(ts_instrument_set_loop_from_selection, ts_instrument_set_loop_from_selection_legacy,
    (TsInstrument *instrument, char *error, size_t error_size), (instrument, error, error_size))
PR13_EDIT_WRAPPER(ts_instrument_clear_loop, ts_instrument_clear_loop_legacy,
    (TsInstrument *instrument, char *error, size_t error_size), (instrument, error, error_size))
PR13_EDIT_WRAPPER(ts_instrument_set_loop_crossfade, ts_instrument_set_loop_crossfade_legacy,
    (TsInstrument *instrument, float milliseconds, char *error, size_t error_size),
    (instrument, milliseconds, error, error_size))
PR13_EDIT_WRAPPER(ts_instrument_set_loop_mode, ts_instrument_set_loop_mode_legacy,
    (TsInstrument *instrument, TsLoopMode mode, char *error, size_t error_size),
    (instrument, mode, error, error_size))
PR13_EDIT_WRAPPER(ts_instrument_crop_selection, ts_instrument_crop_selection_legacy,
    (TsInstrument *instrument, char *error, size_t error_size), (instrument, error, error_size))
PR13_EDIT_WRAPPER(ts_instrument_apply_sample_edit, ts_instrument_apply_sample_edit_legacy,
    (TsInstrument *instrument, TsSampleEditKind kind, float amount, char *error, size_t error_size),
    (instrument, kind, amount, error, error_size))
PR13_EDIT_WRAPPER(ts_instrument_undo, ts_instrument_undo_legacy,
    (TsInstrument *instrument, char *error, size_t error_size), (instrument, error, error_size))
PR13_EDIT_WRAPPER(ts_instrument_redo, ts_instrument_redo_legacy,
    (TsInstrument *instrument, char *error, size_t error_size), (instrument, error, error_size))
PR13_EDIT_WRAPPER(ts_instrument_apply_tape_drag, ts_instrument_apply_tape_drag_legacy,
    (TsInstrument *instrument, TsPostEditKind kind, size_t first, size_t last, int64_t destination, char *error, size_t error_size),
    (instrument, kind, first, last, destination, error, error_size))
#undef PR13_EDIT_WRAPPER

int ts_instrument_set_bank_as_current(TsInstrument *instrument, int slot,
                                      char *error, size_t error_size)
{
    int ok;
    if (instrument == NULL || slot < 0 || slot >= TS_BANK_SLOT_COUNT || !instrument->bank[slot].occupied) {
        set_error(error, error_size, "Choose a filled family slot");
        return 0;
    }
    ok = ts_instrument_set_bank_as_current_legacy(instrument, slot, error, error_size);
    if (ok) instrument->active_bank_slot = slot;
    return ok;
}

int ts_instrument_bank_toggle_lock(TsInstrument *instrument, int slot,
                                   char *error, size_t error_size)
{
    if (instrument == NULL || slot < 0 || slot >= TS_BANK_SLOT_COUNT || !instrument->bank[slot].occupied) {
        set_error(error, error_size, "Choose a filled family slot to lock");
        return 0;
    }
    instrument->bank[slot].locked = !instrument->bank[slot].locked;
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_bank_is_locked(const TsInstrument *instrument, int slot)
{
    return instrument != NULL && slot >= 0 && slot < TS_BANK_SLOT_COUNT &&
           instrument->bank[slot].occupied && instrument->bank[slot].locked;
}

int ts_instrument_bank_clear(TsInstrument *instrument, int slot,
                             char *error, size_t error_size)
{
    if (ts_pr13_lock_request) {
        ts_pr13_lock_request = 0;
        return ts_instrument_bank_toggle_lock(instrument, slot, error, error_size);
    }
    if (ts_instrument_bank_is_locked(instrument, slot)) { set_error(error, error_size, "Family slot is locked"); return 0; }
    return ts_instrument_bank_clear_legacy(instrument, slot, error, error_size);
}

int ts_instrument_bank_rename(TsInstrument *instrument, int slot, const char *name,
                              char *error, size_t error_size)
{
    if (ts_instrument_bank_is_locked(instrument, slot)) { set_error(error, error_size, "Family slot is locked"); return 0; }
    return ts_instrument_bank_rename_legacy(instrument, slot, name, error, error_size);
}

#define PR13_BANK_GUARD(name, legacy, args, callargs) \
int name args { if (ts_instrument_bank_is_locked(instrument, slot)) { set_error(error, error_size, "Family slot is locked"); return 0; } return legacy callargs; }
PR13_BANK_GUARD(ts_instrument_bank_set_loop_full, ts_instrument_bank_set_loop_full_legacy,
    (TsInstrument *instrument, int slot, char *error, size_t error_size), (instrument, slot, error, error_size))
PR13_BANK_GUARD(ts_instrument_bank_clear_loop, ts_instrument_bank_clear_loop_legacy,
    (TsInstrument *instrument, int slot, char *error, size_t error_size), (instrument, slot, error, error_size))
PR13_BANK_GUARD(ts_instrument_bank_set_loop_crossfade, ts_instrument_bank_set_loop_crossfade_legacy,
    (TsInstrument *instrument, int slot, float milliseconds, char *error, size_t error_size),
    (instrument, slot, milliseconds, error, error_size))
PR13_BANK_GUARD(ts_instrument_bank_set_loop_mode, ts_instrument_bank_set_loop_mode_legacy,
    (TsInstrument *instrument, int slot, TsLoopMode mode, char *error, size_t error_size),
    (instrument, slot, mode, error, error_size))
#undef PR13_BANK_GUARD

int ts_instrument_bank_move_loop_endpoint(TsInstrument *instrument, int slot, int endpoint, size_t frame)
{
    if (ts_instrument_bank_is_locked(instrument, slot)) return 0;
    return ts_instrument_bank_move_loop_endpoint_legacy(instrument, slot, endpoint, frame);
}

static float pr13_similarity(TsFamilyRelation relation)
{
    if (relation == TS_FAMILY_CHILD) return 0.50f;
    if (relation == TS_FAMILY_COUSIN) return 0.25f;
    return 0.05f;
}

static void pr13_blend_inheritance(TsBankSlot *candidate, const TsBankSlot *anchor,
                                   TsFamilyRelation relation, float mutation)
{
    float inherited = 1.0f - clampf(mutation, 0.0f, 1.0f) * (1.0f - pr13_similarity(relation));
    float target_peak;
    if (candidate == NULL || anchor == NULL || candidate->sample.data == NULL || anchor->sample.data == NULL) return;
    target_peak = fmaxf(ts_sample_peak(&candidate->sample), ts_sample_peak(&anchor->sample));
    for (size_t i = 0; i < candidate->sample.frames; ++i) {
        size_t source_at = candidate->sample.frames > 1u ?
            (size_t)((double)i * (double)(anchor->sample.frames - 1u) /
                     (double)(candidate->sample.frames - 1u)) : 0u;
        candidate->sample.data[i] = anchor->sample.data[source_at] * inherited +
                                    candidate->sample.data[i] * (1.0f - inherited);
    }
    pr13_match_peak(candidate->sample.data, candidate->sample.frames, target_peak);
}

int ts_instrument_generate_family_candidate(TsInstrument *instrument,
                                            int anchor_slot, int reseed,
                                            int *created_slot,
                                            char *error, size_t error_size)
{
    int ok, active, old_last, old_trajectory, source_anchor;
    TsFamilyRelation relation;
    float mutation;
    if (instrument == NULL) return 0;
    active = instrument->active_bank_slot;
    if (active < 0 || active >= TS_BANK_SLOT_COUNT || !instrument->bank[active].occupied)
        active = anchor_slot >= 0 && anchor_slot < TS_BANK_SLOT_COUNT && instrument->bank[anchor_slot].occupied ? anchor_slot : 0;
    source_anchor = active;
    relation = instrument->family_relation;
    mutation = clampf(instrument->family_mutation, 0.0f, 1.0f);
    old_last = instrument->family_last_slot;
    old_trajectory = instrument->family_trajectory;
    instrument->family_trajectory = 0;
    if (reseed && active > 0 && instrument->bank[active].parent_slot >= 0 &&
        instrument->bank[active].parent_slot < TS_BANK_SLOT_COUNT &&
        instrument->bank[instrument->bank[active].parent_slot].occupied) {
        instrument->family_last_slot = active;
        source_anchor = instrument->bank[active].parent_slot;
        instrument->family_relation = instrument->bank[active].relation;
        instrument->family_mutation = instrument->bank[active].lineage_mutation;
    }
    ok = ts_instrument_generate_family_candidate_legacy(instrument, source_anchor, reseed,
                                                         created_slot, error, error_size);
    if (ok && created_slot != NULL && *created_slot >= 0) {
        int made = *created_slot;
        int blend_anchor = instrument->bank[made].parent_slot;
        if (blend_anchor >= 0 && blend_anchor < TS_BANK_SLOT_COUNT && instrument->bank[blend_anchor].occupied)
            pr13_blend_inheritance(&instrument->bank[made], &instrument->bank[blend_anchor],
                                   instrument->bank[made].relation,
                                   instrument->bank[made].lineage_mutation);
        if (ts_instrument_set_bank_as_current_legacy(instrument, made, error, error_size))
            instrument->active_bank_slot = made;
    }
    instrument->family_trajectory = old_trajectory;
    instrument->family_relation = relation;
    instrument->family_mutation = mutation;
    if (!ok) instrument->family_last_slot = old_last;
    return ok;
}

int ts_instrument_save_recipe(const TsInstrument *instrument, const char *path,
                              char *error, size_t error_size)
{
    TsInstrument encoded;
    unsigned active;
    if (instrument == NULL) return ts_instrument_save_recipe_legacy(instrument, path, error, error_size);
    encoded = *instrument;
    active = instrument->active_bank_slot >= 0 && instrument->active_bank_slot < TS_BANK_SLOT_COUNT ?
             (unsigned)(instrument->active_bank_slot + 1) : 0u;
    encoded.family_sequence = (instrument->family_sequence & PR13_SEQUENCE_MASK) |
                              ((active & 0x1fu) << PR13_ACTIVE_SHIFT);
    for (int i = 0; i < TS_BANK_SLOT_COUNT; ++i) {
        encoded.bank[i].trajectory_step = instrument->bank[i].trajectory_step & ~PR13_LOCK_BIT;
        if (instrument->bank[i].locked) encoded.bank[i].trajectory_step |= PR13_LOCK_BIT;
    }
    return ts_instrument_save_recipe_legacy(&encoded, path, error, error_size);
}

int ts_instrument_load_recipe(TsInstrument *instrument, const char *path,
                              char *error, size_t error_size)
{
    int ok = ts_instrument_load_recipe_legacy(instrument, path, error, error_size);
    unsigned encoded_active;
    if (!ok || instrument == NULL) return ok;
    encoded_active = (instrument->family_sequence >> PR13_ACTIVE_SHIFT) & 0x1fu;
    instrument->family_sequence &= PR13_SEQUENCE_MASK;
    instrument->active_bank_slot = instrument->bank[0].occupied ? 0 : -1;
    for (int i = 0; i < TS_BANK_SLOT_COUNT; ++i) {
        instrument->bank[i].locked = (instrument->bank[i].trajectory_step & PR13_LOCK_BIT) != 0u;
        instrument->bank[i].trajectory_step &= ~PR13_LOCK_BIT;
    }
    if (encoded_active > 0u) {
        int slot = (int)encoded_active - 1;
        if (slot >= 0 && slot < TS_BANK_SLOT_COUNT && instrument->bank[slot].occupied)
            instrument->active_bank_slot = slot;
    }
    set_error(error, error_size, "");
    return 1;
}
