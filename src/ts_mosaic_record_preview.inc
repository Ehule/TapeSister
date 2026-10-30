/* One display-only recording card in either the native or logical canvas.
   It has no event ID, source, selection or playback voice. */
static void mosaic_record_rect(TsFramebuffer *fb,TsUiWaveformDetail *d,const TsUiState *ui,
                                double x,double y,double right,double bottom,uint32_t color)
{
    int left=(int)fmax(d?0:151,fmin(d?d->width:ts_ui_mosaic_canvas_right(ui),floor(x+.5)));
    int top=(int)fmax(d?0:66,fmin(d?d->height:TS_MOSAIC_CANVAS_BOTTOM,floor(y+.5)));
    int end=(int)fmax(d?0:151,fmin(d?d->width:ts_ui_mosaic_canvas_right(ui),floor(right+.5)));
    int last=(int)fmax(d?0:66,fmin(d?d->height:TS_MOSAIC_CANVAS_BOTTOM,floor(bottom+.5)));
    if(end<=left || last<=top)return;
    if(d)mosaic_detail_rect(d,left,top,end,last,color);
    else rect(fb,left,top,end-left,last-top,color);
}
static void mosaic_record_text(TsFramebuffer *fb,TsUiWaveformDetail *d,const TsUiState *ui,
                                double x,double y,const char *label,uint32_t color,double sx,double sy)
{
    for(int n=0;label[n];++n) {
        const char *bits=glyph(label[n]);
        for(int gy=0;gy<7;++gy)for(int gx=0;gx<5;++gx)if(bits[gy*5+gx]=='1')
            mosaic_record_rect(fb,d,ui,x+(n*6+gx)*sx,y+gy*sy,
                x+(n*6+gx+1)*sx,y+(gy+1)*sy,color);
    }
}
static void mosaic_recording_preview(TsFramebuffer *fb,TsUiWaveformDetail *d,const TsUiState *ui,
    const TsMosaicRecordPreview *p,unsigned sample_rate,size_t frames,int recording,const char *state)
{
    if(!ui->mosaic || p->epoch!=ui->mosaic->epoch || !sample_rate)return;
    double sx=d?(double)d->width/(ts_ui_mosaic_canvas_right(ui)-151):1,sy=d?(double)d->height/TS_MOSAIC_CANVAS_HEIGHT:1;
    double scale=ui->mosaic_scale>0?ui->mosaic_scale:24,horizontal=ui->mosaic_hscale>0?ui->mosaic_hscale:1;
    double rate=scale*sy,seconds=(double)frames/sample_rate;
    TsMosaicEvent placement={.start=p->start,.duration=seconds,.x=p->x,.width=86};
    ts_mosaic_space(ui->mosaic,&placement);
    double x=(d?0:151)+mosaic_detail_pixel(placement.x*horizontal*sx)-mosaic_detail_pixel(ui->mosaic_xscroll*horizontal*sx);
    double y=(d?0:66)+mosaic_detail_pixel(p->start*rate)-mosaic_detail_pixel(ui->mosaic_scroll*rate);
    double right=x+fmax(1,mosaic_detail_pixel(86*horizontal*sx));
    double bottom=y+fmax(40*sy,mosaic_detail_pixel(seconds*rate));
    double top_limit=d?0:66,bottom_limit=d?d->height:TS_MOSAIC_CANVAS_BOTTOM;
    if(right<=(d?0:151) || x>=(d?d->width:ts_ui_mosaic_canvas_right(ui)) || bottom<=top_limit || y>=bottom_limit)return;
    uint32_t border=recording?(ui->text_cursor_visible?ts_theme_color(active_palette(), TS_THEME_MOSAIC, TS_COLOR_RECORD, RGB(244,112,117)):ts_theme_color(active_palette(), TS_THEME_MOSAIC, TS_COLOR_RECORD, RGB(183,78,91))):ts_theme_color(active_palette(), TS_THEME_MOSAIC, TS_COLOR_DATA1, RGB(225,169,100));
    uint32_t fill=ts_theme_color(active_palette(), TS_THEME_MOSAIC, TS_COLOR_PANEL, RGB(43,26,35)),wave=ts_theme_color(active_palette(), TS_THEME_MOSAIC, TS_COLOR_RECORD, RGB(238,139,149));
    mosaic_record_rect(fb,d,ui,x,y,right,bottom,fill);
    int first=(int)fmax(top_limit,y+16*sy),last=(int)fmin(bottom_limit,bottom-12*sy);
    double center=(x+right)*.5,half=fmax(0,(right-x-12*sx)*.43);
    double wave_rate=seconds>0?(bottom-y-28*sy)/seconds:rate;
    if(p->frames_per_peak && p->frames && right-x>12*sx)for(int row=first;row<last;++row) {
        double elapsed=fmax(0,(row-y-16*sy)/wave_rate);
        size_t a=(size_t)fmin(p->frames,elapsed*sample_rate);
        size_t b=(size_t)fmin(p->frames,(elapsed+1/wave_rate)*sample_rate);
        if(a>=p->frames)continue;
        if(b<=a)b=a+1;
        size_t begin=a/p->frames_per_peak,end=(b-1)/p->frames_per_peak;
        if(end>=TS_MOSAIC_RECORD_PEAKS)end=TS_MOSAIC_RECORD_PEAKS-1;
        float peak=0;for(size_t bin=begin;bin<=end;++bin)peak=fmaxf(peak,p->peaks[bin]);
        double amplitude=fminf(1,peak)*half;
        mosaic_record_rect(fb,d,ui,center-amplitude-.5,row,center+amplitude+.5,row+1,wave);
    }
    mosaic_record_rect(fb,d,ui,x,y,x+fmin(2*sx,right-x),bottom,border);
    mosaic_record_rect(fb,d,ui,right-fmin(sx,right-x),y,right,bottom,border);
    mosaic_record_rect(fb,d,ui,x,y,right,y+sy,border);
    mosaic_record_rect(fb,d,ui,x,bottom-2*sy,right,bottom,border);
    int chars=(int)((right-x)/sx-12)/6;if(chars>24)chars=24;
    if(chars>0) {
        char label[40];snprintf(label,sizeof(label),"%.*s",chars,state);
        mosaic_record_text(fb,d,ui,x+6*sx,y+4*sy,label,ts_theme_color(active_palette(), TS_THEME_MOSAIC, TS_COLOR_DATA1, RGB(255,215,211)),sx,sy);
        snprintf(label,sizeof(label),"%.1F S",seconds);label[chars]='\0';
        mosaic_record_text(fb,d,ui,x+6*sx,bottom-10*sy,label,border,sx,sy);
    }
}
static void mosaic_recording_card(TsFramebuffer *fb,TsUiWaveformDetail *d,const TsUiState *ui)
{
    if(ui->mosaic_recording) {
        int recording=ui->capture_state==TS_CAPTURE_RECORDING;
        const char *state=!recording?(ui->capture_state==TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER?"ARMED":"FINISHING"):
            ui->mosaic_record_source==TS_RECORD_SOURCE_SOURCES?"REC SRC":ui->mosaic_record_source==TS_RECORD_SOURCE_OUTPUT?"REC OUT":ui->mosaic_record_source==TS_RECORD_SOURCE_EXT?"REC EXT":"REC DRY";
        mosaic_recording_preview(fb,d,ui,&ui->mosaic_record_preview,ui->input_sample_rate,ui->capture_recorded_frames,recording,state);
    }
    if(ui->mosaic_live_recording)mosaic_recording_preview(fb,d,ui,&ui->mosaic_live_preview,ui->mosaic_live_rate,
        ui->mosaic_live_preview.frames,1,"REC LOOP");
}
