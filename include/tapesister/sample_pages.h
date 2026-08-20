#ifndef TAPESISTER_SAMPLE_PAGES_H
#define TAPESISTER_SAMPLE_PAGES_H

#include "tapesister/sample.h"

#include <stddef.h>

typedef struct {
    TsInstrument **pages;
    size_t page_count;
    size_t page_capacity;
    size_t active_page;
    int active_live;
} TsSamplePages;

int ts_sample_pages_init(TsSamplePages *pages,
                         char *error, size_t error_size);
void ts_sample_pages_free(TsSamplePages *pages);
size_t ts_sample_pages_count(const TsSamplePages *pages);
size_t ts_sample_pages_active(const TsSamplePages *pages);
const TsInstrument *ts_sample_pages_page(const TsSamplePages *pages,
                                         const TsInstrument *active,
                                         size_t page);
TsInstrument *ts_sample_pages_page_mut(TsSamplePages *pages,
                                       TsInstrument *active, size_t page);
int ts_sample_pages_switch(TsSamplePages *pages, TsInstrument *active,
                           size_t page, char *error, size_t error_size);
int ts_sample_pages_append_and_switch(TsSamplePages *pages,
                                      TsInstrument *active,
                                      size_t *new_page,
                                      char *error, size_t error_size);
int ts_sample_pages_park(TsSamplePages *pages, TsInstrument *active,
                         char *error, size_t error_size);
int ts_sample_pages_unpark(TsSamplePages *pages, TsInstrument *active,
                           char *error, size_t error_size);
int ts_sample_pages_keep_record_bank(TsSamplePages *pages,
                                     TsInstrument *active_sample,
                                     TsInstrument *record_bank,
                                     size_t *copied,
                                     size_t *first_page,
                                     size_t *last_page,
                                     char *error, size_t error_size);
int ts_sample_pages_save_project(const TsSamplePages *pages,
                                 const TsInstrument *active_sample,
                                 const TsInstrument *record_bank,
                                 const char *path,
                                 char *error, size_t error_size);
int ts_sample_pages_load_project(TsSamplePages *pages,
                                 TsInstrument *active_sample,
                                 TsInstrument *record_bank,
                                 const char *path,
                                 char *error, size_t error_size);

#endif
