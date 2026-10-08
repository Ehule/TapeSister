#include "tapesister/sister_tracker.h"
#include <string.h>
static unsigned u16(const uint8_t *p){return p[0]|((unsigned)p[1]<<8);}
static uint32_t u32(const uint8_t *p){return u16(p)|((uint32_t)u16(p+2)<<16);}
int ts_tracker_preferences_validate(const uint8_t *p,uint32_t size) {
    if(!p || size!=128 || memcmp(p,"TPF1",4))return 0;
    for(unsigned i=4;i<=23;++i)if(i!=13 && p[i]>1)return 0;
    if(p[7]&&!p[6])return 0;
    if(p[13]!=1 && p[13]!=2 && p[13]!=4 && p[13]!=8 && p[13]!=16)return 0;
    if(p[24]>3 || p[25]>1 || p[26]>1 || p[27]>11 || p[28]>2 || p[29]>1 || p[30]>1 || p[31]>3)return 0;
    for(unsigned i=32;i<92;++i)if(p[i]>63)return 0;
    if(!p[92] || p[92]>100 || !p[93] || p[93]>100)return 0;
    for(unsigned i=94;i<126;++i)if(p[i]>1)return 0;
    return p[126]>0 && p[126]<=64 && !p[127];
}
int ts_tracker_embedded_validate(const uint8_t *p,uint32_t size) {
    enum { HEADER=52, ORDERS=256 };
    unsigned channels=p && size>=8 && p[3]=='3'?32:8;
    unsigned LANES=channels*10, RECORD=7+256*channels*7;
    if(!p || size<HEADER+LANES+ORDERS || (memcmp(p,"STH1\0\0\0\0",8) && memcmp(p,"STH2\0\0\0\0",8) &&
        (memcmp(p,"STH3",4) || p[4] || p[5]<2 || p[5]>32 || (p[5]&1) || p[6]>1 || p[7])))return 0;
    unsigned count=u16(p+8),bpm=u16(p+10),orders=u16(p+14),restart=u16(p+16);
    if(!count || count>256 || size!=HEADER+LANES+ORDERS+count*RECORD+(p[3]>='2'?128:0) ||
       bpm<32 || bpm>255 || p[12]>31 || p[13]>64 ||
       !orders || orders>256 || restart>=orders || u16(p+19)>255 ||
       p[21]>7 || p[22]>128 || p[23]>16 || p[24]>1 || p[25]>1 ||
       p[26]>1 || p[27]>1 || p[28]>1 || p[29]>2 || p[50] || p[51]>channels)return 0;
    if(p[3]>='2' && !ts_tracker_preferences_validate(p+size-128,128))return 0;
    const uint8_t *lanes=p+HEADER;
    for(int i=0;i<(int)channels;++i) {
        const uint8_t *l=lanes+i*10;
        if(u16(l)>256 || l[2]>2 || l[3]>=17 || l[4]>(p[3]>='2'?2:1) || l[5]>1 || l[6]>1 || u16(l+7)>512 || l[9])return 0;
    }
    const uint8_t *sequence=lanes+LANES,*r=sequence+ORDERS;
    uint8_t seen[256]={0};uint32_t ids[256]={0};unsigned rows[256]={0};
    for(unsigned i=0;i<count;++i,r+=RECORD) {
        unsigned index=r[0];uint32_t id=u32(r+3);
        if(seen[index] || !id || !u16(r+1) || u16(r+1)>256)return 0;
        for(unsigned k=0;k<i;++k)if(ids[k]==id)return 0;
        ids[i]=id;seen[index]=1;rows[index]=u16(r+1);
        for(unsigned c=0;c<256*channels;++c) {
            const uint8_t *v=r+7+c*7;
            if(v[0]>97 || v[1]>128 || v[3]>35 || (v[5] && v[5]!=0x16 && v[5]!=0x17))return 0;
        }
    }
    /* TapeHead's editor follows a CONTROL head across the extended LEN
       surface, even when the physical pattern contains fewer rows. */
    unsigned editor_rows=rows[p[18]];
    for(int lane=0;lane<(int)channels;++lane)if(u16(lanes+lane*10)>editor_rows)editor_rows=u16(lanes+lane*10);
    if(!seen[p[18]] || u16(p+19)>=editor_rows)return 0;
    for(unsigned i=0;i<orders;++i)if(!seen[sequence[i]])return 0;
    return 1;
}

int ts_tracker_embedded_matches(const TsSisterTracker *t) {
    if(!t->embedded_size)return 1;
    const uint8_t *p=t->embedded_data;
    if(t->channel_count!=(p[3]=='3'?p[5]:8))return 0;
    unsigned count=u16(p+8);
    if(count!=t->pattern_count)return 0;
    unsigned channels=p[3]=='3'?32:8;
    const uint8_t *r=p+52+channels*10+256;
    for(unsigned k=0;k<count;++k,r+=7+256*channels*7) {
        const TsTrackerPattern *native=ts_sister_tracker_pattern_const(t,u32(r+3));
        if(!native || native->rows!=u16(r+1))return 0;
        if(r[0]==p[18] && native->id!=t->editor_pattern)return 0;
    }
    return 1;
}
