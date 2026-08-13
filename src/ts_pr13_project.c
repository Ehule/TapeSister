#include "tapesister/pr13.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint16_t ts_pr13_lock_mask(const TsInstrument *instrument);
void ts_pr13_restore_lock_mask(TsInstrument *instrument, uint16_t mask);

int ts_pr13_save_project(const TsInstrument *instrument, const char *path,
                         char *error, size_t error_size)
{
    FILE *file;
    unsigned char trailer[6];
    uint16_t mask;
    if (!ts_instrument_save_recipe(instrument, path, error, error_size)) return 0;
    file = fopen(path, "ab");
    if (file == NULL) return 0;
    mask = ts_pr13_lock_mask(instrument);
    trailer[0] = 'P'; trailer[1] = '1'; trailer[2] = '3'; trailer[3] = 'L';
    trailer[4] = (unsigned char)(mask & 255u);
    trailer[5] = (unsigned char)(mask >> 8);
    if (fwrite(trailer, 1, sizeof(trailer), file) != sizeof(trailer)) {
        fclose(file);
        return 0;
    }
    if (fclose(file) != 0) return 0;
    if (error != NULL && error_size > 0) error[0] = '\0';
    return 1;
}

static int copy_without_trailer(const char *path, long bytes,
                                char *temporary, size_t temporary_size)
{
    FILE *source;
    FILE *destination;
    unsigned char buffer[4096];
    long remaining = bytes;
    snprintf(temporary, temporary_size, "%s.pr13tmp", path);
    source = fopen(path, "rb");
    if (source == NULL) return 0;
    destination = fopen(temporary, "wb");
    if (destination == NULL) { fclose(source); return 0; }
    while (remaining > 0) {
        size_t want = remaining > (long)sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
        size_t got = fread(buffer, 1, want, source);
        if (got != want || fwrite(buffer, 1, got, destination) != got) {
            fclose(source); fclose(destination); remove(temporary); return 0;
        }
        remaining -= (long)got;
    }
    fclose(source);
    if (fclose(destination) != 0) { remove(temporary); return 0; }
    return 1;
}

int ts_pr13_load_project(TsInstrument *instrument, const char *path,
                         char *error, size_t error_size)
{
    FILE *file;
    unsigned char trailer[6];
    uint16_t mask = 0;
    int has_trailer = 0;
    long total_size = 0;
    char temporary[1024];
    const char *load_path = path;

    file = fopen(path, "rb");
    if (file != NULL && fseek(file, 0, SEEK_END) == 0) {
        total_size = ftell(file);
        if (total_size >= (long)sizeof(trailer) &&
            fseek(file, -(long)sizeof(trailer), SEEK_END) == 0 &&
            fread(trailer, 1, sizeof(trailer), file) == sizeof(trailer) &&
            trailer[0] == 'P' && trailer[1] == '1' && trailer[2] == '3' && trailer[3] == 'L') {
            mask = (uint16_t)trailer[4] | (uint16_t)((uint16_t)trailer[5] << 8);
            has_trailer = 1;
        }
    }
    if (file != NULL) fclose(file);

    if (has_trailer) {
        if (!copy_without_trailer(path, total_size - (long)sizeof(trailer),
                                  temporary, sizeof(temporary))) {
            if (error != NULL && error_size > 0)
                snprintf(error, error_size, "Could not prepare PR13 project for loading");
            return 0;
        }
        load_path = temporary;
    }

    if (!ts_instrument_load_recipe(instrument, load_path, error, error_size)) {
        if (has_trailer) remove(temporary);
        return 0;
    }
    if (has_trailer) remove(temporary);

    ts_pr13_restore_lock_mask(instrument, has_trailer ? mask : 0);
    if (!has_trailer) {
        instrument->process.body = 0.0f;
        instrument->process.edge = 0.0f;
        instrument->process.drift = 0.5f;
    }
    return ts_pr13_rerender(instrument, -1, error, error_size);
}
