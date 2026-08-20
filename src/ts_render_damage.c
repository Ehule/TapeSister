#include "tapesister/render_damage.h"

#include <string.h>

static void set_full_damage(TsRenderDamagePlan *plan, int width, int height)
{
    memset(plan, 0, sizeof(*plan));
    plan->count = 1;
    plan->full_frame = 1;
    plan->damaged_pixels = (size_t)width * (size_t)height;
    plan->rects[0].x = 0;
    plan->rects[0].y = 0;
    plan->rects[0].w = width;
    plan->rects[0].h = height;
}

int ts_render_damage_plan(const uint32_t *current, const uint32_t *previous,
                          int width, int height, TsRenderDamagePlan *plan)
{
    unsigned char changed[TS_RENDER_DAMAGE_MAX_TILES];
    int tiles_x;
    int tiles_y;
    int tile_count;
    int any_changed = 0;

    if (current == NULL || plan == NULL || width <= 0 || height <= 0)
        return 0;
    memset(plan, 0, sizeof(*plan));
    if (previous == NULL) {
        set_full_damage(plan, width, height);
        return 1;
    }

    tiles_x = (width + TS_RENDER_DAMAGE_TILE_W - 1) /
              TS_RENDER_DAMAGE_TILE_W;
    tiles_y = (height + TS_RENDER_DAMAGE_TILE_H - 1) /
              TS_RENDER_DAMAGE_TILE_H;
    tile_count = tiles_x * tiles_y;
    if (tile_count <= 0 || tile_count > TS_RENDER_DAMAGE_MAX_TILES) {
        set_full_damage(plan, width, height);
        return 1;
    }
    memset(changed, 0, (size_t)tile_count);

    for (int tile_y = 0; tile_y < tiles_y; ++tile_y) {
        int y0 = tile_y * TS_RENDER_DAMAGE_TILE_H;
        int tile_h = height - y0 < TS_RENDER_DAMAGE_TILE_H ?
                     height - y0 : TS_RENDER_DAMAGE_TILE_H;
        for (int tile_x = 0; tile_x < tiles_x; ++tile_x) {
            int x0 = tile_x * TS_RENDER_DAMAGE_TILE_W;
            int tile_w = width - x0 < TS_RENDER_DAMAGE_TILE_W ?
                         width - x0 : TS_RENDER_DAMAGE_TILE_W;
            for (int row = 0; row < tile_h; ++row) {
                size_t offset = (size_t)(y0 + row) * (size_t)width +
                                (size_t)x0;
                if (memcmp(current + offset, previous + offset,
                           (size_t)tile_w * sizeof(*current)) != 0) {
                    changed[tile_y * tiles_x + tile_x] = 1u;
                    any_changed = 1;
                    break;
                }
            }
        }
    }
    if (!any_changed) return 0;

    /* Convert each horizontal tile run into a rectangle, extending an
       identical run from the preceding tile row. The resulting rectangles
       never overlap, including at partial edge tiles. */
    for (int tile_y = 0; tile_y < tiles_y; ++tile_y) {
        int tile_x = 0;
        while (tile_x < tiles_x) {
            int run_start;
            int run_end;
            int x;
            int y;
            int right;
            int bottom;
            int merged = 0;
            while (tile_x < tiles_x &&
                   !changed[tile_y * tiles_x + tile_x])
                ++tile_x;
            if (tile_x >= tiles_x) break;
            run_start = tile_x;
            while (tile_x < tiles_x &&
                   changed[tile_y * tiles_x + tile_x])
                ++tile_x;
            run_end = tile_x;
            x = run_start * TS_RENDER_DAMAGE_TILE_W;
            y = tile_y * TS_RENDER_DAMAGE_TILE_H;
            right = run_end * TS_RENDER_DAMAGE_TILE_W;
            bottom = y + TS_RENDER_DAMAGE_TILE_H;
            if (right > width) right = width;
            if (bottom > height) bottom = height;

            for (int i = 0; i < plan->count; ++i) {
                TsRenderDamageRect *rect = &plan->rects[i];
                if (rect->x == x && rect->w == right - x &&
                    rect->y + rect->h == y) {
                    rect->h = bottom - rect->y;
                    merged = 1;
                    break;
                }
            }
            if (!merged) {
                TsRenderDamageRect *rect;
                if (plan->count >= TS_RENDER_DAMAGE_MAX_RECTS) {
                    set_full_damage(plan, width, height);
                    return 1;
                }
                rect = &plan->rects[plan->count++];
                rect->x = x;
                rect->y = y;
                rect->w = right - x;
                rect->h = bottom - y;
            }
        }
    }

    for (int i = 0; i < plan->count; ++i) {
        const TsRenderDamageRect *rect = &plan->rects[i];
        plan->damaged_pixels += (size_t)rect->w * (size_t)rect->h;
    }
    if (plan->damaged_pixels * 100u >=
        (size_t)width * (size_t)height * TS_RENDER_DAMAGE_FULL_PERCENT) {
        set_full_damage(plan, width, height);
    }
    return 1;
}

void ts_render_damage_snapshot_commit(uint32_t *snapshot,
                                      const uint32_t *current,
                                      int width,
                                      const TsRenderDamagePlan *plan)
{
    if (snapshot == NULL || current == NULL || width <= 0 || plan == NULL)
        return;
    for (int i = 0; i < plan->count; ++i) {
        const TsRenderDamageRect *rect = &plan->rects[i];
        for (int y = rect->y; y < rect->y + rect->h; ++y) {
            size_t offset = (size_t)y * (size_t)width + (size_t)rect->x;
            memcpy(snapshot + offset, current + offset,
                   (size_t)rect->w * sizeof(*snapshot));
        }
    }
}
