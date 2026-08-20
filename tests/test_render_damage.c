#include "tapesister/render_damage.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WIDTH = 640, HEIGHT = 400 };

static int contains(const TsRenderDamagePlan *plan, int x, int y)
{
    for (int i = 0; i < plan->count; ++i) {
        const TsRenderDamageRect *rect = &plan->rects[i];
        if (x >= rect->x && x < rect->x + rect->w &&
            y >= rect->y && y < rect->y + rect->h)
            return 1;
    }
    return 0;
}

static void assert_valid_nonoverlapping(const TsRenderDamagePlan *plan)
{
    for (int i = 0; i < plan->count; ++i) {
        const TsRenderDamageRect *left = &plan->rects[i];
        assert(left->x >= 0 && left->y >= 0);
        assert(left->w > 0 && left->h > 0);
        assert(left->x + left->w <= WIDTH);
        assert(left->y + left->h <= HEIGHT);
        for (int j = i + 1; j < plan->count; ++j) {
            const TsRenderDamageRect *right = &plan->rects[j];
            int overlaps = left->x < right->x + right->w &&
                           left->x + left->w > right->x &&
                           left->y < right->y + right->h &&
                           left->y + left->h > right->y;
            assert(!overlaps);
        }
    }
}

static void test_first_identical_and_playhead(void)
{
    size_t count = (size_t)WIDTH * HEIGHT;
    uint32_t *previous = calloc(count, sizeof(*previous));
    uint32_t *current = calloc(count, sizeof(*current));
    TsRenderDamagePlan plan;
    assert(previous != NULL && current != NULL);
    assert(ts_render_damage_plan(current, NULL, WIDTH, HEIGHT, &plan));
    assert(plan.full_frame && plan.count == 1 && plan.damaged_pixels == count);
    assert(!ts_render_damage_plan(current, previous, WIDTH, HEIGHT, &plan));

    for (int y = 64; y < 198; ++y) previous[(size_t)y * WIDTH + 120u] = 1u;
    for (int y = 64; y < 198; ++y) current[(size_t)y * WIDTH + 122u] = 1u;
    assert(ts_render_damage_plan(current, previous, WIDTH, HEIGHT, &plan));
    assert(!plan.full_frame);
    assert(contains(&plan, 120, 100));
    assert(contains(&plan, 122, 100));
    assert(plan.damaged_pixels < count / 10u);
    assert_valid_nonoverlapping(&plan);
    printf("Representative playhead damage: %.2f%% of full frame\n",
           100.0 * (double)plan.damaged_pixels / (double)count);
    free(current);
    free(previous);
}

static void test_boundaries_and_disjoint_regions(void)
{
    size_t count = (size_t)WIDTH * HEIGHT;
    uint32_t *previous = calloc(count, sizeof(*previous));
    uint32_t *current = calloc(count, sizeof(*current));
    TsRenderDamagePlan plan;
    static const int points[][2] = {
        {0, 0}, {15, 15}, {16, 16}, {WIDTH - 1, 0},
        {0, HEIGHT - 1}, {WIDTH - 1, HEIGHT - 1}, {319, 207}
    };
    assert(previous != NULL && current != NULL);
    for (size_t i = 0; i < sizeof(points) / sizeof(points[0]); ++i)
        current[(size_t)points[i][1] * WIDTH + (size_t)points[i][0]] =
            0xff000000u | (uint32_t)i;
    assert(ts_render_damage_plan(current, previous, WIDTH, HEIGHT, &plan));
    assert(!plan.full_frame && plan.count >= 3);
    for (size_t i = 0; i < sizeof(points) / sizeof(points[0]); ++i)
        assert(contains(&plan, points[i][0], points[i][1]));
    assert_valid_nonoverlapping(&plan);
    free(current);
    free(previous);
}

static void test_fragment_and_large_fallbacks(void)
{
    size_t count = (size_t)WIDTH * HEIGHT;
    uint32_t *previous = calloc(count, sizeof(*previous));
    uint32_t *current = calloc(count, sizeof(*current));
    TsRenderDamagePlan plan;
    assert(previous != NULL && current != NULL);
    for (int tile_y = 0; tile_y < HEIGHT / TS_RENDER_DAMAGE_TILE_H; ++tile_y) {
        for (int tile_x = tile_y & 1; tile_x < WIDTH / TS_RENDER_DAMAGE_TILE_W;
             tile_x += 2) {
            size_t at = (size_t)(tile_y * TS_RENDER_DAMAGE_TILE_H) * WIDTH +
                        (size_t)(tile_x * TS_RENDER_DAMAGE_TILE_W);
            current[at] = 1u;
        }
    }
    assert(ts_render_damage_plan(current, previous, WIDTH, HEIGHT, &plan));
    assert(plan.full_frame && plan.count == 1);

    memset(current, 0, count * sizeof(*current));
    for (int y = 0; y < HEIGHT * 3 / 4; ++y)
        for (int x = 0; x < WIDTH; ++x)
            current[(size_t)y * WIDTH + (size_t)x] = 2u;
    assert(ts_render_damage_plan(current, previous, WIDTH, HEIGHT, &plan));
    assert(plan.full_frame && plan.damaged_pixels == count);
    free(current);
    free(previous);
}

static void test_incremental_snapshot(void)
{
    size_t count = (size_t)WIDTH * HEIGHT;
    uint32_t *snapshot = calloc(count, sizeof(*snapshot));
    uint32_t *current = calloc(count, sizeof(*current));
    TsRenderDamagePlan plan;
    assert(snapshot != NULL && current != NULL);
    current[10u * WIDTH + 10u] = 1u;
    current[200u * WIDTH + 500u] = 2u;
    assert(ts_render_damage_plan(current, snapshot, WIDTH, HEIGHT, &plan));
    assert(!plan.full_frame);
    ts_render_damage_snapshot_commit(snapshot, current, WIDTH, &plan);
    assert(memcmp(snapshot, current, count * sizeof(*current)) == 0);

    current[10u * WIDTH + 10u] = 0u;
    current[11u * WIDTH + 11u] = 3u;
    assert(ts_render_damage_plan(current, snapshot, WIDTH, HEIGHT, &plan));
    ts_render_damage_snapshot_commit(snapshot, current, WIDTH, &plan);
    assert(memcmp(snapshot, current, count * sizeof(*current)) == 0);
    free(current);
    free(snapshot);
}

int main(void)
{
    test_first_identical_and_playhead();
    test_boundaries_and_disjoint_regions();
    test_fragment_and_large_fallbacks();
    test_incremental_snapshot();
    puts("Render damage tests passed.");
    return 0;
}
