#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/mosaic_gallery.h"
#include "tapesister/sample_pages.h"
#include "tapesister/pattern_card.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define GALLERY_PID _getpid
#define GALLERY_CHDIR _chdir
#define MKDIR(p) _mkdir(p)
#else
#include <unistd.h>
#define GALLERY_PID getpid
#define GALLERY_CHDIR chdir
#define MKDIR(p) mkdir(p,0700)
#endif
static TsMosaic *scene(float value,int live,int routed)
{
    TsMosaic *m=ts_mosaic_create();assert(m);m->gain=1;
    float data[1000];for(int i=0;i<1000;++i)data[i]=value;
    TsSample s={.data=data,.frames=1000,.channels=1,.sample_rate=1000};char error[160];
    TsMosaicSource *source=ts_mosaic_source(m,&s,error,sizeof(error));assert(source);
    TsMosaicEvent *e=ts_mosaic_add(m,source,0,0);assert(e);e->duration=10;e->looping=1;e->live=live;
    if(routed){e->output_route.matrix_enabled=1;e->output_route.matrix_gain[7]=1000;
        e->output_route.mix_enabled=1;e->output_route.clean_level=100;e->output_route.send_level[0]=50;}
    m->playing=!live;return m;
}
static void render(TsMosaicGallery *g,int frames)
{
    ts_gallery_begin_block(g,(unsigned)frames);
    for(int i=0;i<frames;++i){TsSourceRouteMix mix={0};TsStereoFrame f=ts_gallery_read(g,1000,&mix);assert(isfinite(f.l));}
}
static void render_one(TsMosaic *m,int frames)
{ts_mosaic_begin_block(m,(unsigned)frames);for(int i=0;i<frames;++i){TsSourceRouteMix mix={0};ts_mosaic_read_routed(m,1000,&mix);}}
static void free_gallery(TsMosaicGallery *g)
{TsMosaic *root=g->root;ts_gallery_free(g);ts_mosaic_free(root);}
static void test_fade(void)
{
    char error[160];TsMosaic *a=scene(.25f,0,1),*b=scene(.5f,0,1);
    TsMosaic *ra=scene(.25f,0,1),*rb=scene(.5f,0,1);
    TsMosaicGallery *g=ts_gallery_create(a);assert(g);
    assert(ts_gallery_append_prepared(g,b,error,sizeof(error)));g->fade_seconds=.1;
    render(g,50);render_one(ra,50);assert(ts_gallery_switch(g,1,error,sizeof(error)));
    assert(!ts_gallery_switch(g,0,error,sizeof(error)));assert(!ts_gallery_remove(g,0,error,sizeof(error)));
    ts_gallery_begin_block(g,100);ts_mosaic_begin_block(ra,100);ts_mosaic_begin_block(rb,100);
    for(int i=0;i<100;++i) {
        TsSourceRouteMix mix={0},am={0},bm={0};float t=i/100.f;
        TsStereoFrame f=ts_gallery_read(g,1000,&mix),af=ts_mosaic_read_routed(ra,1000,&am),bf=ts_mosaic_read_routed(rb,1000,&bm);
        assert(fabsf(f.l-(af.l*(1-t)+bf.l*t))<1e-6f);
        assert(fabsf(mix.graph[7].l-(am.graph[7].l*(1-t)+bm.graph[7].l*t))<1e-6f);
        assert(fabsf(mix.reference.l-(am.reference.l*(1-t)+bm.reference.l*t))<1e-6f);
        assert(fabsf(mix.send[0].l-(am.send[0].l*(1-t)+bm.send[0].l*t))<1e-6f);
    }
    assert(g->outgoing<0 && !a->playing && fabs(a->time-.15)<1e-10);
    double stopped=a->time;
    uint64_t hash=ts_gallery_hash(g);render(g,57);assert(a->time==stopped && ts_gallery_hash(g)==hash);
    /* With no fade, this is exactly the established one-canvas read path. */
    render_one(rb,57);TsSourceRouteMix x={0},y={0};
    TsStereoFrame f=ts_gallery_read(g,1000,&x),r=ts_mosaic_read_routed(rb,1000,&y);
    assert(!memcmp(&f,&r,sizeof(f)) && !memcmp(&x,&y,sizeof(x)));
    g->canvas[0].entry=TS_GALLERY_RESUME;g->fade_seconds=0;
    assert(ts_gallery_switch(g,0,error,sizeof(error)));assert(a->time==stopped && a->playing);
    render(g,10);assert(a->time>stopped);
    b->markers[3]=(TsMosaicMarker){.stored=1,.time=2.5};g->canvas[1].entry=TS_GALLERY_MARKER;g->canvas[1].marker=3;
    assert(ts_gallery_switch(g,1,error,sizeof(error)));assert(b->time==2.5 && b->marker_active==4);
    g->canvas[0].entry=TS_GALLERY_MARKER;g->canvas[0].marker=9;
    assert(!ts_gallery_switch(g,0,error,sizeof(error)) && g->active==1 && b->time==2.5);
    free_gallery(g);ts_mosaic_free(ra);ts_mosaic_free(rb);
}
static void test_loop_resume(void)
{
    char error[160];TsMosaic *a=scene(.25f,1,0),*b=scene(.5f,0,0);
    TsMosaicGallery *g=ts_gallery_create(a);assert(g && ts_gallery_append_prepared(g,b,error,sizeof(error)));
    assert(ts_mosaic_live_launch(a,a->events[0].id,0));render(g,50);
    g->fade_seconds=.02;assert(ts_gallery_switch(g,1,error,sizeof(error)));render(g,20);
    double elapsed=a->live[0].elapsed;assert(fabs(elapsed-.07)<1e-10 && a->live_paused && !a->playing);
    render(g,200);assert(a->live[0].elapsed==elapsed);
    g->canvas[0].entry=TS_GALLERY_RESUME;assert(ts_gallery_switch(g,0,error,sizeof(error)));
    assert(!a->live_paused && a->live[0].elapsed==elapsed && !a->playing);
    render(g,20);assert(a->live[0].elapsed>elapsed);
    assert(ts_gallery_switch(g,1,error,sizeof(error)));render(g,20);
    g->canvas[0].entry=TS_GALLERY_START;b->playing=1;
    assert(ts_gallery_switch(g,0,error,sizeof(error)));assert(a->playing && a->time==0 && !a->live[0].state);
    render(g,20);assert(a->live[0].elapsed>.019 && a->live[0].elapsed<.021);
    free_gallery(g);
}
static void test_pages(void)
{
    char error[160];TsMosaic *m=scene(.25f,0,0);m->playing=0;
    TsMosaicGallery *g=ts_gallery_create(m);assert(g);
    for(int i=1;i<TS_GALLERY_LIMIT;++i)assert(ts_gallery_add(g,error,sizeof(error)));
    assert(!ts_gallery_add(g,error,sizeof(error)) && g->count==TS_GALLERY_LIMIT);
    assert(ts_gallery_rename(g,3,"Blue room",error,sizeof(error)));
    assert(!ts_gallery_rename(g,3,"Bad\nname",error,sizeof(error)));
    uint64_t old=g->canvas[2].id;assert(ts_gallery_remove(g,2,error,sizeof(error)));
    assert(!strcmp(g->canvas[2].name,"Blue room") && g->canvas[2].id!=old);
    assert(ts_gallery_add(g,error,sizeof(error)) && g->canvas[g->count-1].id>TS_GALLERY_LIMIT);
    assert(ts_gallery_switch(g,1,error,sizeof(error)) && g->outgoing<0); /* Silent browsing is immediate. */
    assert(ts_gallery_remove(g,0,error,sizeof(error)) && g->root==m && g->active==0);
    free_gallery(g);
}
static void test_storage(void)
{
    char error[256];MKDIR("gallery-storage");MKDIR("gallery-legacy");
    TsMosaic *a=scene(.25f,0,0),*b=scene(.5f,1,1);a->playing=0;
    TsMosaicGallery *g=ts_gallery_create(a);assert(g && ts_gallery_append_prepared(g,b,error,sizeof(error)));
    assert(ts_gallery_rename(g,0,"First painting",error,sizeof(error)));
    assert(ts_gallery_rename(g,1,"Second \"painting\"",error,sizeof(error)));
    b->live_envelope=1;b->markers[2]=(TsMosaicMarker){.stored=1,.time=2};g->canvas[1].entry=TS_GALLERY_MARKER;g->canvas[1].marker=2;
    TsMosaicSource *video=ts_mosaic_video_source(b,"/external/dog footage",1);assert(video);
    TsMosaicEvent *cue=ts_mosaic_add(b,video,1,240);assert(cue);cue->video_random_start=1;cue->video_transition=2;cue->video_fade=1.25;
    g->active=1;g->fade_seconds=1.25;uint64_t hash=ts_gallery_hash(g);
    assert(ts_gallery_save(g,"gallery-storage",error,sizeof(error)));
    /* Text metadata remains portable after Windows CRLF conversion. */
    FILE *manifest=fopen("gallery-storage/gallery.tsg","rb");assert(manifest);
    char metadata[4096];size_t bytes=fread(metadata,1,sizeof(metadata),manifest);assert(!ferror(manifest));fclose(manifest);
    manifest=fopen("gallery-storage/gallery.tsg","wb");assert(manifest);
    for(size_t i=0;i<bytes;++i){if(metadata[i]=='\n')fputc('\r',manifest);fputc(metadata[i],manifest);}assert(!fclose(manifest));
    TsMosaicGallery *loaded=ts_gallery_create(ts_mosaic_create());assert(loaded);
    assert(ts_gallery_load(loaded,"gallery-storage",error,sizeof(error)) && ts_gallery_hash(loaded)==hash);
    assert(loaded->active==1 && !ts_gallery_active(loaded)->playing && !ts_mosaic_live_count(ts_gallery_active(loaded)) && loaded->outgoing<0);
    assert(loaded->canvas[1].scene->live_envelope && !strcmp(loaded->canvas[1].name,g->canvas[1].name));
    assert(ts_mosaic_save(a,"gallery-legacy",error,sizeof(error)));
    TsMosaicGallery *legacy=ts_gallery_create(ts_mosaic_create());assert(legacy);
    assert(ts_gallery_load(legacy,"gallery-legacy",error,sizeof(error)) && legacy->count==1 && legacy->active==0);
    assert(ts_mosaic_hash(legacy->root)==ts_mosaic_hash(a));
    /* Full project publication while the old active canvas is not the root. */
    TsSamplePages pages;TsInstrument bank,record;ts_instrument_init(&bank);ts_instrument_init(&record);
    assert(ts_sample_pages_init(&pages,error,sizeof(error)));
    assert(ts_instrument_activate_silence(&bank,128,1000,error,sizeof(error)));
    pages.mosaic=a;pages.gallery=g;
    if(!ts_sample_pages_save_project(&pages,&bank,&record,NULL,"Gallery Bundle/Gallery Bundle.tsr",error,sizeof(error))){fprintf(stderr,"Gallery save: %s\n",error);abort();}
    assert(!ts_sample_pages_prepare_project("Gallery Bundle/Gallery Bundle.tsr",0,error,sizeof(error)) && strstr(error,"Gallery"));
    assert(ts_sample_pages_load_project(&pages,&bank,&record,"Gallery Bundle/Gallery Bundle.tsr",error,sizeof(error)));
    assert(pages.gallery && ts_gallery_hash(pages.gallery)==hash && pages.mosaic==ts_gallery_active(pages.gallery));
    assert(pages.mosaic!=a && ts_sample_pages_mosaic_root(&pages)==a);
    assert(ts_sample_pages_export_canvas(&pages.gallery->canvas[1],"Video Canvas/Video Canvas.tsr",error,sizeof(error)));
    TsMosaicCanvas imported={0};assert(ts_sample_pages_load_canvas("Video Canvas/Video Canvas.tsr",0,&imported,error,sizeof(error)));
    assert(ts_mosaic_hash(imported.scene)==ts_mosaic_hash(pages.gallery->canvas[1].scene));
    const TsMosaicEvent *imported_video=ts_mosaic_video_at(imported.scene,2);assert(imported_video && imported_video->video_random_start && !strcmp(imported_video->source->video_path,"/external/dog footage"));ts_mosaic_free(imported.scene);
    uint64_t before=ts_gallery_hash(pages.gallery);
    FILE *f=fopen("Gallery Bundle/project-data/gallery.tsg","wb");assert(f);fputs("TAPESISTER_GALLERY 1\nCOUNT 9999 0 1 1\n",f);fclose(f);
    assert(!ts_sample_pages_load_project(&pages,&bank,&record,"Gallery Bundle/Gallery Bundle.tsr",error,sizeof(error)));
    assert(ts_gallery_hash(pages.gallery)==before); /* Failed staging leaves all live canvases intact. */
    ts_sample_pages_free(&pages);ts_mosaic_free(a);ts_instrument_free(&bank);ts_instrument_free(&record);
    free_gallery(loaded);free_gallery(legacy);
}
static void test_canvas_transfer(void)
{
    char error[256];TsSamplePages pages;TsInstrument bank,record;ts_instrument_init(&bank);ts_instrument_init(&record);
    assert(ts_sample_pages_init(&pages,error,sizeof(error)));pages.mosaic=scene(.25f,0,0);
    TsMosaic *root=pages.mosaic;
    assert(ts_instrument_import_sample(&bank,&root->events[0].source->sample,0,0,0,TS_LOOP_FORWARD,error,sizeof(error)));
    pages.tracker.source_id=ts_tile_id_new();TsPatternId pattern;
    assert(ts_sister_tracker_add_pattern(&pages.tracker,16,&pattern,error,sizeof(error)));
    uint8_t alias;TsTileId tile=bank.bank[0].tile_id;
    assert(ts_sister_tracker_bind_tile(&pages.tracker,tile,&alias,error,sizeof(error)));
    pages.tracker.patterns[0]->cells[0][0]=(TsTrackerCell){.tile_id=tile,.note_kind=TS_TRACKER_NOTE_PITCH,.note=60};
    TsMosaicSource *pattern_source=ts_pattern_source(root,&pages,&bank,pattern,error,sizeof(error));assert(pattern_source);
    assert(ts_mosaic_add(root,pattern_source,1,80));root->volume[30]=.4f;
    root->markers[2]=(TsMosaicMarker){.stored=1,.time=2};root->live_envelope=0;
    /* Pre-Gallery project, then the same legacy project reopened and saved
       through the new build's full project path. Both must remain importable. */
    assert(ts_sample_pages_save_project(&pages,&bank,&record,NULL,"Legacy/Legacy.tsr",error,sizeof(error)));
    FILE *old=fopen("Legacy/project-data/mosaic.tsm","rb");assert(old);char old_header[64];assert(fgets(old_header,sizeof(old_header),old) && !strcmp(old_header,"TAPESISTER_MOSAIC 14\n"));fclose(old);
    uint64_t hash=ts_mosaic_hash(root);TsMosaicGalleryCatalog catalog;
    assert(ts_sample_pages_canvas_catalog("Legacy/Legacy.tsr",&catalog,error,sizeof(error)) && catalog.count==1 && !catalog.present);
    assert(!strcmp(catalog.canvas[0].name,"Legacy"));TsMosaicCanvas legacy={0};
    assert(ts_sample_pages_load_canvas("Legacy/Legacy.tsr",0,&legacy,error,sizeof(error)) && ts_mosaic_hash(legacy.scene)==hash);
    assert(ts_sample_pages_load_project(&pages,&bank,&record,"Legacy/Legacy.tsr",error,sizeof(error)));
    assert(pages.gallery && ts_gallery_rename(pages.gallery,0,"A Pale Weight",error,sizeof(error)));
    assert(ts_sample_pages_save_project(&pages,&bank,&record,NULL,"Migrated/Migrated.tsr",error,sizeof(error)));
    TsMosaicCanvas migrated={0};assert(ts_sample_pages_load_canvas("Migrated/Migrated.tsr",0,&migrated,error,sizeof(error)));
    assert(!strcmp(migrated.name,"A Pale Weight") && ts_mosaic_hash(migrated.scene)==hash);
    assert(ts_sample_pages_export_canvas(&migrated,"Portable/Portable.tsr",error,sizeof(error)));
    assert(!ts_sample_pages_export_canvas(&migrated,"Migrated/Migrated.tsr",error,sizeof(error))); /* Never replace a whole project. */
    assert(ts_sample_pages_canvas_catalog("Portable/Portable.tsr",&catalog,error,sizeof(error)) && catalog.count==1);
    TsSampleProjectLoad *standalone=ts_sample_pages_prepare_project("Portable/Portable.tsr",1,error,sizeof(error));assert(standalone);
    const TsSamplePages *exported=ts_sample_pages_prepared_pages(standalone);
    assert(ts_mosaic_hash(exported->mosaic)==hash && !ts_instrument_bank_count(exported->pages[0]));ts_sample_pages_dispose_project(standalone);
    /* Remove original locations: the exported bundle owns all used audio and
       pattern assets. A moved copy can be loaded without either old project. */
    assert(!rename("Legacy","Legacy-moved") && !rename("Migrated","Migrated-moved"));
    assert(!rename("Portable","Portable-moved"));TsMosaicCanvas portable={0};
    assert(ts_sample_pages_load_canvas("Portable-moved/Portable.tsr",0,&portable,error,sizeof(error)) && ts_mosaic_hash(portable.scene)==hash);
    TsPatternCard *pc=portable.scene->events[1].source->pattern;assert(pc && pc->asset_count==1);
    assert(pc->assets[0].audio->hash==root->events[1].source->pattern->assets[0].audio->hash);
    assert(!ts_sample_pages_load_canvas("Portable-moved/Portable.tsr",1,&portable,error,sizeof(error)));
    assert(ts_mosaic_hash(portable.scene)==hash); /* Failure leaves caller ownership alone. */
    /* Catalog can inspect several canvases without loading their banks or the
       other page's PCM. Only choosing a missing/corrupt page must fail. */
    assert(ts_gallery_add(pages.gallery,error,sizeof(error)));
    assert(ts_gallery_rename(pages.gallery,1,"Where Weight Dissolves",error,sizeof(error)));
    assert(ts_sample_pages_save_project(&pages,&bank,&record,NULL,"Combined/Combined.tsr",error,sizeof(error)));
    assert(!remove("Combined/project-data/canvas-002/mosaic.tsm"));
    assert(!ts_sample_pages_prepare_project("Combined/Combined.tsr",1,error,sizeof(error)));
    assert(ts_sample_pages_canvas_catalog("Combined/Combined.tsr",&catalog,error,sizeof(error)) && catalog.count==2);
    TsMosaicCanvas selected={0};assert(ts_sample_pages_load_canvas("Combined/Combined.tsr",0,&selected,error,sizeof(error)) && ts_mosaic_hash(selected.scene)==hash);
    assert(!ts_sample_pages_load_canvas("Combined/Combined.tsr",1,&selected,error,sizeof(error)) && ts_mosaic_hash(selected.scene)==hash);
    ts_mosaic_free(selected.scene);ts_mosaic_free(portable.scene);ts_mosaic_free(migrated.scene);ts_mosaic_free(legacy.scene);
    ts_sample_pages_free(&pages);ts_mosaic_free(root);ts_instrument_free(&bank);ts_instrument_free(&record);
}
int main(void)
{
    char run[128];int created=0;
    for(int i=0;i<100 && !created;++i){snprintf(run,sizeof(run),"run-%lu-%d",(unsigned long)GALLERY_PID(),i);created=MKDIR(run)==0;}
    assert(created && !GALLERY_CHDIR(run));
    test_fade();test_loop_resume();test_pages();test_storage();test_canvas_transfer();
    puts("Mosaic Gallery fades, routes, loop resume, entry points, identity and transactional project storage passed");return 0;
}
