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
    char root[TS_PROJECT_PATH_MAX];
    if(!path_directory(project,root,sizeof(root))){ts_mosaic_free(scene);return 0;}
    TsAudioAssets *assets=ts_audio_assets_begin(root,0);
    if(!assets){ts_mosaic_free(scene);pages_error(error,n,"Not enough memory for canvas audio");return 0;}
    int loaded=ts_mosaic_load(scene,path,error,n);ts_audio_assets_end(assets);
    if(!loaded){ts_mosaic_free(scene);return 0;}
    TsMosaicCanvas canvas={.scene=scene,.id=catalog.canvas[index].id,.entry=catalog.canvas[index].entry,.marker=catalog.canvas[index].marker,.scale=24,.hscale=1};
    memcpy(canvas.name,catalog.canvas[index].name,TS_GALLERY_NAME);*out=canvas;return 1;
}
int ts_sample_pages_export_canvas_with_state(const TsMosaicCanvas *canvas,const TsSisterProjectState *state,const char *path,char *error,size_t n)
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
            TsSisterProjectState *transfer=NULL;
            if(state) {
                transfer=malloc(sizeof(*transfer));
                if(!transfer){pages_error(error,n,"Not enough memory for exported effect routing");ok=0;}
                else {
                    ts_sister_project_state_init(transfer,48000);transfer->matrix=state->matrix;
                    transfer->matrix_layout=state->matrix_layout;int count=0;
                    for(int i=0;i<transfer->matrix_layout.canvas_count;++i) {
                        TsFxCanvasPosition p=transfer->matrix_layout.canvas[i];
                        if(p.kind==3){if(p.canvas && p.canvas!=canvas->id)continue;p.canvas=copy->id;}
                        transfer->matrix_layout.canvas[count++]=p;
                    }
                    transfer->matrix_layout.canvas_count=count;
                }
            }
            if(ok)ok=ts_sample_pages_save_project(portable,portable->pages[0],NULL,transfer,path,error,n);
            free(transfer);
        }
    }
    ts_sample_pages_free(portable);free(portable);return ok;
}

int ts_sample_pages_export_canvas(const TsMosaicCanvas *canvas,const char *path,char *error,size_t n)
{return ts_sample_pages_export_canvas_with_state(canvas,NULL,path,error,n);}

/* A saved effect slot is a project-local address, not an effect identity.
   Clone only the instance graph reachable from the selected cards, then map
   both signal and detector destinations. Fixed buses remain destination-owned. */
static int canvas_effect_destination(int d)
{
    if(d>=TS_MATRIX_EFFECT_BASE && d<TS_MATRIX_PROCESSOR_END)return d-TS_MATRIX_EFFECT_BASE;
    if(d>=TS_MATRIX_EFFECT_SC_BASE && d<TS_MATRIX_INSTANCE_DESTINATIONS)return d-TS_MATRIX_EFFECT_SC_BASE;
    return -1;
}
static unsigned canvas_route_effects(const TsSourceRoute *r)
{
    unsigned used=0;if(!r->matrix_enabled)return 0;
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)
        if(r->matrix_gain[TS_MATRIX_EFFECT_BASE+i] || r->matrix_gain[TS_MATRIX_EFFECT_SC_BASE+i])used|=1u<<i;
    return used;
}
static unsigned canvas_effect_closure(unsigned used,const TsMatrixControls *matrix)
{
    for(unsigned before=~used;before!=used;) {
        before=used;
        for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(used&(1u<<i))used|=canvas_route_effects(&matrix->row[TS_MATRIX_EFFECT_BASE+i]);
    }
    return used;
}
unsigned ts_sample_pages_external_effect_roots(const TsSamplePages *pages,const TsInstrument *bank)
{
    unsigned roots=0;if(!pages)return 0;
    for(size_t p=0;p<pages->page_count;++p) {
        const TsInstrument *page=ts_sample_pages_page(pages,bank,p);if(!page)continue;
        for(int i=0;i<TS_BANK_SLOT_COUNT;++i)if(page->bank[i].occupied)roots|=canvas_route_effects(&page->bank[i].output_route);
    }
    for(int i=0;i<TS_TRACKER_LANES;++i)roots|=canvas_route_effects(&pages->tracker.lanes[i].output_route);
    return roots;
}
unsigned ts_sample_pages_gallery_suspended_effects(const TsSamplePages *pages,const TsInstrument *bank,
    const TsMatrixControls *matrix,TsGalleryEffectActivity *cache,unsigned external_roots)
{
    return ts_gallery_suspended_effects(pages?pages->gallery:NULL,matrix,cache,
        external_roots|ts_sample_pages_external_effect_roots(pages,bank));
}
unsigned ts_gallery_suspended_effects(const TsMosaicGallery *g,const TsMatrixControls *matrix,
    TsGalleryEffectActivity *cache,unsigned external_roots)
{
    if(!g || !matrix || !matrix->enabled || !cache)return 0;
    unsigned owned=0,awake=external_roots,edges[TS_MATRIX_EFFECT_CAPACITY];
    for(int c=0;c<g->count;++c) {
        const TsMosaic *m=g->canvas[c].scene;if(!m)continue;
        if(cache->canvas[c].scene!=m || cache->canvas[c].epoch!=m->epoch || cache->canvas[c].revision!=m->revision) {
            unsigned roots=0;
            for(int e=0;e<TS_MOSAIC_EVENTS;++e)if(m->events[e].id)roots|=canvas_route_effects(&m->events[e].output_route);
            cache->canvas[c].scene=m;cache->canvas[c].epoch=m->epoch;
            cache->canvas[c].revision=m->revision;cache->canvas[c].roots=roots;
        }
        unsigned roots=cache->canvas[c].roots;owned|=roots;
        if(c==g->active || c==g->outgoing)awake|=roots;
    }
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)edges[i]=canvas_route_effects(&matrix->row[TS_MATRIX_EFFECT_BASE+i]);
    for(unsigned before=~owned;before!=owned;) {
        before=owned;for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(owned&(1u<<i))owned|=edges[i];
    }
    /* Configured external owners protect their tails even while silent.
       Unowned processors and fixed buses are project-wide, never parked. */
    for(int row=0;row<TS_MATRIX_ROWS;++row) {
        int slot=ts_matrix_effect_index(row);
        if(slot<0 || !(owned&(1u<<slot)))awake|=canvas_route_effects(&matrix->row[row]);
    }
    for(unsigned before=~awake;before!=awake;) {
        before=awake;for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(awake&(1u<<i))awake|=edges[i];
    }
    return owned&~awake;
}
unsigned ts_sample_pages_canvas_exclusive_effects(const TsSamplePages *pages,const TsInstrument *bank,const TsMosaic *target,const TsMatrixControls *matrix)
{
    if(!pages || !target || !matrix)return 0;
    unsigned candidate=0,shared=0;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(target->events[i].id)candidate|=canvas_route_effects(&target->events[i].output_route);
    candidate=canvas_effect_closure(candidate,matrix);
    int count=pages->gallery?pages->gallery->count:1;
    for(int c=0;c<count;++c) {
        const TsMosaic *scene=pages->gallery?pages->gallery->canvas[c].scene:pages->mosaic;
        if(!scene || scene==target)continue;
        for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(scene->events[i].id)shared|=canvas_route_effects(&scene->events[i].output_route);
    }
    for(size_t p=0;p<pages->page_count;++p) {
        const TsInstrument *page=ts_sample_pages_page(pages,bank,p);if(!page)continue;
        for(int i=0;i<TS_BANK_SLOT_COUNT;++i)if(page->bank[i].occupied)shared|=canvas_route_effects(&page->bank[i].output_route);
    }
    for(int i=0;i<TS_TRACKER_LANES;++i)shared|=canvas_route_effects(&pages->tracker.lanes[i].output_route);
    for(int row=0;row<TS_MATRIX_ROWS;++row) {
        int slot=ts_matrix_effect_index(row);
        if(slot<0 || !(candidate&(1u<<slot)))shared|=canvas_route_effects(&matrix->row[row]);
    }
    return candidate&~canvas_effect_closure(shared,matrix);
}
static TsSourceRoute canvas_route_remap(TsSourceRoute r,const int *map)
{
    TsSourceRoute out=r;
    for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)if(canvas_effect_destination(d)>=0)out.matrix_gain[d]=0;
    for(int d=0;d<TS_MATRIX_DESTINATIONS;++d){int slot=canvas_effect_destination(d);
        if(slot>=0 && r.matrix_gain[d] && map[slot]>=0)out.matrix_gain[(d<TS_MATRIX_PROCESSOR_END?TS_MATRIX_EFFECT_BASE:TS_MATRIX_EFFECT_SC_BASE)+map[slot]]=r.matrix_gain[d];}
    return out;
}
int ts_sample_pages_load_canvas_routed(const char *project,int index,const TsMatrixControls *destination,
    const TsMatrixLayout *layout,uint64_t identity,uint64_t replaced,TsMosaicCanvas *out,TsCanvasRoutingImport *routing,char *error,size_t n)
{
    if(!destination || !layout || !out || !routing || !identity){pages_error(error,n,"Invalid canvas import destination");return 0;}
    TsMosaicCanvas canvas={0};TsSisterProjectState *source=calloc(1,sizeof(*source));
    TsCanvasRoutingImport *next=calloc(1,sizeof(*next));int ok=0,present=0;
    if(!source || !next){pages_error(error,n,"Not enough memory for canvas effects");goto done;}
    if(!ts_sample_pages_load_canvas(project,index,&canvas,error,n))goto done;
    if(!ts_sister_project_state_load(source,project,48000,&present,error,n))goto done;
    unsigned used=0;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(canvas.scene->events[i].id)used|=canvas_route_effects(&canvas.scene->events[i].output_route);
    for(unsigned before=~used;before!=used;) {
        before=used;
        for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(used&(1u<<i)) {
            if(!present || !source->matrix.effect[i].id){pages_error(error,n,"Source canvas refers to a missing effect; import cancelled");goto done;}
            used|=canvas_route_effects(&source->matrix.row[TS_MATRIX_EFFECT_BASE+i]);
        }
    }
    if(used && !destination->enabled){pages_error(error,n,"Enable destination Matrix routing before importing linked effects");goto done;}
    next->matrix=*destination;next->layout=*layout;int map[TS_MATRIX_EFFECT_CAPACITY];
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)map[i]=-1;
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(used&(1u<<i)) {
        int slot=ts_matrix_effect_add(&next->matrix,source->matrix.effect[i].type);
        if(slot<0){pages_error(error,n,"Not enough free effect slots for this canvas; nothing imported");goto done;}
        map[i]=slot;++next->effects;uint32_t id=next->matrix.effect[slot].id;
        next->matrix.effect[slot]=source->matrix.effect[i];next->matrix.effect[slot].id=id;
        /* Stale routes to unused destination slots must not start receiving or
           feeding a newly imported processor. Reject instead of rewriting them. */
        for(int row=0;row<TS_MATRIX_ROWS;++row)if(destination->row[row].matrix_gain[TS_MATRIX_EFFECT_BASE+slot] || destination->row[row].matrix_gain[TS_MATRIX_EFFECT_SC_BASE+slot]) {
            pages_error(error,n,"Destination has cables to empty effect slots; clear those cables first");goto done;
        }
    }
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(map[i]>=0) {
        int from=TS_MATRIX_EFFECT_BASE+i,to=TS_MATRIX_EFFECT_BASE+map[i];
        next->matrix.row[to]=canvas_route_remap(source->matrix.row[from],map);next->matrix.delayed[to]=0;
        for(int d=0;d<TS_MATRIX_INSTANCE_DESTINATIONS;++d)if(source->matrix.delayed[from]&ts_matrix_feedback_bit(d)) {
            int slot=canvas_effect_destination(d),mapped=slot<0?d:(d<TS_MATRIX_PROCESSOR_END?TS_MATRIX_EFFECT_BASE:TS_MATRIX_EFFECT_SC_BASE)+map[slot];
            next->matrix.delayed[to]|=ts_matrix_feedback_bit(mapped);
        }
    }
    if(!ts_matrix_valid(&next->matrix)){pages_error(error,n,"Imported routing conflicts with destination feedback or graph; nothing imported");goto done;}
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(canvas.scene->events[i].id && canvas.scene->events[i].output_route.matrix_enabled)
        canvas.scene->events[i].output_route=canvas_route_remap(canvas.scene->events[i].output_route,map);
    /* Loaded scenes have no undo history; their first edit must capture the
       remapped routes, never the addresses from the original project. */
    canvas.scene->history_count=canvas.scene->history_cursor=canvas.scene->history_first=0;
    ++canvas.scene->revision;
    int count=0;float bottom=0;
    for(int i=0;i<next->layout.canvas_count;++i) {
        TsFxCanvasPosition p=next->layout.canvas[i];if(replaced && p.kind==3 && p.canvas==replaced)continue;
        next->layout.canvas[count++]=p;if(p.y>bottom)bottom=p.y;
    }
    next->layout.canvas_count=count;
    float top=0;int have_top=0;
    for(int i=0;i<source->matrix_layout.canvas_count;++i) {
        TsFxCanvasPosition p=source->matrix_layout.canvas[i];int keep=p.kind==3 && (p.canvas==canvas.id || !p.canvas);
        if(p.kind==4)for(int j=0;j<TS_MATRIX_EFFECT_CAPACITY;++j)if(map[j]>=0 && p.id==source->matrix.effect[j].id)keep=1;
        if(keep && (!have_top || p.y<top)){top=p.y;have_top=1;}
    }
    for(int i=0;i<source->matrix_layout.canvas_count;++i) {
        TsFxCanvasPosition p=source->matrix_layout.canvas[i];int keep=0;
        if(p.kind==3 && (p.canvas==canvas.id || !p.canvas) && ts_mosaic_find(canvas.scene,p.id)){p.canvas=identity;keep=1;}
        if(p.kind==4)for(int j=0;j<TS_MATRIX_EFFECT_CAPACITY;++j)if(map[j]>=0 && p.id==source->matrix.effect[j].id){p.id=next->matrix.effect[map[j]].id;keep=1;break;}
        if(!keep)continue;
        if(next->layout.canvas_count>=TS_FX_CANVAS_POSITIONS){pages_error(error,n,"FX canvas layout is full; nothing imported");goto done;}
        p.y+=bottom+320-top;next->layout.canvas[next->layout.canvas_count++]=p;
    }
    if(!ts_matrix_layout_valid(&next->layout)){pages_error(error,n,"Imported FX layout exceeds the canvas limits");goto done;}
    canvas.id=identity;*out=canvas;*routing=*next;ok=1;
done:
    if(!ok)ts_mosaic_free(canvas.scene);free(source);free(next);return ok;
}
