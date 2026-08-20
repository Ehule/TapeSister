#ifndef TAPESISTER_RENDER_DAMAGE_H
#define TAPESISTER_RENDER_DAMAGE_H

#include <stddef.h>
#include <stdint.h>

enum {
    TS_RENDER_DAMAGE_TILE_W = 16,
    TS_RENDER_DAMAGE_TILE_H = 16,
    TS_RENDER_DAMAGE_MAX_TILES = 2048,
    TS_RENDER_DAMAGE_MAX_RECTS = 32,
    TS_RENDER_DAMAGE_FULL_PERCENT = 70
};

typedef struct {
    int x;
    int y;
    int w;
    int h;
} TsRenderDamageRect;

typedef struct {
    int count;
    int full_frame;
    size_t damaged_pixels;
    TsRenderDamageRect rects[TS_RENDER_DAMAGE_MAX_RECTS];
} TsRenderDamagePlan;

int ts_render_damage_plan(const uint32_t *current, const uint32_t *previous,
                          int width, int height, TsRenderDamagePlan *plan);
void ts_render_damage_snapshot_commit(uint32_t *snapshot,
                                      const uint32_t *current,
                                      int width,
                                      const TsRenderDamagePlan *plan);

#endif
