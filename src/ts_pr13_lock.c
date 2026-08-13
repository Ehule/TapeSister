#include "tapesister/pr13.h"

#include <stdio.h>

typedef struct {
    const TsInstrument *instrument;
    uint16_t mask;
} LockState;

static LockState states[8];

static LockState *find_state(const TsInstrument *instrument, int create)
{
    LockState *empty = NULL;
    for (int i = 0; i < 8; ++i) {
        if (states[i].instrument == instrument) return &states[i];
        if (empty == NULL && states[i].instrument == NULL) empty = &states[i];
    }
    if (create && empty != NULL) {
        empty->instrument = instrument;
        empty->mask = 0;
        return empty;
    }
    return NULL;
}

uint16_t ts_pr13_lock_mask(const TsInstrument *instrument)
{
    LockState *state = find_state(instrument, 0);
    return state != NULL ? state->mask : 0;
}

void ts_pr13_restore_lock_mask(TsInstrument *instrument, uint16_t mask)
{
    LockState *state = find_state(instrument, 1);
    if (state != NULL) state->mask = mask;
}

int ts_pr13_slot_locked(const TsInstrument *instrument, int slot)
{
    if (instrument == NULL || slot < 0 || slot >= TS_BANK_SLOT_COUNT) return 0;
    return (ts_pr13_lock_mask(instrument) & (uint16_t)(1u << slot)) != 0;
}

int ts_pr13_set_slot_locked(TsInstrument *instrument, int slot, int locked,
                            char *error, size_t error_size)
{
    uint16_t mask;
    if (instrument == NULL || slot < 0 || slot >= TS_BANK_SLOT_COUNT ||
        !instrument->bank[slot].occupied) {
        if (error != NULL && error_size > 0)
            snprintf(error, error_size, "Choose an occupied Family slot");
        return 0;
    }
    mask = ts_pr13_lock_mask(instrument);
    if (locked) mask |= (uint16_t)(1u << slot);
    else mask &= (uint16_t)~(1u << slot);
    ts_pr13_restore_lock_mask(instrument, mask);
    if (error != NULL && error_size > 0) error[0] = '\0';
    return 1;
}

int ts_pr13_toggle_slot_lock(TsInstrument *instrument, int slot,
                             char *error, size_t error_size)
{
    return ts_pr13_set_slot_locked(instrument, slot,
                                   !ts_pr13_slot_locked(instrument, slot),
                                   error, error_size);
}
