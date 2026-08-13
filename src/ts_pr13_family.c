#include "tapesister/pr13.h"

int ts_pr13_generate_family_candidate(TsInstrument *instrument, int active_slot,
                                      int reseed, int *created_slot,
                                      char *error, size_t error_size)
{
    int anchor = active_slot;
    int old_last;
    int old_path;
    float old_mutation;
    TsFamilyRelation old_relation;

    if (instrument == 0) return 0;
    if (anchor < 0 || anchor >= TS_BANK_SLOT_COUNT || !instrument->bank[anchor].occupied)
        anchor = instrument->family_anchor_slot;

    old_last = instrument->family_last_slot;
    old_path = instrument->family_trajectory;
    old_mutation = instrument->family_mutation;
    old_relation = instrument->family_relation;

    if (reseed && active_slot > 0 && active_slot < TS_BANK_SLOT_COUNT &&
        instrument->bank[active_slot].occupied) {
        TsBankSlot *selected = &instrument->bank[active_slot];
        if (selected->parent_slot >= 0 && selected->parent_slot < TS_BANK_SLOT_COUNT)
            anchor = selected->parent_slot;
        instrument->family_relation = selected->relation;
        instrument->family_mutation = selected->lineage_mutation;
    }

    if (instrument->family_relation == TS_FAMILY_CHILD)
        instrument->family_mutation *= 0.50f;
    else if (instrument->family_relation == TS_FAMILY_COUSIN)
        instrument->family_mutation *= 0.75f;
    else if (instrument->family_relation == TS_FAMILY_STRANGER)
        instrument->family_mutation = 0.90f + instrument->family_mutation * 0.09f;

    instrument->family_last_slot = -1;
    instrument->family_trajectory = 0;
    {
        int ok = ts_instrument_generate_family_candidate(instrument, anchor, 0,
                                                          created_slot, error, error_size);
        instrument->family_relation = old_relation;
        instrument->family_mutation = old_mutation;
        instrument->family_trajectory = old_path;
        if (!ok) instrument->family_last_slot = old_last;
        return ok;
    }
}
