#include "tapesister/pr13.h"

#include <stdio.h>
#include <string.h>

static void set_error13(char *error, size_t size, const char *message)
{
    if (error != NULL && size > 0) snprintf(error, size, "%s", message);
}

void ts_pr13_neutral_process(TsProcessRecipe *process)
{
    if (process == NULL) return;
    ts_process_recipe_reset(process);
    process->body = 0.0f;
    process->edge = 0.0f;
    process->drift = 0.5f;
}

static TsProcessRecipe legacy_neutral(TsProcessRecipe process)
{
    process.body = 0.5f;
    process.edge = 0.0f;
    process.drift = 0.0f;
    return process;
}

static int editable(const TsInstrument *instrument, int slot,
                    char *error, size_t error_size)
{
    if (slot >= 0 && ts_pr13_slot_locked(instrument, slot)) {
        set_error13(error, error_size, "Family slot is locked - unlock it before editing");
        return 0;
    }
    return 1;
}

static int is_terminal_fade(TsPostEditKind kind)
{
    return kind == TS_POST_FADE_IN || kind == TS_POST_FADE_OUT;
}

static void hide_terminal_fades(TsInstrument *instrument, TsPostEdit *saved, int *saved_count)
{
    int kept = 0;
    *saved_count = instrument->post_edit_count;
    memcpy(saved, instrument->post_edits, sizeof(instrument->post_edits));
    for (int i = 0; i < *saved_count; ++i)
        if (!is_terminal_fade(saved[i].kind)) instrument->post_edits[kept++] = saved[i];
    instrument->post_edit_count = kept;
}

static void restore_terminal_stack(TsInstrument *instrument, const TsPostEdit *saved, int saved_count)
{
    memcpy(instrument->post_edits, saved, sizeof(instrument->post_edits));
    instrument->post_edit_count = saved_count;
}

static void apply_terminal_fades(TsSample *sample, const TsPostEdit *saved, int saved_count)
{
    if (sample == NULL || sample->data == NULL) return;
    for (int index = 0; index < saved_count; ++index) {
        const TsPostEdit *operation = &saved[index];
        size_t first, last, length;
        if (!is_terminal_fade(operation->kind)) continue;
        first = operation->first > sample->frames ? sample->frames : operation->first;
        last = operation->last > sample->frames ? sample->frames : operation->last;
        if (last <= first) continue;
        length = last - first;
        for (size_t i = 0; i < length; ++i) {
            float gain = length > 1u ?
                (operation->kind == TS_POST_FADE_IN ?
                 (float)i / (float)(length - 1u) :
                 (float)(length - 1u - i) / (float)(length - 1u)) : 1.0f;
            sample->data[first + i] *= gain;
        }
    }
}

static void init_empty_slot13(TsBankSlot *slot)
{
    TsTuning tuning = {TS_KEYBOARD_BASE_NOTE, 0.0f};
    memset(slot, 0, sizeof(*slot));
    ts_sample_init(&slot->sample);
    slot->tuning = tuning;
    slot->audible_tuning = tuning;
    slot->loop_crossfade_ms = 8.0f;
    slot->relation = TS_FAMILY_CAPTURED;
    slot->parent_slot = -1;
    slot->lineage_mutation = 0.35f;
    ts_pr13_neutral_process(&slot->process);
}

static TsProcessRecipe inherited_process(const TsInstrument *instrument, int slot)
{
    TsProcessRecipe process;
    const TsBankSlot *selected = &instrument->bank[slot];
    if (selected->has_process) return selected->process;
    if (selected->parent_slot >= 0 && selected->parent_slot < TS_BANK_SLOT_COUNT &&
        instrument->bank[selected->parent_slot].occupied &&
        instrument->bank[selected->parent_slot].has_process)
        return instrument->bank[selected->parent_slot].process;
    ts_pr13_neutral_process(&process);
    return process;
}

int ts_pr13_sync_active_slot(TsInstrument *instrument, int active_slot,
                             char *error, size_t error_size)
{
    TsBankSlot *slot;
    char name[128];
    if (instrument == NULL || active_slot < 0 || active_slot >= TS_BANK_SLOT_COUNT ||
        !instrument->bank[active_slot].occupied) {
        set_error13(error, error_size, "");
        return 1;
    }
    if (!editable(instrument, active_slot, error, error_size)) return 0;
    slot = &instrument->bank[active_slot];
    snprintf(name, sizeof(name), "%s", slot->sample.name);
    if (!ts_sample_clone(&slot->sample, &instrument->current, error, error_size)) return 0;
    snprintf(slot->sample.name, sizeof(slot->sample.name), "%s", name);
    slot->tuning = instrument->tuning;
    slot->audible_tuning = instrument->audible_tuning;
    slot->process = instrument->process;
    slot->has_process = 1;
    slot->has_loop = instrument->has_loop;
    slot->loop_first = instrument->loop_first;
    slot->loop_last = instrument->loop_last;
    slot->loop_crossfade_ms = instrument->loop_crossfade_ms;
    slot->loop_mode = instrument->loop_mode;
    set_error13(error, error_size, "");
    return 1;
}

int ts_pr13_activate_slot(TsInstrument *instrument, int active_slot,
                          char *error, size_t error_size)
{
    TsProcessRecipe process;
    if (instrument == NULL || active_slot < 0 || active_slot >= TS_BANK_SLOT_COUNT ||
        !instrument->bank[active_slot].occupied) {
        set_error13(error, error_size, "Audition a filled bank slot first");
        return 0;
    }
    process = inherited_process(instrument, active_slot);
    if (!ts_instrument_set_bank_as_current(instrument, active_slot, error, error_size)) return 0;
    instrument->process = process;
    instrument->bank[active_slot].process = process;
    instrument->bank[active_slot].has_process = 1;
    set_error13(error, error_size, "");
    return 1;
}

int ts_pr13_bank_capture(TsInstrument *instrument, int active_slot, int slot,
                         TsBankCaptureKind kind, char *error, size_t error_size)
{
    (void)active_slot;
    if (!ts_instrument_bank_capture(instrument, slot, kind, error, error_size)) return 0;
    instrument->bank[slot].process = instrument->process;
    instrument->bank[slot].has_process = 1;
    return 1;
}

int ts_pr13_bank_clear(TsInstrument *instrument, int slot,
                       char *error, size_t error_size)
{
    int fallback = -1;
    if (instrument == NULL || slot < 0 || slot >= TS_BANK_SLOT_COUNT) {
        set_error13(error, error_size, "Invalid bank slot");
        return 0;
    }
    if (!instrument->bank[slot].occupied) {
        set_error13(error, error_size, "Bank slot is already empty");
        return 0;
    }
    ts_sample_free(&instrument->bank[slot].sample);
    init_empty_slot13(&instrument->bank[slot]);
    if (instrument->family_last_slot == slot) instrument->family_last_slot = -1;
    if (instrument->family_anchor_slot == slot) {
        for (int i = 0; i < TS_BANK_SLOT_COUNT; ++i) {
            if (instrument->bank[i].occupied) { fallback = i; break; }
        }
        instrument->family_anchor_slot = fallback;
    }
    set_error13(error, error_size, "");
    return 1;
}

static int consolidate_if_full(TsInstrument *instrument, int need_sample, int need_post,
                               char *error, size_t error_size)
{
    size_t selection_first;
    size_t selection_last;
    int has_selection;
    if ((!need_sample || instrument->sample_edit_count < TS_SAMPLE_EDIT_DEPTH) &&
        (!need_post || instrument->post_edit_count < TS_POST_EDIT_DEPTH)) return 1;
    selection_first = instrument->selection_first;
    selection_last = instrument->selection_last;
    has_selection = instrument->has_selection;
    if (!ts_instrument_commit_current(instrument, error, error_size)) return 0;
    ts_pr13_neutral_process(&instrument->process);
    if (has_selection && selection_first < selection_last &&
        selection_last <= instrument->current.frames) {
        instrument->selection_first = selection_first;
        instrument->selection_last = selection_last;
        instrument->has_selection = 1;
    }
    set_error13(error, error_size, "");
    return 1;
}

int ts_pr13_apply_sample_edit(TsInstrument *instrument, int active_slot,
                              TsSampleEditKind kind, float amount,
                              char *error, size_t error_size)
{
    int before_sample;
    int before_post;
    int is_fade = kind == TS_SAMPLE_EDIT_FADE_IN || kind == TS_SAMPLE_EDIT_FADE_OUT;
    if (instrument == NULL || !editable(instrument, active_slot, error, error_size)) return 0;
    if (!consolidate_if_full(instrument, !is_fade && instrument->post_edit_count == 0,
                             is_fade || instrument->post_edit_count > 0,
                             error, error_size)) return 0;
    before_sample = instrument->sample_edit_count;
    before_post = instrument->post_edit_count;
    if (!ts_instrument_apply_sample_edit(instrument, kind, amount, error, error_size)) return 0;
    if (is_fade && instrument->sample_edit_count == before_sample + 1 &&
        instrument->post_edit_count == before_post) {
        TsSampleEdit edit = instrument->sample_edits[before_sample];
        TsPostEdit post;
        if (instrument->post_edit_count >= TS_POST_EDIT_DEPTH &&
            !consolidate_if_full(instrument, 0, 1, error, error_size)) return 0;
        memset(&post, 0, sizeof(post));
        post.kind = kind == TS_SAMPLE_EDIT_FADE_IN ? TS_POST_FADE_IN : TS_POST_FADE_OUT;
        post.first = edit.first;
        post.last = edit.last;
        post.amount = edit.amount;
        instrument->sample_edit_count = before_sample;
        instrument->post_edits[instrument->post_edit_count++] = post;
    }
    return ts_pr13_rerender(instrument, active_slot, error, error_size);
}

int ts_pr13_crop_selection(TsInstrument *instrument, int active_slot,
                           char *error, size_t error_size)
{
    if (instrument == NULL || !editable(instrument, active_slot, error, error_size)) return 0;
    if (!consolidate_if_full(instrument, 0, instrument->post_edit_count > 0,
                             error, error_size)) return 0;
    if (!ts_instrument_crop_selection(instrument, error, error_size)) return 0;
    return ts_pr13_rerender(instrument, active_slot, error, error_size);
}

int ts_pr13_apply_tape_drag(TsInstrument *instrument, int active_slot,
                            TsPostEditKind kind, size_t first, size_t last,
                            int64_t destination, char *error, size_t error_size)
{
    if (instrument == NULL || !editable(instrument, active_slot, error, error_size)) return 0;
    if (!consolidate_if_full(instrument, 0, 1, error, error_size)) return 0;
    if (!ts_instrument_apply_tape_drag(instrument, kind, first, last, destination,
                                       error, error_size)) return 0;
    return ts_pr13_rerender(instrument, active_slot, error, error_size);
}

int ts_pr13_set_process_and_tunings(TsInstrument *instrument, int active_slot,
                                    const TsProcessRecipe *process,
                                    const TsTuning *tuning,
                                    const TsTuning *audible_tuning,
                                    char *error, size_t error_size)
{
    TsProcessRecipe core_process;
    TsPostEdit saved_post[TS_POST_EDIT_DEPTH];
    int saved_post_count;
    int before_undo;
    if (instrument == NULL || process == NULL || tuning == NULL || audible_tuning == NULL) {
        set_error13(error, error_size, "Invalid PR13 process settings");
        return 0;
    }
    if (!editable(instrument, active_slot, error, error_size)) return 0;
    core_process = legacy_neutral(*process);
    before_undo = instrument->undo_count;
    hide_terminal_fades(instrument, saved_post, &saved_post_count);
    if (!ts_instrument_set_process_and_tunings(instrument, &core_process, tuning,
                                               audible_tuning, error, error_size)) {
        restore_terminal_stack(instrument, saved_post, saved_post_count);
        return 0;
    }
    restore_terminal_stack(instrument, saved_post, saved_post_count);
    if (instrument->undo_count > before_undo) {
        TsEditSnapshot *prior = &instrument->undo[instrument->undo_count - 1];
        memcpy(prior->post_edits, saved_post, sizeof(prior->post_edits));
        prior->post_edit_count = saved_post_count;
    }
    instrument->process = *process;
    if (!ts_pr13_apply_body_edge_drift(&instrument->current,
                                       process->body, process->edge, process->drift,
                                       error, error_size)) return 0;
    apply_terminal_fades(&instrument->current, saved_post, saved_post_count);
    return ts_pr13_sync_active_slot(instrument, active_slot, error, error_size);
}

int ts_pr13_set_process(TsInstrument *instrument, int active_slot,
                        const TsProcessRecipe *process,
                        char *error, size_t error_size)
{
    return ts_pr13_set_process_and_tunings(instrument, active_slot, process,
                                           &instrument->tuning,
                                           &instrument->audible_tuning,
                                           error, error_size);
}

int ts_pr13_set_process_and_tuning(TsInstrument *instrument, int active_slot,
                                   const TsProcessRecipe *process,
                                   const TsTuning *tuning,
                                   char *error, size_t error_size)
{
    return ts_pr13_set_process_and_tunings(instrument, active_slot, process,
                                           tuning, tuning, error, error_size);
}

int ts_pr13_rerender(TsInstrument *instrument, int active_slot,
                     char *error, size_t error_size)
{
    TsEditSnapshot undo[TS_HISTORY_DEPTH];
    TsEditSnapshot redo[TS_HISTORY_DEPTH];
    TsPostEdit saved_post[TS_POST_EDIT_DEPTH];
    int saved_post_count;
    int undo_count;
    int redo_count;
    TsProcessRecipe desired;
    TsProcessRecipe core_process;
    if (instrument == NULL || !editable(instrument, active_slot, error, error_size)) return 0;
    desired = instrument->process;
    core_process = legacy_neutral(desired);
    memcpy(undo, instrument->undo, sizeof(undo));
    memcpy(redo, instrument->redo, sizeof(redo));
    undo_count = instrument->undo_count;
    redo_count = instrument->redo_count;
    hide_terminal_fades(instrument, saved_post, &saved_post_count);
    if (!ts_instrument_set_process_and_tunings(instrument, &core_process,
                                               &instrument->tuning,
                                               &instrument->audible_tuning,
                                               error, error_size)) {
        restore_terminal_stack(instrument, saved_post, saved_post_count);
        return 0;
    }
    restore_terminal_stack(instrument, saved_post, saved_post_count);
    memcpy(instrument->undo, undo, sizeof(undo));
    memcpy(instrument->redo, redo, sizeof(redo));
    instrument->undo_count = undo_count;
    instrument->redo_count = redo_count;
    instrument->process = desired;
    if (!ts_pr13_apply_body_edge_drift(&instrument->current,
                                       desired.body, desired.edge, desired.drift,
                                       error, error_size)) return 0;
    apply_terminal_fades(&instrument->current, saved_post, saved_post_count);
    return ts_pr13_sync_active_slot(instrument, active_slot, error, error_size);
}
