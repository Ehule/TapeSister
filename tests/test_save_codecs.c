#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Inject failures into the real codecs, including failures hidden by a later
   successful fclose. No production fault switches. */
static int fault_write,fault_close,fault_error;
static size_t checked_fwrite(const void *p,size_t size,size_t n,FILE *f)
{if(fault_write){errno=ENOSPC;return 0;}return fwrite(p,size,n,f);}
static int checked_fclose(FILE *f)
{int result=fclose(f);if(fault_close){errno=EIO;return EOF;}return result;}
static int checked_ferror(FILE *f){return fault_error?1:ferror(f);}
#define fwrite checked_fwrite
#define fclose checked_fclose
#define ferror checked_ferror
#include "../src/ts_sample.c"
#undef fwrite
#undef fclose
#undef ferror

static int cancel_save(void *data){return ((TsSaveControl *)data)->bytes>=65536;}
static int boundaries(void)
{
    static TsInstrument bank;char error[256];ts_instrument_init(&bank);
    TsSample sample={.frames=33554432,.sample_rate=48000,.channels=1};
    sample.data=malloc(sample.frames*4);assert(sample.data);
    for(size_t i=0;i<sample.frames;++i)sample.data[i]=.125f;
    snprintf(sample.name,sizeof(sample.name),"BOUNDARY");
    for(int slots=8;slots<=16;slots+=8) {
        for(int i=0;i<slots;++i) {
            bank.bank[i].occupied=1;bank.bank[i].tile_id=ts_tile_id_new();bank.bank[i].sample=sample;
        }
        const char *path="save-boundary.tsr";
        assert(ts_instrument_save_recipe(&bank,path,error,sizeof(error)));
        assert(ts_instrument_validate_recipe(path,error,sizeof(error)));
#ifdef _WIN32
        struct _stat64 info;assert(!_stat64(path,&info));
#else
        struct stat info;assert(!stat(path,&info));
#endif
        assert((uint64_t)info.st_size>(uint64_t)slots*268435456ull);
        printf("TSR file beyond %d GiB: %llu bytes; streaming validation passed\n",slots/4,(unsigned long long)info.st_size);fflush(stdout);
        assert(!remove(path));
    }
    /* Deliberately borrowed PCM in this boundary fixture; production tile
       ownership is unchanged. Do not free the aliases as separate samples. */
    for(int i=0;i<16;++i)bank.bank[i].sample.data=NULL;
    ts_instrument_free(&bank);ts_sample_free(&sample);return 0;
}
int main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--boundaries"))return boundaries();
    static TsInstrument bank;char error[256];
    ts_instrument_init(&bank);
    assert(ts_instrument_activate_silence(&bank,40000,48000,error,sizeof(error)));
    for(size_t i=0;i<bank.current.frames;++i)bank.current.data[i]=(float)(i%64)/256;
    FILE *a=tmpfile(),*b=tmpfile();assert(a && b);
    assert(write_audio32(a,&bank.current));
    for(size_t i=0;i<bank.current.frames;++i)put_float(b,bank.current.data[i]);
    rewind(a);rewind(b);int x,y;do{x=fgetc(a);y=fgetc(b);assert(x==y);}while(x!=EOF);
    fclose(a);fclose(b);
    const char *path="save-codec-test.tsr",*wav="save-codec-test.wav";
    assert(ts_instrument_save_recipe(&bank,path,error,sizeof(error)));
    assert(ts_instrument_validate_recipe(path,error,sizeof(error)));
    FILE *f=fopen(path,"r+b");assert(f);assert(!fseek(f,0,SEEK_END));assert(fputc(0,f)!=EOF);assert(!fclose(f));
    assert(!ts_instrument_validate_recipe(path,error,sizeof(error)));
    fault_write=1;
    assert(!ts_instrument_save_recipe(&bank,path,error,sizeof(error)));
    assert(!ts_sample_save_wav16(&bank.current,wav,error,sizeof(error)));
    assert(!ts_sample_save_wav32f(&bank.current,wav,error,sizeof(error)));
    fault_write=0;fault_close=1;
    assert(!ts_instrument_save_recipe(&bank,path,error,sizeof(error)));
    assert(!ts_sample_save_wav16(&bank.current,wav,error,sizeof(error)));
    assert(!ts_sample_save_wav32f(&bank.current,wav,error,sizeof(error)));
    fault_close=0;
    fault_error=1;
    assert(!ts_instrument_save_recipe(&bank,path,error,sizeof(error)));
    assert(!ts_sample_save_wav16(&bank.current,wav,error,sizeof(error)));
    assert(!ts_sample_save_wav32f(&bank.current,wav,error,sizeof(error)));
    fault_error=0;
    TsSaveControl c={0};c.userdata=&c;c.cancelled=cancel_save;ts_save_io_bind(&c);
    assert(!ts_instrument_save_recipe(&bank,path,error,sizeof(error)));
    assert(c.was_cancelled && c.bytes==65536);ts_save_io_bind(NULL);
    size_t frames=bank.current.frames;bank.current.frames=100000001;
    assert(!ts_instrument_save_recipe(&bank,path,error,sizeof(error)));
    assert(strstr(error,"100000000"));bank.current.frames=frames;
    ts_instrument_free(&bank);remove(path);remove(wav);
    puts("save codecs: compatible bytes, bounded cancellation, read/write/close failures passed");return 0;
}
