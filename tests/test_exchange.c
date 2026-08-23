#include "tapesister/exchange.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
#endif

static int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static int test_mkdir(const char *path)
{
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0700);
#endif
}

static int regular_file(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

static void remove_test_folder(const char *folder)
{
    DIR *directory = opendir(folder);
    struct dirent *entry;
    if (directory != NULL) {
        while ((entry = readdir(directory)) != NULL) {
            char path[1200];
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            snprintf(path, sizeof(path), "%s/%s", folder, entry->d_name);
            remove(path);
        }
        closedir(directory);
    }
    rmdir(folder);
}

static int write_incoming_offer(const TsInstrument *source, const char *folder,
                                int unsafe)
{
    FILE *manifest;
    char path[1200];
    if (test_mkdir(folder) != 0) return 0;
    snprintf(path, sizeof(path), "%s/01_alpha.wav", folder);
    if (!ts_sample_save_wav16_tuned_looped(
            &source->bank[0].sample, &source->bank[0].tuning,
            source->bank[0].has_loop, source->bank[0].loop_first,
            source->bank[0].loop_last, source->bank[0].loop_mode,
            path, NULL, 0)) return 0;
    snprintf(path, sizeof(path), "%s/04_delta.wav", folder);
    if (!ts_sample_save_wav16_tuned_looped(
            &source->bank[0].sample, &source->bank[0].tuning,
            source->bank[0].has_loop, source->bank[0].loop_first,
            source->bank[0].loop_last, source->bank[0].loop_mode,
            path, NULL, 0)) return 0;
    snprintf(path, sizeof(path), "%s/%s", folder, TS_EXCHANGE_MANIFEST_NAME);
    manifest = fopen(path, "wb");
    if (manifest == NULL) return 0;
    fprintf(manifest,
            "TAPESISTER_EXCHANGE 1\n"
            "sender=tapehead\n"
            "recipient=tapesister\n"
            "layout=separate_instruments\n"
            "count=2\n"
            "item=1,7,1,%s\n"
            "item=4,8,1,04_delta.wav\n",
            unsafe ? "../01_alpha.wav" : "01_alpha.wav");
    return fclose(manifest) == 0;
}

int main(void)
{
    TsInstrument source;
    TsInstrument received;
    TsInstrument paged_active;
    TsSamplePages pages;
    TsExchangeOffer offer;
    char destination[TS_EXCHANGE_PATH_MAX] = "";
    char pages_destination[TS_EXCHANGE_PATH_MAX] = "";
    char error[256];
    int created_slot = -1;

    remove_test_folder("test-exchange-root/outgoing_set.partial");
    remove_test_folder("test-exchange-root/incoming_0001");
    remove_test_folder("test-exchange-root/incoming_0002.partial");
    remove_test_folder("test-exchange-root/incoming_corrupt");
    remove_test_folder("test-exchange-root/incoming_bad");
    remove_test_folder("test-exchange-root");
    CHECK(test_mkdir("test-exchange-root") == 0);
    CHECK(ts_exchange_presence_touch("test-exchange-root", "tapesister"));
    CHECK(ts_exchange_presence_active(
        "test-exchange-root", "tapesister", 5));
    CHECK(!ts_exchange_presence_active(
        "test-exchange-root", "tapehead", 5));

    ts_instrument_init(&source);
    ts_instrument_init(&received);
    ts_instrument_init(&paged_active);
    CHECK(ts_sample_pages_init(&pages, error, sizeof(error)));
    ts_exchange_offer_init(&offer);
    CHECK(ts_instrument_generate(&source, TS_GENERATOR_FM, 0x12345678u,
                                 error, sizeof(error)));
    CHECK(ts_instrument_generate_family_candidate(
        &source, 0, 0, &created_slot, error, sizeof(error)));
    CHECK(created_slot > 0);
    CHECK(ts_instrument_bank_count(&source) == 2);

    CHECK(ts_exchange_publish_bank(
        &source, "test-exchange-root", TS_EXCHANGE_LAYOUT_INSTRUMENT_SAMPLES,
        destination, sizeof(destination), error, sizeof(error)));
    CHECK(ts_exchange_offer_load(&offer, destination, error, sizeof(error)));
    CHECK(strcmp(offer.sender, "tapesister") == 0);
    CHECK(strcmp(offer.recipient, "tapehead") == 0);
    CHECK(offer.layout == TS_EXCHANGE_LAYOUT_INSTRUMENT_SAMPLES);
    CHECK(offer.item_count == 2);
    CHECK(offer.items[0].tile == 1 && offer.items[0].instrument == 0 &&
          offer.items[0].sample == 1);
    CHECK(offer.items[1].instrument == 0 &&
          offer.items[1].sample == offer.items[1].tile);
    {
        char manifest[1200];
        snprintf(manifest, sizeof(manifest), "%s/%s", destination,
                 TS_EXCHANGE_MANIFEST_NAME);
        CHECK(regular_file(manifest));
    }

    CHECK(ts_sample_pages_switch(&pages, &paged_active, 0u,
                                 error, sizeof(error)));
    CHECK(ts_instrument_generate(&paged_active, TS_GENERATOR_TONAL, 0x50414731u,
                                 error, sizeof(error)));
    {
        size_t second_page = 0u;
        CHECK(ts_sample_pages_append_and_switch(&pages, &paged_active,
                                                &second_page,
                                                error, sizeof(error)));
        CHECK(second_page == 1u);
    }
    CHECK(ts_instrument_generate(&paged_active, TS_GENERATOR_PULSE, 0x50414732u,
                                 error, sizeof(error)));
    CHECK(ts_exchange_publish_pages(
        &pages, &paged_active, "test-exchange-root",
        pages_destination, sizeof(pages_destination), error, sizeof(error)));
    {
        char manifest_path[1200];
        char contents[2048];
        FILE *manifest;
        size_t used;
        snprintf(manifest_path, sizeof(manifest_path), "%s/%s",
                 pages_destination, TS_EXCHANGE_MANIFEST_NAME);
        manifest = fopen(manifest_path, "rb");
        CHECK(manifest != NULL);
        used = manifest != NULL ? fread(contents, 1, sizeof(contents) - 1u, manifest) : 0u;
        if (manifest != NULL) fclose(manifest);
        contents[used] = '\0';
        CHECK(strstr(contents, "TAPESISTER_EXCHANGE 2\n") != NULL);
        CHECK(strstr(contents, "layout=page_instruments\n") != NULL);
        CHECK(strstr(contents, "count=2\n") != NULL);
        CHECK(strstr(contents, "item=1,1,1,P001_01_") != NULL);
        CHECK(strstr(contents, "item=1,2,1,P002_01_") != NULL);
    }

    CHECK(ts_instrument_generate(&received, TS_GENERATOR_NOISE, 33u,
                                 error, sizeof(error)));
    {
        FILE *file;
        uint64_t before_hash = ts_sample_hash(&received.current);
        CHECK(test_mkdir("test-exchange-root/incoming_corrupt") == 0);
        CHECK(ts_sample_save_wav16(
            &source.bank[0].sample,
            "test-exchange-root/incoming_corrupt/01_alpha.wav",
            error, sizeof(error)));
        file = fopen("test-exchange-root/incoming_corrupt/02_bad.wav", "wb");
        CHECK(file != NULL);
        if (file != NULL) {
            CHECK(fputs("not a wave", file) >= 0);
            CHECK(fclose(file) == 0);
        }
        file = fopen("test-exchange-root/incoming_corrupt/exchange.tsexchange", "wb");
        CHECK(file != NULL);
        if (file != NULL) {
            CHECK(fputs(
                "TAPESISTER_EXCHANGE 1\n"
                "sender=tapehead\nrecipient=tapesister\n"
                "layout=instrument_samples\ncount=2\n"
                "item=1,4,1,01_alpha.wav\n"
                "item=2,4,2,02_bad.wav\n", file) >= 0);
            CHECK(fclose(file) == 0);
        }
        CHECK(ts_exchange_offer_load(
            &offer, "test-exchange-root/incoming_corrupt", error, sizeof(error)));
        CHECK(!ts_exchange_import_offer(&received, &offer, error, sizeof(error)));
        CHECK(ts_sample_hash(&received.current) == before_hash);
        CHECK(ts_instrument_bank_count(&received) == 1);
        CHECK(!regular_file(
            "test-exchange-root/incoming_corrupt/tapesister.received"));
    }
    remove_test_folder("test-exchange-root/incoming_corrupt");

    CHECK(write_incoming_offer(&source, "test-exchange-root/incoming_0001", 0));
    CHECK(ts_exchange_find_pending("test-exchange-root", &offer,
                                   error, sizeof(error)));
    CHECK(strcmp(offer.sender, "tapehead") == 0);
    CHECK(offer.layout == TS_EXCHANGE_LAYOUT_SEPARATE_INSTRUMENTS);
    CHECK(offer.item_count == 2);
    CHECK(ts_exchange_import_offer(&received, &offer, error, sizeof(error)));
    CHECK(ts_instrument_bank_count(&received) == 2);
    CHECK(received.bank[0].occupied);
    CHECK(!received.bank[1].occupied && !received.bank[2].occupied);
    CHECK(received.bank[3].occupied);
    CHECK(received.selected_slot == 0);
    CHECK(regular_file("test-exchange-root/incoming_0001/tapesister.received"));
    CHECK(!ts_exchange_find_pending("test-exchange-root", &offer,
                                    error, sizeof(error)));

    CHECK(write_incoming_offer(
        &source, "test-exchange-root/incoming_0002.partial", 0));
    CHECK(!ts_exchange_find_pending("test-exchange-root", &offer,
                                    error, sizeof(error)));

    CHECK(write_incoming_offer(&source, "test-exchange-root/incoming_bad", 1));
    CHECK(!ts_exchange_offer_load(&offer, "test-exchange-root/incoming_bad",
                                  error, sizeof(error)));
    CHECK(strstr(error, "invalid item") != NULL);

    ts_instrument_free(&received);
    ts_instrument_free(&source);
    ts_sample_pages_free(&pages);
    ts_instrument_free(&paged_active);
    remove_test_folder(destination);
    remove_test_folder(pages_destination);
    remove_test_folder("test-exchange-root/incoming_0001");
    remove_test_folder("test-exchange-root/incoming_0002.partial");
    remove_test_folder("test-exchange-root/incoming_corrupt");
    remove_test_folder("test-exchange-root/incoming_bad");
    remove("test-exchange-root/.tapesister.running");
    CHECK(rmdir("test-exchange-root") == 0);

    if (failures != 0) {
        fprintf(stderr, "%d exchange test(s) failed\n", failures);
        return 1;
    }
    puts("exchange tests passed");
    return 0;
}
