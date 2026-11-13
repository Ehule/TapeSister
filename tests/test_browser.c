#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/browser.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define getcwd _getcwd
#define chdir _chdir
#define getpid _getpid
#define make_dir(path) _mkdir(path)
#define remove_dir _rmdir
#else
#include <unistd.h>
#define make_dir(path) mkdir(path,0700)
#define remove_dir rmdir
#endif

static int same_folder(const char *a,const char *b)
{
#ifdef _WIN32
    char x[TS_BROWSER_PATH_MAX],y[TS_BROWSER_PATH_MAX];
    assert(_fullpath(x,a,sizeof(x)) && _fullpath(y,b,sizeof(y)));
    return !_stricmp(x,y);
#else
    struct stat x,y;
    return !stat(a,&x) && !stat(b,&y) && x.st_dev==y.st_dev && x.st_ino==y.st_ino;
#endif
}
static void child(TsBrowser *b,const char *name)
{
    int at=-1;
    for(int i=0;i<b->entry_count;++i)if(!strcmp(b->entries[i].name,name)){at=i;break;}
    assert(at>=0);ts_browser_select(b,at);assert(ts_browser_enter_selected_directory(b));
}
static void file_at(const char *path)
{FILE *f=fopen(path,"wb");assert(f);assert(!fclose(f));}

int main(void)
{
    char cwd[TS_BROWSER_PATH_MAX],fixture[64],a[128],b[128],config[128],gone[128],file[160];
    assert(getcwd(cwd,sizeof(cwd)));
    snprintf(fixture,sizeof(fixture),"test-browser-history-%ld",(long)getpid());
    snprintf(a,sizeof(a),"%s/Samples",fixture);snprintf(b,sizeof(b),"%s/Patches",fixture);
    snprintf(config,sizeof(config),"%s/Config",fixture);snprintf(gone,sizeof(gone),"%s/Gone",fixture);
    assert(!make_dir(fixture) && !make_dir(a) && !make_dir(b) && !make_dir(config) && !make_dir(gone));
    snprintf(file,sizeof(file),"%s/drum.wav",a);file_at(file);
    snprintf(file,sizeof(file),"%s/patch.sunsynth",b);file_at(file);
    TsBrowser *one=calloc(1,sizeof(*one)),*two=calloc(1,sizeof(*two)),*three=calloc(1,sizeof(*three));
    assert(one && two && three);ts_browser_init(one);ts_browser_init(two);
    assert(ts_browser_open(one,TS_BROWSER_LOAD_WAV,NULL) && same_folder(one->directory,cwd));
    child(one,fixture);child(one,"Samples");ts_browser_close(one);
    assert(ts_browser_open(one,TS_BROWSER_LOAD_WAV,NULL) && same_folder(one->directory,a));
    ts_browser_close(one); /* Cancel still remembers navigation. */
    assert(ts_browser_open(two,TS_BROWSER_LOAD_SUNSYNTH,NULL) && same_folder(two->directory,a));
    assert(ts_browser_parent(two));child(two,"Patches");ts_browser_close(two);
    assert(ts_browser_open(one,TS_BROWSER_LOAD_WAV,NULL) && same_folder(one->directory,b));
    ts_browser_init(three);assert(ts_browser_open(three,TS_BROWSER_IMPORT_CANVAS,NULL) && same_folder(three->directory,b));

    /* Config pickers retain explicit defaults without displacing sample history. */
    snprintf(three->directory,sizeof(three->directory),"%s",config);
    assert(ts_browser_open(three,TS_BROWSER_SELECT_SAMPLE_DIRECTORY,NULL));
    assert(same_folder(three->directory,config));ts_browser_close(three);
    assert(ts_browser_open(three,TS_BROWSER_LOAD_WAV,NULL) && same_folder(three->directory,b));
    ts_browser_close(three);

    /* Intentional caller navigation wins and the selected file survives reopen. */
    snprintf(one->directory,sizeof(one->directory),"%s",a);
    assert(ts_browser_open(one,TS_BROWSER_LOAD_WAV,NULL) && same_folder(one->directory,a));
    int selected=-1;for(int i=0;i<one->entry_count;++i)if(!strcmp(one->entries[i].name,"drum.wav"))selected=i;
    assert(selected>=0);ts_browser_select(one,selected);ts_browser_close(one);
    assert(ts_browser_open(one,TS_BROWSER_LOAD_WAV,NULL));
    assert(ts_browser_selected_path(one,file,sizeof(file)) && strstr(file,"drum.wav"));
    ts_browser_close(one);

    /* Remember absolute locations even if another subsystem changes cwd. */
    assert(!chdir(b));ts_browser_init(three);
    assert(ts_browser_open(three,TS_BROWSER_LOAD_WAV,NULL));
    assert(strstr(three->directory,"Samples") && three->entry_count==1);
    ts_browser_close(three);assert(!chdir(cwd));

    snprintf(one->directory,sizeof(one->directory),"%s",fixture);
    assert(ts_browser_open(one,TS_BROWSER_LOAD_WAV,NULL));
    int missing=-1;for(int i=0;i<one->entry_count;++i)if(!strcmp(one->entries[i].name,"Gone"))missing=i;
    assert(missing>=0);ts_browser_select(one,missing);assert(!remove_dir(gone));
    assert(!ts_browser_enter_selected_directory(one));
    assert(same_folder(one->directory,fixture));ts_browser_close(one);
    assert(ts_browser_open(two,TS_BROWSER_LOAD_WAV,NULL) && same_folder(two->directory,fixture));
    assert(!make_dir(gone));assert(ts_browser_refresh(two));child(two,"Gone");ts_browser_close(two);
    assert(!remove_dir(gone));
    assert(ts_browser_open(one,TS_BROWSER_EXPORT_WAV,"take.wav"));
    assert(same_folder(one->directory,cwd) && strstr(one->message,"LAST FOLDER UNAVAILABLE"));
    assert(!strcmp(one->filename,"take.wav"));ts_browser_close(one);
    assert(getcwd(file,sizeof(file)) && same_folder(file,cwd));

    snprintf(file,sizeof(file),"%s/drum.wav",a);assert(!remove(file));
    snprintf(file,sizeof(file),"%s/patch.sunsynth",b);assert(!remove(file));
    assert(!remove_dir(a) && !remove_dir(b) && !remove_dir(config) && !remove_dir(fixture));
    free(one);free(two);free(three);
    puts("Session browser history: startup, reopen/cancel, cross-window/mode, explicit paths, config isolation, cwd changes and missing-folder fallback passed.");
    return 0;
}
