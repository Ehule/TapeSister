#include "tapesister/pr13.h"

#include <stdio.h>
#include <string.h>

static void set_error13(char *error, size_t size, const char *message)
{
    if (error != NULL && size > 0) snprintf(error, size, "%s", message);
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
    slot->has_loop = instrument->has_loop;
    slot->loop_first = instrument->loop_first;
    slot->loop_last = instrument->loop_last;
    slot->loop_crossfade_ms = instrument->loop_crossfade_ms;
    slot->loop_mode = instrument->loop_mode;
    set_error13(error, error_size, "");
    return 1;
}

int ts_pr13_set_process_and_tunings(TsInstrument *instrument, int active_slot,
                                    const TsProcessRecipe *process,
                                    const TsTuning *tuning,
                                    const TsTuning *audible_tuning,
                                    char *error, size_t error_size)
{
    TsProcessRecipe core_process;
    if (instrument == NULL || process == NULL || tuning == NULL || audible_tuning == NULL) {
        set_error13(error, error_size, "Invalid PR13 process settings");
        return 0;
    }
    if (!editable(instrument, active_slot, error, error_size)) return 0;
    core_process = legacy_neutral(*process);
    if (!ts_instrument_set_process_and_tunings(instrument, &core_process, tuning,
                                               audible_tuning, error, error_size)) return 0;
    instrument->process = *process;
    if (!ts_pr13_apply_body_edge_drift(&instrument->current,
                                       process->body, process->edge, process->drift,
                                       error, error_size)) return 0;
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
    if (!ts_instrument_set_process_and_tunings(instrument, &core_process,
                                               &instrument->tuning,
                                               &instrument->audible_tuning,
                                               error, error_size)) return 0;
    memcpy(instrument->undo, undo, sizeof(undo));
    memcpy(instrument->redo, redo, sizeof(redo));
    instrument->undo_count = undo_count;
    instrument->redo_count = redo_count;
    instrument->process = desired;
    if (!ts_pr13_apply_body_edge_drift(&instrument->current,
                                       desired.body, desired.edge, desired.drift,
                                       error, error_size)) return 0;
    return ts_pr13_sync_active_slot(instrument, active_slot, error, error_size);
}
