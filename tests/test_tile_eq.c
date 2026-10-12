#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/master_eq.h"
#include "tapesister/mosaic.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#define RMDIR(p) _rmdir(p)
#else
#include <unistd.h>
#define MKDIR(p) mkdir(p,0700)
#define RMDIR(p) rmdir(p)
#endif
int main(void)
{
    char error[160];TsSample sample={0};sample.frames=8192;sample.sample_rate=48000;sample.channels=2;
    sample.data=calloc(sample.frames*2,sizeof(float));assert(sample.data);
    for(size_t i=0;i<sample.frames;++i){sample.data[i*2]=.3f*sinf(i*.02f);sample.data[i*2+1]=.1f*cosf(i*.09f);}
    TsInstrument *doc=malloc(sizeof(*doc)),*loaded=malloc(sizeof(*loaded));assert(doc && loaded);ts_instrument_init(doc);ts_instrument_init(loaded);
    assert(ts_instrument_import_sample(doc,&sample,1,100,8000,TS_LOOP_FORWARD,error,sizeof(error)));
    uint64_t original=ts_sample_hash(&doc->current);
    TsMasterEqControls eq;ts_master_eq_preset(&eq,TS_EQ_PRESET_TELEPHONE);
    assert(ts_instrument_set_tile_eq(doc,&eq,error,sizeof(error)));
    uint64_t filtered=ts_sample_hash(&doc->current);assert(filtered!=original && doc->tile_eq.enabled);
    int history=doc->undo_count;assert(ts_instrument_set_tile_eq(doc,&eq,error,sizeof(error)));assert(doc->undo_count==history);
    assert(ts_instrument_undo(doc,error,sizeof(error)) && ts_sample_hash(&doc->current)==original);
    assert(ts_instrument_redo(doc,error,sizeof(error)) && ts_sample_hash(&doc->current)==filtered);
    assert(ts_instrument_save_recipe(doc,"test-tile-eq.tsr",error,sizeof(error)));
    if(!ts_instrument_load_recipe(loaded,"test-tile-eq.tsr",error,sizeof(error))){fprintf(stderr,"%s\n",error);abort();}
    assert(!memcmp(&loaded->tile_eq,&eq,sizeof(eq)) && ts_sample_hash(&loaded->current)==filtered);
    eq.enabled=0;assert(ts_instrument_set_tile_eq(loaded,&eq,error,sizeof(error)));assert(ts_sample_hash(&loaded->current)==original);
    eq.enabled=1;eq.band[2].gain_db=6;assert(ts_instrument_set_tile_eq(doc,&eq,error,sizeof(error)));
    TsSample expected={0};TsMasterEqControls flat;ts_master_eq_default(&flat);
    assert(ts_instrument_render_tile_eq(&expected,doc,&flat,error,sizeof(error)));assert(ts_sample_apply_eq(&expected,&eq));
    assert(ts_sample_hash(&doc->current)==ts_sample_hash(&expected));ts_sample_free(&expected);
    /* Saving/reopening a Mosaic card retains the pre-EQ source and one curve. */
    TsMosaic *m=ts_mosaic_create(),*reload=ts_mosaic_create();assert(m && reload);
    TsMosaicSource *source=ts_mosaic_source_document(m,doc,error,sizeof(error));assert(source && source->eq_dry.data);
    assert(ts_mosaic_source_document(m,doc,error,sizeof(error))==source);
    TsMosaicEvent *event=ts_mosaic_add(m,source,0,0);assert(event);event->output_route.matrix_enabled=1;event->output_route.matrix_gain[3]=750;
    assert(ts_mosaic_copy(m,event->id,2,0));MKDIR("test-mosaic-eq");
    assert(ts_mosaic_save(m,"test-mosaic-eq",error,sizeof(error)));
    assert(ts_mosaic_load(reload,"test-mosaic-eq",error,sizeof(error)));
    assert(ts_mosaic_hash(m)==ts_mosaic_hash(reload));source=reload->events[0].source;
    assert(source==reload->events[1].source && reload->events[0].output_route.matrix_gain[3]==750);
    assert(ts_mosaic_import_source(loaded,source,0,0,source->sample.frames,TS_LOOP_FORWARD,error,sizeof(error)));
    assert(!memcmp(&loaded->tile_eq,&eq,sizeof(eq)));
    eq.enabled=0;assert(ts_instrument_set_tile_eq(loaded,&eq,error,sizeof(error)));
    assert(ts_sample_hash(&loaded->current)==original);
    ts_sample_free(&sample);ts_instrument_free(doc);ts_instrument_free(loaded);free(doc);free(loaded);ts_mosaic_free(m);ts_mosaic_free(reload);
    remove("test-tile-eq.tsr");remove("test-mosaic-eq/mosaic.tsm");remove("test-mosaic-eq/mosaic-000.wav");remove("test-mosaic-eq/mosaic-000-dry.wav");RMDIR("test-mosaic-eq");
    puts("Tile EQ re-edit, undo/redo, TSR and Mosaic editable-source persistence passed");return 0;
}
