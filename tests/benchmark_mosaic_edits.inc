/* Explicit timings, never a timing-sensitive CI gate. Uses the real controller
   operations including their checkpoint/copy work, with source setup excluded. */
static void benchmark_mosaic_edits(void)
{
    TsMosaic *saved=ui.mosaic;
    const int counts[]={128,512,1024};
    for(int scene=0;scene<3;++scene)for(int operation=0;operation<3;++operation) {
        int count=counts[scene];if(operation==0 && count>TS_MOSAIC_EVENTS/2)continue;
        double best=1e9;
        for(int trial=0;trial<3;++trial) {
            TsMosaic *m=ts_mosaic_create();assert(m);ui.mosaic=m;
            char error[160];TsMosaicSource *source=ts_mosaic_source(m,&instrument.current,error,sizeof(error));assert(source);
            mosaic_select_only(&ui,0);
            for(int i=0;i<count;++i) {
                TsMosaicEvent *e=ts_mosaic_add(m,source,operation==2?i*.01:1,(count-1-i)*94);assert(e);
                e->duration=20;mosaic_select_add(&ui,e->id);
            }
            m->auto_space=1;
            ui.mosaic_ghost_count=mosaic_selected_events(&ui,ui.mosaic_ghosts);
            mosaic.drag_copy=1;mosaic.drag_moved=0;
            Uint64 start=SDL_GetPerformanceCounter();
            if(operation==0)assert(mosaic_begin_drag_change(&ui,&mosaic));
            else if(operation==1) {m->events[count-1].x=1;mosaic_tidy(&ui,1);}
            else mosaic_align(&ui,SDLK_UP);
            double ms=(SDL_GetPerformanceCounter()-start)*1000.0/SDL_GetPerformanceFrequency();
            if(ms<best)best=ms;
            assert(m->auto_space && m->history_count==1);
            ui.mosaic_ghost_count=0;ts_mosaic_free(m);
        }
        printf("%4d cards / %s: %.3f ms\n",count,operation==0?"copy setup":operation==1?"tidy":"align",best);
    }
    ui.mosaic=saved;mosaic.drag_copy=mosaic.drag_moved=0;mosaic_select_only(&ui,0);
}
