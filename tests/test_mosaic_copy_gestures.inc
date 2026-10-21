static void test_mosaic_copy_gestures(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic;
    const int sizes[]={3,3,32,64,65};
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
            e->duration=1;e->gain=.25f+i*.005f;e->pan=(i%3-1)*.5f;
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
            click(window,154,67,1);motion(window,445,250);release(window,445,250);
        }
        TsMosaicEvent selected[TS_MOSAIC_EVENTS],next[TS_MOSAIC_EVENTS];
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
                char message[80];snprintf(message,sizeof(message),"COPY NEEDS %d FREE / %d AVAILABLE (128 CARD LIMIT)",count,TS_MOSAIC_EVENTS-total);
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
