/* Fade ink is drawn in each card's local coordinates, so it translates with
   native waveforms without shimmer. Never scan outside the visible surface. */
static double mosaic_fade_handle_y(double height,double fade,int part,double sy)
{
    double y=part==1?fade:height-fade;
    return fmax(3*sy,fmin(height-4*sy,y));
}
int ts_ui_mosaic_fade_handle(const TsUiState *ui,const TsMosaicEvent *e,int part,double *x,double *y)
{
    if(part==2 && e->live && !e->live_finite)return 0;
    double h=ui->mosaic_hscale>0?ui->mosaic_hscale:1;
    double scale=ui->mosaic_scale>0?ui->mosaic_scale:24;
    if(e->width*h<24 || e->duration*scale<14 || part<1 || part>2)return 0;
    double in,out;ts_mosaic_event_fades(e,&in,&out);
    *x=151+(e->x+e->width-ui->mosaic_xscroll)*h-(part==1?15:5);
    *y=66+(e->start-ui->mosaic_scroll)*scale+
        mosaic_fade_handle_y(e->duration*scale,(part==1?in:out)*scale,part,1);
    return *x>=151 && *x<TS_MOSAIC_CANVAS_RIGHT && *y>=66 && *y<TS_MOSAIC_CANVAS_BOTTOM;
}
/* Pan is a balance across the card's width, independent of its placement.
   The grip sits below the text; the rest of the waveform remains a move target. */
int ts_ui_mosaic_pan_handle(const TsUiState *ui,const TsMosaicEvent *e,double *x,double *y,double *span)
{
    double h=ui->mosaic_hscale>0?ui->mosaic_hscale:1;
    double scale=ui->mosaic_scale>0?ui->mosaic_scale:24;
    double width=e->width*h;
    if(width<40 || e->duration*scale<48)return 0;
    *span=width*.5-8;
    *x=151+(e->x-ui->mosaic_xscroll)*h+width*.5+fmax(-1,fmin(1,e->pan))*(*span);
    *y=66+(e->start-ui->mosaic_scroll)*scale+30;
    return *x>=151 && *x<TS_MOSAIC_CANVAS_RIGHT && *y>=66 && *y<TS_MOSAIC_CANVAS_BOTTOM;
}
typedef struct {
    uint32_t *pixels;
    int stride,left,top,right,bottom;
    double sx,sy,rate;
} MosaicFadeSurface;
static void mosaic_fade_rect(const MosaicFadeSurface *s,int64_t x,int64_t y,int64_t right,int64_t bottom,uint32_t ink)
{
    int a=(int)fmax(s->left,fmin(s->right,(double)x)),b=(int)fmax(s->left,fmin(s->right,(double)right));
    int first=(int)fmax(s->top,fmin(s->bottom,(double)y)),last=(int)fmax(s->top,fmin(s->bottom,(double)bottom));
    for(int yy=first;yy<last;++yy)for(int xx=a;xx<b;++xx)s->pixels[(size_t)yy*s->stride+xx]=ink;
}
static void mosaic_fade_shape(const MosaicFadeSurface *s,const TsMosaicEvent *e,
    int64_t x,int64_t y,int64_t right,int64_t bottom,uint32_t ink)
{
    double in,out;ts_mosaic_event_fades(e,&in,&out);
    if(in==0 && out==0)return;
    int first=(int)fmax(s->top,fmin(s->bottom,(double)y)),last=(int)fmax(s->top,fmin(s->bottom,(double)bottom));
    int a=(int)fmax(s->left,fmin(s->right,(double)x)),b=(int)fmax(s->left,fmin(s->right,(double)right));
    int line=(int)fmax(1,round(s->sx));
    for(int yy=first;yy<last;++yy) {
        double elapsed=(yy-y)/s->rate,gain=1;
        if(in>0)gain=fmin(gain,elapsed/in);
        if(out>0)gain=fmin(gain,(e->duration-elapsed)/out);
        if(gain>=1)continue;
        gain=fmax(0,gain);
        int64_t edge=x+(int64_t)floor((right-x-1)*gain+.5);
        int shade=(int)fmax(a,fmin(b,(double)edge));
        for(int xx=shade;xx<b;++xx) {
            uint32_t *p=&s->pixels[(size_t)yy*s->stride+xx];
            *p=0xff000000u|((*p&0x00fefefeu)>>1);
        }
        mosaic_fade_rect(s,edge,yy,(int64_t)fmin(right,edge+line),yy+1,ink);
    }
}
static void mosaic_fade_handles(const MosaicFadeSurface *s,const TsUiState *ui,const TsMosaicEvent *e,
    int64_t x,int64_t y,int64_t right,int64_t bottom,uint32_t ink)
{
    if((right-x)/s->sx<24 || (bottom-y)/s->sy<14)return;
    double in,out;ts_mosaic_event_fades(e,&in,&out);
    uint32_t dark=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_BACKGROUND,RGB(15,16,19));
    for(int part=1;part<=2;++part) {
        int64_t hx=right-(int64_t)floor((part==1?15:5)*s->sx+.5);
        int64_t hy=y+(int64_t)floor(mosaic_fade_handle_y(bottom-y,(part==1?in:out)*s->rate,part,s->sy)+.5);
        int64_t rx=(int64_t)fmax(2,floor(3*s->sx+.5)),ry=(int64_t)fmax(2,floor(3*s->sy+.5));
        uint32_t color=ui->mosaic_card_mix_event==e->id && ui->mosaic_card_mix_part==part?
            ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_TEXT,RGB(224,214,233)):ink;
        mosaic_fade_rect(s,hx-rx,hy-ry,hx+rx+1,hy+ry+1,dark);
        mosaic_fade_rect(s,hx-rx+1,hy-ry+1,hx+rx,hy+ry,color);
        /* Direction notch: drag IN downward, OUT upward. */
        int64_t notch=hy+(part==1?ry:-ry);
        mosaic_fade_rect(s,hx,notch,hx+1,notch+1,color);
    }
}
static void mosaic_card_pan(const MosaicFadeSurface *s,const TsUiState *ui,const TsMosaicEvent *e,
    int64_t x,int64_t y,int64_t right,int64_t bottom,uint32_t source)
{
    if((right-x)/s->sx<40 || (bottom-y)/s->sy<48)return;
    int64_t center=x+(int64_t)floor((right-x)*.5+.5);
    int64_t pan=x+(int64_t)floor((right-x)*.5+fmax(-1,fmin(1,e->pan))*((right-x)*.5-8*s->sx)+.5);
    int64_t first=y+(int64_t)round(26*s->sy),last=bottom-(int64_t)round(14*s->sy);
    int64_t width=(int64_t)fmax(1,round(s->sx)),dash=(int64_t)fmax(1,round(6*s->sy));
    uint32_t ink=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_DATA2,RGB(129,206,200));
    uint32_t dim=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_EDGE,RGB(66,60,77));
    int hot=ui->mosaic_card_mix_event==e->id && ui->mosaic_card_mix_part==3;
    /* Dashes are card-relative and work is bounded even when deeply zoomed. */
    int64_t from=first<s->top?first+(s->top-first)/dash*dash:first;
    for(int64_t yy=from;yy<last && yy<s->bottom;yy+=dash)
        mosaic_fade_rect(s,center,yy,center+width,(int64_t)fmin(last,yy+fmax(1,round(2*s->sy))),dim);
    mosaic_fade_rect(s,pan,first,pan+width,last,hot?ink:mosaic_mix_color(ink,dim,.45f));
    int64_t hy=y+(int64_t)round(30*s->sy);
    uint32_t dark=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_BACKGROUND,RGB(15,16,19));
    for(int row=-4;row<=4;++row) {
        int radius=4-abs(row);
        mosaic_fade_rect(s,pan-(int64_t)round(radius*s->sx),hy+(int64_t)round(row*s->sy),
            pan+(int64_t)round(radius*s->sx)+width,hy+(int64_t)round((row+1)*s->sy),dark);
        if(radius>0)mosaic_fade_rect(s,pan-(int64_t)round((radius-1)*s->sx),hy+(int64_t)round(row*s->sy),
            pan+(int64_t)round((radius-1)*s->sx)+width,hy+(int64_t)round((row+1)*s->sy),hot?source:ink);
    }
}
