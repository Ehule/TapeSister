#include "tapesister/tile_id.h"
#include <stdatomic.h>

static atomic_uint_fast64_t next_id = ATOMIC_VAR_INIT(1);

int ts_tile_id_valid(TsTileId id)
{
    return id != 0 && id < UINT64_MAX;
}

TsTileId ts_tile_id_new(void)
{
    uint_fast64_t next = atomic_load_explicit(&next_id, memory_order_relaxed);
    while (next < UINT64_MAX) {
        if (atomic_compare_exchange_weak_explicit(&next_id, &next, next + 1,
                memory_order_relaxed, memory_order_relaxed)) return next;
    }
    return 0;
}

void ts_tile_id_reserve(TsTileId id)
{
    uint_fast64_t next = atomic_load_explicit(&next_id, memory_order_relaxed);
    if (!ts_tile_id_valid(id)) return;
    while (next <= id && !atomic_compare_exchange_weak_explicit(
            &next_id, &next, id + 1, memory_order_relaxed, memory_order_relaxed)) {}
}
