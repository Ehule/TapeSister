/* Recovery is deliberately a copy operation, not a transaction replay. The
   in-memory rename journal may have been lost, and user entries can be split
   between backup and staging. Neither version is an automatic winner. */
static int project_recovery_suffix(char *directory)
{
    const char *suffix[]={".tapesister-backup", ".tapesister-partial"};
    size_t length=strlen(directory);
    for(int i=0;i<2;++i) {
        size_t n=strlen(suffix[i]);
        if(length>n && path_component_equal(directory+length-n,suffix[i])) {
            directory[length-n]=0;return i+1;
        }
    }
    return 0;
}

static int project_recovery_manifest_name(const char *directory,char *name,size_t size)
{
    char path[TS_PROJECT_PATH_MAX],line[TS_PROJECT_PATH_MAX+32];
    if(project_entry_kind(directory)!=PROJECT_ENTRY_DIRECTORY || !project_join(path,sizeof(path),directory,"manifest.txt") ||
       project_entry_kind(path)!=PROJECT_ENTRY_FILE)return 0;
    FILE *file=fopen(path,"rb");if(!file)return 0;
    int ok=fgets(line,sizeof(line),file) && !strcmp(line,"TAPESISTER_PROJECT 2\n") &&
        fgets(line,sizeof(line),file) && !strncmp(line,"project=",8);
    fclose(file);if(!ok)return 0;
    line[strcspn(line,"\r\n")]=0;const char *value=line+8;
    size_t length=strlen(value);
    if(!project_relative_valid(value) || strchr(value,'/') || length>=size)return 0;
    memcpy(name,value,length+1);return 1;
}

int ts_sample_pages_recovery_info(const char *path,TsProjectRecovery *info,char *error,size_t size)
{
    char directory[TS_PROJECT_PATH_MAX],original[TS_PROJECT_PATH_MAX],stem[TS_PROJECT_PATH_MAX],name[TS_PROJECT_PATH_MAX];
    if(!path || !info)return project_file_error(error,size,"Invalid recovery path","");
    memset(info,0,sizeof(*info));
    if(!path_directory(path,directory,sizeof(directory)) || !project_stem(path,stem,sizeof(stem)))
        return project_file_error(error,size,"Project recovery path is too long",path);
    if(strlen(path_name(path))>=sizeof(name))return project_file_error(error,size,"Project recovery name is too long",path);
    snprintf(name,sizeof(name),"%s",path_name(path));
    snprintf(original,sizeof(original),"%s",directory);
    int transaction=0;
    /* Guard descendants too: Save As inside backup/subfolder must not edit
       the protected source. A real project's matching manifest/name can
       distinguish a legitimate suffix-like project name. */
    for(size_t end=0;;++end) {
        if(!original[end] || original[end]=='/' || original[end]=='\\') {
            if(end) {
                char prefix[TS_PROJECT_PATH_MAX],stripped[TS_PROJECT_PATH_MAX],member[TS_PROJECT_PATH_MAX],member_stem[TS_PROJECT_PATH_MAX];
                memcpy(prefix,original,end);prefix[end]=0;snprintf(stripped,sizeof(stripped),"%s",prefix);
                int type=project_recovery_suffix(stripped);
                if(type) {
                    int has_name=project_recovery_manifest_name(prefix,member,sizeof(member));
                    int legitimate=(!original[end] && path_component_equal(path_name(prefix),stem)) ||
                        (has_name && project_stem(member,member_stem,sizeof(member_stem)) && path_component_equal(path_name(prefix),member_stem));
                    if(!legitimate) {
                        transaction=type;snprintf(directory,sizeof(directory),"%s",stripped);
                        snprintf(original,sizeof(original),"%s",prefix);
                        if(has_name)snprintf(name,sizeof(name),"%s",member);
                        break;
                    }
                }
            }
            if(!original[end])break;
        }
    }
    if(!transaction && !path_component_equal(path_name(directory),stem) && !bundle_is_owned(original,name)) {
        pages_error(error,size,"");return 1;
    }
    if(!project_join(info->project,sizeof(info->project),directory,name))
        return project_file_error(error,size,"Project recovery path is too long",path);
    const char *suffix[]={"", ".tapesister-backup", ".tapesister-partial"};
    int kind[TS_PROJECT_RECOVERY_COUNT];
    for(int i=0;i<TS_PROJECT_RECOVERY_COUNT;++i) {
        int n=snprintf(info->directory[i],sizeof(info->directory[i]),"%s%s",directory,suffix[i]);
        if(n<0 || (size_t)n>=sizeof(info->directory[i]))return project_file_error(error,size,"Project recovery path is too long",directory);
        kind[i]=project_entry_kind(info->directory[i]);
        if(kind[i]!=PROJECT_ENTRY_MISSING)info->present|=1u<<i;
        if(kind[i]==PROJECT_ENTRY_DIRECTORY)info->folders|=1u<<i;
        if(kind[i]==PROJECT_ENTRY_FILE)info->files|=1u<<i;
    }
    info->from_transaction=transaction!=0;
    char marker[TS_PROJECT_PATH_MAX];
    info->current_marker=kind[0]==PROJECT_ENTRY_DIRECTORY &&
        (!project_join(marker,sizeof(marker),info->directory[0],".tapesister-partial") || project_entry_kind(marker)!=PROJECT_ENTRY_MISSING);
    info->attention=transaction || kind[TS_PROJECT_RECOVERY_BACKUP]!=PROJECT_ENTRY_MISSING ||
        (kind[TS_PROJECT_RECOVERY_STAGED]!=PROJECT_ENTRY_MISSING &&
         (kind[TS_PROJECT_RECOVERY_STAGED]!=PROJECT_ENTRY_DIRECTORY || !stage_is_owned(info->directory[TS_PROJECT_RECOVERY_STAGED]))) || info->current_marker;
    pages_error(error,size,"");return 1;
}

static FILE *project_recovery_read(const char *path)
{
    /* Do not follow a file replaced by a link between enumeration and open. */
#ifdef _WIN32
    HANDLE h=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(h==INVALID_HANDLE_VALUE)return NULL;
    BY_HANDLE_FILE_INFORMATION attributes;
    if(!GetFileInformationByHandle(h,&attributes) ||
       (attributes.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))) {CloseHandle(h);return NULL;}
    int fd=_open_osfhandle((intptr_t)h,_O_RDONLY|_O_BINARY);
    if(fd<0){CloseHandle(h);return NULL;}
    FILE *file=_fdopen(fd,"rb");if(!file)_close(fd);return file;
#else
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)return NULL;
    struct stat attributes;
    if(fstat(fd,&attributes) || !S_ISREG(attributes.st_mode)){close(fd);return NULL;}
    FILE *file=fdopen(fd,"rb");if(!file)close(fd);return file;
#endif
}

typedef struct { const char *source,*destination;unsigned depth; } ProjectRecoveryCopy;
static int project_recovery_copy_file(const char *from,const char *to,char *error,size_t size)
{
    FILE *input=project_recovery_read(from);
    if(!input)return project_file_error(error,size,"Cannot read recovery file (close other writers)",from);
    FILE *output=fopen(to,"wbx");
    if(!output){fclose(input);return project_file_error(error,size,"Cannot create exclusive recovery file",to);}
    unsigned char buffer[16384];size_t n;int ok=1;
    while((n=fread(buffer,1,sizeof(buffer),input))!=0)
        if(fwrite(buffer,1,n,output)!=n){ok=0;break;}
    if(ferror(input) || ferror(output))ok=0;
    if(fclose(input))ok=0;
    if(fclose(output))ok=0;
    return ok?1:project_file_error(error,size,"Recovery copy is incomplete; originals kept",from);
}

static int project_recovery_copy_entry(const char *relative,void *context,char *error,size_t size)
{
    ProjectRecoveryCopy *copy=context;char from[TS_PROJECT_PATH_MAX],to[TS_PROJECT_PATH_MAX];
    if(!project_relative_valid(relative) || !project_join(from,sizeof(from),copy->source,relative) ||
       !project_join(to,sizeof(to),copy->destination,relative))return project_file_error(error,size,"Cannot safely address recovery entry",relative);
    int kind=project_entry_kind(from);
    if(kind==PROJECT_ENTRY_DIRECTORY) {
        if(copy->depth>=64)return project_file_error(error,size,"Recovery folder nesting exceeds 64 levels",relative);
        if(TS_PAGES_MKDIR(to))return project_file_error(error,size,"Cannot create exclusive recovery folder",to);
        ++copy->depth;
        int ok=project_children(copy->source,relative,project_recovery_copy_entry,copy,error,size);
        --copy->depth;return ok;
    }
    if(kind!=PROJECT_ENTRY_FILE)return project_file_error(error,size,"Cannot copy recovery link, special or unreadable entry",from);
    return project_recovery_copy_file(from,to,error,size);
}

int ts_sample_pages_copy_recovery(const char *path,char *destination,size_t destination_size,char *error,size_t size)
{
    TsProjectRecovery info;char root[TS_PROJECT_PATH_MAX],readme[TS_PROJECT_PATH_MAX],pending[TS_PROJECT_PATH_MAX];
    const char *labels[]={"current","backup","staged"};
    if(!destination || !destination_size)return project_file_error(error,size,"Missing recovery destination","");
    destination[0]=0;
    if(!ts_sample_pages_recovery_info(path,&info,error,size))return 0;
    if(!info.attention || !info.present)return project_file_error(error,size,"No recovery entries to copy",path);
    int created=0;
    for(unsigned i=1;i<=9999;++i) {
        int n=snprintf(root,sizeof(root),"%s.tapesister-recovery-%03u",info.directory[0],i);
        if(n<0 || (size_t)n>=sizeof(root) || (size_t)n>=destination_size ||
           !project_join(readme,sizeof(readme),root,"RECOVERY.txt") ||
           !project_join(pending,sizeof(pending),root,"RECOVERY_INCOMPLETE"))return project_file_error(error,size,"Recovery destination path is too long",info.directory[0]);
#ifdef _WIN32
        int made=TS_PAGES_MKDIR(root);
#else
        /* A private source folder must not become world-readable merely
           because the sibling parent has broader permissions. */
        int made=mkdir(root,0700);
#endif
        if(!made){created=1;break;}
        if(errno!=EEXIST)return project_file_error(error,size,"Cannot create recovery workspace",root);
    }
    if(!created)return project_file_error(error,size,"No unused recovery workspace name",info.directory[0]);
    snprintf(destination,destination_size,"%s",root);
    FILE *marker=fopen(pending,"wbx");
    if(!marker)return project_file_error(error,size,"Cannot mark incomplete recovery workspace",root);
    int marked=fputs("Copy not complete. Keep originals and retry to a new workspace.\n",marker)!=EOF;
    if(fclose(marker))marked=0;
    if(!marked)return project_file_error(error,size,"Cannot finish recovery workspace marker",root);
    FILE *notes=fopen(readme,"wbx");
    if(!notes)return project_file_error(error,size,"Cannot create recovery instructions; incomplete workspace kept",root);
    int ok=fputs("TAPESISTER RECOVERY COPIES\n\nA RECOVERY_INCOMPLETE marker means copying did not finish. Keep originals and retry.\n"
        "Originals are unchanged. No version has been chosen or merged.\n"
        "current = original project location; backup = previous save folder; staged = interrupted new save.\n"
        "These names do not prove validity or recency. User-added files may be split between folders.\n"
        "Open a TSR inside a copied folder to inspect it. Check/copy user files from ALL folders before cleanup.\n"
        "Save As elsewhere to keep the current in-memory session. It does not collect extra files from the old folders.\n"
        "Only ordinary file contents and folders are copied; metadata/alternate streams are not archived.\n"
        "Stop other instances and external writers before copying.\n\nSources:\n",notes)!=EOF;
    for(int i=0;ok && i<TS_PROJECT_RECOVERY_COUNT;++i)if(info.present&(1u<<i))
        ok=fprintf(notes,"%s: %s\n",labels[i],info.directory[i])>0;
    for(int i=0;ok && i<TS_PROJECT_RECOVERY_COUNT;++i)if(info.present&(1u<<i)) {
        char to[TS_PROJECT_PATH_MAX];
        if(!project_join(to,sizeof(to),root,labels[i])){ok=0;break;}
        int kind=project_entry_kind(info.directory[i]);
        if(kind==PROJECT_ENTRY_DIRECTORY) {
            if(TS_PAGES_MKDIR(to)){ok=0;project_file_error(error,size,"Cannot create recovery copy folder",to);break;}
            ProjectRecoveryCopy copy={info.directory[i],to,0};
            ok=project_children(copy.source,"",project_recovery_copy_entry,&copy,error,size);
        } else {
            /* A regular file at a reserved path is user material too. */
            ok=kind==PROJECT_ENTRY_FILE?project_recovery_copy_file(info.directory[i],to,error,size):
                project_file_error(error,size,"Cannot copy recovery obstruction",info.directory[i]);
        }
    }
    if(fprintf(notes,"\nCOPY %s\n",ok?"COMPLETE":"INCOMPLETE - originals and partial workspace kept")<0)ok=0;
    if(fclose(notes))ok=0;
    if(!ok){if(error && size && !error[0])project_file_error(error,size,"Recovery copy incomplete; originals kept",root);return 0;}
    if(remove(pending))return project_file_error(error,size,"Recovery copy completed but its incomplete marker could not be cleared",root);
    pages_error(error,size,"");return 1;
}
