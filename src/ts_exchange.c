#include "tapesister/exchange.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define ts_rmdir(path) _rmdir(path)
#else
#include <dirent.h>
#include <unistd.h>
#define ts_rmdir(path) rmdir(path)
#endif

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0) snprintf(error, error_size, "%s", message);
}

static int path_join(char *path, size_t path_size,
                     const char *folder, const char *name)
{
    size_t length;
    int written;
    if (path == NULL || path_size == 0 || folder == NULL || folder[0] == '\0' ||
        name == NULL || name[0] == '\0') return 0;
    length = strlen(folder);
    written = snprintf(path, path_size, "%s%s%s", folder,
                       folder[length - 1u] == '/' || folder[length - 1u] == '\\' ?
                       "" : "/", name);
    return written >= 0 && (size_t)written < path_size;
}

static int path_is_directory(const char *path)
{
    struct stat info;
    if (path == NULL || path[0] == '\0' || stat(path, &info) != 0) return 0;
#ifdef _WIN32
    return (info.st_mode & _S_IFDIR) != 0;
#else
    return S_ISDIR(info.st_mode);
#endif
}

static int path_is_regular(const char *path)
{
    struct stat info;
    if (path == NULL || path[0] == '\0' || stat(path, &info) != 0) return 0;
#ifdef _WIN32
    return (info.st_mode & _S_IFREG) != 0;
#else
    return S_ISREG(info.st_mode);
#endif
}

static int safe_filename(const char *name)
{
    const unsigned char *at = (const unsigned char *)name;
    size_t length;
    if (name == NULL || name[0] == '\0') return 0;
    length = strlen(name);
    if (length >= TS_EXCHANGE_FILENAME_MAX || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0) return 0;
    while (*at != '\0') {
        if (!( (*at >= 'a' && *at <= 'z') || (*at >= 'A' && *at <= 'Z') ||
               (*at >= '0' && *at <= '9') || *at == '-' || *at == '_' ||
               *at == '.' )) return 0;
        ++at;
    }
    return strstr(name, "..") == NULL;
}

static void bank_safe_name(const char *source, char *destination, size_t size)
{
    size_t used = 0;
    if (size == 0) return;
    while (source != NULL && *source != '\0' && used + 1u < size) {
        unsigned char c = (unsigned char)*source++;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_')
            destination[used++] = (char)c;
        else if (used > 0 && destination[used - 1u] != '_')
            destination[used++] = '_';
    }
    while (used > 0 && destination[used - 1u] == '_') --used;
    destination[used] = '\0';
    if (used == 0) snprintf(destination, size, "sample");
}

void ts_exchange_offer_init(TsExchangeOffer *offer)
{
    if (offer == NULL) return;
    memset(offer, 0, sizeof(*offer));
    offer->layout = TS_EXCHANGE_LAYOUT_INSTRUMENT_SAMPLES;
}

const char *ts_exchange_layout_name(TsExchangeLayout layout)
{
    return layout == TS_EXCHANGE_LAYOUT_SEPARATE_INSTRUMENTS ?
           "separate_instruments" : "instrument_samples";
}

static int parse_layout(const char *value, TsExchangeLayout *layout)
{
    if (strcmp(value, "instrument_samples") == 0) {
        *layout = TS_EXCHANGE_LAYOUT_INSTRUMENT_SAMPLES;
        return 1;
    }
    if (strcmp(value, "separate_instruments") == 0) {
        *layout = TS_EXCHANGE_LAYOUT_SEPARATE_INSTRUMENTS;
        return 1;
    }
    return 0;
}

static void trim_line(char *line)
{
    size_t length = strlen(line);
    while (length > 0 && (line[length - 1u] == '\n' || line[length - 1u] == '\r'))
        line[--length] = '\0';
}

static int copy_field(char *destination, size_t destination_size,
                      const char *source)
{
    int written = snprintf(destination, destination_size, "%s", source);
    return written >= 0 && (size_t)written < destination_size;
}

int ts_exchange_offer_load(TsExchangeOffer *offer, const char *folder,
                           char *error, size_t error_size)
{
    TsExchangeOffer loaded;
    FILE *file;
    char manifest[TS_EXCHANGE_PATH_MAX];
    char line[512];
    int declared_count = -1;
    int saw_header = 0;
    int saw_sender = 0;
    int saw_recipient = 0;
    int saw_layout = 0;
    int used_tiles[TS_BANK_SLOT_COUNT] = {0};
    ts_exchange_offer_init(&loaded);
    if (!path_is_directory(folder) ||
        !path_join(manifest, sizeof(manifest), folder, TS_EXCHANGE_MANIFEST_NAME)) {
        set_error(error, error_size, "Invalid exchange offer folder");
        return 0;
    }
    file = fopen(manifest, "rb");
    if (file == NULL) {
        set_error(error, error_size, "Exchange manifest was not found");
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *value;
        trim_line(line);
        if (!saw_header) {
            if (strcmp(line, "TAPESISTER_EXCHANGE 1") != 0) {
                fclose(file);
                set_error(error, error_size, "Unsupported exchange manifest version");
                return 0;
            }
            saw_header = 1;
            continue;
        }
        value = strchr(line, '=');
        if (value == NULL) continue;
        *value++ = '\0';
        if (strcmp(line, "sender") == 0) {
            saw_sender = copy_field(loaded.sender, sizeof(loaded.sender), value);
        } else if (strcmp(line, "recipient") == 0) {
            saw_recipient = copy_field(loaded.recipient, sizeof(loaded.recipient), value);
        } else if (strcmp(line, "layout") == 0) {
            saw_layout = parse_layout(value, &loaded.layout);
        } else if (strcmp(line, "count") == 0) {
            char tail;
            if (sscanf(value, "%d%c", &declared_count, &tail) != 1)
                declared_count = -1;
        } else if (strcmp(line, "item") == 0) {
            TsExchangeItem item;
            char filename[TS_EXCHANGE_FILENAME_MAX];
            char tail;
            char wav_path[TS_EXCHANGE_PATH_MAX];
            if (loaded.item_count >= TS_BANK_SLOT_COUNT ||
                sscanf(value, "%d,%d,%d,%255[^,\r\n]%c",
                       &item.tile, &item.instrument, &item.sample,
                       filename, &tail) != 4 ||
                item.tile < 1 || item.tile > TS_BANK_SLOT_COUNT ||
                item.instrument < 0 || item.instrument > 255 ||
                item.sample < 1 || item.sample > 255 ||
                used_tiles[item.tile - 1] || !safe_filename(filename) ||
                !path_join(wav_path, sizeof(wav_path), folder, filename) ||
                !path_is_regular(wav_path)) {
                fclose(file);
                set_error(error, error_size, "Exchange manifest contains an invalid item");
                return 0;
            }
            item.filename[0] = '\0';
            if (!copy_field(item.filename, sizeof(item.filename), filename)) {
                fclose(file);
                set_error(error, error_size, "Exchange filename is too long");
                return 0;
            }
            used_tiles[item.tile - 1] = 1;
            loaded.items[loaded.item_count++] = item;
        }
    }
    if (ferror(file)) {
        fclose(file);
        set_error(error, error_size, "Could not read exchange manifest");
        return 0;
    }
    fclose(file);
    if (!saw_header || !saw_sender || !saw_recipient || !saw_layout ||
        declared_count <= 0 || declared_count != loaded.item_count) {
        set_error(error, error_size, "Exchange manifest is incomplete");
        return 0;
    }
    if (!copy_field(loaded.folder, sizeof(loaded.folder), folder)) {
        set_error(error, error_size, "Exchange offer path is too long");
        return 0;
    }
    *offer = loaded;
    set_error(error, error_size, "");
    return 1;
}

static int offer_is_received(const char *folder)
{
    char path[TS_EXCHANGE_PATH_MAX];
    return path_join(path, sizeof(path), folder, TS_EXCHANGE_RECEIVED_NAME) &&
           path_is_regular(path);
}

static int consider_pending(const char *root, const char *name,
                            TsExchangeOffer *best, int *found)
{
    TsExchangeOffer candidate;
    char folder[TS_EXCHANGE_PATH_MAX];
    char ignored[160];
    size_t name_length = strlen(name);
    if ((name_length >= 8u && strcmp(name + name_length - 8u, ".partial") == 0) ||
        !path_join(folder, sizeof(folder), root, name) ||
        !path_is_directory(folder) || offer_is_received(folder) ||
        !ts_exchange_offer_load(&candidate, folder, ignored, sizeof(ignored)) ||
        strcmp(candidate.recipient, "tapesister") != 0 ||
        strcmp(candidate.sender, "tapehead") != 0) return 1;
    if (!*found || strcmp(candidate.folder, best->folder) > 0) {
        *best = candidate;
        *found = 1;
    }
    return 1;
}

int ts_exchange_find_pending(const char *exchange_root, TsExchangeOffer *offer,
                             char *error, size_t error_size)
{
    TsExchangeOffer best;
    int found = 0;
    ts_exchange_offer_init(&best);
    if (!path_is_directory(exchange_root)) {
        set_error(error, error_size, "FT2 exchange path is not an existing folder");
        return 0;
    }
#ifdef _WIN32
    {
        struct _finddata_t entry;
        intptr_t handle;
        char pattern[TS_EXCHANGE_PATH_MAX];
        if (!path_join(pattern, sizeof(pattern), exchange_root, "*")) {
            set_error(error, error_size, "FT2 exchange path is too long");
            return 0;
        }
        handle = _findfirst(pattern, &entry);
        if (handle != -1) {
            do {
                if ((entry.attrib & _A_SUBDIR) != 0 && strcmp(entry.name, ".") != 0 &&
                    strcmp(entry.name, "..") != 0)
                    (void)consider_pending(exchange_root, entry.name, &best, &found);
            } while (_findnext(handle, &entry) == 0);
            _findclose(handle);
        }
    }
#else
    {
        DIR *directory = opendir(exchange_root);
        struct dirent *entry;
        if (directory == NULL) {
            snprintf(error, error_size, "Could not open FT2 exchange path: %s",
                     strerror(errno));
            return 0;
        }
        while ((entry = readdir(directory)) != NULL)
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
                (void)consider_pending(exchange_root, entry->d_name, &best, &found);
        closedir(directory);
    }
#endif
    if (!found) {
        set_error(error, error_size, "No pending Tapehead transfer");
        return 0;
    }
    *offer = best;
    set_error(error, error_size, "");
    return 1;
}

static int write_manifest(const TsInstrument *instrument, const char *folder,
                          TsExchangeLayout layout,
                          char *error, size_t error_size)
{
    char path[TS_EXCHANGE_PATH_MAX];
    char temporary[TS_EXCHANGE_PATH_MAX];
    FILE *file;
    if (!path_join(path, sizeof(path), folder, TS_EXCHANGE_MANIFEST_NAME) ||
        snprintf(temporary, sizeof(temporary), "%s.tmp", path) < 0 ||
        strlen(path) + 4u >= sizeof(temporary)) {
        set_error(error, error_size, "Exchange manifest path is too long");
        return 0;
    }
    file = fopen(temporary, "wb");
    if (file == NULL) {
        snprintf(error, error_size, "Could not create exchange manifest: %s",
                 strerror(errno));
        return 0;
    }
    fprintf(file, "TAPESISTER_EXCHANGE 1\n");
    fprintf(file, "sender=tapesister\nrecipient=tapehead\nlayout=%s\ncount=%d\n",
            ts_exchange_layout_name(layout), ts_instrument_bank_count(instrument));
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot) {
        const TsBankSlot *bank = &instrument->bank[slot];
        char safe[96];
        char filename[TS_EXCHANGE_FILENAME_MAX];
        int target_instrument;
        int target_sample;
        if (!bank->occupied) continue;
        bank_safe_name(bank->sample.name, safe, sizeof(safe));
        if (snprintf(filename, sizeof(filename), "%02d_%s.wav", slot + 1, safe) < 0 ||
            strlen(safe) + 8u >= sizeof(filename)) {
            fclose(file);
            remove(temporary);
            set_error(error, error_size, "Exchange WAV filename is too long");
            return 0;
        }
        if (layout == TS_EXCHANGE_LAYOUT_SEPARATE_INSTRUMENTS) {
            target_instrument = slot + 1;
            target_sample = 1;
        } else {
            target_instrument = 0;
            target_sample = slot + 1;
        }
        fprintf(file, "item=%d,%d,%d,%s\n", slot + 1,
                target_instrument, target_sample, filename);
    }
    if (fclose(file) != 0 || rename(temporary, path) != 0) {
        remove(temporary);
        set_error(error, error_size, "Could not publish exchange manifest");
        return 0;
    }
    return 1;
}

static void remove_partial_folder(const char *folder)
{
#ifdef _WIN32
    struct _finddata_t entry;
    intptr_t handle;
    char pattern[TS_EXCHANGE_PATH_MAX];
    if (path_join(pattern, sizeof(pattern), folder, "*")) {
        handle = _findfirst(pattern, &entry);
        if (handle != -1) {
            do {
                char path[TS_EXCHANGE_PATH_MAX];
                if ((entry.attrib & _A_SUBDIR) == 0 &&
                    path_join(path, sizeof(path), folder, entry.name)) remove(path);
            } while (_findnext(handle, &entry) == 0);
            _findclose(handle);
        }
    }
#else
    DIR *directory = opendir(folder);
    struct dirent *entry;
    if (directory != NULL) {
        while ((entry = readdir(directory)) != NULL) {
            char path[TS_EXCHANGE_PATH_MAX];
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0 &&
                path_join(path, sizeof(path), folder, entry->d_name)) remove(path);
        }
        closedir(directory);
    }
#endif
    ts_rmdir(folder);
}

int ts_exchange_publish_bank(const TsInstrument *instrument,
                             const char *exchange_root,
                             TsExchangeLayout layout,
                             char *destination, size_t destination_size,
                             char *error, size_t error_size)
{
    char final_path[TS_EXCHANGE_PATH_MAX];
    char partial_path[TS_EXCHANGE_PATH_MAX];
    if (instrument == NULL || layout < 0 || layout >= TS_EXCHANGE_LAYOUT_COUNT ||
        !path_is_directory(exchange_root)) {
        set_error(error, error_size, "Configure an existing FT2 exchange path first");
        return 0;
    }
    if (!ts_instrument_next_family_path(instrument, exchange_root,
                                        final_path, sizeof(final_path),
                                        error, error_size)) return 0;
    if (destination != NULL &&
        (destination_size == 0 || strlen(final_path) >= destination_size)) {
        set_error(error, error_size, "Exchange destination buffer is too small");
        return 0;
    }
    if (snprintf(partial_path, sizeof(partial_path), "%s.partial", final_path) < 0 ||
        strlen(final_path) + 8u >= sizeof(partial_path)) {
        set_error(error, error_size, "Exchange staging path is too long");
        return 0;
    }
    remove_partial_folder(partial_path);
    if (!ts_instrument_export_bank(instrument, partial_path, error, error_size)) return 0;
    if (!write_manifest(instrument, partial_path, layout, error, error_size) ||
        rename(partial_path, final_path) != 0) {
        remove_partial_folder(partial_path);
        if (error != NULL && error[0] == '\0')
            set_error(error, error_size, "Could not atomically publish exchange folder");
        return 0;
    }
    if (destination != NULL)
        (void)copy_field(destination, destination_size, final_path);
    set_error(error, error_size, "");
    return 1;
}

static int acknowledge_offer(const TsExchangeOffer *offer,
                             char *error, size_t error_size)
{
    char path[TS_EXCHANGE_PATH_MAX];
    char temporary[TS_EXCHANGE_PATH_MAX];
    FILE *file;
    if (!path_join(path, sizeof(path), offer->folder, TS_EXCHANGE_RECEIVED_NAME) ||
        snprintf(temporary, sizeof(temporary), "%s.tmp", path) < 0 ||
        strlen(path) + 4u >= sizeof(temporary)) {
        set_error(error, error_size, "Exchange acknowledgement path is too long");
        return 0;
    }
    file = fopen(temporary, "wb");
    if (file == NULL) {
        set_error(error, error_size, "Could not create exchange acknowledgement");
        return 0;
    }
    if (fputs("recipient=tapesister\nstatus=imported\n", file) < 0) {
        fclose(file);
        remove(temporary);
        set_error(error, error_size, "Could not write exchange acknowledgement");
        return 0;
    }
    if (fclose(file) != 0) {
        remove(temporary);
        set_error(error, error_size, "Could not acknowledge imported transfer");
        return 0;
    }
    if (rename(temporary, path) != 0) {
        remove(temporary);
        set_error(error, error_size, "Could not publish exchange acknowledgement");
        return 0;
    }
    return 1;
}

int ts_exchange_import_offer(TsInstrument *instrument,
                             const TsExchangeOffer *offer,
                             char *error, size_t error_size)
{
    TsInstrument staged;
    int first_tile = -1;
    char path[TS_EXCHANGE_PATH_MAX];
    char load_error[160];
    if (instrument == NULL || offer == NULL || offer->item_count <= 0 ||
        strcmp(offer->sender, "tapehead") != 0 ||
        strcmp(offer->recipient, "tapesister") != 0) {
        set_error(error, error_size, "Transfer is not addressed from Tapehead to TapeSister");
        return 0;
    }
    ts_instrument_init(&staged);
    for (int item = 0; item < offer->item_count; ++item) {
        int tile = offer->items[item].tile - 1;
        if (!path_join(path, sizeof(path), offer->folder,
                       offer->items[item].filename)) {
            ts_instrument_free(&staged);
            set_error(error, error_size, "Incoming WAV path is too long");
            return 0;
        }
        staged.selected_slot = tile;
        if (!ts_instrument_load_wav(&staged, path, load_error, sizeof(load_error))) {
            ts_instrument_free(&staged);
            snprintf(error, error_size, "Could not stage tile %02d: %.110s",
                     tile + 1, load_error);
            return 0;
        }
        if (first_tile < 0 || tile < first_tile) first_tile = tile;
    }
    if (first_tile < 0 ||
        !ts_instrument_select_bank(&staged, first_tile, load_error, sizeof(load_error))) {
        ts_instrument_free(&staged);
        set_error(error, error_size, "Incoming transfer did not contain an editable tile");
        return 0;
    }
    if (!acknowledge_offer(offer, error, error_size)) {
        ts_instrument_free(&staged);
        return 0;
    }
    ts_instrument_free(instrument);
    *instrument = staged;
    set_error(error, error_size, "");
    return 1;
}
