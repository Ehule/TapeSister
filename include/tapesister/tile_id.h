#ifndef TAPESISTER_TILE_ID_H
#define TAPESISTER_TILE_ID_H

#include <stdint.h>

/* Project-scoped content identity, independent of slot and audio generation.
   Zero means no tile. Cross-project copies must allocate a fresh identity. */
typedef uint64_t TsTileId;
TsTileId ts_tile_id_new(void);
int ts_tile_id_valid(TsTileId id);
void ts_tile_id_reserve(TsTileId id);

#endif
