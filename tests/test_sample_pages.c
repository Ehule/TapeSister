#include "tapesister/sample_pages.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define TS_RMDIR(path) _rmdir(path)
#else
#include <unistd.h>
#define TS_RMDIR(path) rmdir(path)
#endif

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static int add_sound(TsInstrument *instrument, int slot, float value)
{
    char error[160];
    if (!ts_instrument_select_bank(instrument, slot, error, sizeof(error)) ||
        !ts_instrument_activate_silence(instrument, 8u, 48000,
                                        error, sizeof(error))) return 0;
    for (size_t frame = 0; frame < instrument->current.frames; ++frame)
        instrument->current.data[frame] = value;
    snprintf(instrument->current.name, sizeof(instrument->current.name),
             "SOUND %02d", slot + 1);
    return 1;
}

static void test_bundle_path(void)
{
    char path[512];
    char error[160];
    CHECK(ts_sample_pages_bundle_project_path(
        "sets/Terra Night.tsr", path, sizeof(path), error, sizeof(error)));
    CHECK(strcmp(path, "sets/Terra Night/Terra Night.tsr") == 0);
    CHECK(ts_sample_pages_bundle_project_path(
        path, path, sizeof(path), error, sizeof(error)));
    CHECK(strcmp(path, "sets/Terra Night/Terra Night.tsr") == 0);
    CHECK(ts_sample_pages_bundle_project_path(
        "Portable.tsr", path, sizeof(path), error, sizeof(error)));
    CHECK(strcmp(path, "Portable/Portable.tsr") == 0);
}

static void remove_project(const char *path, size_t pages, int record)
{
    char made[1200];
    const char *slash = strrchr(path, '/');
    if (slash != NULL) {
        size_t directory_length = (size_t)(slash - path);
        char directory[512];
        if (directory_length >= sizeof(directory)) return;
        memcpy(directory, path, directory_length);
        directory[directory_length] = '\0';
        remove(path);
        for (size_t page = 0u; page < pages; ++page) {
            if (page > 0u) {
                snprintf(made, sizeof(made),
                         "%s/project-data/page-%03zu.tsr", directory, page + 1u);
                remove(made);
            }
            for (int slot = 1; slot <= 16; ++slot) {
                snprintf(made, sizeof(made),
                         "%s/samples/page-%03zu/%02d_SOUND-%02d.wav",
                         directory, page + 1u, slot, slot);
                remove(made);
                snprintf(made, sizeof(made),
                         "%s/samples/page-%03zu/%02d_SILENCE.wav",
                         directory, page + 1u, slot);
                remove(made);
            }
            snprintf(made, sizeof(made), "%s/samples/page-%03zu",
                     directory, page + 1u);
            TS_RMDIR(made);
        }
        if (record) {
            snprintf(made, sizeof(made), "%s/project-data/record-bank.tsr",
                     directory);
            remove(made);
            for (int slot = 1; slot <= 16; ++slot) {
                snprintf(made, sizeof(made),
                         "%s/samples/record-bank/%02d_SOUND-%02d.wav",
                         directory, slot, slot);
                remove(made);
                snprintf(made, sizeof(made),
                         "%s/samples/record-bank/%02d_SILENCE.wav",
                         directory, slot);
                remove(made);
            }
            snprintf(made, sizeof(made), "%s/samples/record-bank", directory);
            TS_RMDIR(made);
        }
        snprintf(made, sizeof(made), "%s/manifest.txt", directory); remove(made);
        snprintf(made, sizeof(made), "%s/sister-state.ini", directory); remove(made);
        snprintf(made, sizeof(made), "%s/project-data", directory); TS_RMDIR(made);
        snprintf(made, sizeof(made), "%s/samples", directory); TS_RMDIR(made);
        TS_RMDIR(directory);
        return;
    }
    remove(path);
    for (size_t page = 1u; page < pages; ++page) {
        snprintf(made, sizeof(made), "%s.samples/page-%03zu.tsr", path, page + 1u);
        remove(made);
    }
    if (record) {
        snprintf(made, sizeof(made), "%s.samples/record-bank.tsr", path);
        remove(made);
    }
    snprintf(made, sizeof(made), "%s.samples/manifest.txt", path);
    remove(made);
    snprintf(made, sizeof(made), "%s.samples", path);
    TS_RMDIR(made);
}

static void test_page_cycle_and_keep(void)
{
    TsInstrument active;
    TsInstrument record;
    TsSamplePages pages;
    char error[160];
    size_t copied;
    size_t first;
    size_t last;
    ts_instrument_init(&active);
    ts_instrument_init(&record);
    CHECK(ts_sample_pages_init(&pages, error, sizeof(error)));
    CHECK(ts_sample_pages_count(&pages) == 1u);
    CHECK(ts_sample_pages_switch(&pages, &active, 0u, error, sizeof(error)));
    for (int slot = 0; slot < 15; ++slot) CHECK(add_sound(&active, slot, 0.1f));
    CHECK(add_sound(&record, 0, 0.5f));
    CHECK(add_sound(&record, 1, 0.7f));
    CHECK(ts_sample_pages_keep_record_bank(&pages, &active, &record,
                                           &copied, &first, &last,
                                           error, sizeof(error)));
    CHECK(copied == 2u && first == 1u && last == 2u);
    CHECK(ts_sample_pages_count(&pages) == 2u);
    CHECK(ts_instrument_bank_count(&record) == 0);
    CHECK(active.bank[0].occupied && active.bank[14].occupied);
    CHECK(active.bank[15].occupied);
    CHECK(active.bank[0].sample.data[0] == 0.1f);
    CHECK(active.bank[15].sample.data[0] == 0.5f);
    CHECK(ts_sample_pages_switch(&pages, &active, 1u, error, sizeof(error)));
    CHECK(ts_sample_pages_active(&pages) == 1u);
    CHECK(active.bank[0].occupied);
    CHECK(active.bank[0].sample.data[0] == 0.7f);
    CHECK(ts_sample_pages_switch(&pages, &active, 0u, error, sizeof(error)));
    CHECK(active.bank[0].occupied);
    {
        size_t appended = 0u;
        CHECK(ts_sample_pages_append_and_switch(&pages, &active, &appended,
                                                error, sizeof(error)));
        CHECK(appended == 2u && ts_sample_pages_count(&pages) == 3u);
        CHECK(ts_sample_pages_active(&pages) == 2u);
        CHECK(ts_instrument_bank_count(&active) == 0);
        CHECK(active.selected_slot == 0);
        CHECK(ts_sample_pages_switch(&pages, &active, 1u,
                                     error, sizeof(error)));
        CHECK(active.bank[0].occupied && active.bank[0].sample.data[0] == 0.7f);
        CHECK(ts_sample_pages_switch(&pages, &active, 2u,
                                     error, sizeof(error)));
        CHECK(add_sound(&active, 2, 0.9f));
        CHECK(ts_sample_pages_remove_last_and_switch(
            &pages, &active, 0u, error, sizeof(error)));
        CHECK(ts_sample_pages_count(&pages) == 2u &&
              ts_sample_pages_active(&pages) == 0u);
        CHECK(active.bank[0].occupied && active.bank[0].sample.data[0] == 0.1f);
    }
    ts_sample_pages_free(&pages);
    ts_instrument_free(&record);
    ts_instrument_free(&active);
}

static void test_project_roundtrip_with_record_bank(void)
{
    const char *path = "test-paged-project/test-paged-project.tsr";
    TsInstrument active;
    TsInstrument record;
    TsInstrument restored_active;
    TsInstrument restored_record;
    TsSamplePages pages;
    TsSamplePages restored;
    TsSisterProjectState state;
    TsSisterProjectState restored_state;
    char error[160];
    size_t copied;
    int state_present = 0;
    remove_project(path, 2u, 1);
    ts_instrument_init(&active);
    ts_instrument_init(&record);
    ts_instrument_init(&restored_active);
    ts_instrument_init(&restored_record);
    CHECK(ts_sample_pages_init(&pages, error, sizeof(error)));
    CHECK(ts_sample_pages_init(&restored, error, sizeof(error)));
    for (int slot = 0; slot < 16; ++slot) CHECK(add_sound(&active, slot, 0.1f));
    CHECK(add_sound(&record, 0, 0.6f));
    CHECK(add_sound(&record, 1, 0.8f));
    CHECK(ts_sample_pages_keep_record_bank(&pages, &active, &record,
                                           &copied, NULL, NULL,
                                           error, sizeof(error)));
    CHECK(copied == 2u && ts_sample_pages_count(&pages) == 2u);
    CHECK(add_sound(&record, 3, 0.9f));
    CHECK(ts_sample_pages_switch(&pages, &active, 1u, error, sizeof(error)));
    /* Record Bank mode parks every Sample page, so save that exact layout. */
    CHECK(ts_sample_pages_park(&pages, &active, error, sizeof(error)));
    ts_sister_project_state_init(&state, 48000u);
    state.page_count = 2u;
    state.active_page = 1u;
    CHECK(ts_sample_pages_save_project(&pages, NULL, &record, &state,
                                       path, error, sizeof(error)));
    {
        FILE *manifest = fopen("test-paged-project/manifest.txt", "rb");
        FILE *wav = fopen(
            "test-paged-project/samples/page-001/01_SOUND-01.wav", "rb");
        FILE *sister = fopen("test-paged-project/sister-state.ini", "rb");
        CHECK(manifest != NULL);
        CHECK(wav != NULL);
        CHECK(sister != NULL);
        if (manifest != NULL) fclose(manifest);
        if (wav != NULL) fclose(wav);
        if (sister != NULL) fclose(sister);
    }
    CHECK(ts_sister_project_state_load(
        &restored_state, path, 48000u, &state_present,
        error, sizeof(error)));
    CHECK(state_present && restored_state.page_count == 2u &&
          restored_state.active_page == 1u);
    CHECK(ts_sample_pages_unpark(&pages, &active, error, sizeof(error)));
    CHECK(ts_sample_pages_load_project(&restored, &restored_active,
                                       &restored_record, path,
                                       error, sizeof(error)));
    CHECK(ts_sample_pages_count(&restored) == 2u);
    CHECK(ts_sample_pages_active(&restored) == 1u);
    CHECK(ts_instrument_bank_count(&restored_active) == 2);
    CHECK(restored_active.bank[0].occupied && restored_active.bank[1].occupied);
    CHECK(ts_instrument_bank_count(&restored_record) == 1);
    CHECK(restored_record.bank[3].occupied);
    CHECK(ts_sample_pages_switch(&restored, &restored_active, 0u,
                                 error, sizeof(error)));
    CHECK(ts_instrument_bank_count(&restored_active) == 16);

    /* The folder is the portable unit; moving or renaming that folder does not
       introduce an absolute path or force another nested project folder. */
    CHECK(rename("test-paged-project", "test-paged-project-moved") == 0);
    {
        char moved_path[512];
        CHECK(ts_sample_pages_bundle_project_path(
            "test-paged-project-moved/test-paged-project.tsr",
            moved_path, sizeof(moved_path), error, sizeof(error)));
        CHECK(strcmp(moved_path,
                     "test-paged-project-moved/test-paged-project.tsr") == 0);
        CHECK(ts_sample_pages_load_project(
            &restored, &restored_active, &restored_record,
            moved_path, error, sizeof(error)));
        CHECK(ts_sample_pages_count(&restored) == 2u);
    }
    CHECK(rename("test-paged-project-moved", "test-paged-project") == 0);

    /* A later save replaces the complete folder, so removed pages, REC BANK
       audio, and their WAV copies cannot survive as stale project members. */
    CHECK(ts_instrument_bank_clear_all(&record, error, sizeof(error)));
    CHECK(ts_sample_pages_remove_last_and_switch(
        &pages, &active, 0u, error, sizeof(error)));
    state.page_count = 1u;
    state.active_page = 0u;
    CHECK(ts_sample_pages_save_project(
        &pages, &active, &record, &state, path, error, sizeof(error)));
    {
        FILE *stale_page = fopen(
            "test-paged-project/project-data/page-002.tsr", "rb");
        FILE *stale_wav = fopen(
            "test-paged-project/samples/record-bank/04_SILENCE.wav", "rb");
        CHECK(stale_page == NULL);
        CHECK(stale_wav == NULL);
        if (stale_page != NULL) fclose(stale_page);
        if (stale_wav != NULL) fclose(stale_wav);
    }
    ts_sample_pages_free(&restored);
    ts_sample_pages_free(&pages);
    ts_instrument_free(&restored_record);
    ts_instrument_free(&restored_active);
    ts_instrument_free(&record);
    ts_instrument_free(&active);
    remove_project(path, 1u, 0);
}

static void test_legacy_single_page_project(void)
{
    const char *path = "test-legacy-project.tsr";
    TsInstrument legacy;
    TsInstrument active;
    TsInstrument record;
    TsSamplePages pages;
    char error[160];
    remove_project(path, 1u, 0);
    ts_instrument_init(&legacy);
    ts_instrument_init(&active);
    ts_instrument_init(&record);
    CHECK(ts_sample_pages_init(&pages, error, sizeof(error)));
    CHECK(add_sound(&legacy, 4, 0.42f));
    CHECK(ts_instrument_save_recipe(&legacy, path, error, sizeof(error)));
    CHECK(ts_sample_pages_load_project(&pages, &active, &record,
                                       path, error, sizeof(error)));
    CHECK(ts_sample_pages_count(&pages) == 1u);
    CHECK(ts_sample_pages_active(&pages) == 0u);
    CHECK(active.bank[4].occupied);
    CHECK(ts_instrument_bank_count(&record) == 0);
    ts_sample_pages_free(&pages);
    ts_instrument_free(&record);
    ts_instrument_free(&active);
    ts_instrument_free(&legacy);
    remove_project(path, 1u, 0);
}

static void test_legacy_split_project(void)
{
    const char *path = "test-legacy-split.tsr";
    const char *directory = "test-legacy-split.tsr.samples";
    TsInstrument first;
    TsInstrument second;
    TsInstrument saved_record;
    TsInstrument active;
    TsInstrument record;
    TsSamplePages pages;
    char error[160];
    FILE *manifest;
    remove_project(path, 2u, 1);
    ts_instrument_init(&first);
    ts_instrument_init(&second);
    ts_instrument_init(&saved_record);
    ts_instrument_init(&active);
    ts_instrument_init(&record);
    CHECK(ts_sample_pages_init(&pages, error, sizeof(error)));
    CHECK(add_sound(&first, 0, 0.1f));
    CHECK(add_sound(&second, 2, 0.2f));
    CHECK(add_sound(&saved_record, 4, 0.3f));
    CHECK(ts_instrument_save_recipe(&first, path, error, sizeof(error)));
#ifdef _WIN32
    CHECK(_mkdir(directory) == 0);
#else
    CHECK(mkdir(directory, 0775) == 0);
#endif
    CHECK(ts_instrument_save_recipe(
        &second, "test-legacy-split.tsr.samples/page-002.tsr",
        error, sizeof(error)));
    CHECK(ts_instrument_save_recipe(
        &saved_record, "test-legacy-split.tsr.samples/record-bank.tsr",
        error, sizeof(error)));
    manifest = fopen("test-legacy-split.tsr.samples/manifest.txt", "wb");
    CHECK(manifest != NULL);
    if (manifest != NULL) {
        fputs("TAPESISTER_PAGES 1\npage_count=2\nactive_page=1\nrecord_bank=1\n",
              manifest);
        fclose(manifest);
    }
    CHECK(ts_sample_pages_load_project(
        &pages, &active, &record, path, error, sizeof(error)));
    CHECK(ts_sample_pages_count(&pages) == 2u);
    CHECK(ts_sample_pages_active(&pages) == 1u);
    CHECK(active.bank[2].occupied);
    CHECK(record.bank[4].occupied);
    ts_sample_pages_free(&pages);
    ts_instrument_free(&record);
    ts_instrument_free(&active);
    ts_instrument_free(&saved_record);
    ts_instrument_free(&second);
    ts_instrument_free(&first);
    remove_project(path, 2u, 1);
}

static void test_unrelated_folder_is_never_replaced(void)
{
    const char *directory = "test-refuse-project";
    const char *sentinel = "test-refuse-project/keep.txt";
    const char *path = "test-refuse-project/test-refuse-project.tsr";
    TsInstrument active;
    TsInstrument record;
    TsSamplePages pages;
    char error[160];
    FILE *file;
    remove(sentinel);
    TS_RMDIR(directory);
#ifdef _WIN32
    CHECK(_mkdir(directory) == 0);
#else
    CHECK(mkdir(directory, 0775) == 0);
#endif
    file = fopen(sentinel, "wb");
    CHECK(file != NULL);
    if (file != NULL) {
        fputs("do not replace\n", file);
        fclose(file);
    }
    ts_instrument_init(&active);
    ts_instrument_init(&record);
    CHECK(ts_sample_pages_init(&pages, error, sizeof(error)));
    CHECK(add_sound(&active, 0, 0.25f));
    CHECK(!ts_sample_pages_save_project(
        &pages, &active, &record, NULL, path, error, sizeof(error)));
    file = fopen(sentinel, "rb");
    CHECK(file != NULL);
    if (file != NULL) fclose(file);
    ts_sample_pages_free(&pages);
    ts_instrument_free(&record);
    ts_instrument_free(&active);
    remove(sentinel);
    TS_RMDIR(directory);
}

int main(void)
{
    test_bundle_path();
    test_page_cycle_and_keep();
    test_project_roundtrip_with_record_bank();
    test_legacy_single_page_project();
    test_legacy_split_project();
    test_unrelated_folder_is_never_replaced();
    if (failures != 0) {
        fprintf(stderr, "%d Sample page test(s) failed\n", failures);
        return 1;
    }
    puts("paged Sample Banks, KEEP, and project persistence tests passed");
    return 0;
}
