/* Config uses physical channel numbers on every backend. Device catalogs are
   queried only on the control thread; callback ranges change only after SAVE. */
static unsigned ts_audio_range_offered(const TsConfig *config,int capture)
{
    const char *name=capture?config->record_input_device:config->audio_output_device;
    if(config->audio_backend==TS_AUDIO_BACKEND_JACK) {
        if(strstr(name,"Insert"))return 2;
        return capture?8:16;
    }
    SDL_AudioSpec spec;
#if defined(TAPESISTER_HAS_ASIO)
    if(config->audio_backend==TS_AUDIO_BACKEND_ASIO) {
        for(int i=0;i<ts_asio_count(capture);++i)
            if(!*name || !strcmp(name,ts_asio_name(i,capture))) {
                SDL_zero(spec);return ts_asio_spec(i,capture,&spec)==0?spec.channels:0;
            }
        return 0;
    }
#endif
    if(config->audio_backend!=ts_started_backend)return 0;
    if(!*name) {
        SDL_zero(spec);
        if(ts_native_default_info(NULL,&spec,capture)==0 && spec.channels)return spec.channels;
    }
    for(int i=0;i<ts_native_device_count(capture);++i) {
        const char *found=ts_native_device_name(i,capture);
        if(found && (!*name || !strcmp(name,found))) {
            SDL_zero(spec);if(ts_native_device_spec(i,capture,&spec)==0 && spec.channels)return spec.channels;
        }
    }
    /* Some SDL backends omit catalog formats; use the negotiated active layout
       only when it belongs to this exact selection. */
    if(!strcmp(name,capture?ts_active_capture:ts_active_output))
        return capture?(ts_real_capture?ts_capture_endpoint.channels:0):(ts_real_output?ts_output_endpoint.channels:0);
    return 0;
}
static void ts_audio_range_render(TsFramebuffer *fb,const TsConfig *config,int capture,
    uint32_t row,uint32_t edge,uint32_t color)
{
    int y=capture?TS_AUDIO_CONFIG_INPUT_RANGE_Y:TS_AUDIO_CONFIG_OUTPUT_RANGE_Y;
    TsAudioChannelRange range=capture?config->audio_input_range:config->audio_output_range;
    unsigned offered=ts_audio_range_offered(config,capture);
    char label[64];
    cfg_text(fb,24,y+5,capture?"INPUT RANGE":"OUTPUT RANGE",color);
    for(int last=0;last<2;++last) {
        int x=130+last*155;
        cfg_frame(fb,x,y,150,TS_AUDIO_CONFIG_ROW_H,row,edge);
        cfg_text(fb,x+5,y+5,"<",color);cfg_text(fb,x+138,y+5,">",color);
        if(last && !range.last) {
            if(offered)snprintf(label,sizeof(label),"LAST ALL(%u)",offered);
            else snprintf(label,sizeof(label),"LAST ALL");
        } else snprintf(label,sizeof(label),"%s %d",last?"LAST":"FIRST",last?range.last:range.first);
        cfg_text(fb,x+21,y+5,label,color);
    }
    if(offered && !ts_audio_range_fits(range,offered))snprintf(label,sizeof(label),"DEVICE HAS %u",offered);
    else if(offered || range.last)snprintf(label,sizeof(label),"%d-%u ENABLED",range.first,range.last?(unsigned)range.last:offered);
    else snprintf(label,sizeof(label),"SAVE TO APPLY");
    cfg_text(fb,455,y+5,label,color);
}
static int ts_audio_range_click(int x,int y)
{
    for(int capture=0;capture<2;++capture) {
        int top=capture?TS_AUDIO_CONFIG_INPUT_RANGE_Y:TS_AUDIO_CONFIG_OUTPUT_RANGE_Y;
        if(y<top || y>=top+TS_AUDIO_CONFIG_ROW_H)continue;
        TsConfig *config=&ts_live_ui->config;
        TsAudioChannelRange *range=capture?&config->audio_input_range:&config->audio_output_range;
        unsigned offered=ts_audio_range_offered(config,capture);
        int maximum=offered?(int)offered:255;
        for(int last=0;last<2;++last) {
            int left=130+last*155;
            if(x<left || x>=left+150)continue;
            int delta=x<left+28?-1:x>=left+122?1:0;
            if(last && delta) {
                if(!range->last)range->last=delta<0?SDL_max(maximum,range->first):range->first;
                else if(delta>0 && range->last>=maximum)range->last=0;
                else range->last=SDL_clamp(range->last+delta,range->first,255);
            } else if(!last && delta) {
                range->first=SDL_clamp(range->first+delta,1,range->last?range->last:maximum);
            }
        }
        snprintf(ts_live_ui->status,sizeof(ts_live_ui->status),
            "%s RANGE: SAVE CONFIG TO APPLY; CHANNEL NUMBERS AND SAVED ROUTES STAY FIXED",capture?"INPUT":"OUTPUT");
        return 1;
    }
    return 0;
}
