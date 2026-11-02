static int gallery_line(FILE *f,char *line,size_t n)
{
    if(!fgets(line,(int)n,f))return 0;
    size_t len=strlen(line);
    if(len && line[len-1]=='\n'){line[--len]=0;if(len && line[len-1]=='\r')line[--len]=0;}
    return 1;
}
static int gallery_path(char *out,size_t n,const char *dir,int count,int i)
{
    int wrote=count==1?snprintf(out,n,"%s",dir):snprintf(out,n,"%s/canvas-%03d",dir,i+1);
    return wrote>=0 && (size_t)wrote<n;
}
static int gallery_name_decode(char *out,const char *hex)
{
    size_t n=strlen(hex);if(!n || n%2 || n>=TS_GALLERY_NAME*2)return 0;
    for(size_t i=0;i<n;i+=2) {
        unsigned v=0;for(int k=0;k<2;++k){int c=hex[i+k];if(c>='0' && c<='9')c-='0';else if(c>='a' && c<='f')c=c-'a'+10;else return 0;v=v*16+(unsigned)c;}
        if(v<32 || v>126)return 0;out[i/2]=(char)v;
    }
    out[n/2]=0;return 1;
}
int ts_gallery_save(const TsMosaicGallery *g,const char *dir,char *e,size_t n)
{
    if(!g || !dir || g->count<1 || g->count>TS_GALLERY_LIMIT || g->active<0 || g->active>=g->count ||
       !g->next_id || g->next_id==UINT64_MAX || !isfinite(g->fade_seconds) || g->fade_seconds<0 || g->fade_seconds>30)
        return gallery_error(e,n,"Invalid Gallery settings");
    char path[2048];
    for(int i=0;i<g->count;++i) {
        const TsMosaicCanvas *c=&g->canvas[i];
        if(!c->scene || !c->id || c->id>=g->next_id || c->entry<0 || c->entry>TS_GALLERY_MARKER || c->marker<0 || c->marker>=TS_MOSAIC_MARKERS ||
           !c->name[0] || !memchr(c->name,0,TS_GALLERY_NAME))return gallery_error(e,n,"Invalid canvas definition");
        for(const unsigned char *p=(const unsigned char *)c->name;*p;++p)if(*p<32 || *p>126)return gallery_error(e,n,"Invalid canvas name");
        for(int j=0;j<i;++j)if(g->canvas[j].id==c->id || g->canvas[j].scene==c->scene)return gallery_error(e,n,"Duplicate canvas identity");
        if(!gallery_path(path,sizeof(path),dir,g->count,i))return gallery_error(e,n,"Gallery path is too long");
        if(g->count>1 && GALLERY_MKDIR(path) && errno!=EEXIST)return gallery_error(e,n,"Cannot create canvas folder");
        if(!ts_mosaic_save(c->scene,path,e,n))return 0;
    }
    if(snprintf(path,sizeof(path),"%s/gallery.tsg",dir)<0 || strlen(dir)+13>=sizeof(path))return gallery_error(e,n,"Gallery path is too long");
    FILE *f=fopen(path,"wb");if(!f)return gallery_error(e,n,"Cannot write Gallery manifest");
    int ok=fprintf(f,"TAPESISTER_GALLERY 1\nCOUNT %d %d %llu %.17g\n",g->count,g->active,(unsigned long long)g->next_id,g->fade_seconds)>0;
    for(int i=0;i<g->count && ok;++i) {
        const TsMosaicCanvas *c=&g->canvas[i];
        ok=fprintf(f,"CANVAS %llu %d %d ",(unsigned long long)c->id,c->entry,c->marker)>0;
        for(const unsigned char *p=(const unsigned char *)c->name;*p && ok;++p)ok=fprintf(f,"%02x",*p)>0;
        if(ok)ok=fputc('\n',f)!=EOF;
    }
    if(ok)ok=fputs("END\n",f)>=0;
    if(fclose(f))ok=0;if(!ok)return gallery_error(e,n,"Cannot finish Gallery manifest");
    if(g->count>1) {
        snprintf(path,sizeof(path),"%s/mosaic.tsm",dir);f=fopen(path,"wb");
        if(!f)return gallery_error(e,n,"Cannot write Gallery compatibility guard");
        ok=fputs("TAPESISTER_MOSAIC_GALLERY 1\n",f)>=0;
        if(fclose(f))ok=0;if(!ok)return gallery_error(e,n,"Cannot finish Gallery compatibility guard");
    }
    return 1;
}
int ts_gallery_catalog(const char *dir,TsMosaicGalleryCatalog *out,char *e,size_t n)
{
    char path[2048],line[256],extra;TsMosaicGalleryCatalog catalog={0};
    if(!dir || !out || snprintf(path,sizeof(path),"%s/gallery.tsg",dir)<0 || strlen(dir)+13>=sizeof(path))return gallery_error(e,n,"Gallery path is too long");
    FILE *f=fopen(path,"rb");
    if(!f) {
        if(errno!=ENOENT)return gallery_error(e,n,"Cannot read Gallery manifest");
        catalog.count=1;catalog.next_id=2;catalog.fade_seconds=2;catalog.canvas[0].id=1;
        snprintf(catalog.canvas[0].name,TS_GALLERY_NAME,"Canvas 1");*out=catalog;return 1;
    }
    int count,active;unsigned long long next;double fade;
    if(!gallery_line(f,line,sizeof(line)) || strcmp(line,"TAPESISTER_GALLERY 1") ||
       !gallery_line(f,line,sizeof(line)) || sscanf(line,"COUNT %d %d %llu %lf %c",&count,&active,&next,&fade,&extra)!=4 ||
       count<1 || count>TS_GALLERY_LIMIT || active<0 || active>=count || !next || next==UINT64_MAX || !isfinite(fade) || fade<0 || fade>30)goto invalid;
    for(int i=0;i<count;++i) {
        unsigned long long id;int entry,marker;char name[TS_GALLERY_NAME*2];
        if(!gallery_line(f,line,sizeof(line)) || sscanf(line,"CANVAS %llu %d %d %95s %c",&id,&entry,&marker,name,&extra)!=4 ||
           !id || id>=next || entry<0 || entry>TS_GALLERY_MARKER || marker<0 || marker>=TS_MOSAIC_MARKERS)goto invalid;
        for(int j=0;j<i;++j)if(catalog.canvas[j].id==id)goto invalid;
        TsMosaicCanvasInfo *c=&catalog.canvas[i];c->id=id;c->entry=entry;c->marker=marker;
        if(!gallery_name_decode(c->name,name))goto invalid;
    }
    if(!gallery_line(f,line,sizeof(line)) || strcmp(line,"END") || fgets(line,sizeof(line),f) || ferror(f))goto invalid;
    fclose(f);catalog.count=count;catalog.active=active;catalog.next_id=next;catalog.fade_seconds=fade;catalog.present=1;*out=catalog;return 1;
invalid:
    fclose(f);return gallery_error(e,n,"Malformed or unsupported Gallery manifest");
}
int ts_gallery_load(TsMosaicGallery *g,const char *dir,char *e,size_t n)
{
    if(!g || g->count!=1 || g->canvas[0].scene!=g->root)return gallery_error(e,n,"Gallery load requires fresh staging storage");
    TsMosaicGalleryCatalog catalog;if(!ts_gallery_catalog(dir,&catalog,e,n))return 0;
    if(!catalog.present)return ts_mosaic_load(g->root,dir,e,n);
    for(int i=0;i<catalog.count;++i) {
        if(i && !ts_gallery_add(g,e,n))return 0;
        TsMosaicCanvas *c=&g->canvas[i];TsMosaicCanvasInfo *info=&catalog.canvas[i];
        c->id=info->id;c->entry=info->entry;c->marker=info->marker;memcpy(c->name,info->name,TS_GALLERY_NAME);
        char path[2048];if(!gallery_path(path,sizeof(path),dir,catalog.count,i))return gallery_error(e,n,"Gallery path is too long");
        char file[2048];int wrote=snprintf(file,sizeof(file),"%s/mosaic.tsm",path);
        if(wrote<0 || (size_t)wrote>=sizeof(file))return gallery_error(e,n,"Gallery path is too long");
        FILE *check=fopen(file,"rb");if(!check)return gallery_error(e,n,"Canvas arrangement is missing or unreadable");fclose(check);
        if(!ts_mosaic_load(c->scene,path,e,n))return 0;
    }
    g->active=catalog.active;g->next_id=catalog.next_id;g->fade_seconds=catalog.fade_seconds;return 1;
}
