#include "tapesister/pr13.h"

#include <stdio.h>

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

int ts_pr13_load_project(TsInstrument *instrument, const char *path,
                         char *error, size_t error_size)
{
    FILE *file;
    unsigned char trailer[6];
    int has_trailer = 0;
    if (!ts_instrument_load_recipe(instrument, path, error, error_size)) return 0;
    file = fopen(path, "rb");
    if (file != NULL && fseek(file, -(long)sizeof(trailer), SEEK_END) == 0 &&
        fread(trailer, 1, sizeof(trailer), file) == sizeof(trailer) &&
        trailer[0] == 'P' && trailer[1] == '1' && trailer[2] == '3' && trailer[3] == 'L') {
        uint16_t mask = (uint16_t)trailer[4] | (uint16_t)((uint16_t)trailer[5] << 8);
        ts_pr13_restore_lock_mask(instrument, mask);
        has_trailer = 1;
    }
    if (file != NULL) fclose(file);
    if (!has_trailer) {
        ts_pr13_restore_lock_mask(instrument, 0);
        instrument->process.body = 0.0f;
        instrument->process.edge = 0.0f;
        instrument->process.drift = 0.5f;
    }
    return ts_pr13_rerender(instrument, -1, error, error_size);
}
