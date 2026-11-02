/* Reuse verified generated files through sibling hard links. Old files remain
   immutable; on filesystems without hard links, copy in bounded blocks. The
   existing ownership manifest is still the authority for user-file protection.
   Source signatures supplement (never replace) the stored-file fingerprints. */
typedef struct {
    char old[TS_PROJECT_PATH_MAX],staging[TS_PROJECT_PATH_MAX];
    ProjectFiles previous,current,owned;
} ProjectSaveReuse;
static void project_save_completed(void)
{TsSaveControl *c=ts_save_io_control();if(c)++c->completed;}
static void project_save_reuse_begin(ProjectSaveReuse *r,const char *path)
{
    if(!path || !path_directory(path,r->old,sizeof(r->old)))return;
    if(snprintf(r->staging,sizeof(r->staging),"%s.tapesister-partial",r->old)>=(int)sizeof(r->staging)) {r->staging[0]=0;return;}
    char manifest[TS_PROJECT_PATH_MAX],line[TS_PROJECT_PATH_MAX+64];
    if(!bundle_is_owned(r->old,path_name(path)) ||
       !project_read_ownership(r->old,path_name(path),&r->owned,NULL,0) ||
       !project_join(manifest,sizeof(manifest),r->old,"manifest.txt"))return;
    FILE *f=fopen(manifest,"rb");if(!f)return;
    while(fgets(line,sizeof(line),f)) {
        unsigned long long hash;int offset=0;
        line[strcspn(line,"\r\n")]=0;
        if(sscanf(line,"reuse=%16llx,%n",&hash,&offset)==1 && offset)
            (void)project_files_add(&r->previous,line+offset,(uint64_t)hash,1);
    }
    fclose(f);
}
static void project_save_reuse_end(ProjectSaveReuse *r)
{project_files_free(&r->previous);project_files_free(&r->current);project_files_free(&r->owned);}
static int project_save_reuse(void *context,const char *path,uint64_t signature)
{
    ProjectSaveReuse *r=context;size_t length=strlen(r->staging);
    if(!length || strncmp(path,r->staging,length) || path[length]!='/')return 0;
    const char *relative=path+length+1;
    if(!project_files_add(&r->current,relative,signature,1))return -1;
    ProjectFile *source=project_files_find(&r->previous,relative),*owned=project_files_find(&r->owned,relative);
    if(!source || source->hash!=signature || !owned || !owned->fingerprint)return 0;
    if(!strncmp(relative,"samples/audio/",14) && owned->hash!=signature)return 0;
    char old[TS_PROJECT_PATH_MAX];uint64_t hash;
    if(!project_join(old,sizeof(old),r->old,relative) || project_entry_kind(old)!=PROJECT_ENTRY_FILE ||
       !project_file_hash(old,&hash) || hash!=owned->hash || !ts_save_io_poll(0))return 0;
#ifdef _WIN32
    if(CreateHardLinkA(path,old,NULL))return 1;
#else
    if(link(old,path)==0)return 1;
#endif
    FILE *in=fopen(old,"rb"),*out=NULL;int ok=in!=NULL;
    if(ok)out=fopen(path,"wbx");
    if(!out)ok=0;
    unsigned char buffer[65536];
    while(ok) {
        size_t n=fread(buffer,1,sizeof(buffer),in);
        if(n && (fwrite(buffer,1,n,out)!=n || !ts_save_io_poll(n))){ok=0;break;}
        if(n<sizeof(buffer)){if(ferror(in))ok=0;break;}
    }
    if(in && fclose(in))ok=0;
    if(out && fclose(out))ok=0;
    return ok?1:-1;
}
static int project_save_reuse_manifest(FILE *f)
{
    TsSaveControl *c=ts_save_io_control();if(!c || c->reuse!=project_save_reuse)return 1;
    ProjectSaveReuse *r=c->reuse_userdata;
    for(size_t i=0;i<r->current.count;++i)
        if(fprintf(f,"reuse=%016llx,%s\n",(unsigned long long)r->current.files[i].hash,r->current.files[i].path)<0)return 0;
    return 1;
}
static void project_save_retain(const char *staging,char *error,size_t size)
{
    /* Keep failed material under a fresh name, so retry never deletes it and
       never needs to move or damage the last valid project. */
    char recovery[TS_PROJECT_PATH_MAX];static atomic_uint sequence;
    for(unsigned attempt=0;attempt<1000;++attempt) {
        unsigned id=atomic_fetch_add(&sequence,1u)+1u;
        int n=snprintf(recovery,sizeof(recovery),"%s-failed-%lu-%u",staging,(unsigned long)time(NULL),id);
        if(n<0 || (size_t)n>=sizeof(recovery))break;
        if(project_entry_kind(recovery)!=PROJECT_ENTRY_MISSING)continue;
        if(!rename_directory(staging,recovery))break;
        TsSaveControl *c=ts_save_io_control();if(c)snprintf(c->recovery,sizeof(c->recovery),"%s",recovery);
        if(error && size) {
            size_t used=strlen(error);
            snprintf(error+used,size-used,"; recovery: %s",recovery);
        }
        return;
    }
    /* A failed recovery rename must not authorize deleting the staged files. */
    (void)write_partial_marker(staging,1,NULL,0);
    TsSaveControl *c=ts_save_io_control();if(c)snprintf(c->recovery,sizeof(c->recovery),"%s",staging);
}
