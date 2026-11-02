/* Included by ts_sample.c: uses its bounded endian codecs and sample checks.
   No global cache survives a transaction, and no writable PCM pointer is
   shared between samples. Legacy codecs and standalone exports stay intact. */
typedef struct {
    uint32_t h[8];
    uint64_t bytes;
    unsigned char block[64];
    size_t used;
} AssetSha;
static uint32_t asset_rotr(uint32_t x, unsigned n) { return (x>>n)|(x<<(32-n)); }
static void asset_sha_block(AssetSha *s, const unsigned char *p)
{
    static const uint32_t k[64]={
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t w[64];
    for(int i=0;i<16;++i)w[i]=(uint32_t)p[4*i]<<24|(uint32_t)p[4*i+1]<<16|(uint32_t)p[4*i+2]<<8|p[4*i+3];
    for(int i=16;i<64;++i){uint32_t a=w[i-15],b=w[i-2];w[i]=w[i-16]+(asset_rotr(a,7)^asset_rotr(a,18)^(a>>3))+w[i-7]+(asset_rotr(b,17)^asset_rotr(b,19)^(b>>10));}
    uint32_t a=s->h[0],b=s->h[1],c=s->h[2],d=s->h[3],e=s->h[4],f=s->h[5],g=s->h[6],h=s->h[7];
    for(int i=0;i<64;++i){uint32_t t=h+(asset_rotr(e,6)^asset_rotr(e,11)^asset_rotr(e,25))+((e&f)^(~e&g))+k[i]+w[i];
        uint32_t u=(asset_rotr(a,2)^asset_rotr(a,13)^asset_rotr(a,22))+((a&b)^(a&c)^(b&c));
        h=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;}
    s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h;
}
static AssetSha asset_sha_init(void)
{ return (AssetSha){{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19},0,{0},0}; }
static void asset_sha_add(AssetSha *s,const void *data,size_t n)
{
    const unsigned char *p=data;s->bytes+=n;
    while(n){size_t take=64-s->used;if(take>n)take=n;memcpy(s->block+s->used,p,take);s->used+=take;p+=take;n-=take;
        if(s->used==64){asset_sha_block(s,s->block);s->used=0;}}
}
static void asset_sha_end(AssetSha *s,char id[65])
{
    uint64_t bits=s->bytes*8;unsigned char pad[128]={0x80};
    size_t n=s->used<56?56-s->used:120-s->used;asset_sha_add(s,pad,n);
    for(int i=0;i<8;++i)pad[i]=(unsigned char)(bits>>(56-i*8));
    asset_sha_add(s,pad,8);
    for(int i=0;i<8;++i)snprintf(id+i*8,9,"%08x",s->h[i]);
}
typedef struct {
    char id[65];
    uint64_t file_hash;
    uint32_t rate;
    size_t frames;
    uint8_t channels;
    int verified;
} AssetEntry;
typedef struct { const float *data;size_t frames;uint32_t rate;uint8_t channels;char id[65]; } AssetPointer;
struct TsAudioAssets {
    char root[2048];
    int writing;
    AssetEntry *entries;
    size_t count, capacity;
    /* Bounded lookup only inside the pinned save snapshot. */
    AssetPointer pointers[256];
    size_t pointer_count;
    struct TsAudioAssets *previous;
};
static _Thread_local TsAudioAssets *audio_assets;
TsAudioAssets *ts_audio_assets_begin(const char *root,int writing)
{
    if(!root || strlen(root)>=2048)return NULL;
    TsAudioAssets *s=calloc(1,sizeof(*s));if(!s)return NULL;
    strcpy(s->root,root);s->writing=writing;s->previous=audio_assets;audio_assets=s;return s;
}
void ts_audio_assets_end(TsAudioAssets *s)
{if(s){audio_assets=s->previous;free(s->entries);free(s);}}
int ts_audio_assets_writing(void){return audio_assets && audio_assets->writing;}
int ts_audio_assets_active(void){return audio_assets!=NULL;}
static int asset_parent(char *path)
{
    char *last=NULL;for(char *p=path;*p;++p)if(*p=='/' || *p=='\\')last=p;
    if(!last){strcpy(path,".");return 1;}if(last==path)last[1]=0;else *last=0;return 1;
}
int ts_audio_assets_root(char *out,size_t size,const char *path,unsigned depth)
{
    if(!path || strlen(path)>=size || depth>1)return 0;
    strcpy(out,path);asset_parent(out);
    if(depth){const char *last=out;for(const char *p=out;*p;++p)if(*p=='/' || *p=='\\')last=p+1;
        if(strcmp(last,"project-data"))return 0;
        asset_parent(out);}
    return 1;
}
int ts_audio_assets_depth(const char *path)
{
    if(!audio_assets)return -1;
    char root[2048];
    for(unsigned i=0;i<2;++i)if(ts_audio_assets_root(root,sizeof(root),path,i) && !strcmp(root,audio_assets->root))return (int)i;
    return -1;
}
static int asset_id_valid(const char *id)
{return strlen(id)==64 && strspn(id,"0123456789abcdef")==64;}
static AssetEntry *asset_entry(const char *id,int create)
{
    TsAudioAssets *s=audio_assets;if(!s)return NULL;
    for(size_t i=0;i<s->count;++i)if(!strcmp(s->entries[i].id,id))return &s->entries[i];
    if(!create)return NULL;
    if(s->count==s->capacity){size_t n=s->capacity?s->capacity*2:32;AssetEntry *p=realloc(s->entries,n*sizeof(*p));if(!p)return NULL;s->entries=p;s->capacity=n;}
    AssetEntry *e=&s->entries[s->count++];memset(e,0,sizeof(*e));strcpy(e->id,id);return e;
}
static int asset_path(char *path,size_t n,const char *id)
{
    if(!audio_assets || !asset_id_valid(id))return 0;
    int length=snprintf(path,n,"%s/samples/audio/%s.wav",audio_assets->root,id);
    return length>=0 && (size_t)length<n;
}
static void asset_le(unsigned char *p,uint32_t n,int bytes)
{for(int i=0;i<bytes;++i)p[i]=(unsigned char)(n>>(i*8));}
static int asset_header(const TsSample *s,unsigned char header[44])
{
    size_t n;
    if(!s || !s->frames || s->frames>100000000u || s->sample_rate<1000 || !ts_sample_scalar_count(s,&n) || n>(UINT32_MAX-36u)/4u || s->sample_rate>UINT32_MAX/(s->channels*4u))return 0;
    memcpy(header,"RIFF",4);asset_le(header+4,(uint32_t)n*4+36,4);memcpy(header+8,"WAVEfmt ",8);
    asset_le(header+16,16,4);asset_le(header+20,3,2);asset_le(header+22,s->channels,2);
    asset_le(header+24,s->sample_rate,4);asset_le(header+28,s->sample_rate*s->channels*4u,4);
    asset_le(header+32,s->channels*4,2);asset_le(header+34,32,2);memcpy(header+36,"data",4);asset_le(header+40,(uint32_t)n*4,4);return 1;
}
static int asset_verify(const char *id,const TsSample *sample)
{
    AssetEntry *e=asset_entry(id,1);if(!e)return 0;
    if(e->verified)return e->rate==sample->sample_rate && e->frames==sample->frames && e->channels==sample->channels;
    char path[2048],actual[65];unsigned char expected[44],header[44],buffer[65536];
    if(!asset_path(path,sizeof(path),id) || !asset_header(sample,expected))return 0;
    FILE *f=fopen(path,"rb");if(!f)return 0;
    int ok=fread(header,1,44,f)==44 && !memcmp(header,expected,44);
    int verify_sha=!audio_assets->writing;
    AssetSha sha=asset_sha_init();uint64_t hash=UINT64_C(14695981039346656037);
    if(ok){if(verify_sha)asset_sha_add(&sha,header,44);hash=ts_save_io_hash(hash,header,44);}
    uint64_t left=(uint64_t)sample->frames*sample->channels*4;
    while(ok && left){size_t n=left>sizeof(buffer)?sizeof(buffer):(size_t)left;
        if(!ts_save_io_poll(0) || fread(buffer,1,n,f)!=n){ok=0;break;}
        for(size_t i=0;i<n;i+=4){uint32_t bits=le32(buffer+i);float v;memcpy(&v,&bits,4);if(!isfinite(v)){ok=0;break;}}
        if(verify_sha)asset_sha_add(&sha,buffer,n);
        hash=ts_save_io_hash(hash,buffer,n);left-=n;}
    if(ok)ok=fgetc(f)==EOF && !ferror(f);
    if(fclose(f))ok=0;
    if(verify_sha){asset_sha_end(&sha,actual);if(strcmp(actual,id))ok=0;}
    else if(hash!=e->file_hash)ok=0;
    if(ok){e->verified=1;e->file_hash=hash;e->rate=sample->sample_rate;e->frames=sample->frames;e->channels=sample->channels;}
    return ok;
}
static int asset_same_pcm(const float *a,const float *b,size_t count)
{
    if(a==b)return 1;
    const unsigned char *x=(const unsigned char *)a,*y=(const unsigned char *)b;
    for(size_t left=count*4;left;) {
        size_t n=left>65536?65536:left;
        if(!ts_save_io_poll(0) || memcmp(x,y,n))return 0;
        x+=n;y+=n;left-=n;
    }
    return 1;
}
static int asset_store(const TsSample *sample,char id[65])
{
    if(!ts_audio_assets_writing() || !sample || !sample->data)return 0;
    TsAudioAssets *s=audio_assets;
    size_t scalar_count;
    if(!ts_sample_scalar_count(sample,&scalar_count))return 0;
    for(size_t i=0;i<s->pointer_count;++i){AssetPointer *p=&s->pointers[i];
        if(p->frames==sample->frames && p->rate==sample->sample_rate && p->channels==sample->channels &&
           asset_same_pcm(p->data,sample->data,scalar_count)){strcpy(id,p->id);return 1;}}
    unsigned char header[44],buffer[65536];size_t count;
    if(!asset_header(sample,header) || !ts_sample_scalar_count(sample,&count) || !ts_save_io_stage("Fingerprinting shared audio",sample->name))return 0;
    AssetSha sha=asset_sha_init();asset_sha_add(&sha,header,44);
    uint64_t file_hash=ts_save_io_hash(UINT64_C(14695981039346656037),header,44);
    for(size_t first=0;first<count;){size_t n=count-first;if(n>sizeof(buffer)/4)n=sizeof(buffer)/4;
        if(!ts_save_io_poll(0))return 0;
        for(size_t i=0;i<n;++i){float v=finite_sample(sample->data[first+i]);uint32_t bits;memcpy(&bits,&v,4);asset_le(buffer+i*4,bits,4);}
        asset_sha_add(&sha,buffer,n*4);file_hash=ts_save_io_hash(file_hash,buffer,n*4);first+=n;}
    asset_sha_end(&sha,id);
    AssetEntry *entry=asset_entry(id,0);
    if(!entry){
        char path[2048];if(!asset_path(path,sizeof(path),id))return 0;
        /* The source FNV also has to match the old managed-file inventory.
           This verifies reuse without a second SHA pass over the same PCM. */
        int reused=ts_save_io_reuse(path,file_hash);if(reused<0)return 0;
        if(!reused){
            if(!ts_save_io_stage("Writing shared audio",sample->name))return 0;
            FILE *f=fopen(path,"wbx");if(!f)return 0;
            int ok=fwrite(header,1,44,f)==44 && write_audio32(f,sample) && !ferror(f);if(fclose(f))ok=0;if(!ok)return 0;
        }
        entry=asset_entry(id,1);if(!entry)return 0;entry->file_hash=file_hash;
        if(!ts_save_io_stage("Validating shared audio",sample->name) || !asset_verify(id,sample))return 0;
    } else if(!asset_verify(id,sample))return 0;
    if(s->pointer_count<256){AssetPointer *p=&s->pointers[s->pointer_count++];*p=(AssetPointer){sample->data,sample->frames,sample->sample_rate,sample->channels,{0}};strcpy(p->id,id);}
    return 1;
}
int ts_audio_assets_put_block(FILE *f,const TsSample *sample)
{
    char id[65];const char *end=sample?memchr(sample->name,0,sizeof(sample->name)):NULL;
    if(!end || !asset_store(sample,id))return 0;
    put32(f,sample->sample_rate);put64(f,sample->frames);put32(f,sample->channels);
    put32(f,(uint32_t)(end-sample->name));
    return fwrite(sample->name,1,(size_t)(end-sample->name),f)==(size_t)(end-sample->name) && fwrite(id,1,64,f)==64 && !ferror(f);
}
int ts_audio_assets_get_block(FILE *f,TsSample *sample,int validate_only)
{
    uint32_t channels,name;uint64_t frames;char id[65]={0};size_t count,bytes;
    if(!get32(f,&sample->sample_rate) || !get64(f,&frames) || !get32(f,&channels) || !get32(f,&name) ||
       frames>SIZE_MAX || channels>2 || !ts_sample_dimensions((size_t)frames,(uint8_t)channels,&count,&bytes) || name>=sizeof(sample->name) ||
       fread(sample->name,1,name,f)!=name || fread(id,1,64,f)!=64)return 0;
    sample->name[name]=0;sample->frames=(size_t)frames;sample->channels=(uint8_t)channels;
    if(!asset_id_valid(id) || !asset_verify(id,sample))return 0;
    if(validate_only)return 1;
    char path[2048];if(!asset_path(path,sizeof(path),id))return 0;
    FILE *in=fopen(path,"rb");if(!in)return 0;
    int ok=fseek(in,44,SEEK_SET)==0;
    if(ok){sample->data=malloc(bytes);if(!sample->data){fclose(in);return -1;}}
    unsigned char buffer[65536];
    for(size_t first=0;ok && first<count;){size_t n=count-first;if(n>sizeof(buffer)/4)n=sizeof(buffer)/4;
        if(fread(buffer,4,n,in)!=n){ok=0;break;}
        for(size_t i=0;i<n;++i){uint32_t bits=le32(buffer+i*4);memcpy(sample->data+first+i,&bits,4);}first+=n;}
    if(fclose(in))ok=0;
    return ok;
}
int ts_audio_assets_save_ref(const TsSample *s,const char *path,char *error,size_t size)
{
    FILE *f=fopen(path,"wbx");if(!f){set_error(error,size,"Could not create audio reference");return 0;}
    int ok=fwrite("TSAR1\r\n\032",1,8,f)==8 && ts_audio_assets_put_block(f,s) && !ferror(f);if(fclose(f))ok=0;
    if(!ok)sample_io_error(error,size,"Could not finish shared audio reference");
    return ok;
}
int ts_audio_assets_load_ref(TsSample *s,const char *path,char *error,size_t size)
{
    FILE *f=fopen(path,"rb");TsSample loaded={0};char magic[8];
    if(!f){if(error && size)snprintf(error,size,"Missing audio reference: %s",path);return 0;}
    int ok=fread(magic,1,8,f)==8 && !memcmp(magic,"TSAR1\r\n\032",8) && ts_audio_assets_get_block(f,&loaded,0)>0 && fgetc(f)==EOF && !ferror(f);
    if(fclose(f))ok=0;
    if(ok){ts_sample_free(s);*s=loaded;}else {ts_sample_free(&loaded);if(error && size)snprintf(error,size,"Missing or corrupt shared audio: %s",path);}return ok;
}
int ts_audio_assets_known_hash(const char *path,uint64_t *hash)
{
    if(!audio_assets)return 0;
    char expected[2048];
    for(size_t i=0;i<audio_assets->count;++i){AssetEntry *e=&audio_assets->entries[i];
        if(e->verified && asset_path(expected,sizeof(expected),e->id) && !strcmp(expected,path)){*hash=e->file_hash;return 1;}}
    return 0;
}
