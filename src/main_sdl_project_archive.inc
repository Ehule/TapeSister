/* ZIP64 / stored entries: audio is already large, so export avoids another
   compression pass. Only the private native-save snapshot is walked. */
#include <dirent.h>
#ifdef _WIN32
#include <direct.h>
#define archive_mkdir(p) _mkdir(p)
#define archive_rmdir(p) _rmdir(p)
#define archive_tell(f) _ftelli64(f)
#define archive_seek(f,p) _fseeki64(f,p,SEEK_SET)
typedef struct _stat64 ArchiveStat;
#define archive_stat(p,s) _stat64(p,s)
#else
#define archive_mkdir(p) mkdir(p,0700)
#define archive_rmdir(p) rmdir(p)
#define archive_tell(f) ftello(f)
#define archive_seek(f,p) fseeko(f,p,SEEK_SET)
typedef struct stat ArchiveStat;
#define archive_stat(p,s) stat(p,s)
#endif
typedef struct {char *name;uint64_t size,offset;uint32_t crc;} ProjectArchiveEntry;
typedef struct {ProjectArchiveEntry *entry;size_t count,capacity;} ProjectArchiveList;
static int project_archive_join(char *out,size_t size,const char *a,const char *b)
{int n=snprintf(out,size,"%s/%s",a,b);return n>=0 && (size_t)n<size;}
static int project_archive_walk(const char *root,const char *relative,ProjectArchiveList *list,int remove_tree)
{
    char folder[2048];if(!project_archive_join(folder,sizeof(folder),root,relative))return 0;
    DIR *dir=opendir(folder);if(!dir)return 0;int ok=1;struct dirent *item;
    while(ok && (item=readdir(dir))) {
        if(!strcmp(item->d_name,".") || !strcmp(item->d_name,".."))continue;
        char path[2048],name[2048];
        if(!project_archive_join(path,sizeof(path),folder,item->d_name) ||
           (relative[0]?!project_archive_join(name,sizeof(name),relative,item->d_name):
             snprintf(name,sizeof(name),"%s",item->d_name)>=(int)sizeof(name))) {ok=0;break;}
        ArchiveStat st;if(archive_stat(path,&st)){ok=0;break;}
#ifdef _WIN32
        if(GetFileAttributesA(path)&FILE_ATTRIBUTE_REPARSE_POINT){ok=0;break;}
#else
        struct stat link;if(lstat(path,&link) || S_ISLNK(link.st_mode)){ok=0;break;}
#endif
        if(S_ISDIR(st.st_mode))ok=project_archive_walk(root,name,list,remove_tree);
        else if(remove_tree)ok=remove(path)==0;
        else {
            if(!S_ISREG(st.st_mode) || st.st_size<0){ok=0;break;}
            if(list->count==list->capacity) {
                size_t capacity=list->capacity?list->capacity*2:64;
                ProjectArchiveEntry *entries=realloc(list->entry,capacity*sizeof(*entries));
                if(!entries){ok=0;break;}list->entry=entries;list->capacity=capacity;
            }
            char *copy=malloc(strlen(name)+1);if(!copy){ok=0;break;}strcpy(copy,name);
            list->entry[list->count++]=(ProjectArchiveEntry){.name=copy,.size=(uint64_t)st.st_size};
        }
    }
    closedir(dir);if(remove_tree && ok)ok=archive_rmdir(folder)==0;return ok;
}
static void project_archive_number(FILE *file,uint64_t value,int bytes)
{for(int i=0;i<bytes;++i){fputc((int)(value&255),file);value>>=8;}}
static int project_archive_zip(const char *root,const char *destination,TsSaveControl *control,char *error,size_t capacity)
{
    ProjectArchiveList list={0};char temporary[2048]={0},path[2048];FILE *file=NULL;
    int ok=0,temporary_owned=0;uint32_t table[256];
    TsSaveControl *previous=ts_save_io_bind(control);
    if(!project_archive_walk(root,"",&list,0))goto done;
    if(snprintf(temporary,sizeof(temporary),"%s.part-%llu",destination,(unsigned long long)SDL_GetPerformanceCounter())>=(int)sizeof(temporary))goto done;
    file=fopen(temporary,"wbx");if(!file)goto done;temporary_owned=1;
    for(unsigned i=0;i<256;++i){uint32_t c=i;for(int j=0;j<8;++j)c=(c>>1)^((c&1)?0xedb88320u:0);table[i]=c;}
    unsigned char buffer[65536];
    for(size_t i=0;i<list.count;++i) {
        ProjectArchiveEntry *e=&list.entry[i];size_t length=strlen(e->name);
        if(length>65535 || !ts_save_io_stage("Writing ZIP",e->name) ||
           !project_archive_join(path,sizeof(path),root,e->name))goto done;
        FILE *input=fopen(path,"rb");if(!input)goto done;
        int64_t position=archive_tell(file);if(position<0){fclose(input);goto done;}e->offset=(uint64_t)position;
        project_archive_number(file,0x04034b50,4);project_archive_number(file,45,2);
        project_archive_number(file,2048,2);project_archive_number(file,0,2);
        project_archive_number(file,0,2);project_archive_number(file,33,2);project_archive_number(file,0,4);
        project_archive_number(file,UINT32_MAX,4);project_archive_number(file,UINT32_MAX,4);
        project_archive_number(file,length,2);project_archive_number(file,20,2);fwrite(e->name,1,length,file);
        project_archive_number(file,1,2);project_archive_number(file,16,2);
        project_archive_number(file,e->size,8);project_archive_number(file,e->size,8);
        uint64_t left=e->size;uint32_t crc=UINT32_MAX;int copied=1;
        while(left) {
            size_t wanted=left<sizeof(buffer)?(size_t)left:sizeof(buffer),got=fread(buffer,1,wanted,input);
            if(got!=wanted || fwrite(buffer,1,got,file)!=got || !ts_save_io_poll(got)){copied=0;break;}
            for(size_t b=0;b<got;++b)crc=table[(crc^buffer[b])&255]^(crc>>8);
            left-=got;
        }
        if(fclose(input) || !copied)goto done;
        e->crc=~crc;position=archive_tell(file);if(position<0 || archive_seek(file,e->offset+14))goto done;
        project_archive_number(file,e->crc,4);if(archive_seek(file,position))goto done;
    }
    int64_t central=archive_tell(file);if(central<0)goto done;
    for(size_t i=0;i<list.count;++i) {
        ProjectArchiveEntry *e=&list.entry[i];size_t length=strlen(e->name);
        project_archive_number(file,0x02014b50,4);project_archive_number(file,45,2);project_archive_number(file,45,2);
        project_archive_number(file,2048,2);project_archive_number(file,0,2);project_archive_number(file,0,2);project_archive_number(file,33,2);
        project_archive_number(file,e->crc,4);project_archive_number(file,UINT32_MAX,4);project_archive_number(file,UINT32_MAX,4);
        project_archive_number(file,length,2);project_archive_number(file,28,2);project_archive_number(file,0,2);
        project_archive_number(file,0,2);project_archive_number(file,0,2);project_archive_number(file,0,4);project_archive_number(file,UINT32_MAX,4);
        fwrite(e->name,1,length,file);project_archive_number(file,1,2);project_archive_number(file,24,2);
        project_archive_number(file,e->size,8);project_archive_number(file,e->size,8);project_archive_number(file,e->offset,8);
    }
    int64_t end=archive_tell(file);if(end<central)goto done;
    project_archive_number(file,0x06064b50,4);project_archive_number(file,44,8);
    project_archive_number(file,45,2);project_archive_number(file,45,2);project_archive_number(file,0,4);project_archive_number(file,0,4);
    project_archive_number(file,list.count,8);project_archive_number(file,list.count,8);
    project_archive_number(file,(uint64_t)(end-central),8);project_archive_number(file,(uint64_t)central,8);
    project_archive_number(file,0x07064b50,4);project_archive_number(file,0,4);project_archive_number(file,(uint64_t)end,8);project_archive_number(file,1,4);
    project_archive_number(file,0x06054b50,4);project_archive_number(file,0,2);project_archive_number(file,0,2);
    project_archive_number(file,65535,2);project_archive_number(file,65535,2);
    project_archive_number(file,UINT32_MAX,4);project_archive_number(file,UINT32_MAX,4);project_archive_number(file,0,2);
    if(ferror(file) || !ts_save_io_stage("Finishing ZIP",destination))goto done;
    int closed=fclose(file);file=NULL;if(closed)goto done;
#ifdef _WIN32
    ok=MoveFileExA(temporary,destination,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    ok=rename(temporary,destination)==0;
#endif
done:
    if(file)fclose(file);if(temporary_owned && !ok)remove(temporary);
    for(size_t i=0;i<list.count;++i)free(list.entry[i].name);free(list.entry);
    if(!ok)snprintf(error,capacity,control && control->was_cancelled?"ZIP export cancelled; previous ZIP kept":"ZIP export failed; previous ZIP kept (check destination space and permissions)");
    ts_save_io_bind(previous);return ok;
}
