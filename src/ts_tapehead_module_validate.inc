/* Check offsets, packed patterns and host limits before calling the legacy
   loaders. They populate only temporary data; failure leaves the live song alone. */
static unsigned module_u16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t module_u32(const uint8_t *p) { return module_u16(p) | (uint32_t)module_u16(p+2) << 16; }
static unsigned module_be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static int module_read(FILE *f,uint32_t length,uint32_t at,void *p,size_t n) {
    return at <= length && n <= length-at && !fseek(f,at,SEEK_SET) && fread(p,1,n,f)==n;
}
static int module_mod_channels(const uint8_t *id) {
    if(id[0]>='1' && id[0]<='9' && !memcmp(id+1,"CHN",3))return id[0]-'0';
    if(id[0]>='0' && id[0]<='9' && id[1]>='0' && id[1]<='9' && id[2]=='C' && (id[3]=='H' || id[3]=='N'))
        return 10*(id[0]-'0')+id[1]-'0';
    if(!memcmp(id,"CD61",4))return 6;
    if(!memcmp(id,"CD81",4) || !memcmp(id,"OKTA",4) || !memcmp(id,"OCTA",4) ||
       !memcmp(id,"FLT8",4) || !memcmp(id,"EXO8",4))return 8;
    if(!memcmp(id,"FA0",3) && id[3]>='4' && id[3]<='8')return id[3]-'0';
    static const char *four[]={"M.K.","M!K!","NSMS","LARD","PATT","FLT4","EXO4","N.T.","M&K!","FEST"};
    for(unsigned i=0;i<sizeof(four)/sizeof(four[0]);++i)if(!memcmp(id,four[i],4))return 4;
    return 0;
}
static int module_validate_mod(FILE *f,uint32_t length,int *stk,char *error,size_t size) {
    uint8_t h[1084]={0};
    if(!module_read(f,length,0,h,MIN(length,sizeof(h))))return fail(error,size,"Unable to read MOD header");
    int channels=length>=1084?module_mod_channels(h+1080):0;
    *stk=channels==0;
    if(channels>32)return fail(error,size,"Module exceeds 32 tracks");
    unsigned samples=*stk?15:31, head=*stk?600:1084, order_at=20+samples*30;
    if(length<head || !h[order_at] || h[order_at]>129 || (*stk && h[order_at+1]>220))
        return fail(error,size,"Invalid MOD / SoundTracker header");
    /* 129 is a historical ProTracker quirk supported by the bundled loader. */
    if(h[order_at]>128 && (*stk || memcmp(h+1080,"M.K.",4)))return fail(error,size,"Invalid MOD order count");
    unsigned patterns=0;
    int paired=!*stk && (!memcmp(h+1080,"FLT8",4) || !memcmp(h+1080,"EXO8",4));
    for(unsigned i=0;i<128;++i) {
        unsigned order=h[order_at+2+i];
        if(order>127)return fail(error,size,"Invalid MOD pattern order");
        patterns=MAX(patterns,(paired?order/2:order)+1);
    }
    uint64_t needed=head+(uint64_t)patterns*64*(*stk?4:channels)*4;
    for(unsigned i=0;i<samples;++i) {
        const uint8_t *s=h+20+i*30;
        /* No signature in a 15-sample file: require plausible sample fields. */
        if(*stk && (s[24]>15 || s[25]>64))return fail(error,size,"Unrecognized MOD format");
        needed+=2*module_be16(s+22);
    }
    if(needed>length)return fail(error,size,"Truncated MOD patterns or sample data");
    return 1;
}
static int module_validate_it(FILE *f,uint32_t length,unsigned *patterns,char *error,size_t size) {
    uint8_t h[192],table[3072],orders[257];
    if(!module_read(f,length,0,h,sizeof(h)) || memcmp(h,"IMPM",4))return fail(error,size,"Invalid IT header");
    unsigned no=module_u16(h+32),ni=module_u16(h+34),ns=module_u16(h+36),np=module_u16(h+38);
    int instruments=module_u16(h+44)&4,old=module_u16(h+42)<0x200;
    if(!no || no>257 || ni>128 || ns>256 || (!instruments && ns>128) || np>256)
        return fail(error,size,"IT exceeds supported orders, patterns or 128 instruments");
    if(!h[50] || h[50]>31 || h[51]<32)return fail(error,size,"IT initial speed / tempo is outside TrackSister's range");
    if(!module_read(f,length,192,orders,no) || !module_read(f,length,192+no,table,4*(ni+ns+np)))
        return fail(error,size,"Truncated IT order / offset table");
    unsigned playable=0;
    for(unsigned i=0;i<no && orders[i]!=255;++i)if(orders[i]!=254) {
        if(orders[i]>=np)return fail(error,size,"IT order references a missing pattern");
        if(++playable>255)return fail(error,size,"IT exceeds 255 playable orders");
    }
    if(!playable)return fail(error,size,"IT has no playable song orders");
    for(unsigned i=0;i<ni && instruments;++i) {
        uint8_t ins[554];
        if(!module_read(f,length,module_u32(table+4*i),ins,old?554:550) || memcmp(ins,"IMPI",4))
            return fail(error,size,"Invalid or truncated IT instrument");
        uint8_t used[256]={0};unsigned count=0;
        for(unsigned n=0;n<120;++n) {
            unsigned smp=ins[65+n*2];
            if(smp>ns)return fail(error,size,"IT instrument references a missing sample");
            if(n>=12 && n<108 && smp && !used[smp-1]){used[smp-1]=1;++count;}
        }
        if(count>16)return fail(error,size,"IT instrument exceeds 16 mapped samples");
        if(!old)for(unsigned e=0;e<3;++e)if(ins[304+e*82+1]>25)
            return fail(error,size,"Invalid IT envelope");
    }
    for(unsigned i=0;i<ns;++i) {
        uint8_t s[80];
        if(!module_read(f,length,module_u32(table+4*(ni+i)),s,sizeof(s)) || memcmp(s,"IMPS",4))
            return fail(error,size,"Invalid or truncated IT sample header");
        uint32_t frames=module_u32(s+48),at=module_u32(s+72);
        if(frames>TS_CANVAS_MAX_FRAMES)return fail(error,size,"IT sample exceeds the tile frame limit");
        if(!frames)continue;
        if(s[18]&4)return fail(error,size,"Stereo IT samples are not supported by this converter; use raw import");
        if((s[46]&~5) || (!(s[18]&8) && (s[46]&4)))
            return fail(error,size,"Unsupported IT sample encoding");
        if(!at || at>=length)return fail(error,size,"Invalid IT sample offset");
        unsigned width=(s[18]&2)?2:1;
        if(s[18]&8) {
            /* The decoder checks variable-width bitstreams as it unpacks. */
            for(uint32_t remaining=frames;remaining;) {
                uint8_t packed[2];
                if(!module_read(f,length,at,packed,2))return fail(error,size,"Truncated compressed IT sample");
                unsigned bytes=module_u16(packed);at+=2;
                if(!bytes || bytes>length-at)return fail(error,size,"Invalid compressed IT block length");
                at+=bytes;remaining-=MIN(remaining,32768/width);
            }
        } else if((uint64_t)frames*width>length-at)return fail(error,size,"Truncated IT sample data");
    }
    for(unsigned i=0;i<np;++i) {
        uint32_t at=module_u32(table+4*(ni+ns+i));if(!at)continue;
        uint8_t ph[8];
        if(!module_read(f,length,at,ph,8))return fail(error,size,"Truncated IT pattern header");
        unsigned bytes=module_u16(ph),rows=module_u16(ph+2);
        if(!rows || rows>256)return fail(error,size,"IT patterns must have 1-256 rows");
        if(!module_read(f,length,at+8,tmpBuffer,bytes))return fail(error,size,"Truncated IT pattern data");
        unsigned pos=0,row=0;uint8_t masks[64]={0};
        while(pos<bytes && row<rows) {
            unsigned b=tmpBuffer[pos++];if(!b){++row;continue;}
            unsigned ch=(b-1)&63;
            if(ch>=32)return fail(error,size,"IT uses tracks beyond the 32-track limit");
            if(b&128){if(pos==bytes)goto bad_pattern;masks[ch]=tmpBuffer[pos++];}
            unsigned m=masks[ch],take=!!(m&1)+!!(m&2)+!!(m&4)+2*!!(m&8);
            if(take>bytes-pos)goto bad_pattern;
            if(m&1)++pos;
            if(m&2) {
                unsigned ins=tmpBuffer[pos++];
                if(ins>128 || ins>(instruments?ni:ns))return fail(error,size,"IT pattern references an unsupported instrument");
            }
            if(m&4)++pos;
            if(m&8)pos+=2;
        }
        if(row!=rows || pos!=bytes)goto bad_pattern;
    }
    *patterns=np;return 1;
bad_pattern:
    return fail(error,size,"Invalid packed IT pattern");
}
