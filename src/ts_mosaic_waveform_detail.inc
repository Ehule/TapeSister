/* Two native surfaces cover every visible event, the ruler and source palette.
   Controls stay on the logical framebuffer. Complete event cards move together;
   the shared compositor restores later playhead, box-selection and dialog ink. */
static void mosaic_detail_span(TsUiWaveformDetail *d,int y,double first,double last,uint32_t color)
{
    int a=(int)floor(first),b=(int)ceil(last);
    if(a<0)a=0;
    if(b>=d->width)b=d->width-1;
    for(int x=a;x<=b;++x) {
        double coverage=fmin(x+1.0,last)-fmax((double)x,first);
        if(coverage>=1)d->pixels[(size_t)y*d->width+x]=color;
        else detail_plot(d,x,y,color,coverage);
    }
}
static void mosaic_detail_envelope(const TsMosaicSource *s,size_t first,size_t last,float *peak,float *rms)
{
    if(first>=s->sample.frames)first=s->sample.frames-1;
    if(last<=first)last=first+1;
    if(last>s->sample.frames)last=s->sample.frames;
    /* At close zoom, query the actual frames rather than enlarging cached
       envelope bins. At overview zoom the immutable peak/RMS cache bounds work. */
    if(last-first<=2*(s->sample.frames/TS_MOSAIC_PEAKS+1)) {
        double energy=0;*peak=0;
        for(size_t i=first;i<last;++i) {
            TsStereoFrame v=ts_sample_read_frame(&s->sample,i);
            *peak=fmaxf(*peak,fmaxf(fabsf(v.l),fabsf(v.r)));
            energy+=((double)v.l*v.l+(double)v.r*v.r)*.5;
        }
        *rms=(float)sqrt(energy/(last-first));
    } else mosaic_envelope(s,first,last,peak,rms);
}
/* Sample intervals belong to the event, not the moving viewport. Draw the same
   rows at whole native-pixel offsets: interpolating their widths makes narrow
   peaks grow and shrink during scrolling. Only visible rows are queried. */
static void mosaic_detail_event_row(const TsMosaicEvent *e,int64_t row,double rate,float *peak,float *rms)
{
    double duration=(double)(e->last-e->first)/e->source->sample.sample_rate;
    double elapsed=fmax(0,(double)row/rate),span=1/rate;
    *peak=*rms=0;
    if(!e->looping && elapsed>=duration)return;
    double phase=fmod(elapsed,duration);
    size_t first=e->first+(size_t)(phase*e->source->sample.sample_rate);
    size_t last=e->first+(size_t)(fmin(duration,phase+span)*e->source->sample.sample_rate);
    mosaic_detail_envelope(e->source,first,last,peak,rms);
    if(e->looping && phase+span>=duration) {
        float p,r;mosaic_detail_envelope(e->source,e->first,span>=duration?e->last:
            e->first+(size_t)((phase+span-duration)*e->source->sample.sample_rate),&p,&r);
        *peak=fmaxf(*peak,p);*rms=fmaxf(*rms,r);
    }
}
/* Round world positions once, then subtract the shared viewport offset.
   Separately rounding top/bottom after scrolling changes a card's height; mixing
   coarse labels with native waveforms makes its contents move out of step. */
static int64_t mosaic_detail_pixel(double value)
{
    return (int64_t)floor(value+.5);
}
static void mosaic_detail_rect(TsUiWaveformDetail *d,int64_t left,int64_t top,
                               int64_t right,int64_t bottom,uint32_t color)
{
    int a=(int)fmax(0,fmin(d->width,(double)left)),b=(int)fmax(0,fmin(d->width,(double)right));
    int first=(int)fmax(0,fmin(d->height,(double)top)),last=(int)fmax(0,fmin(d->height,(double)bottom));
    for(int y=first;y<last;++y)for(int x=a;x<b;++x)d->pixels[(size_t)y*d->width+x]=color;
}
static void mosaic_detail_text(TsUiWaveformDetail *d,int64_t x,int64_t y,
                               const char *value,uint32_t color,double sx,double sy)
{
    /* Keep the familiar pixel font, but rasterize in the card's local space.
       Clipping happens after translation, including partially visible labels. */
    for(int n=0;value[n];++n) {
        char c=value[n];if(c>='a' && c<='z')c=(char)(c-'a'+'A');
        const char *bits=glyph(c);
        for(int gy=0;gy<7;++gy)for(int gx=0;gx<5;++gx)if(bits[gy*5+gx]=='1')
            mosaic_detail_rect(d,x+mosaic_detail_pixel((n*6+gx)*sx),y+mosaic_detail_pixel(gy*sy),
                x+mosaic_detail_pixel((n*6+gx+1)*sx),y+mosaic_detail_pixel((gy+1)*sy),color);
    }
}
static void mosaic_detail_card(TsUiWaveformDetail *d,const TsUiState *ui,
                                const TsMosaicEvent *e,int selected,int dim,int ghost)
{
    if(!e->source)return;
    double sx=(double)d->width/(TS_MOSAIC_CANVAS_RIGHT-151),sy=(double)d->height/310;
    double horizontal=ui->mosaic_hscale>0?ui->mosaic_hscale:1;
    double scale=ui->mosaic_scale>0?ui->mosaic_scale:24,rate=scale*sy;
    int64_t pan_x=mosaic_detail_pixel(ui->mosaic_xscroll*horizontal*sx),pan_y=mosaic_detail_pixel(ui->mosaic_scroll*rate);
    int64_t x=mosaic_detail_pixel(e->x*horizontal*sx)-pan_x,y=mosaic_detail_pixel(e->start*rate)-pan_y;
    int64_t right=mosaic_detail_pixel((e->x+e->width)*horizontal*sx)-pan_x;
    int64_t bottom=mosaic_detail_pixel((e->start+e->duration)*rate)-pan_y;
    if(right<=0 || x>=d->width || bottom<=0 || y>=d->height || right<=x || bottom<=y)return;
    int64_t w=right-x,h=bottom-y,bx=mosaic_detail_pixel(sx),by=mosaic_detail_pixel(sy);
    if(bx<1)bx=1;
    if(by<1)by=1;
    uint32_t color=mosaic_color(e->source->hash),fill=selected?RGB(50,46,59):RGB(32,31,39);
    if(dim){color=mosaic_mix_color(color,RGB(19,20,26),.70f);fill=RGB(23,24,30);}
    if(ghost)color=mosaic_mix_color(color,RGB(16,18,24),.55f);
    else mosaic_detail_rect(d,x,y,right,bottom,fill);
    uint32_t outer=mosaic_mix_color(color,fill,.50f);
    int first=(int)fmax(0,fmin(d->height,(double)y+mosaic_detail_pixel(24*sy)));
    int last=(int)fmax(0,fmin(d->height,(double)bottom-mosaic_detail_pixel(14*sy)));
    double a=fmax(0,(double)x+2*bx),b=fmin(d->width,(double)right-2*bx);
    double center=x+w*.5,half=fmax(0,(w-14*sx)*.43);
    double duration=(double)(e->last-e->first)/e->source->sample.sample_rate;
    if(duration>0 && b>a)for(int py=first;py<last;++py) {
        int64_t row=py-y;
        if(ghost && ((int64_t)(row/sy)&1))continue;
        double elapsed=(double)row/rate;
        if(!e->looping && elapsed>=duration)continue;
        float peak,rms;mosaic_detail_event_row(e,row,rate,&peak,&rms);
        double amplitude=fminf(1,peak)*half,body=fminf(1,rms)*half;
        mosaic_detail_span(d,py,fmax(a,center-amplitude-.5),fmin(b,center+amplitude+.5),outer);
        mosaic_detail_span(d,py,fmax(a,center-body-.5),fmin(b,center+body+.5),color);
        if(e->looping && 1/scale<duration && fmod(elapsed,duration)<1/scale)
            mosaic_detail_span(d,py,fmax(a,x+4*sx),fmin(b,right-4*sx),dim?RGB(35,34,41):RGB(72,65,77));
    }
    if(ghost) {
        /* Bound dash work to the viewport even for day-long, highly zoomed tiles. */
        int64_t dx=mosaic_detail_pixel(6*sx),dy=mosaic_detail_pixel(6*sy);
        if(dx<1)dx=1;
        if(dy<1)dy=1;
        for(int64_t xx=x+(x<0?(-x/dx)*dx:0);xx<right && xx<d->width;xx+=dx) {
            mosaic_detail_rect(d,xx,y,(int64_t)fmin(right,xx+3*bx),(int64_t)fmin(bottom,y+by),color);
            mosaic_detail_rect(d,xx,(int64_t)fmax(y,bottom-by),(int64_t)fmin(right,xx+3*bx),bottom,color);
        }
        for(int64_t yy=y+(y<0?(-y/dy)*dy:0);yy<bottom && yy<d->height;yy+=dy) {
            mosaic_detail_rect(d,x,yy,(int64_t)fmin(right,x+bx),(int64_t)fmin(bottom,yy+3*by),color);
            mosaic_detail_rect(d,(int64_t)fmax(x,right-bx),yy,right,(int64_t)fmin(bottom,yy+3*by),color);
        }
        return;
    }
    uint32_t border=selected?ui->palette.colors[TS_PALETTE_MOSAIC_HIGHLIGHT]:color;
    int64_t edge_y=(int64_t)fmin(h,(selected?2:1)*by);
    mosaic_detail_rect(d,x,y,x+(int64_t)fmin(w,2*bx),bottom,border);
    mosaic_detail_rect(d,right-(int64_t)fmin(w,(selected?2:1)*bx),y,right,bottom,border);
    mosaic_detail_rect(d,x,y,right,y+edge_y,border);
    mosaic_detail_rect(d,x,bottom-edge_y,right,bottom,border);
    int chars=(int)((w/sx-12)/6);if(chars>24)chars=24;
    char label[100];int64_t tx=x+mosaic_detail_pixel(6*sx);
    if(chars>0 && h>=mosaic_detail_pixel(14*sy)) {
        snprintf(label,sizeof(label),"%.*s",chars,e->name);
        mosaic_detail_text(d,tx,y+mosaic_detail_pixel(4*sy),label,color,sx,sy);
    }
    if(chars>0 && h>=mosaic_detail_pixel(25*sy)) {
        char notes[40]="",note[8];
        for(int n=0;n<e->note_count;++n) {
            size_t used=strlen(notes);snprintf(notes+used,sizeof(notes)-used,"%s%s",n?" ":"",ts_midi_note_name(e->notes[n],note,sizeof(note)));
        }
        snprintf(label,sizeof(label),"%.*s",chars,notes);
        mosaic_detail_text(d,tx,y+mosaic_detail_pixel(14*sy),label,dim?RGB(66,64,75):RGB(156,147,165),sx,sy);
    }
    if(chars>=4 && h>mosaic_detail_pixel(38*sy)) {
        const char *state=e->muted?"MUTE":e->solo?"SOLO":dim?(chars>=8?"SOLO OUT":"EXCL"):e->looping?"LOOP":"ONCE";
        mosaic_detail_text(d,tx,bottom-mosaic_detail_pixel(11*sy),state,color,sx,sy);
    }
}
static int mosaic_detail_cards(TsFramebuffer *fb,const TsUiState *ui,int any_solo,double interval)
{
    TsUiWaveformDetail *d=detail_region(0,151,66,TS_MOSAIC_CANVAS_RIGHT-151,310);if(!d)return 0;
    /* The whole moving scene shares the native raster. Do not enlarge coarse
       card fragments underneath it; later controls/overlays use the compositor. */
    mosaic_detail_rect(d,0,0,d->width,d->height,RGB(11,13,16));
    double sy=(double)d->height/310,scale=ui->mosaic_scale>0?ui->mosaic_scale:24;
    double rate=scale*sy;int64_t pan=mosaic_detail_pixel(ui->mosaic_scroll*rate);
    int64_t line=mosaic_detail_pixel(sy);if(line<1)line=1;
    int64_t tick=(int64_t)floor(ui->mosaic_scroll/interval);
    for(;;++tick) {
        int64_t y=mosaic_detail_pixel(tick*interval*rate)-pan;if(y>=d->height)break;
        mosaic_detail_rect(d,0,y,d->width,y+line,RGB(28,30,35));
    }
    for(int i=0;i<ui->mosaic_ghost_count;++i)mosaic_detail_card(d,ui,&ui->mosaic_ghosts[i],0,0,1);
    if(ui->mosaic)for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        const TsMosaicEvent *e=&ui->mosaic->events[i];if(!e->id)continue;
        mosaic_detail_card(d,ui,e,e->id==ui->mosaic_selected || e->id==ui->mosaic_selection[i],e->muted || (any_solo && !e->solo),0);
    }
    d->valid=1;detail_capture_slot(fb,0);return 1;
}
static int mosaic_detail_screen_x(const TsUiWaveformDetail *d,int x)
{
    return detail_output_width>0?x*detail_output_width/TS_UI_WIDTH-d->output_x:
        (x-d->x)*d->width/d->logical_width;
}
static int mosaic_detail_screen_y(const TsUiWaveformDetail *d,int y)
{
    return detail_output_height>0?y*detail_output_height/TS_UI_HEIGHT-d->output_y:
        (y-d->y)*d->height/d->logical_height;
}
static void mosaic_detail_ruler(TsUiWaveformDetail *d,const TsUiState *ui,double interval)
{
    int left=mosaic_detail_screen_x(d,113),label_x=mosaic_detail_screen_x(d,115),tick_x=mosaic_detail_screen_x(d,147);
    mosaic_detail_rect(d,left,0,d->width,d->height,RGB(11,13,16));
    /* Both surfaces have exactly the same vertical bounds and pixel scale.
       Labels and tick marks use the canvas's world rounding and pan offset. */
    double sx=(double)(d->width-left)/38,sy=(double)d->height/310;
    double rate=(ui->mosaic_scale>0?ui->mosaic_scale:24)*sy;
    int64_t pan=mosaic_detail_pixel(ui->mosaic_scroll*rate),line=mosaic_detail_pixel(sy);
    if(line<1)line=1;
    int64_t tick=(int64_t)floor(ui->mosaic_scroll/interval);
    for(;;++tick) {
        double time=tick*interval;int64_t y=mosaic_detail_pixel(time*rate)-pan;
        if(y>=d->height)break;
        char label[32];mosaic_time_label(label,sizeof(label),time);
        mosaic_detail_text(d,label_x,y+mosaic_detail_pixel(2*sy),label,RGB(133,127,141),sx,sy);
        mosaic_detail_rect(d,tick_x,y,d->width,y+line,RGB(28,30,35));
    }
}
static void mosaic_detail_sources(TsFramebuffer *fb,const TsUiState *ui,double interval)
{
    /* Extend the existing sidebar surface to cover the time ruler. The event
       surface and its raster dimensions stay untouched. */
    TsUiWaveformDetail *d=detail_region(1,10,66,141,310);if(!d)return;
    static TsWaveformCache cache[16];
    detail_background(d,fb);
    int source_y=mosaic_detail_screen_y(d,74);
    double sx=(double)mosaic_detail_screen_x(d,103)/93;
    double sy=(double)(mosaic_detail_screen_y(d,340)-source_y)/266;
    for(int i=0;i<16;++i) {
        int n=ui->mosaic_source_offset+i;if(n>=ui->mosaic_source_count)break;
        const TsSample *s=ui->mosaic_sources[n].sample;if(!s || !s->data)continue;
        int x=(i%2)*49,y=(i/2)*34,selected=n==ui->mosaic_source_selected;
        uint32_t fill=selected?RGB(51,47,59):RGB(40,36,44);
        uint32_t color=mosaic_color(ui->mosaic_sources[n].hash);
        int first=(int)((x+4)*sx),last=(int)((x+39)*sx);
        int top=source_y+(int)((y+15)*sy),bottom=source_y+(int)((y+26)*sy);
        int width=last-first,bins=width<TS_WAVEFORM_CACHE_MAX_COLUMNS?width:TS_WAVEFORM_CACHE_MAX_COLUMNS;
        TsWaveformRequest request={.sample=s,.first=0,.last=s->frames,.width=bins,.revision=(ui->mosaic?ui->mosaic->epoch:0)^ui->mosaic_sources[n].hash};
        if(!ts_waveform_cache_prepare(&cache[i],&request))continue;
        for(int py=top;py<bottom;++py)for(int px=first;px<last;++px)d->pixels[(size_t)py*d->width+px]=fill;
        for(int px=first;px<last;++px) {
            const TsWaveformColumn *c=&cache[i].columns[(px-first)*bins/width];
            double lo=source_y+(y+20.5-fmaxf(-1,fminf(1,c->maximum))*5)*sy;
            double hi=source_y+(y+20.5-fmaxf(-1,fminf(1,c->minimum))*5)*sy;
            detail_line(d,px,lo,px,hi,color);
        }
    }
    mosaic_detail_ruler(d,ui,interval);
    d->valid=1;detail_capture_slot(fb,1);
}
