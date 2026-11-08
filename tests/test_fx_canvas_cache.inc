static void cache_poll(void)
{
    matrix_window.polled=SDL_GetTicks()-50;
    matrix_update(0,&audio,&ui,&pages,&bank);
}
static void canvas_cache(void)
{
    MatrixWindow *w=&matrix_window;
    for(int i=0;i<4;++i)cache_poll(); /* Settle initial placement and inspector. */
    uint64_t frames=mx_present_count,builds=fx_graph_cache.builds,sorts=mx_order_cache.sorts;
    for(int i=0;i<8;++i) {
        audio.sister.processed_frames+=512;
        cache_poll();
        fx_build(&pages,&bank);
    }
    assert(mx_present_count==frames && fx_graph_cache.builds==builds && mx_order_cache.sorts==sorts);
    pointer(SDL_MOUSEMOTION,500,350,0,0);cache_poll();
    assert(mx_present_count==frames && fx_graph_cache.builds==builds);
    /* A meter change paints, but doesn't rebuild the graph. */
    audio.sister.matrix.output_peak[TS_MATRIX_MAIN]=.2f;cache_poll();
    assert(mx_present_count>frames && fx_graph_cache.builds==builds);
    audio.sister.matrix.output_peak[TS_MATRIX_MAIN]=0;cache_poll();
    /* Changes from other views / project restoration need no dirty flag. */
    TsSourceRoute route=audio.sister.matrix.controls.row[TS_MATRIX_MOSAIC];
    audio.sister.matrix.controls.row[TS_MATRIX_MOSAIC].matrix_gain[TS_MATRIX_MAIN]=331;
    w->dirty=0;cache_poll();assert(fx_graph_cache.builds>builds);
    audio.sister.matrix.controls.row[TS_MATRIX_MOSAIC]=route;cache_poll();
    builds=fx_graph_cache.builds;
    TsFxCanvasPosition *position=fx_position((MatrixRow){.kind=2,.index=TS_MATRIX_MOSAIC},0);assert(position);
    float x=position->x;position->x+=32;w->dirty=0;cache_poll();
    assert(fx_graph_cache.builds>builds && node(TS_MATRIX_MOSAIC)->x==x+32);
    position->x=x;cache_poll();
    assert(pages.tracker.channel_count>0);
    char name[sizeof(pages.tracker.lanes[0].name)];memcpy(name,pages.tracker.lanes[0].name,sizeof(name));
    snprintf(pages.tracker.lanes[0].name,sizeof(name),"CACHE RENAME");w->dirty=0;cache_poll();
    int found=0;for(int i=0;i<w->count;++i)if(w->rows[i].kind==0 && w->rows[i].index==0)
        found=strstr(w->rows[i].name,"CACHE RENAME")!=NULL;
    assert(found);memcpy(pages.tracker.lanes[0].name,name,sizeof(name));cache_poll();
    /* Visibility must block all presentation, including pending edits. */
    frames=mx_present_count;SDL_HideWindow(w->window);w->dirty=1;cache_poll();
    assert(mx_present_count==frames);SDL_ShowWindow(w->window);cache_poll();assert(mx_present_count>frames);
    puts("Idle graph/paint reuse, meter updates, external edits, rename and visibility passed.");
}
