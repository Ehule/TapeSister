#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/audio_assets.h"
#include "tapesister/sample_pages.h"
#include "tapesister/mosaic.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define MAKE_DIR(p) _mkdir(p)
#define CHANGE_DIR(p) _chdir(p)
#else
#include <unistd.h>
#define MAKE_DIR(p) mkdir(p,0700)
#define CHANGE_DIR(p) chdir(p)
#endif
#include "tsr_legacy_fixture.h"

static char error[2048];
static uint64_t file_hash(const char *path)
{
    FILE *f=fopen(path,"rb");assert(f);uint64_t hash=1;int c;
    while((c=fgetc(f))!=EOF)hash=hash*131+(unsigned)c;
    assert(!ferror(f));assert(!fclose(f));return hash;
}
static void same_audio(const TsSample *a,const TsSample *b)
{
    assert(a->frames==b->frames && a->sample_rate==b->sample_rate && a->channels==b->channels);
    assert(!memcmp(a->data,b->data,a->frames*a->channels*sizeof(float)));
}
static void header(const char *path,const char *expected)
{
    char magic[8];FILE *f=fopen(path,"rb");assert(f);
    assert(fread(magic,1,8,f)==8 && !memcmp(magic,expected,8));assert(!fclose(f));
}
static void asset_path_from_ref(const char *ref,const char *root,char out[512])
{
    FILE *f=fopen(ref,"rb");assert(f);assert(!fseek(f,-64,SEEK_END));char id[65]={0};
    assert(fread(id,1,64,f)==64);assert(!fclose(f));
    assert(strlen(id)==64 && strspn(id,"0123456789abcdef")==64);
    snprintf(out,512,"%s/samples/audio/%s.wav",root,id);
}
static void migrate_versions(void)
{
    static TsInstrument seed;ts_instrument_init(&seed);
    TsSample sample={.frames=128,.channels=2,.sample_rate=48000};
    sample.data=malloc(sample.frames*8);assert(sample.data);
    for(size_t i=0;i<sample.frames*2;++i)sample.data[i]=(float)(i%17)*.03125f;
    sample.data[0]=-0.0f;sample.data[1]=nextafterf(.25f,1.0f);
    strcpy(sample.name,"EXACT STEREO");
    assert(ts_instrument_import_sample(&seed,&sample,0,0,0,TS_LOOP_FORWARD,error,sizeof(error)));
    assert(ts_instrument_save_recipe(&seed,"seed.tsr",error,sizeof(error)));
    header("seed.tsr","TSR41\r\n\032");
    const int versions[]={31,34,35,37,38,39,40,41};
    for(size_t v=0;v<sizeof(versions)/sizeof(*versions);++v) {
        char old[64],root[64],path[160];int version=versions[v];
        snprintf(old,sizeof(old),"legacy-%d.tsr",version);
        if(version==41)assert(ts_instrument_save_recipe(&seed,old,error,sizeof(error)));
        else assert(test_write_legacy_single_tile("seed.tsr",old,version));
        uint64_t unchanged=file_hash(old);
        static TsInstrument bank,record;TsSamplePages pages;
        ts_instrument_init(&bank);ts_instrument_init(&record);
        assert(ts_sample_pages_init(&pages,error,sizeof(error)));
        assert(ts_sample_pages_load_project(&pages,&bank,&record,old,error,sizeof(error)));
        assert(file_hash(old)==unchanged);same_audio(&sample,&bank.current);
        TsTileId id=bank.bank[0].tile_id;
        snprintf(root,sizeof(root),"migrated-%d",version);
        snprintf(path,sizeof(path),"%s/%s.tsr",root,root);
        TsSaveControl control={0};
        assert(ts_sample_pages_save_project_controlled(&pages,&bank,&record,NULL,path,&control,error,sizeof(error)));
        assert(control.bytes==sample.frames*8); /* rendered + baseline share PCM */
        header(path,"TSR42\r\n\032");assert(file_hash(old)==unchanged);
        ts_sample_pages_free(&pages);ts_instrument_free(&bank);ts_instrument_free(&record);
        assert(ts_sample_pages_init(&pages,error,sizeof(error)));
        assert(ts_sample_pages_load_project(&pages,&bank,&record,path,error,sizeof(error)));
        same_audio(&sample,&bank.current);same_audio(&sample,&bank.parent);
        assert(bank.bank[0].tile_id==id);
        /* A standalone bank export remains self-contained and portable. */
        assert(ts_instrument_save_recipe(&bank,"standalone.tsr",error,sizeof(error)));
        header("standalone.tsr","TSR41\r\n\032");
        assert(ts_instrument_validate_recipe("standalone.tsr",error,sizeof(error)));
        ts_sample_pages_free(&pages);ts_instrument_free(&bank);ts_instrument_free(&record);
    }
    ts_instrument_free(&seed);ts_sample_free(&sample);
}
static void edited_history_roundtrip(void)
{
    static TsInstrument bank,reopened,record;TsSamplePages pages;
    ts_instrument_init(&bank);ts_instrument_init(&reopened);ts_instrument_init(&record);
    assert(ts_sample_pages_init(&pages,error,sizeof(error)));
    assert(ts_instrument_activate_silence(&bank,1000,48000,error,sizeof(error)));
    for(size_t i=0;i<bank.current.frames;++i)bank.current.data[i]=(float)i/2000;
    assert(ts_instrument_import_sample(&bank,&bank.current,0,0,0,TS_LOOP_FORWARD,error,sizeof(error)));
    float replacement[100];for(int i=0;i<100;++i)replacement[i]=.75f-(float)i/500;
    TsSample patch={.data=replacement,.frames=100,.channels=1,.sample_rate=48000};
    assert(ts_instrument_apply_rendered_replacement(&bank,&patch,100,200,error,sizeof(error)));
    assert(bank.bank[0].patch_count>0);
    ts_instrument_set_selection(&bank,110,180);
    assert(ts_instrument_apply_sample_edit(&bank,TS_SAMPLE_EDIT_REVERSE,1,error,sizeof(error)));
    assert(ts_instrument_undo(&bank,error,sizeof(error)) && bank.undo_count && bank.redo_count);
    assert(ts_sample_pages_save_project(&pages,&bank,NULL,NULL,"history/history.tsr",error,sizeof(error)));
    assert(ts_instrument_load_recipe(&reopened,"history/history.tsr",error,sizeof(error)));
    same_audio(&bank.current,&reopened.current);same_audio(&bank.parent,&reopened.parent);
    assert(bank.undo_count==reopened.undo_count && bank.redo_count==reopened.redo_count);
    assert(bank.bank[0].patch_count==reopened.bank[0].patch_count);
    for(int p=0;p<bank.bank[0].patch_count;++p)same_audio(&bank.bank[0].patches[p].sample,&reopened.bank[0].patches[p].sample);
    assert(ts_instrument_redo(&bank,error,sizeof(error)) && ts_instrument_redo(&reopened,error,sizeof(error)));
    same_audio(&bank.current,&reopened.current);
    assert(ts_instrument_undo(&bank,error,sizeof(error)) && ts_instrument_undo(&reopened,error,sizeof(error)));
    same_audio(&bank.current,&reopened.current);
    ts_sample_pages_free(&pages);ts_instrument_free(&bank);ts_instrument_free(&reopened);ts_instrument_free(&record);
}
static void integrity_and_incremental(void)
{
    static TsInstrument bank,record;TsSamplePages pages;
    ts_instrument_init(&bank);ts_instrument_init(&record);
    assert(ts_sample_pages_init(&pages,error,sizeof(error)));
    assert(ts_instrument_activate_silence(&bank,1000,48000,error,sizeof(error)));
    for(size_t i=0;i<bank.current.frames;++i)bank.current.data[i]=.125f;
    assert(ts_instrument_import_sample(&bank,&bank.current,0,0,0,TS_LOOP_FORWARD,error,sizeof(error)));
    pages.mosaic=ts_mosaic_create();assert(pages.mosaic);
    TsMosaicSource *source=ts_mosaic_source(pages.mosaic,&bank.current,error,sizeof(error));assert(source);
    assert(ts_mosaic_add(pages.mosaic,source,0,0));
    TsSaveControl control={0};
    assert(ts_sample_pages_save_project_controlled(&pages,&bank,NULL,NULL,"shared/shared.tsr",&control,error,sizeof(error)));
    assert(control.bytes==4000); /* bank, baseline, Mosaic and collection */
    char asset[512];asset_path_from_ref("shared/samples/page-001/01_SILENCE.tsa","shared",asset);
    char mosaic[512];asset_path_from_ref("shared/project-data/mosaic-000.tsa","shared",mosaic);
    /* Independently generated SHA-256 known answer for the canonical WAV. */
    assert(strstr(asset,"bcffb5bc50692d39afdc7140d7253b8d38929284e6f7509a3c5f6440b6a9d16e.wav"));
    assert(!strcmp(asset,mosaic));uint64_t original=file_hash(asset);
#ifndef _WIN32
    struct stat before,after;assert(!stat(asset,&before));
#endif
    pages.mosaic->events[0].start=2;
    assert(ts_sample_pages_save_project_controlled(&pages,&bank,NULL,NULL,"shared/shared.tsr",&control,error,sizeof(error)));
    assert(control.bytes==0 && file_hash(asset)==original);
#ifndef _WIN32
    assert(!stat(asset,&after) && before.st_ino==after.st_ino);
#endif
    bank.current.data[0]=nextafterf(bank.current.data[0],1.0f);
    assert(ts_sample_pages_save_project_controlled(&pages,&bank,NULL,NULL,"shared/shared.tsr",&control,error,sizeof(error)));
    assert(control.bytes==4000); /* one changed sound, not the entire bank */
    TsSample changed={0};assert(ts_sample_clone(&changed,&bank.current,error,sizeof(error)));
    ts_mosaic_free(pages.mosaic);pages.mosaic=NULL;ts_sample_pages_free(&pages);ts_instrument_free(&bank);
    assert(ts_sample_pages_init(&pages,error,sizeof(error)));
    assert(ts_sample_pages_load_project(&pages,&bank,&record,"shared/shared.tsr",error,sizeof(error)));
    same_audio(&changed,&bank.current);assert(bank.parent.data[0]==.125f);
    uint64_t live=ts_sample_save_signature(&bank.current);
    assert(!rename("shared/manifest.txt","held-manifest.txt"));
    assert(!ts_sample_pages_load_project(&pages,&bank,&record,"shared/shared.tsr",error,sizeof(error)));
    assert(ts_sample_save_signature(&bank.current)==live);
    assert(!rename("held-manifest.txt","shared/manifest.txt"));
    /* Missing and bit-corrupt assets fail transactionally, retaining the live project. */
    asset_path_from_ref("shared/samples/page-001/01_SILENCE.tsa","shared",asset);
    assert(!rename(asset,"held.wav"));
    assert(!ts_sample_pages_load_project(&pages,&bank,&record,"shared/shared.tsr",error,sizeof(error)));
    assert(ts_sample_save_signature(&bank.current)==live);assert(!rename("held.wav",asset));
    FILE *f=fopen(asset,"r+b");assert(f);assert(!fseek(f,44,SEEK_SET));int c=fgetc(f);assert(c!=EOF);
    assert(!fseek(f,44,SEEK_SET));assert(fputc(c^1,f)!=EOF);assert(!fclose(f));
    assert(!ts_sample_pages_load_project(&pages,&bank,&record,"shared/shared.tsr",error,sizeof(error)));
    assert(ts_sample_save_signature(&bank.current)==live);
    ts_sample_free(&changed);ts_sample_pages_free(&pages);ts_instrument_free(&bank);ts_instrument_free(&record);
}
int main(void)
{
    char work[128];
#ifdef _WIN32
    snprintf(work,sizeof(work),"shared-audio-%lu-%lu",(unsigned long)GetCurrentProcessId(),(unsigned long)GetTickCount());
    assert(!MAKE_DIR(work));
#else
    strcpy(work,"shared-audio-XXXXXX");assert(mkdtemp(work));
#endif
    assert(!CHANGE_DIR(work));migrate_versions();edited_history_roundtrip();integrity_and_incremental();
    puts("legacy TSR31/34/35/37/38/39/40/41 -> shared TSR42, exact stereo, deduplication, incremental edits, corruption and original-file preservation passed");
    return 0;
}
