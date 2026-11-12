/* Uses the production painter, compositor, damage planner and SDL uploader.
   Dummy/software rendering measures CPU work, not desktop GPU/VSync stalls. */
static void benchmark_ui_frames(SDL_Window *window)
{
    unsigned width=(unsigned)strtoul(getenv("TS_BENCH_UI"),NULL,10),height=width*9/16;
    REQUIRE(width>=640 && width<=3840);
    SDL_SetWindowSize(window,(int)width,(int)height);
    SDL_Renderer *renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);REQUIRE(renderer);
    SDL_RenderSetLogicalSize(renderer,TS_UI_WIDTH,TS_UI_HEIGHT);
    SDL_Texture *texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,TS_UI_WIDTH,TS_UI_HEIGHT);
    REQUIRE(texture);SDL_Texture *details_texture[2]={NULL,NULL};TsUiWaveformDetail details[2]={{0}};
    static TsFramebuffer frame;static uint32_t snapshot[TS_UI_WIDTH*TS_UI_HEIGHT];int valid=0;
    ui.mosaic=audio.mosaic;ui.mosaic_open=1;ui.mosaic_scale=getenv("TS_BENCH_UI_DETAIL")?24:.45;
    ui.mosaic_hscale=1;ui.mosaic_scroll=getenv("TS_BENCH_UI_DETAIL")?450:0;
    ui.mosaic_source_count=0;ui.mosaic_source_selected=-1;
    for(int i=0;i<TS_MOSAIC_EVENTS && ui.mosaic_source_count<16;++i) {
        TsMosaicEvent *e=&audio.mosaic->events[i];if(!e->id || !e->source)continue;
        int n=ui.mosaic_source_count++;
        ui.mosaic_sources[n].source=e->source;ui.mosaic_sources[n].sample=&e->source->sample;
        ui.mosaic_sources[n].hash=e->source->hash;ui.mosaic_sources[n].event=e->id;
        ui.mosaic_sources[n].name=e->name;ui.mosaic_sources[n].bank_page=ui.mosaic_sources[n].bank_slot=-1;
    }
    double paint=0,copy=0;uint64_t pixels=0,hash=UINT64_C(14695981039346656037),rasters=0;
    const unsigned frames=180,warmup=10;
    for(unsigned i=0;i<frames+warmup;++i) {
        ui.mosaic_time=i/30.;
        if(getenv("TS_BENCH_UI_PAN")){ui.mosaic_scroll+=.017;ui.mosaic_xscroll+=.1;}
        if(getenv("TS_BENCH_UI_COLD"))details[0].raster_valid=0;
        double start=thread_seconds();
        native_waveform_prepare(renderer,details,1);ts_ui_render(&frame,&ui,&bank);
        double drawn=thread_seconds();
        REQUIRE(update_texture_damage(texture,&frame,snapshot,&valid));
        SDL_RenderCopy(renderer,texture,NULL,NULL);native_waveform_copy(renderer,details_texture,details,&frame);
        SDL_RenderPresent(renderer);double finished=thread_seconds();
        if(i==warmup-1)rasters=details[0].raster_count;
        if(i<warmup)continue;
        paint+=drawn-start;copy+=finished-drawn;
        hash=hash_bytes(hash,frame.pixels,sizeof(frame.pixels));
        for(int d=0;d<2;++d)if(details[d].valid) {
            hash=hash_bytes(hash,details[d].pixels,(size_t)details[d].width*details[d].height*sizeof(uint32_t));
            pixels+=(uint64_t)details[d].dirty_w*details[d].dirty_h;
        }
    }
    printf("{\"ui_width\":%u,\"ui_height\":%u,\"frames\":%u,\"paint_ms\":%.6f,\"upload_copy_ms\":%.6f,\"total_ms\":%.6f,\"native_pixels_per_frame\":%.3f,\"card_rasters\":%" PRIu64 ",\"pixel_hash\":\"%016" PRIx64 "\"}\n",
        width,height,frames,1000*paint/frames,1000*copy/frames,1000*(paint+copy)/frames,
        pixels/(double)frames,details[0].raster_count-rasters,hash);
    for(int i=0;i<2;++i){SDL_DestroyTexture(details_texture[i]);ts_ui_waveform_detail_free(&details[i]);}
    SDL_DestroyTexture(texture);SDL_DestroyRenderer(renderer);
}
