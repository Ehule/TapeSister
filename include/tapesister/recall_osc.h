#ifndef TAPESISTER_RECALL_OSC_H
#define TAPESISTER_RECALL_OSC_H
#include <stdint.h>
/* Open/send/close only on the UI thread. Host is a numeric IPv4 address. */
typedef struct TsRecallSocket TsRecallSocket;
TsRecallSocket *ts_recall_open(const char *host,unsigned port);
int ts_recall_send(TsRecallSocket *,uint32_t id,uint32_t session,uint32_t seq,
                   uint32_t total,uint32_t hits,float level);
void ts_recall_close(TsRecallSocket *);
#endif
