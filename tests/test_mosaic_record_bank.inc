static void test_mosaic_record_bank(void)
{
    TsInstrument *bank=malloc(sizeof(*bank)),*record=malloc(sizeof(*record));
    TsUiState *view=malloc(sizeof(*view));AudioState *a=routing_audio(0);
    MosaicController *c=calloc(1,sizeof(*c));assert(bank && record && view && c);
    ts_instrument_init(bank);ts_instrument_init(record);ts_ui_init(view);
    char error[160];TsSamplePages pages;assert(ts_sample_pages_init(&pages,error,sizeof(error)));
    c->pages=&pages;view->mosaic=pages.mosaic=a->mosaic;view->mosaic_open=1;
    assert(ts_instrument_import_sample(bank,&instrument.current,0,0,0,TS_LOOP_FORWARD,error,sizeof(error)));
    uint64_t original=ts_sample_hash(&bank->current);
    for(int i=1;i<TS_BANK_SLOT_COUNT;++i)assert(ts_instrument_copy_selected(bank,i,error,sizeof(error)));
    TsMosaicSource *old=ts_mosaic_source(a->mosaic,&bank->current,error,sizeof(error));assert(old);
    TsMosaicEvent *owner=ts_mosaic_add(a->mosaic,old,0,0);assert(owner);
    assert(mosaic_enter(0,a,view,bank,c,owner->id));uint64_t selected=view->mosaic_selected;
    float data[960];for(int i=0;i<960;++i)data[i]=.3f*sinf(i*.03f);
    ExternalInputState *input=mosaic_record_input_fixture(a);
    input->mosaic_epoch=a->mosaic->epoch;input->mosaic_start=3;input->mosaic_x=220;
    atomic_store(&input->record_source,TS_RECORD_SOURCE_OUTPUT);
    int page=-1,slot=-1;
    assert(install_mosaic_take(0,view,bank,&pages,input,data,480,48000,2,&page,&slot,error,sizeof(error)));
    assert(page==1 && slot==0 && pages.page_count==2 && view->sample_page_count==2);
    assert(view->mosaic_editing && view->mosaic_selected==selected);
    assert(ts_sample_hash(&bank->current)==original && ts_sample_hash(&c->bank->current)==original);
    const TsInstrument *added=ts_sample_pages_page(&pages,bank,1);assert(added);
    assert(added->bank[0].occupied && !memcmp(added->current.data,data,sizeof(data)));
    assert(added->current.channels==2 && added->current.frames==480);
    TsMosaicEvent *take=ts_mosaic_find(a->mosaic,a->mosaic->next_id-1);assert(take && take!=owner);
    uint64_t take_id=take->id,hash=take->source->hash;
    assert(!take->looping && take->start==3 && !memcmp(take->source->sample.data,data,sizeof(data)));
    assert(mosaic_leave(0,a,view,bank,c));
    assert(ts_sample_pages_switch(&pages,bank,1,error,sizeof(error)));
    assert(ts_sample_hash(&bank->current)==hash && bank->selected_slot==0);
    assert(ts_sample_save_wav32f(&bank->current,"test-mosaic-bank.wav",error,sizeof(error)));
    TsSample exported={0};assert(ts_sample_load_wav(&exported,"test-mosaic-bank.wav",error,sizeof(error)));
    assert(exported.channels==2 && exported.frames==480 && !memcmp(exported.data,data,sizeof(data)));
    ts_sample_free(&exported);assert(!remove("test-mosaic-bank.wav"));
    assert(ts_sample_pages_save_project(&pages,bank,record,NULL,"test-mosaic-record-bank/test-mosaic-record-bank.tsr",error,sizeof(error)));
    assert(ts_sample_pages_load_project(&pages,bank,record,"test-mosaic-record-bank/test-mosaic-record-bank.tsr",error,sizeof(error)));
    take=ts_mosaic_find(pages.mosaic,take_id);assert(take && take->source->hash==hash);
    assert(pages.page_count==2 && ts_sample_hash(&bank->current)==hash);
    TsAmplitudeGesture gesture;ts_amplitude_gesture_init(&gesture);
    assert(ts_instrument_amplitude_gesture_begin(bank,&gesture,error,sizeof(error)));
    assert(ts_instrument_amplitude_gesture_preview(bank,&gesture,0,.25f,479,.25f,error,sizeof(error)));
    assert(ts_instrument_amplitude_gesture_commit(bank,&gesture,error,sizeof(error)));
    assert(ts_sample_hash(&bank->current)!=hash && take->source->hash==hash);
    assert(!memcmp(take->source->sample.data,data,sizeof(data))); /* Main editor owns an independent copy. */
    /* Arrangement undo does not remove the separately published Sample tile. */
    ts_mosaic_checkpoint(pages.mosaic);ts_mosaic_delete(pages.mosaic,take_id);
    assert(ts_mosaic_undo(pages.mosaic,0) && ts_mosaic_find(pages.mosaic,take_id));
    assert(bank->bank[0].occupied);
    mosaic_controller_free(c);free(c);free(input);ts_sample_pages_free(&pages);
    ts_instrument_free(bank);free(bank);ts_instrument_free(record);free(record);free(view);routing_free(a);
}
