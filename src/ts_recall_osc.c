#include "tapesister/recall_osc.h"
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET RecallFd;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int RecallFd;
#define INVALID_SOCKET (-1)
#endif

struct TsRecallSocket { RecallFd fd; struct sockaddr_in remote; };

TsRecallSocket *ts_recall_open(const char *host,unsigned port)
{
    if(!host || !port || port>65535)return NULL;
    TsRecallSocket *s=calloc(1,sizeof(*s));
    if(!s)return NULL;
#ifdef _WIN32
    WSADATA wsa;
    if(WSAStartup(MAKEWORD(2,2),&wsa)){free(s);return NULL;}
#endif
    s->fd=socket(AF_INET,SOCK_DGRAM,0);
    s->remote.sin_family=AF_INET;s->remote.sin_port=htons((uint16_t)port);
    if(s->fd==INVALID_SOCKET || inet_pton(AF_INET,host,&s->remote.sin_addr)!=1)goto fail;
#ifdef _WIN32
    u_long on=1;
    if(ioctlsocket(s->fd,FIONBIO,&on)!=0)goto fail;
#else
    if(fcntl(s->fd,F_SETFL,O_NONBLOCK)<0)goto fail;
#endif
    return s;
fail:
    ts_recall_close(s);return NULL;
}
int ts_recall_send(TsRecallSocket *s,uint32_t id,uint32_t session,uint32_t seq,
                   uint32_t total,uint32_t hits,float level)
{
    if(!s)return 0;
    unsigned char packet[44]="/hr/v1/send";
    memcpy(packet+12,",iiiiif",7);
    uint32_t bits;
    memcpy(&bits,&level,4);
    uint32_t words[6]={htonl(id),htonl(session),htonl(seq),htonl(total),htonl(hits),htonl(bits)};
    memcpy(packet+20,words,sizeof(words));
    return sendto(s->fd,(const char *)packet,sizeof(packet),0,
                  (const struct sockaddr *)&s->remote,sizeof(s->remote))==(int)sizeof(packet);
}
void ts_recall_close(TsRecallSocket *s)
{
    if(!s)return;
#ifdef _WIN32
    if(s->fd!=INVALID_SOCKET)closesocket(s->fd);
    WSACleanup();
#else
    if(s->fd!=INVALID_SOCKET)close(s->fd);
#endif
    free(s);
}
