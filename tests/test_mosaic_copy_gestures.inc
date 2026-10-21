static void test_mosaic_copy_gestures(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic;
    const int sizes[]={3,3,TS_MOSAIC_EVENTS/4,TS_MOSAIC_EVENTS/2,TS_MOSAIC_EVENTS/2+1};
    for(int scenario=0;scenario<5;++scenario) {
        int count=sizes[scenario];
        TsMosaic *scene=ts_mosaic_create();assert(scene);ui.mosaic=audio.mosaic=scene;
        ui.mosaic_open=1;ui.mosaic_follow=0;ui.mosaic_scroll=ui.mosaic_xscroll=0;
        ui.mosaic_scale=4;ui.mosaic_hscale=1;mosaic_poll(0,&ui,&instrument,&mosaic);
        char error[160];TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
        TsSample variation={0};assert(ts_sample_clone(&variation,&instrument.current,error,sizeof(error)));
        variation.data[0]+=.125f;
        TsMosaicSource *other=ts_mosaic_source(scene,&variation,error,sizeof(error));assert(other);ts_sample_free(&variation);
        for(int i=0;i<count;++i) {
            TsMosaicEvent *e=ts_mosaic_add(scene,i%2?other:source,1+2*(i/3),20+94*(i%3));assert(e);
            e->duration=1;e->gain=.25f+(i%100)*.005f;e->pan=(i%3-1)*.5f;
        }
        scene->auto_space=scenario==1;
        uint64_t bank_hash=ts_sample_hash(&instrument.current);int bank_slot=instrument.selected_slot;
        /* First select a source, then switch to a canvas group. The sidebar
           may follow the clicked source, but must never start a source drag. */
        mosaic_select_click(window,20,85,KMOD_NONE);assert(mosaic.picked_count==1);
        mosaic_select_click(window,275,72,KMOD_NONE);assert(ui.mosaic_selected==scene->events[1].id);
        assert(!mosaic.picked_count && !source->pins);
        SDL_SetModState(KMOD_SHIFT);
        if(count==3) {
            /* SDL can report double/triple clicks even while Shift is held. */
            click(window,180,72,2);assert(!mosaic.active && mosaic.drag_copy);
            release(window,180,72);click(window,370,72,3);release(window,370,72);
        } else {
            key(window,SDLK_a,KMOD_CTRL);
        }
        static TsMosaicEvent selected[TS_MOSAIC_EVENTS],next[TS_MOSAIC_EVENTS];
        assert(mosaic_selected_events(&ui,selected)==count && selected[0].id==scene->events[1].id);
        int copies=count==3?6:TS_MOSAIC_EVENTS/count-1;
        for(int attempt=0;attempt<copies+(count>3);++attempt) {
            assert(mosaic_selected_events(&ui,selected)==count);
            /* Alternate the primary and another member as the drag handle. */
            TsMosaicEvent *handle=&selected[attempt%2];
            ui.mosaic_xscroll=handle->x-20;ui.mosaic_scroll=handle->start-1;
            uint64_t before=ts_mosaic_hash(scene);int history=scene->history_count,total=mosaic_event_count(scene);
            click(window,180,72,2+attempt%3);
            assert(mosaic.drag==1 && mosaic.drag_copy && !mosaic.active && !mosaic.source_drag.source);
            mosaic_arrange_test_frame();motion(window,181,73); /* Below the copy threshold. */
            assert(mosaic_event_count(scene)==total && scene->history_count==history);
            motion(window,185,80);mosaic_arrange_test_frame();motion(window,190,84);release(window,190,84);
            mosaic_arrange_test_frame();
            assert(!mosaic.drag && !mosaic.active && !mosaic.picked_count && !ui.mosaic_source_pick_count);
            assert(!source->pins && !other->pins && mosaic_selected_events(&ui,next)==count);
            if(attempt==copies) {
                assert(ts_mosaic_hash(scene)==before && scene->history_count==history);
                assert(ui.mosaic_selected==selected[0].id && !memcmp(selected,next,count*sizeof(*next)));
                char message[80];snprintf(message,sizeof(message),"COPY NEEDS %d FREE / %d AVAILABLE (%d CARD LIMIT)",count,TS_MOSAIC_EVENTS-total,TS_MOSAIC_EVENTS);
                assert(!strcmp(ui.status,message));
                continue;
            }
            assert(mosaic_event_count(scene)==total+count && scene->history_count==history+1);
            double dx=next[0].x-selected[0].x;
            for(int i=0;i<count;++i) {
                assert(next[i].id!=selected[i].id && next[i].source==selected[i].source);
                assert(next[i].start==selected[i].start+3 && next[i].x==selected[i].x+dx);
                assert(next[i].duration==selected[i].duration && next[i].gain==selected[i].gain && next[i].pan==selected[i].pan);
                assert(!memcmp(ts_mosaic_find(scene,selected[i].id),&selected[i],sizeof(*selected)));
            }
        }
        SDL_SetModState(KMOD_NONE);
        if(copies) {
            uint64_t after=ts_mosaic_hash(scene);int total=mosaic_event_count(scene);
            key(window,SDLK_z,KMOD_CTRL);assert(mosaic_event_count(scene)==total-count);
            key(window,SDLK_y,KMOD_CTRL);assert(ts_mosaic_hash(scene)==after);
        }
        /* Ordinary double-click still opens the editor. */
        ui.mosaic_scroll=ui.mosaic_xscroll=0;click(window,180,72,2);
        assert(mosaic.active==scene->events[0].id);assert(mosaic_leave(0,&audio,&ui,&instrument,&mosaic));
        assert(ts_sample_hash(&instrument.current)==bank_hash && instrument.selected_slot==bank_slot);
        mosaic_drag_end(&ui,&mosaic);ui.mosaic=audio.mosaic=saved;ts_mosaic_free(scene);
        mosaic_poll(0,&ui,&instrument,&mosaic);
    }
    ui.mosaic_scroll=ui.mosaic_xscroll=0;ui.mosaic_scale=24;ui.mosaic_hscale=1;
}

static void test_mosaic_large_browser(void)
{
    TsMosaic *saved=ui.mosaic,*scene=ts_mosaic_create();assert(scene);
    ui.mosaic=audio.mosaic=scene;ui.mosaic_open=1;ui.mosaic_follow=0;
    ui.mosaic_scroll=ui.mosaic_xscroll=0;ui.mosaic_scale=24;ui.mosaic_hscale=1;
    float data[32];TsSample sample={.data=data,.frames=32,.sample_rate=48000,.channels=1};
    char error[160];TsMosaicSource *source=NULL;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        if(!(i%2)) {
            for(int n=0;n<32;++n)data[n]=.1f+(i+n)*.0001f;
            source=ts_mosaic_source(scene,&sample,error,sizeof(error));assert(source);
        }
        TsMosaicEvent *e=ts_mosaic_add(scene,source,i,0);assert(e);e->duration=1;
    }
    mosaic_arrange_test_frame();
    assert(ui.mosaic_source_count==TS_BANK_SLOT_COUNT+TS_MOSAIC_EVENTS/2);
    for(int i=0;i<TS_MOSAIC_EVENTS/2;++i) {
        assert(ui.mosaic_sources[TS_BANK_SLOT_COUNT+i].event==scene->events[i*2].id);
        assert(ui.mosaic_sources[TS_BANK_SLOT_COUNT+i].source==scene->events[i*2+1].source);
    }
    mosaic_select_only(&ui,scene->events[TS_MOSAIC_EVENTS-1].id);
    mosaic_source_select_event(&ui,&scene->events[TS_MOSAIC_EVENTS-1]);mosaic_arrange_test_frame();
    assert(ui.mosaic_source_selected==ui.mosaic_source_count-1);
    assert(ui.mosaic_source_page==ui.mosaic_source_pages-1);
    /* Exercise the actual application callback, including its buffer hook. */
    float out[1024];scene->playing=1;
    audio_callback(&audio,(Uint8 *)out,sizeof(out));
    assert(!scene->block_remaining && scene->schedule.active_count==1 && scene->schedule.pending_count==TS_MOSAIC_EVENTS-1);
    assert(scene->time>0 && scene->time<.02);
    ts_mosaic_delete(scene,scene->events[0].id);ts_mosaic_delete(scene,scene->events[1].id);ts_mosaic_collect(scene);
    int sources=0;for(TsMosaicSource *s=scene->sources;s;s=s->next)++sources;
    assert(sources==TS_MOSAIC_EVENTS/2-1);mosaic_arrange_test_frame();
    assert(ui.mosaic_source_count==TS_BANK_SLOT_COUNT+sources);
    ui.mosaic=audio.mosaic=saved;ts_mosaic_free(scene);mosaic_poll(0,&ui,&instrument,&mosaic);
}
