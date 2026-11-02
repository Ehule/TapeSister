/* A canvas travels as an ordinary single-canvas TSR bundle. Audio and pattern
   assets come from its immutable scene, not the destination's bank identities. */
static int canvas_project_directory(const char *project,char *dir,size_t size,char *error,size_t n)
{
    if(!project || !*project){pages_error(error,n,"Select a TSR project");return 0;}
    FILE *f=fopen(project,"rb");char magic[3];
    if(!f){pages_error(error,n,"Cannot open canvas TSR");return 0;}
    int native=fread(magic,1,3,f)==3 && !memcmp(magic,"TSR",3);fclose(f);
    if(!native){pages_error(error,n,"Select a native TSR project");return 0;}
    int layout=0,record=0,found=0,mosaic=0,tracker=0;size_t count=1,active=0;
    if(!read_manifest(project,dir,size,&layout,&count,&active,&record,&found,&mosaic,&tracker,error,n))return 0;
    if(!found || layout!=2){pages_error(error,n,"This project has no portable Mosaic arrangement");return 0;}
    size_t length=strlen(dir);
    if(length+14>=size){pages_error(error,n,"Canvas path is too long");return 0;}
    strcpy(dir+length,"/project-data");return 1;
}
int ts_sample_pages_canvas_catalog(const char *project,TsMosaicGalleryCatalog *out,char *error,size_t n)
{
    char dir[TS_PROJECT_PATH_MAX],path[TS_PROJECT_PATH_MAX];TsMosaicGalleryCatalog catalog;
    if(!out || !canvas_project_directory(project,dir,sizeof(dir),error,n))return 0;
    if(!ts_gallery_catalog(dir,&catalog,error,n))return 0;
    if(snprintf(path,sizeof(path),"%s/mosaic.tsm",dir)<0 || strlen(dir)+12>=sizeof(path)){pages_error(error,n,"Canvas path is too long");return 0;}
    FILE *f=fopen(path,"rb");if(!f){pages_error(error,n,"Project has no Mosaic arrangement");return 0;}fclose(f);
    if(!catalog.present) {
        const char *name=path_name(project);size_t length=strlen(name);if(length>=4)length-=4;
        size_t used=0;while(used<length && used<TS_GALLERY_NAME-1){unsigned char c=(unsigned char)name[used];catalog.canvas[0].name[used]=(c>=32 && c<=126)?(char)c:'_';++used;}
        catalog.canvas[0].name[used]=0;if(!used)strcpy(catalog.canvas[0].name,"Imported canvas");
    }
    *out=catalog;return 1;
}
int ts_sample_pages_load_canvas(const char *project,int index,TsMosaicCanvas *out,char *error,size_t n)
{
    TsMosaicGalleryCatalog catalog;char dir[TS_PROJECT_PATH_MAX],path[TS_PROJECT_PATH_MAX];
    if(!out || !ts_sample_pages_canvas_catalog(project,&catalog,error,n))return 0;
    if(index<0 || index>=catalog.count){pages_error(error,n,"Canvas is not available");return 0;}
    if(!canvas_project_directory(project,dir,sizeof(dir),error,n))return 0;
    int wrote=catalog.count==1?snprintf(path,sizeof(path),"%s",dir):snprintf(path,sizeof(path),"%s/canvas-%03d",dir,index+1);
    if(wrote<0 || (size_t)wrote>=sizeof(path)){pages_error(error,n,"Canvas path is too long");return 0;}
    size_t length=strlen(path);char arrangement[TS_PROJECT_PATH_MAX];
    if(length+12>=sizeof(arrangement)){pages_error(error,n,"Canvas path is too long");return 0;}
    strcpy(arrangement,path);strcat(arrangement,"/mosaic.tsm");FILE *f=fopen(arrangement,"rb");
    if(!f){pages_error(error,n,"Selected canvas arrangement is missing");return 0;}fclose(f);
    TsMosaic *scene=ts_mosaic_create();if(!scene){pages_error(error,n,"Not enough memory for imported canvas");return 0;}
    if(!ts_mosaic_load(scene,path,error,n)){ts_mosaic_free(scene);return 0;}
    TsMosaicCanvas canvas={.scene=scene,.entry=catalog.canvas[index].entry,.marker=catalog.canvas[index].marker,.scale=24,.hscale=1};
    memcpy(canvas.name,catalog.canvas[index].name,TS_GALLERY_NAME);*out=canvas;return 1;
}
int ts_sample_pages_export_canvas(const TsMosaicCanvas *canvas,const char *path,char *error,size_t n)
{
    if(!canvas || !canvas->scene || !path){pages_error(error,n,"Select a canvas to export");return 0;}
    char directory[TS_PROJECT_PATH_MAX];
    if(!path_directory(path,directory,sizeof(directory)) || project_entry_kind(directory)!=PROJECT_ENTRY_MISSING){
        pages_error(error,n,"Choose a new canvas bundle name; existing folders are kept");return 0;
    }
    TsSamplePages *portable=calloc(1,sizeof(*portable));
    if(!portable){pages_error(error,n,"Not enough memory to export canvas");return 0;}
    int ok=ts_sample_pages_init(portable,error,n);
    if(ok) {
        portable->mosaic=canvas->scene;
        ok=ts_sample_pages_enable_gallery(portable,error,n);
        if(ok) {
            TsMosaicCanvas *copy=&portable->gallery->canvas[0];
            memcpy(copy->name,canvas->name,TS_GALLERY_NAME);copy->entry=canvas->entry;copy->marker=canvas->marker;
            /* The bank and tracker are intentionally empty. The scene owns all
               audio, EQ originals and immutable pattern sample assets it uses. */
            ok=ts_sample_pages_save_project(portable,portable->pages[0],NULL,NULL,path,error,n);
        }
    }
    ts_sample_pages_free(portable);free(portable);return ok;
}
