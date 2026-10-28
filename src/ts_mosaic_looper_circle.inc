/* A small native-pixel layer, drawn after the canvas. Geometry is cached by
   output scale; dragging changes only the destination. Never called by audio. */
void ts_ui_mosaic_looper_circle_free(TsUiLooperCircle *d)
{
    free(d->pixels);free(d->base);free(d->disc_alpha);free(d->ring_alpha);free(d->phase);free(d->ring_pixels);
    memset(d,0,sizeof(*d));
}
static uint32_t looper_blend(uint32_t a,uint32_t b,unsigned alpha)
{
    unsigned r=(((a>>16)&255)*(255-alpha)+((b>>16)&255)*alpha+127)/255;
    unsigned g=(((a>>8)&255)*(255-alpha)+((b>>8)&255)*alpha+127)/255;
    unsigned blue=((a&255)*(255-alpha)+(b&255)*alpha+127)/255;
    return 0xff000000u|(r<<16)|(g<<8)|blue;
}
static void looper_native_text(TsUiLooperCircle *d,double y,const char *label,double scale,uint32_t ink)
{
    double left=d->width*.5-strlen(label)*3*scale;
    for(size_t n=0;label[n];++n) {
        const char *bits=glyph(label[n]);
        for(int gy=0;gy<7;++gy)for(int gx=0;gx<5;++gx)if(bits[gy*5+gx]=='1') {
            int a=(int)lrint(left+(n*6+gx)*scale),b=(int)lrint(left+(n*6+gx+1)*scale);
            int top=(int)lrint(y+gy*scale),bottom=(int)lrint(y+(gy+1)*scale);
            for(int yy=top;yy<bottom && yy<d->height;++yy)for(int xx=a;xx<b && xx<d->width;++xx)
                if(xx>=0 && yy>=0)d->base[(size_t)yy*d->width+xx]=ink;
        }
    }
}
int ts_ui_mosaic_looper_circle(TsUiLooperCircle *d,const TsUiState *ui,int width,int height)
{
    d->valid=d->changed=0;
    if(!ui->mosaic_open || !ui->mosaic_live_open || ui->mosaic_live_advanced || !ui->mosaic ||
        ui->portal.open || ui->tracker_open || ui->midi_learn_active || ui->file_busy || ts_ui_foreground_panel_open(ui) || width<=0 || height<=0)return 0;
    double scale=fmin(width/(double)TS_UI_WIDTH,height/(double)TS_UI_HEIGHT);
    int side=2*(int)ceil(48*scale)+1;if(side<3 || side>2049)return 0;
    if(!d->pixels || d->width!=side || d->scale!=scale) {
        TsUiLooperCircle next={0};size_t count=(size_t)side*side;
        next.pixels=malloc(count*sizeof(uint32_t));next.base=malloc(count*sizeof(uint32_t));
        next.disc_alpha=malloc(count);next.ring_alpha=malloc(count);
        next.phase=malloc(count*sizeof(uint16_t));next.ring_pixels=malloc(count*sizeof(unsigned));
        if(!next.pixels || !next.base || !next.disc_alpha || !next.ring_alpha || !next.phase || !next.ring_pixels) {
            ts_ui_mosaic_looper_circle_free(&next);return 0;
        }
        next.width=next.height=side;next.scale=scale;next.last_phase=UINT32_MAX;
        for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
            double dx=x-side/2,dy=y-side/2,distance=hypot(dx,dy);size_t at=(size_t)y*side+x;
            next.disc_alpha[at]=(uint8_t)lrint(255*fmax(0,fmin(1,31*scale-distance+.5)));
            next.ring_alpha[at]=(uint8_t)lrint(255*fmax(0,fmin(1,fmin(44*scale-distance+.5,distance-39*scale+.5))));
            double phase=atan2(dx,-dy)/6.283185307179586;if(phase<0)phase+=1;
            next.phase[at]=(uint16_t)fmin(65535,floor(phase*65536));
            if(next.ring_alpha[at])next.ring_pixels[next.ring_count++]=(unsigned)at;
        }
        ts_ui_mosaic_looper_circle_free(d);*d=next;
    }
    int x,y;ts_ui_mosaic_live_origin(ui,&x,&y);
    d->x=(int)lrint((x+83.5)*width/TS_UI_WIDTH)-side/2;
    d->y=(int)lrint((y+86.5)*height/TS_UI_HEIGHT)-side/2;
    const TsMosaicEvent *e=ts_mosaic_find(ui->mosaic,ui->mosaic_sequence_running?ui->mosaic_sequence_id:ui->mosaic_selected);
    const TsMosaicLive *l=e && e->live?mosaic_live_snapshot(ui,e):NULL;
    int state=l && l->id==e->id?l->state:0;
    uint32_t panel=ts_theme_color(&ui->palette,TS_THEME_MOSAIC,TS_COLOR_PANEL,RGB(31,28,38));
    uint32_t disc=ui->mosaic_live_finishing?RGB(99,49,55):RGB(229,24,32);
    uint32_t dim=ts_theme_color(&ui->palette,TS_THEME_MOSAIC,TS_COLOR_EDGE,RGB(64,54,70));
    uint32_t ink=ui->mosaic_live_recording?RGB(255,139,119):state==TS_MOSAIC_LIVE_PLAYING?RGB(129,221,202):RGB(245,184,91);
    double phase=ui->mosaic_live_recording?ui->mosaic_live_seconds/(ui->mosaic_live_duration>0?ui->mosaic_live_duration:60):
        ui->mosaic_live_finishing?0:l?l->phase:0;
    if(!ui->mosaic_live_recording && !ui->mosaic_live_finishing && ui->mosaic_sequence_running)
        phase=ui->mosaic_sequence_duration>0 && l?l->elapsed/ui->mosaic_sequence_duration:0;
    uint32_t keys[]={panel,disc,dim,ink,(uint32_t)ui->mosaic_live_finishing,(uint32_t)ui->mosaic->live_capture.length,
        (uint32_t)ui->mosaic->live_capture.units,1};
    int base_changed=memcmp(d->keys,keys,sizeof(keys))!=0;
    if(base_changed) {
        for(size_t i=0;i<(size_t)side*side;++i)d->base[i]=looper_blend(panel,disc,d->disc_alpha[i]);
        looper_native_text(d,side/2-9*scale,ui->mosaic_live_finishing?"...":"REC",2*scale,RGB(255,255,255));
        char label[24];if(ui->mosaic->live_capture.length)snprintf(label,sizeof(label),"%d %s",ui->mosaic->live_capture.length,ui->mosaic->live_capture.units?"BEATS":"SEC");
        else snprintf(label,sizeof(label),"MANUAL");
        looper_native_text(d,side/2+12*scale,label,scale,RGB(255,225,225));memcpy(d->keys,keys,sizeof(keys));
    }
    uint32_t progress=(uint32_t)lrint(fmax(0,fmin(1,phase))*65536);
    if(base_changed || d->last_phase!=progress) {
        memcpy(d->pixels,d->base,(size_t)side*side*sizeof(uint32_t));
        for(unsigned n=0;n<d->ring_count;++n) {
            unsigned at=d->ring_pixels[n];d->pixels[at]=looper_blend(d->base[at],d->phase[at]<progress?ink:dim,d->ring_alpha[at]);
        }
        d->last_phase=progress;d->changed=1;++d->raster_count;
    }
    d->valid=1;return 1;
}
