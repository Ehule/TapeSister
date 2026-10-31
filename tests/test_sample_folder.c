#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/sample_folder.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#define remove_dir(p) _rmdir(p)
#else
#include <unistd.h>
#define make_dir(p) mkdir(p, 0775)
#define remove_dir(p) rmdir(p)
#endif

static int cancel_now(void *unused) { (void)unused; return 1; }

static void write_fixture(const char *path, float value, int loop)
{
    float values[64];
    for (size_t i = 0; i < 64; ++i) values[i] = i % 2 ? -value : value;
    TsSample sample = {0};
    sample.data = values; sample.frames = 32; sample.channels = 2; sample.sample_rate = 32000;
    TsTuning tuning = {48, 13.0f};
    char error[160];
    assert(ts_sample_save_wav16_tuned_looped(&sample, &tuning, loop, 4, 28,
        TS_LOOP_PING_PONG, path, error, sizeof(error)));
}

int main(void)
{
    const char *dir = "test-sample-folder";
    char path[256], error[160];
    assert(!make_dir(dir));
    /* Deliberately exceed the file browser's 1,024-entry display limit. */
    for (int i = 0; i < 1030; ++i) {
        snprintf(path, sizeof(path), "%s/note-%04d.txt", dir, i);
        FILE *file = fopen(path, "wb"); assert(file); fclose(file);
    }
    for (int i = 16; i >= 0; --i) {
        snprintf(path, sizeof(path), "%s/s%02d.WAV", dir, i);
        write_fixture(path, .1f + i * .01f, i == 0);
    }
    snprintf(path, sizeof(path), "%s/broken.mp3", dir);
    FILE *file = fopen(path, "wb"); assert(file); fputs("not audio", file); fclose(file);
    snprintf(path, sizeof(path), "%s/nested", dir); assert(!make_dir(path));
    snprintf(path, sizeof(path), "%s/nested/skip.wav", dir); write_fixture(path, .5f, 0);

    TsSamplePages pages;
    static TsInstrument active, record;
    ts_instrument_init(&active); ts_instrument_init(&record);
    assert(ts_sample_pages_init(&pages, error, sizeof(error)));
    assert(ts_instrument_activate_silence(&active, 32, 44100, error, sizeof(error)));
    assert(ts_instrument_sync_selected(&active, error, sizeof(error)));
    /* Locked empty destinations must remain empty, including on other pages. */
    active.bank[1].locked = 1; /* Exercise a protected empty slot from stored state. */
    assert(ts_sample_pages_append(&pages, error, sizeof(error)));
    TsInstrument *second = pages.pages[1];
    second->bank[0].locked = 1;
    const float *audio = active.bank[0].sample.data;
    const float *editor = active.current.data;
    TsTileId id = active.bank[0].tile_id;
    uint64_t hash = ts_sample_hash(&active.current);
    int undo_count = active.undo_count;

    TsSampleFolderImport *load = ts_sample_folder_prepare(&pages, &active, error, sizeof(error));
    assert(load && ts_sample_folder_decode(load, dir, NULL, NULL, error, sizeof(error)));
    assert(!active.bank[2].occupied && pages.page_count == 2); /* Staging is isolated. */
    TsSampleFolderSummary result = ts_sample_folder_summary(load);
    assert(result.imported == 17 && result.failed == 1 && result.skipped == 1031);
    assert(result.first_page == 1 && result.last_page == 2);
    assert(ts_sample_folder_publish(load, &pages, &active, error, sizeof(error)));
    assert(!ts_sample_folder_publish(load, &pages, &active, error, sizeof(error)));
    ts_sample_folder_dispose(load);
    assert(active.bank[0].sample.data == audio && active.bank[0].tile_id == id);
    assert(active.current.data == editor && ts_sample_hash(&active.current) == hash && active.undo_count == undo_count);
    assert(!active.bank[1].occupied && active.bank[1].locked);
    assert(!second->bank[0].occupied && second->bank[0].locked);
    assert(!strcmp(active.bank[2].sample.name, "s00.WAV"));
    assert(active.bank[2].sample.channels == 2 && active.bank[2].sample.sample_rate == 32000);
    assert(active.bank[2].has_loop && active.bank[2].loop_first == 4 && active.bank[2].loop_last == 28);
    assert(active.bank[2].loop_mode == TS_LOOP_PING_PONG);
    assert(active.bank[2].tuning.root_note == 60); /* Same unity-pitch contract as LOAD. */
    assert(!strcmp(second->bank[1].sample.name, "s14.WAV"));
    assert(ts_sample_pages_validate_tile_ids(&pages, &active, error, sizeof(error)));

    /* A changed late destination rejects the entire publication, even earlier empty slots. */
    load = ts_sample_folder_prepare(&pages, &active, error, sizeof(error));
    assert(load && ts_sample_folder_decode(load, dir, NULL, NULL, error, sizeof(error)));
    second->bank[4].locked = 1;
    assert(!ts_sample_folder_publish(load, &pages, &active, error, sizeof(error)));
    assert(!second->bank[4].occupied && !second->bank[5].occupied && pages.page_count == 2);
    ts_sample_folder_dispose(load);

    load = ts_sample_folder_prepare(&pages, &active, error, sizeof(error));
    assert(load && !ts_sample_folder_decode(load, dir, cancel_now, NULL, error, sizeof(error)));
    assert(!ts_sample_folder_publish(load, &pages, &active, error, sizeof(error)));
    ts_sample_folder_dispose(load);
    assert(pages.page_count == 2 && active.bank[0].sample.data == audio);

    /* Overflow adds pages and updates an initially empty selected editor. */
    TsSamplePages blank;
    static TsInstrument empty;
    ts_instrument_init(&empty);
    assert(ts_sample_pages_init(&blank, error, sizeof(error)));
    load = ts_sample_folder_prepare(&blank, &empty, error, sizeof(error));
    assert(load && ts_sample_folder_decode(load, dir, NULL, NULL, error, sizeof(error)));
    assert(ts_sample_folder_publish(load, &blank, &empty, error, sizeof(error)));
    ts_sample_folder_dispose(load);
    assert(blank.page_count == 2 && empty.current.frames == 32 && empty.current.channels == 2);
    assert(empty.has_loop && empty.loop_mode == TS_LOOP_PING_PONG);
    assert(!strcmp(blank.pages[1]->current.name, "s16.WAV"));
    assert(ts_sample_pages_validate_tile_ids(&blank, &empty, error, sizeof(error)));

    /* The parked Sample bank can receive imports while the REC bank stays live. */
    assert(ts_sample_pages_park(&blank, &empty, error, sizeof(error)));
    load = ts_sample_folder_prepare(&blank, &record, error, sizeof(error));
    assert(load && ts_sample_folder_decode(load, dir, NULL, NULL, error, sizeof(error)));
    assert(ts_sample_folder_publish(load, &blank, &record, error, sizeof(error)));
    ts_sample_folder_dispose(load);
    assert(blank.page_count == 3 && !record.current.data && !blank.active_live);
    assert(ts_sample_pages_unpark(&blank, &empty, error, sizeof(error)));
    assert(ts_sample_pages_validate_tile_ids(&blank, &empty, error, sizeof(error)));
    ts_sample_pages_free(&blank); ts_instrument_free(&empty);
    ts_sample_pages_free(&pages); ts_instrument_free(&active); ts_instrument_free(&record);

    for (int i = 0; i < 1030; ++i) {
        snprintf(path, sizeof(path), "%s/note-%04d.txt", dir, i); assert(!remove(path));
    }
    for (int i = 0; i < 17; ++i) {
        snprintf(path, sizeof(path), "%s/s%02d.WAV", dir, i); assert(!remove(path));
    }
    snprintf(path, sizeof(path), "%s/broken.mp3", dir); assert(!remove(path));
    snprintf(path, sizeof(path), "%s/nested/skip.wav", dir); assert(!remove(path));
    snprintf(path, sizeof(path), "%s/nested", dir); assert(!remove_dir(path));
    assert(!remove_dir(dir));
    puts("Sample folder import tests passed");
    return 0;
}
