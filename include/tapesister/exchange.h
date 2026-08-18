#ifndef TAPESISTER_EXCHANGE_H
#define TAPESISTER_EXCHANGE_H

#include <stddef.h>

#include "tapesister/sample.h"

enum {
    TS_EXCHANGE_PATH_MAX = 1024,
    TS_EXCHANGE_FILENAME_MAX = 256,
    TS_EXCHANGE_APP_MAX = 32
};

#define TS_EXCHANGE_MANIFEST_NAME "exchange.tsexchange"
#define TS_EXCHANGE_RECEIVED_NAME "tapesister.received"
#define TS_EXCHANGE_TAPEHEAD_PRESENCE ".tapehead.running"
#define TS_EXCHANGE_TAPESISTER_PRESENCE ".tapesister.running"

typedef enum {
    TS_EXCHANGE_LAYOUT_INSTRUMENT_SAMPLES = 0,
    TS_EXCHANGE_LAYOUT_SEPARATE_INSTRUMENTS,
    TS_EXCHANGE_LAYOUT_COUNT
} TsExchangeLayout;

typedef struct {
    int tile;
    int instrument;
    int sample;
    char filename[TS_EXCHANGE_FILENAME_MAX];
} TsExchangeItem;

typedef struct {
    char folder[TS_EXCHANGE_PATH_MAX];
    char sender[TS_EXCHANGE_APP_MAX];
    char recipient[TS_EXCHANGE_APP_MAX];
    TsExchangeLayout layout;
    int item_count;
    TsExchangeItem items[TS_BANK_SLOT_COUNT];
} TsExchangeOffer;

void ts_exchange_offer_init(TsExchangeOffer *offer);
const char *ts_exchange_layout_name(TsExchangeLayout layout);
int ts_exchange_offer_load(TsExchangeOffer *offer, const char *folder,
                           char *error, size_t error_size);
int ts_exchange_find_pending(const char *exchange_root, TsExchangeOffer *offer,
                             char *error, size_t error_size);
int ts_exchange_presence_touch(const char *exchange_root, const char *application);
int ts_exchange_presence_active(const char *exchange_root,
                                const char *application,
                                unsigned int maximum_age_seconds);
int ts_exchange_publish_bank(const TsInstrument *instrument,
                             const char *exchange_root,
                             TsExchangeLayout layout,
                             char *destination, size_t destination_size,
                             char *error, size_t error_size);
int ts_exchange_import_offer(TsInstrument *instrument,
                             const TsExchangeOffer *offer,
                             char *error, size_t error_size);

#endif
