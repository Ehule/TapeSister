#include "tapesister/recall_osc.h"

typedef struct {
    TsRecallSocket *socket;
    Uint32 last_poll,retry,last_epoch;
    struct { uint32_t id,session,sequence,total;uint64_t generation; } bus[TS_MATRIX_EFFECT_CAPACITY];
} RecallSender;

static void recall_send_poll(RecallSender *sender,SDL_AudioDeviceID device,AudioState *audio)
{
    TsMatrixEffects *effects=&audio->sister.matrix_effects;
    if(!audio->sister.matrix.controls.enabled || !effects->recall_mask) {
        if(sender->socket){
            ts_recall_close(sender->socket);sender->socket=NULL;
            memset(sender->bus,0,sizeof(sender->bus));
        }
        return;
    }
    Uint32 now=SDL_GetTicks();
    if(now-sender->last_poll<32)return;
    sender->last_poll=now;
    if(!sender->socket) {
        if(sender->retry && now-sender->retry<1000)return;
        sender->retry=now;
        const char *host=SDL_getenv("TAPESISTER_RECALL_HOST");
        const char *port=SDL_getenv("TAPESISTER_RECALL_PORT");
        char *end=NULL;
        long number=port?strtol(port,&end,10):9011;
        if((end && *end) || number<1 || number>65535)return;
        sender->socket=ts_recall_open(host?host:"127.0.0.1",(unsigned)number);
        if(!sender->socket)return;
        sender->retry=0;
    }
    TsRecallReading reads[TS_MATRIX_EFFECT_CAPACITY];
    if(device)SDL_LockAudioDevice(device);
    for(int slot=0;slot<TS_MATRIX_EFFECT_CAPACITY;++slot)reads[slot]=ts_matrix_recall_take(effects,slot);
    if(device)SDL_UnlockAudioDevice(device);

    for(int slot=0;slot<TS_MATRIX_EFFECT_CAPACITY;++slot) {
        TsRecallReading r=reads[slot];
        if(!r.id){sender->bus[slot].id=0;continue;}
        if(r.id!=sender->bus[slot].id || r.generation!=sender->bus[slot].generation) {
            if(!sender->last_epoch)sender->last_epoch=(uint32_t)SDL_GetPerformanceCounter();
            sender->bus[slot].id=r.id;
            sender->bus[slot].generation=r.generation;
            sender->bus[slot].session=++sender->last_epoch;
            sender->bus[slot].sequence=sender->bus[slot].total=0;
        }
        sender->bus[slot].total+=r.hits;
        ts_recall_send(sender->socket,r.id,sender->bus[slot].session,++sender->bus[slot].sequence,
                       sender->bus[slot].total,r.hits,r.level);
    }
}
