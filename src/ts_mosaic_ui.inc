/* Mosaic uses the native pixel renderer and the same master strip. Geometry
   is time in seconds vertically; horizontal space carries no audio meaning. */
static uint32_t mosaic_color(uint64_t id)
{
    static const uint32_t colors[]={0xff81cec8,0xffd5b37c,0xffba93ba,0xffa2b983,0xff8da9cb};
    return colors[id%5];
}
static void mosaic_button(TsFramebuffer *fb,int x,int y,int w,const char *label,int active,int small)
{
    int h=small?16:24;
    rect(fb,x,y,w,h,RGB(64,54,70));rect(fb,x+2,y+2,w-4,h-4,active?RGB(45,65,66):RGB(46,39,51));
    text(fb,x+(w-(int)strlen(label)*6)/2,y+(h-7)/2,label,active?RGB(134,214,205):RGB(210,199,217),1);
}
static void mosaic_canvas_rect(TsFramebuffer *fb,int x,int y,int w,int h,uint32_t color)
{
    int right=x+w,bottom=y+h;
    if(x<151)x=151;
    if(y<66)y=66;
    if(right>631)right=631;
    if(bottom>376)bottom=376;
    if(right>x && bottom>y)rect(fb,x,y,right-x,bottom-y,color);
}
static void mosaic_render(TsFramebuffer *fb,const TsUiState *ui,const TsInstrument *instrument)
{
    const TsMosaic *m=ui->mosaic;
    clear(fb,RGB(15,16,19));
    rect(fb,0,0,640,32,RGB(12,12,12));sister_portal_render(fb,ui);
    text(fb,164,13,"MOSAIC",RGB(198,186,206),1);
    mosaic_button(fb,214,4,46,"CFG",0,0);mosaic_button(fb,264,4,32,"FT2",0,0);
    mosaic_button(fb,300,4,56,"CANVAS",0,0);mosaic_button(fb,360,4,38,"SAVE",0,0);
    mosaic_button(fb,402,4,58,"EXPORT",0,0);mosaic_button(fb,464,4,28,"CDP",0,0);
    mosaic_button(fb,TS_UI_MASTER_LIMITER_X,TS_UI_MASTER_LIMITER_Y,TS_UI_MASTER_LIMITER_W,"LIM",ui->master_output.limiter_enabled,0);
    master_output_fader(fb,ui->master_output.gain,TS_UI_MASTER_OUTPUT_X,TS_UI_MASTER_OUTPUT_Y);
    master_output_meter(fb,&ui->master_output,TS_UI_MASTER_METER_X,TS_UI_MASTER_METER_Y);
    text(fb,12,43,"SOURCES",RGB(198,186,206),1);
    char label[100];snprintf(label,sizeof(label),"PAGE %02d",ui->sample_page+1);text(fb,12,55,label,RGB(133,127,141),1);
    mosaic_button(fb,118,39,42,ui->mosaic_playing?"PAUSE":"PLAY",ui->mosaic_playing,1);
    mosaic_button(fb,164,39,38,"STOP",0,1);
    int rec=ui->file_record_state==TS_PERFORMANCE_FILE_RECORDING;
    mosaic_button(fb,206,39,64,rec?"STOP REC":"REC FILE",rec,1);
    mosaic_button(fb,274,39,44,"REPEAT",m && m->repeat,1);
    snprintf(label,sizeof(label),"%06.2f s",ui->mosaic_time);text(fb,330,44,label,mosaic_color(0),1);
    mosaic_button(fb,490,39,52,"FIT ALL",0,1);mosaic_button(fb,548,39,24,"-",0,1);mosaic_button(fb,578,39,24,"+",0,1);
    for(int i=0;i<TS_BANK_SLOT_COUNT;++i) {
        int x=10+(i%2)*49,y=74+(i/2)*34;
        const TsSample *sample=i==instrument->selected_slot?&instrument->current:&instrument->bank[i].sample;
        uint32_t color=RGB(135,171,174);
        rect(fb,x,y,44,28,sample->data?RGB(40,36,44):RGB(23,23,28));
        snprintf(label,sizeof(label),"%02d",i+1);text(fb,x+4,y+4,label,sample->data?color:RGB(133,127,141),1);
        if(sample->data) {
            for(int px=0;px<35;++px) {
                size_t f=(size_t)px*sample->frames/35;TsStereoFrame a=ts_sample_read_frame(sample,f);
                int h=(int)(fminf(1,fmaxf(fabsf(a.l),fabsf(a.r)))*5);
                rect(fb,x+4+px,y+20-h,1,1+2*h,color);
            }
        }
    }
    text(fb,12,354,"DRAG INTO",RGB(133,127,141),1);text(fb,12,365,"THE CANVAS",RGB(133,127,141),1);
    rect(fb,113,66,518,310,RGB(11,13,16));
    snprintf(label,sizeof(label),"%.84s",ui->status);text(fb,118,58,label,RGB(133,127,141),1);
    double horizontal=ui->mosaic_hscale>0?ui->mosaic_hscale:1;
    double scale=ui->mosaic_scale>0?ui->mosaic_scale:24;
    double decade=pow(10,floor(log10(40/scale))),step=40/scale/decade;
    double interval=(step<=1?1:step<=2?2:step<=5?5:10)*decade;
    for(double t=ceil(ui->mosaic_scroll/interval)*interval;t<ui->mosaic_scroll+310/scale;t+=interval) {
        int y=66+(int)((t-ui->mosaic_scroll)*scale);
        if(t<100)snprintf(label,sizeof(label),"%04.1f",t);else if(t<10000)snprintf(label,sizeof(label),"%04.0f",t);else snprintf(label,sizeof(label),"%3.0fm",t/60);text(fb,115,y+2,label,RGB(133,127,141),1);
        rect(fb,147,y,484,1,RGB(28,30,35));
    }
    if(m)for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        const TsMosaicEvent *e=&m->events[i];if(!e->id)continue;
        int x=151+(int)((e->x-ui->mosaic_xscroll)*horizontal),w=(int)(e->width*horizontal);
        double top=66+(e->start-ui->mosaic_scroll)*scale,bottom=top+e->duration*scale;
        if(x+w<=151 || x>=631 || w<1 || bottom<=66 || top>=376)continue;
        int y=(int)fmax(66,top),end=(int)fmin(376,bottom),h=end-y;
        if(h<1)h=1;
        uint32_t color=mosaic_color(e->source->hash),fill=e->id==ui->mosaic_selected?RGB(48,43,54):RGB(32,31,39);
        mosaic_canvas_rect(fb,x,y,w,h,fill);mosaic_canvas_rect(fb,x,y,2,h,color);
        mosaic_canvas_rect(fb,x+w-1,y,1,h,e->id==ui->mosaic_selected?color:RGB(64,57,68));
        if(top>=66)mosaic_canvas_rect(fb,x,y,w,1,color);
        if(bottom<376)mosaic_canvas_rect(fb,x,end-1,w,1,color);
        double duration=(double)(e->last-e->first)/e->source->sample.sample_rate;
        int header=top>=66?24:0;
        for(int yy=y+header;yy<end-15;++yy) {
            double elapsed=(ui->mosaic_scroll+(yy-66)/scale-e->start);
            if(!e->looping && elapsed>=duration)continue;
            double phase=fmod(fmax(0,elapsed),duration)/duration;
            size_t frame=e->first+(size_t)(phase*(e->last-e->first));
            int bin=(int)(frame*256/e->source->sample.frames);if(bin>255)bin=255;
            int amplitude=(int)(fminf(1,e->source->peaks[bin])*(w-16)*.43f);
            mosaic_canvas_rect(fb,x+w/2-amplitude,yy,1+amplitude*2,1,color);
            if(e->looping && yy>y+header && fmod(elapsed,duration)<1/scale)
                mosaic_canvas_rect(fb,x+4,yy,w-8,1,RGB(72,65,77));
        }
        if(top>=66 && h>=14 && x>=151 && w>=24) {
            int chars=((x+w>631?631-x:w)-12)/6;if(chars>24)chars=24;
            snprintf(label,sizeof(label),"%.*s",chars,e->name);text(fb,x+6,y+4,label,color,1);
        }
        if(top>=66 && h>=25 && x>=151 && w>=24) {
            char notes[40]="",note[8];
            for(int n=0;n<e->note_count;++n) {
                size_t used=strlen(notes);snprintf(notes+used,sizeof(notes)-used,"%s%s",n?" ":"",ts_midi_note_name(e->notes[n],note,sizeof(note)));
            }
            int chars=((x+w>631?631-x:w)-12)/6;snprintf(label,sizeof(label),"%.*s",chars,notes);text(fb,x+6,y+14,label,RGB(133,127,141),1);
        }
        if(h>38 && bottom<376 && x>=151 && x+36<631 && w>=36)text(fb,x+6,end-11,e->looping?"LOOP":"ONCE",color,1);
    }
    int py=66+(int)((ui->mosaic_time-ui->mosaic_scroll)*scale);
    if(py>=66 && py<376){rect(fb,147,py,484,1,mosaic_color(0));rect(fb,143,py-2,4,5,mosaic_color(0));}
    rect(fb,0,380,640,20,RGB(20,19,25));
    text(fb,10,386,"DBL CLICK: EDIT   SHIFT: ALIGN   WHEEL: SCROLL   CTRL: ZOOM",RGB(133,127,141),1);
    if(rec) {
        double seconds=ui->file_record_rate?(double)ui->file_record_frames/ui->file_record_rate:0;
        snprintf(label,sizeof(label),"REC %05.1fs",seconds);text(fb,552,386,label,RGB(231,122,130),1);
    }
}
