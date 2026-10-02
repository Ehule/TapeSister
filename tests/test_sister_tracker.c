#include "tapesister/sample_pages.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* These checks execute in Release too; setup never lives inside assert(). */
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "SisterTracker check failed at %d: %s\n", __LINE__, #condition); \
    exit(1); } } while (0)
static char error[256];

static TsInstrument *instrument(void)
{
    TsInstrument *i = malloc(sizeof(*i));
    CHECK(i);
    ts_instrument_init(i);
    return i;
}
static void release(TsInstrument *i) { ts_instrument_free(i); free(i); }
static void sound(TsInstrument *i, int slot, float value)
{
    CHECK(ts_instrument_select_bank(i, slot, error, sizeof(error)));
    CHECK(ts_instrument_activate_silence(i, 8, 48000, error, sizeof(error)));
    for (size_t f = 0; f < i->current.frames; ++f) i->current.data[f] = value;
    CHECK(ts_instrument_sync_selected(i, error, sizeof(error)));
    CHECK(ts_tile_id_valid(i->bank[slot].tile_id));
}
static void score(TsSisterTracker *t, TsTileId tile)
{
    TsPatternId id;
    uint8_t alias;
    CHECK(ts_sister_tracker_add_pattern(t, 64, &id, error, sizeof(error)));
    CHECK(ts_sister_tracker_bind_tile(t, tile, &alias, error, sizeof(error)));
    CHECK(alias == 1);
    CHECK(ts_sister_tracker_insert_order(t, 0, id, error, sizeof(error)));
    TsTrackerPattern *p = ts_sister_tracker_pattern(t, id);
    p->cells[0][0] = (TsTrackerCell){ .tile_id = tile, .note_kind = TS_TRACKER_NOTE_PITCH,
                                   .note = 0, .has_volume = 1, .volume = 0,
                                   .tune_command = 'M', .tune_value = 60,
                                   .fx_command = 'Z', .fx_value = 0 };
    p->cells[255][7] = (TsTrackerCell){ .tile_id = tile, .note_kind = TS_TRACKER_NOTE_CUT,
                                     .fx_command = '0', .fx_value = 0 };
    t->lanes[7].length = 256;
    t->lanes[7].mode = TS_TRACKER_SONG;
    t->lanes[7].ratio = 16;
    t->lanes[7].trim = 2;
    t->lanes[7].route = TS_TRACKER_ROUTE_MAIN_DIRECT;
    t->lanes[7].output_channel = 3;
    CHECK(ts_sister_tracker_validate(t, error, sizeof(error)));
}

static void test_model(void)
{
    TsSisterTracker t, copy;
    TsPatternId original, duplicate, later;
    TsTileId tile = ts_tile_id_new();
    ts_sister_tracker_init(&t);
    ts_sister_tracker_init(&copy);
    CHECK(!t.pattern_count && !t.order_count && t.bpm == 125 && t.ticks_per_line == 6);
    CHECK(t.control_lane == -1 && t.loop && ts_sister_tracker_validate(&t, error, sizeof(error)));
    score(&t, tile);
    original = t.orders[0];
    CHECK(ts_sister_tracker_set_rows(&t, original, 1));
    CHECK(ts_sister_tracker_pattern(&t, original)->cells[255][7].tile_id == tile);
    CHECK(ts_sister_tracker_copy_pattern(&t, original, &duplicate, error, sizeof(error)));
    CHECK(duplicate != original);
    CHECK(ts_sister_tracker_pattern(&t, duplicate)->cells[255][7].fx_command == '0');
    CHECK(ts_sister_tracker_insert_order(&t, 1, original, error, sizeof(error)));
    CHECK(!ts_sister_tracker_remove_pattern(&t, original, error, sizeof(error)));
    CHECK(ts_sister_tracker_remove_pattern(&t, duplicate, error, sizeof(error)));
    CHECK(ts_sister_tracker_add_pattern(&t, 256, &later, error, sizeof(error)));
    CHECK(later > duplicate);
    t.restart_order = 1;
    CHECK(ts_sister_tracker_insert_order(&t, 0, later, error, sizeof(error)));
    CHECK(t.restart_order == 2);
    CHECK(ts_sister_tracker_remove_order(&t, 0) && t.restart_order == 1);
    CHECK(ts_sister_tracker_clone(&copy, &t, error, sizeof(error)));
    CHECK(ts_sister_tracker_hash(&t) == ts_sister_tracker_hash(&copy));
    ts_sister_tracker_pattern(&copy, original)->cells[0][0].note = 12;
    CHECK(ts_sister_tracker_pattern(&t, original)->cells[0][0].note == 0);
    CHECK(ts_sister_tracker_hash(&t) != ts_sister_tracker_hash(&copy));
    CHECK(ts_sister_tracker_set_rows(&t, original, 256));
    CHECK(ts_sister_tracker_save_file(&t, "score.tst", error, sizeof(error)));
    CHECK(ts_sister_tracker_load_file(&copy, "score.tst", error, sizeof(error)));
    CHECK(ts_sister_tracker_hash(&t) == ts_sister_tracker_hash(&copy));
    uint64_t before = ts_sister_tracker_hash(&copy);
    FILE *f = fopen("score.tst", "r+b"); CHECK(f);
    CHECK(fseek(f, 6, SEEK_SET) == 0 && fputc(2, f) != EOF && fclose(f) == 0);
    CHECK(!ts_sister_tracker_load_file(&copy, "score.tst", error, sizeof(error)));
    CHECK(ts_sister_tracker_hash(&copy) == before);
    f = fopen("score.tst", "wb"); CHECK(f);
    CHECK(fwrite("SISTRK\1\0", 1, 8, f) == 8 && fclose(f) == 0);
    CHECK(!ts_sister_tracker_load_file(&copy, "score.tst", error, sizeof(error)));
    CHECK(ts_sister_tracker_hash(&copy) == before);
    CHECK(ts_sister_tracker_save_file(&t, "score.tst", error, sizeof(error)));
    f = fopen("score.tst", "ab"); CHECK(f);
    CHECK(fputc(0, f) != EOF && fclose(f) == 0);
    CHECK(!ts_sister_tracker_load_file(&copy, "score.tst", error, sizeof(error)));
    CHECK(ts_sister_tracker_hash(&copy) == before);
    t.lanes[0].trim = NAN;
    CHECK(!ts_sister_tracker_validate(&t, error, sizeof(error)));
    t.lanes[0].trim = 1;
    t.orders[0] = 999;
    CHECK(!ts_sister_tracker_validate(&t, error, sizeof(error)));
    t.orders[0] = original;
    t.aliases[2] = tile;
    CHECK(!ts_sister_tracker_validate(&t, error, sizeof(error)));
    t.aliases[2] = 0;
    ts_sister_tracker_pattern(&t, original)->cells[0][0].volume = 0x41;
    CHECK(!ts_sister_tracker_validate(&t, error, sizeof(error)));
    ts_sister_tracker_pattern(&t, original)->cells[0][0].volume = 0;
    CHECK(ts_sister_tracker_validate(&t, error, sizeof(error)));
    /* Reserve a foreign missing ID on load, not just currently present tiles. */
    TsTileId high_id = UINT64_C(1) << 40;
    t.aliases[1] = high_id;
    for (int row = 0; row < TS_TRACKER_ROWS; ++row) for (int lane = 0; lane < TS_TRACKER_LANES; ++lane)
        if (ts_sister_tracker_pattern(&t, original)->cells[row][lane].tile_id == tile)
            ts_sister_tracker_pattern(&t, original)->cells[row][lane].tile_id = high_id;
    CHECK(ts_sister_tracker_save_file(&t, "score.tst", error, sizeof(error)));
    CHECK(ts_sister_tracker_load_file(&copy, "score.tst", error, sizeof(error)));
    CHECK(ts_tile_id_new() > high_id);
    ts_sister_tracker_free(&t);
    ts_sister_tracker_free(&copy);
    remove("score.tst");
}

static void test_limits(void)
{
    TsSisterTracker t;
    TsPatternId id;
    uint8_t alias;
    ts_sister_tracker_init(&t);
    for (int i = 0; i < 256; ++i)
        CHECK(ts_sister_tracker_add_pattern(&t, 64, &id, error, sizeof(error)));
    CHECK(!ts_sister_tracker_add_pattern(&t, 64, &id, error, sizeof(error)) && !id);
    for (int i = 0; i < 256; ++i)
        CHECK(ts_sister_tracker_insert_order(&t, (uint16_t)i, t.patterns[0]->id, error, sizeof(error)));
    CHECK(!ts_sister_tracker_insert_order(&t, 256, t.patterns[0]->id, error, sizeof(error)));
    for (int i = 1; i < 256; ++i) {
        CHECK(ts_sister_tracker_bind_tile(&t, ts_tile_id_new(), &alias, error, sizeof(error)));
        CHECK(alias == i);
    }
    CHECK(ts_sister_tracker_bind_tile(&t, t.aliases[255], &alias, error, sizeof(error)) && alias == 255);
    CHECK(!ts_sister_tracker_bind_tile(&t, ts_tile_id_new(), &alias, error, sizeof(error)) && !alias);
    CHECK(ts_sister_tracker_validate(&t, error, sizeof(error)));
    CHECK(ts_sister_tracker_save_file(&t, "limit.tst", error, sizeof(error)));
    uint64_t hash = ts_sister_tracker_hash(&t);
    CHECK(ts_sister_tracker_load_file(&t, "limit.tst", error, sizeof(error)));
    CHECK(hash == ts_sister_tracker_hash(&t));
    ts_sister_tracker_free(&t);
    remove("limit.tst");
}

static void test_tiles(void)
{
    TsInstrument *active = instrument(), *saved = instrument(), *event = instrument();
    TsSamplePages pages;
    TsTileLocation at;
    CHECK(ts_sample_pages_init(&pages, error, sizeof(error)));
    sound(active, 0, 0.25f);
    TsTileId id = active->bank[0].tile_id;
    score(&pages.tracker, id);
    CHECK(ts_instrument_apply_sample_edit(active, TS_SAMPLE_EDIT_REVERSE, 1.0f, error, sizeof(error)));
    CHECK(active->bank[0].tile_id == id);
    CHECK(ts_instrument_undo(active, error, sizeof(error)) && active->bank[0].tile_id == id);
    CHECK(ts_instrument_redo(active, error, sizeof(error)) && active->bank[0].tile_id == id);
    CHECK(ts_instrument_import_sample(active, &active->current, 0, 0, 0, TS_LOOP_FORWARD,
                                      error, sizeof(error)));
    CHECK(active->bank[0].tile_id == id);
    CHECK(ts_instrument_copy_selected(active, 1, error, sizeof(error)));
    CHECK(active->bank[1].tile_id != id);
    CHECK(ts_instrument_copy_bank_slot_from(event, 0, active, 0, error, sizeof(error)));
    CHECK(event->bank[0].tile_id != id); /* Explicit foreign copy remaps. */
    CHECK(ts_sample_pages_append(&pages, error, sizeof(error)));
    CHECK(ts_sample_pages_move_tile(&pages, active, (TsTileLocation){0, 0},
                                    (TsTileLocation){1, 2}, error, sizeof(error)));
    CHECK(ts_sample_pages_find_tile(&pages, active, id, &at) && at.page == 1 && at.slot == 2);
    CHECK(ts_sample_pages_switch(&pages, active, 1, error, sizeof(error)));
    CHECK(ts_sample_pages_find_tile(&pages, active, id, &at) && at.page == 1 && at.slot == 2);
    CHECK(ts_sample_pages_move_tile(&pages, active, (TsTileLocation){1, 2},
                                    (TsTileLocation){1, 5}, error, sizeof(error)));
    CHECK(ts_sample_pages_find_tile(&pages, active, id, &at) && at.slot == 5);
    CHECK(ts_instrument_clone(saved, active, error, sizeof(error)));
    CHECK(ts_instrument_bank_clear(active, 5, error, sizeof(error)));
    CHECK(!ts_sample_pages_find_tile(&pages, active, id, NULL));
    sound(active, 5, 0.5f);
    CHECK(active->bank[5].tile_id != id && !ts_sample_pages_find_tile(&pages, active, id, NULL));
    CHECK(pages.tracker.aliases[1] == id);
    /* Prepared history snapshots restore identity along with content. */
    CHECK(ts_instrument_clone(active, saved, error, sizeof(error)));
    CHECK(ts_sample_pages_find_tile(&pages, active, id, &at) && at.slot == 5);
    pages.mosaic_bank = active;
    CHECK(ts_sample_pages_find_tile(&pages, event, id, &at) && at.slot == 5);
    CHECK(!ts_sample_pages_find_tile(&pages, event, event->bank[0].tile_id, NULL));
    pages.mosaic_bank = NULL;
    CHECK(ts_sample_pages_park(&pages, active, error, sizeof(error)));
    CHECK(ts_sample_pages_find_tile(&pages, NULL, id, &at) && at.slot == 5);
    CHECK(ts_sample_pages_unpark(&pages, active, error, sizeof(error)));
    CHECK(ts_sample_pages_validate_tile_ids(&pages, active, error, sizeof(error)));
    /* A selected tile's not-yet-flushed audio and its identity both move. */
    CHECK(ts_instrument_select_bank(active, 5, error, sizeof(error)));
    active->current.data[0] = 0.9f;
    CHECK(ts_sample_pages_move_tile(&pages, active, (TsTileLocation){1, 5},
                                    (TsTileLocation){0, 4}, error, sizeof(error)));
    const TsBankSlot *moved = ts_sample_pages_find_tile(&pages, active, id, &at);
    CHECK(moved && at.page == 0 && at.slot == 4 && moved->sample.data[0] == 0.9f);
    ts_sample_pages_free(&pages);
    release(active); release(saved); release(event);
}

static void test_project(void)
{
    const char *path = "tracker-project/tracker-project.tsr";
    const char *tracker_path = "tracker-project/project-data/sister-tracker.tst";
    TsInstrument *active = instrument(), *record = instrument();
    TsInstrument *restored = instrument(), *restored_record = instrument();
    TsSamplePages pages, loaded;
    CHECK(ts_sample_pages_init(&pages, error, sizeof(error)));
    CHECK(ts_sample_pages_init(&loaded, error, sizeof(error)));
    sound(active, 0, 0.25f);
    TsTileId id = active->bank[0].tile_id;
    score(&pages.tracker, id);
    CHECK(ts_sample_pages_append_and_switch(&pages, active, NULL, error, sizeof(error)));
    sound(active, 2, 0.75f);
    sound(record, 3, 0.5f);
    CHECK(ts_sample_pages_save_project(&pages, active, record, NULL, path, error, sizeof(error)));
    CHECK(ts_sample_pages_load_project(&loaded, restored, restored_record, path, error, sizeof(error)));
    CHECK(loaded.active_page == 1 && loaded.page_count == 2);
    CHECK(ts_sister_tracker_hash(&loaded.tracker) == ts_sister_tracker_hash(&pages.tracker));
    CHECK(ts_sample_pages_find_tile(&loaded, restored, id, NULL));
    CHECK(restored_record->bank[3].tile_id == record->bank[3].tile_id);
    uint64_t tracker_hash = ts_sister_tracker_hash(&loaded.tracker);
    TsTileId active_id = restored->bank[2].tile_id;
    TsTileId record_id = restored_record->bank[3].tile_id;
    FILE *f = fopen(tracker_path, "r+b"); CHECK(f);
    CHECK(fseek(f, 6, SEEK_SET) == 0 && fputc(2, f) != EOF && fclose(f) == 0);
    CHECK(!ts_sample_pages_load_project(&loaded, restored, restored_record, path, error, sizeof(error)));
    CHECK(tracker_hash == ts_sister_tracker_hash(&loaded.tracker));
    CHECK(restored->bank[2].tile_id == active_id && restored_record->bank[3].tile_id == record_id);
    CHECK(loaded.active_page == 1 && loaded.page_count == 2);
    CHECK(ts_sample_pages_save_project(&pages, active, record, NULL, path, error, sizeof(error)));
    CHECK(remove(tracker_path) == 0);
    CHECK(!ts_sample_pages_load_project(&loaded, restored, restored_record, path, error, sizeof(error)));
    CHECK(tracker_hash == ts_sister_tracker_hash(&loaded.tracker));
    /* Old manifest with no tracker field migrates to empty defaults. */
    f = fopen("tracker-project/manifest.txt", "wb"); CHECK(f);
    CHECK(fprintf(f, "TAPESISTER_PROJECT 2\nproject=tracker-project.tsr\npage_count=2\nactive_page=1\nrecord_bank=1\n") > 0);
    CHECK(fclose(f) == 0);
    CHECK(ts_sample_pages_load_project(&loaded, restored, restored_record, path, error, sizeof(error)));
    CHECK(!loaded.tracker.pattern_count && loaded.tracker.bpm == 125 && loaded.tracker.control_lane == -1);
    CHECK(ts_sample_pages_save_project(&pages, active, record, NULL, path, error, sizeof(error)));
    /* Invalid definitions leave the previously saved bundle readable. */
    pages.tracker.lanes[0].trim = NAN;
    CHECK(!ts_sample_pages_save_project(&pages, active, record, NULL, path, error, sizeof(error)));
    pages.tracker.lanes[0].trim = 1;
    CHECK(ts_sample_pages_load_project(&loaded, restored, restored_record, path, error, sizeof(error)));
    CHECK(tracker_hash == ts_sister_tracker_hash(&loaded.tracker));
    TsInstrument *first = ts_sample_pages_page_mut(&pages, active, 0);
    TsTileId second = active->bank[2].tile_id;
    active->bank[2].tile_id = first->bank[0].tile_id;
    CHECK(!ts_sample_pages_validate_tile_ids(&pages, active, error, sizeof(error)));
    CHECK(!ts_sample_pages_save_project(&pages, active, record, NULL, path, error, sizeof(error)));
    CHECK(ts_instrument_save_recipe(active, "tracker-project/project-data/page-002.tsr", error, sizeof(error)));
    CHECK(!ts_sample_pages_load_project(&loaded, restored, restored_record, path, error, sizeof(error)));
    CHECK(ts_sister_tracker_hash(&loaded.tracker) == tracker_hash && restored->bank[2].tile_id == active_id);
    active->bank[2].tile_id = second;
    /* Deleted/missing aliases survive save/load without referring to a refill. */
    CHECK(ts_instrument_bank_clear(first, 0, error, sizeof(error)));
    CHECK(ts_sample_pages_save_project(&pages, active, record, NULL, path, error, sizeof(error)));
    CHECK(ts_sample_pages_load_project(&loaded, restored, restored_record, path, error, sizeof(error)));
    CHECK(loaded.tracker.aliases[1] == id && !ts_sample_pages_find_tile(&loaded, restored, id, NULL));
    CHECK(ts_tile_id_new() > id);
    ts_sample_pages_free(&pages); ts_sample_pages_free(&loaded);
    release(active); release(record); release(restored); release(restored_record);
}

static void test_tsr31_migration(void)
{
    TsInstrument *source = instrument(), *loaded = instrument();
    TsInstrument *record = instrument();
    TsSamplePages pages;
    sound(source, 0, 0.25f);
    CHECK(ts_instrument_save_recipe(source, "identity32.tsr", error, sizeof(error)));
    FILE *in = fopen("identity32.tsr", "rb"), *out = fopen("legacy31.tsr", "wb");
    unsigned char header[72]; CHECK(in && out);
    CHECK(fread(header, 1, sizeof(header), in) == sizeof(header));
    /* TSR32 adds the eight-byte identity after the first occupied flag. */
    header[4] = '1';
    CHECK(fwrite(header, 1, 64, out) == 64);
    int byte;
    while ((byte = fgetc(in)) != EOF) CHECK(fputc(byte, out) != EOF);
    CHECK(fclose(in) == 0 && fclose(out) == 0);
    CHECK(ts_sample_pages_init(&pages, error, sizeof(error)));
    CHECK(ts_sample_pages_load_project(&pages, loaded, record, "legacy31.tsr", error, sizeof(error)));
    CHECK(ts_tile_id_valid(loaded->bank[0].tile_id));
    CHECK(loaded->bank[0].tile_id != source->bank[0].tile_id);
    CHECK(ts_sample_hash(&loaded->current) == ts_sample_hash(&source->current));
    CHECK(!pages.tracker.pattern_count && pages.tracker.bpm == 125);
    CHECK(ts_instrument_save_recipe(loaded, "identity32.tsr", error, sizeof(error)));
    TsTileId id = loaded->bank[0].tile_id;
    CHECK(ts_instrument_load_recipe(loaded, "identity32.tsr", error, sizeof(error)));
    CHECK(loaded->bank[0].tile_id == id);
    ts_sample_pages_free(&pages);
    release(source); release(loaded); release(record);
    remove("identity32.tsr"); remove("legacy31.tsr");
}

static void test_identity_exhaustion(void)
{
    TsInstrument *i = instrument();
    sound(i, 0, 0.25f);
    TsTileId original = i->bank[0].tile_id;
    CHECK(!ts_tile_id_valid(0) && !ts_tile_id_valid(UINT64_MAX));
    ts_tile_id_reserve(UINT64_MAX - 1);
    CHECK(!ts_tile_id_new());
    CHECK(!ts_instrument_copy_selected(i, 1, error, sizeof(error)));
    CHECK(!i->bank[1].occupied && i->bank[0].tile_id == original);
    CHECK(ts_instrument_select_bank(i, 1, error, sizeof(error)));
    CHECK(!ts_instrument_activate_silence(i, 8, 48000, error, sizeof(error)));
    CHECK(!i->bank[1].occupied);
    release(i);
}

int main(void)
{
    test_model(); test_limits(); test_tiles(); test_project(); test_tsr31_migration();
    test_identity_exhaustion(); /* Last: deliberately exhaust this test process. */
    puts("SisterTracker model, identity, persistence and migration checks passed");
    return 0;
}
