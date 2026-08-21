#include "tapesister/sample_pages.h"

#include <stdio.h>
#include <string.h>

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

static void remove_project(const char *path, size_t pages, int record)
{
    char made[512];
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
    const char *path = "test-paged-project.tsr";
    TsInstrument active;
    TsInstrument record;
    TsInstrument restored_active;
    TsInstrument restored_record;
    TsSamplePages pages;
    TsSamplePages restored;
    char error[160];
    size_t copied;
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
    CHECK(ts_sample_pages_save_project(&pages, NULL, &record,
                                       path, error, sizeof(error)));
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
    ts_sample_pages_free(&restored);
    ts_sample_pages_free(&pages);
    ts_instrument_free(&restored_record);
    ts_instrument_free(&restored_active);
    ts_instrument_free(&record);
    ts_instrument_free(&active);
    remove_project(path, 2u, 1);
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

int main(void)
{
    test_page_cycle_and_keep();
    test_project_roundtrip_with_record_bank();
    test_legacy_single_page_project();
    if (failures != 0) {
        fprintf(stderr, "%d Sample page test(s) failed\n", failures);
        return 1;
    }
    puts("paged Sample Banks, KEEP, and project persistence tests passed");
    return 0;
}
