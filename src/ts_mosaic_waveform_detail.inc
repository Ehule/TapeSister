/* Two native surfaces cover every visible event and the source palette.
   Controls stay on the logical framebuffer. Only signal interiors are replaced;
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
static void mosaic_detail_cards(TsFramebuffer *fb,const TsUiState *ui,int any_solo)
{
    TsUiWaveformDetail *d=detail_region(0,151,66,480,310);
    if(!d || !ui->mosaic)return;
    detail_background(d,fb);
    double sx=(double)d->width/480,sy=(double)d->height/310;
    double horizontal=ui->mosaic_hscale>0?ui->mosaic_hscale:1,scale=ui->mosaic_scale;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        const TsMosaicEvent *e=&ui->mosaic->events[i];if(!e->id || !e->source)continue;
        double left=151+(e->x-ui->mosaic_xscroll)*horizontal,width=e->width*horizontal;
        double top=66+(e->start-ui->mosaic_scroll)*scale,bottom=top+e->duration*scale;
        if(left+width<=151 || left>=631 || width<5 || bottom<=66 || top>=376)continue;
        int x=(int)left,w=(int)width,y=(int)fmax(66,top),end=(int)fmin(376,bottom);
        int first=y+(top>=66?24:0),last=end-(bottom<376?14:0);
        if(last<=first)continue;
        int selected=e->id==ui->mosaic_selected || e->id==ui->mosaic_selection[i];
        int dim=e->muted || (any_solo && !e->solo);
        uint32_t color=mosaic_color(e->source->hash),fill=selected?RGB(50,46,59):RGB(32,31,39);
        if(dim){color=mosaic_mix_color(color,RGB(19,20,26),.70f);fill=RGB(23,24,30);}
        uint32_t outer=mosaic_mix_color(color,fill,.50f);
        int a=(int)(fmax(151,x+2)-151)*sx,b=(int)((fmin(631,x+w-2)-151)*sx);
        int py=(int)((first-66)*sy),stop=(int)((last-66)*sy);
        double center=(x+w/2-151+.5)*sx,half=fmax(0,(w-14)*.43)*sx;
        double duration=(double)(e->last-e->first)/e->source->sample.sample_rate;
        for(;py<stop;++py) {
            for(int px=a;px<b;++px)d->pixels[(size_t)py*d->width+px]=fill;
            double elapsed=fmax(0,ui->mosaic_scroll+(py/sy)/scale-e->start);
            if(!e->looping && elapsed>=duration)continue;
            double phase=fmod(elapsed,duration),span=1/(scale*sy);
            size_t f=e->first+(size_t)(phase*e->source->sample.sample_rate);
            size_t l=e->first+(size_t)(fmin(duration,phase+span)*e->source->sample.sample_rate);
            float peak,rms;mosaic_detail_envelope(e->source,f,l,&peak,&rms);
            if(e->looping && phase+span>=duration) {
                float p,r;mosaic_detail_envelope(e->source,e->first,span>=duration?e->last:
                    e->first+(size_t)((phase+span-duration)*e->source->sample.sample_rate),&p,&r);
                peak=fmaxf(peak,p);rms=fmaxf(rms,r);
            }
            double amplitude=fminf(1,peak)*half,body=fminf(1,rms)*half;
            mosaic_detail_span(d,py,fmax(a,center-amplitude-.5),fmin(b,center+amplitude+.5),outer);
            mosaic_detail_span(d,py,fmax(a,center-body-.5),fmin(b,center+body+.5),color);
            /* Repeat separators remain one logical pixel, like the other UI. */
            int ly=66+(int)((2LL*py+1)*310/(2LL*d->height));
            double logical_phase=fmod(fmax(0,ui->mosaic_scroll+(ly-66)/scale-e->start),duration);
            if(e->looping && 1/scale<duration && ly>first && logical_phase<1/scale)
                mosaic_detail_span(d,py,fmax(a,(x+4-151)*sx),fmin(b,(x+w-4-151)*sx),dim?RGB(35,34,41):RGB(72,65,77));
        }
    }
    d->valid=1;detail_capture_slot(fb,0);
}
static void mosaic_detail_sources(TsFramebuffer *fb,const TsUiState *ui)
{
    TsUiWaveformDetail *d=detail_region(1,10,74,93,266);if(!d)return;
    static TsWaveformCache cache[16];
    detail_background(d,fb);
    double sx=(double)d->width/93,sy=(double)d->height/266;
    for(int i=0;i<16;++i) {
        int n=ui->mosaic_source_page*16+i;if(n>=ui->mosaic_source_count)break;
        const TsSample *s=ui->mosaic_sources[n].sample;if(!s || !s->data)continue;
        int x=(i%2)*49,y=(i/2)*34,selected=n==ui->mosaic_source_selected;
        uint32_t fill=selected?RGB(51,47,59):RGB(40,36,44);
        uint32_t color=ui->mosaic_sources[n].source?mosaic_color(ui->mosaic_sources[n].source->hash):RGB(135,171,174);
        int first=(int)((x+4)*sx),last=(int)((x+39)*sx);
        int top=(int)((y+15)*sy),bottom=(int)((y+26)*sy);
        int width=last-first,bins=width<TS_WAVEFORM_CACHE_MAX_COLUMNS?width:TS_WAVEFORM_CACHE_MAX_COLUMNS;
        TsWaveformRequest request={.sample=s,.first=0,.last=s->frames,.width=bins,.revision=(ui->mosaic?ui->mosaic->epoch:0)^(ui->mosaic_sources[n].source?ui->mosaic_sources[n].source->hash:0)};
        if(!ts_waveform_cache_prepare(&cache[i],&request))continue;
        for(int py=top;py<bottom;++py)for(int px=first;px<last;++px)d->pixels[(size_t)py*d->width+px]=fill;
        for(int px=first;px<last;++px) {
            const TsWaveformColumn *c=&cache[i].columns[(px-first)*bins/width];
            double lo=(y+20.5-fmaxf(-1,fminf(1,c->maximum))*5)*sy;
            double hi=(y+20.5-fmaxf(-1,fminf(1,c->minimum))*5)*sy;
            detail_line(d,px,lo,px,hi,color);
        }
    }
    d->valid=1;detail_capture_slot(fb,1);
}
