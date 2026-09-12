/* Mosaic uses the native pixel renderer and the same master strip. Geometry
   is time in seconds vertically; horizontal space carries no audio meaning. */
static uint32_t mosaic_color(uint64_t id)
{
    return active_palette()->colors[TS_PALETTE_MOSAIC_TILE_1+id%5];
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
    if(right>TS_MOSAIC_CANVAS_RIGHT)right=TS_MOSAIC_CANVAS_RIGHT;
    if(bottom>376)bottom=376;
    if(right>x && bottom>y)rect(fb,x,y,right-x,bottom-y,color);
}
static void mosaic_time_label(char *label,size_t size,double time)
{
    if(time<100)snprintf(label,size,"%04.1f",time);
    else if(time<10000)snprintf(label,size,"%04.0f",time);
    else snprintf(label,size,"%3.0fm",time/60);
}
static void mosaic_ruler_label(TsFramebuffer *fb,int y,const char *label)
{
    /* The logical fallback must also clip glyphs at the canvas edges. */
    for(int n=0;label[n];++n) {
        char c=label[n];if(c>='a' && c<='z')c=(char)(c-'a'+'A');
        const char *bits=glyph(c);
        for(int gy=0;gy<7;++gy)if(y+gy>=66 && y+gy<376)
            for(int gx=0;gx<5;++gx)if(bits[gy*5+gx]=='1' && 115+n*6+gx<151)
                rect(fb,115+n*6+gx,y+gy,1,1,RGB(133,127,141));
    }
}
static uint32_t mosaic_mix_color(uint32_t a,uint32_t b,float t)
{
    unsigned r=(unsigned)(((a>>16)&255)*(1-t)+((b>>16)&255)*t);
    unsigned g=(unsigned)(((a>>8)&255)*(1-t)+((b>>8)&255)*t);
    unsigned v=(unsigned)((a&255)*(1-t)+(b&255)*t);
    return 0xff000000u|(r<<16)|(g<<8)|v;
}
static void mosaic_envelope(const TsMosaicSource *s,size_t first,size_t last,float *peak,float *rms)
{
    if(last<=first)last=first+1;
    if(last>s->sample.frames)last=s->sample.frames;
    size_t a=first*TS_MOSAIC_PEAKS/s->sample.frames,b=(last*TS_MOSAIC_PEAKS+s->sample.frames-1)/s->sample.frames;
    if(a>=TS_MOSAIC_PEAKS)a=TS_MOSAIC_PEAKS-1;
    if(b>TS_MOSAIC_PEAKS)b=TS_MOSAIC_PEAKS;if(b<=a)b=a+1;
    double energy=0;*peak=0;
    for(size_t i=a;i<b;++i){*peak=fmaxf(*peak,s->peaks[i]);energy+=(double)s->rms[i]*s->rms[i];}
    *rms=(float)sqrt(energy/(b-a));
}
static void mosaic_card(TsFramebuffer *fb,const TsUiState *ui,const TsMosaicEvent *e,int selected,int dim,int ghost)
{
    if(!e->source)return;
    double horizontal=ui->mosaic_hscale>0?ui->mosaic_hscale:1,scale=ui->mosaic_scale;
    double left=151+(e->x-ui->mosaic_xscroll)*horizontal,width=e->width*horizontal;
    double top=66+(e->start-ui->mosaic_scroll)*scale,bottom=top+e->duration*scale;
    if(left+width<=151 || left>=TS_MOSAIC_CANVAS_RIGHT || width<1 || bottom<=66 || top>=376)return;
    int x=(int)left,w=(int)width,y=(int)fmax(66,top),end=(int)fmin(376,bottom),h=end-y;
    if(h<1)h=1;
    uint32_t color=mosaic_color(e->source->hash),fill=selected?RGB(50,46,59):RGB(32,31,39);
    if(dim){color=mosaic_mix_color(color,RGB(19,20,26),.70f);fill=RGB(23,24,30);}
    if(ghost)color=mosaic_mix_color(color,RGB(16,18,24),.55f);
    if(!ghost)mosaic_canvas_rect(fb,x,y,w,h,fill);
    double duration=(double)(e->last-e->first)/e->source->sample.sample_rate;
    int first_row=(int)fmax(66,top+24),last_row=(int)fmin(376,bottom-14);
    for(int yy=first_row;yy<last_row;++yy) {
        if(ghost && (yy&1))continue;
        double elapsed=fmax(0,ui->mosaic_scroll+(yy-66)/scale-e->start);
        if(!e->looping && elapsed>=duration)continue;
        double phase=fmod(elapsed,duration),span=1/scale;
        size_t first=e->first+(size_t)(phase*e->source->sample.sample_rate);
        size_t last=e->first+(size_t)(fmin(duration,phase+span)*e->source->sample.sample_rate);
        float peak,rms;mosaic_envelope(e->source,first,last,&peak,&rms);
        if(e->looping && phase+span>=duration) {
            float p,r;mosaic_envelope(e->source,e->first,span>=duration?e->last:
                e->first+(size_t)((phase+span-duration)*e->source->sample.sample_rate),&p,&r);
            peak=fmaxf(peak,p);rms=fmaxf(rms,r);
        }
        float half=fmaxf(0,(w-14)*.43f);
        int amplitude=(int)(fminf(1,peak)*half),body=(int)(fminf(1,rms)*half);
        mosaic_canvas_rect(fb,x+w/2-amplitude,yy,1+amplitude*2,1,mosaic_mix_color(color,fill,.50f));
        mosaic_canvas_rect(fb,x+w/2-body,yy,1+body*2,1,color);
        if(e->looping && span<duration && yy>first_row && phase<span)
            mosaic_canvas_rect(fb,x+4,yy,w-8,1,dim?RGB(35,34,41):RGB(72,65,77));
    }
    if(ghost) {
        for(int xx=x;xx<x+w;xx+=6){mosaic_canvas_rect(fb,xx,y,3,1,color);mosaic_canvas_rect(fb,xx,end-1,3,1,color);}
        for(int yy=y;yy<end;yy+=6){mosaic_canvas_rect(fb,x,yy,1,3,color);mosaic_canvas_rect(fb,x+w-1,yy,1,3,color);}
        return;
    }
    mosaic_canvas_rect(fb,x,y,2,h,color);mosaic_canvas_rect(fb,x+w-1,y,1,h,color);
    if(top>=66)mosaic_canvas_rect(fb,x,y,w,1,color);
    if(bottom<376)mosaic_canvas_rect(fb,x,end-1,w,1,color);
    if(selected) {
        uint32_t border=ui->palette.colors[TS_PALETTE_MOSAIC_HIGHLIGHT];
        mosaic_canvas_rect(fb,x,y,2,h,border);mosaic_canvas_rect(fb,x+w-2,y,2,h,border);
        if(top>=66)mosaic_canvas_rect(fb,x,y,w,2,border);
        if(bottom<376)mosaic_canvas_rect(fb,x,end-2,w,2,border);
    }
    char label[100];
    if(top>=66 && h>=14 && x>=151 && w>=24) {
        int chars=((x+w>631?631-x:w)-12)/6;if(chars>24)chars=24;
        snprintf(label,sizeof(label),"%.*s",chars,e->name);text(fb,x+6,y+4,label,color,1);
    }
    if(top>=66 && h>=25 && x>=151 && w>=24) {
        char notes[40]="",note[8];
        for(int n=0;n<e->note_count;++n) {
            size_t used=strlen(notes);snprintf(notes+used,sizeof(notes)-used,"%s%s",n?" ":"",ts_midi_note_name(e->notes[n],note,sizeof(note)));
        }
        int chars=((x+w>631?631-x:w)-12)/6;snprintf(label,sizeof(label),"%.*s",chars,notes);
        text(fb,x+6,y+14,label,dim?RGB(66,64,75):RGB(156,147,165),1);
    }
    if(h>38 && bottom<376 && x>=151 && x+36<631 && w>=36) {
        int chars=((x+w>631?631-x:w)-12)/6;
        const char *state=e->muted?"MUTE":e->solo?"SOLO":dim?(chars>=8?"SOLO OUT":"EXCL"):e->looping?"LOOP":"ONCE";
        snprintf(label,sizeof(label),"%.*s",chars,state);text(fb,x+6,end-11,label,color,1);
    }
}

#include "ts_mosaic_waveform_detail.inc"

static void mosaic_slider(TsFramebuffer *fb,int x,int y,int width,double amount,int center,int enabled)
{
    uint32_t ink=enabled?RGB(129,206,200):RGB(65,62,73);
    rect(fb,x,y+2,width,2,RGB(66,60,77));
    if(center)rect(fb,x+width/2,y,1,6,RGB(185,171,154));
    int at=(int)llround(fmax(0,fmin(1,amount))*(width-1));
    if(enabled)rect(fb,x+at-1,y,3,6,ink);
}
static void mosaic_mix_controls(TsFramebuffer *fb,const TsUiState *ui,int any_solo)
{
    TsMosaicEvent *e=ts_mosaic_find(ui->mosaic,ui->mosaic_selected);
    const char *names[]={"LEVEL","PAN","IN","OUT"};
    for(int i=0;i<4;++i) {
        int x=TS_MOSAIC_MIX_X+i*TS_MOSAIC_MIX_STEP;char label[32];double value=0;
        if(!e)snprintf(label,sizeof(label),"%s --",names[i]);
        else if(i==0) {
            value=e->gain/2;
            if(e->gain<=0)snprintf(label,sizeof(label),"LEVEL -INF");
            else snprintf(label,sizeof(label),"LEVEL %+.1fDB",20*log10(e->gain));
        } else if(i==1) {
            value=(e->pan+1)*.5;
            if(fabsf(e->pan)<.005f)snprintf(label,sizeof(label),"PAN CENTER");
            else snprintf(label,sizeof(label),"PAN %s %.0f%%",e->pan<0?"L":"R",100*fabsf(e->pan));
        } else {
            double seconds=i==2?e->fade_in:e->fade_out;
            value=sqrt(fmin(1,seconds/e->duration));
            snprintf(label,sizeof(label),"%s %.2fS",names[i],seconds);
        }
        text(fb,x,382,label,e?RGB(182,174,190):RGB(97,92,105),1);
        mosaic_slider(fb,x+2,392,TS_MOSAIC_MIX_W-4,value,i<2,e!=NULL);
    }
    if(any_solo)mosaic_button(fb,444,381,94,"CLEAR SOLO",1,1);
    else {
        int count=0;for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
            uint64_t id=ui->mosaic->events[i].id;
            count+=id && (id==ui->mosaic_selected || id==ui->mosaic_selection[i]);
        }
        char label[20];snprintf(label,sizeof(label),"%d SELECTED",count);text(fb,444,386,label,RGB(133,127,141),1);
    }
}

static void mosaic_volume_lane(TsFramebuffer *fb,const TsUiState *ui,double interval)
{
    const TsMosaic *m=ui->mosaic;
    int left=TS_MOSAIC_ENV_X,right=TS_MOSAIC_ENV_RIGHT;
    rect(fb,left-1,66,right-left+2,310,RGB(31,25,38));
    rect(fb,left,67,right-left,308,RGB(11,13,16));
    double end=ts_mosaic_end(m),scale=ui->mosaic_scale>0?ui->mosaic_scale:24;
    for(double t=ceil(ui->mosaic_scroll/interval)*interval;t<ui->mosaic_scroll+310/scale;t+=interval) {
        int y=66+(int)((t-ui->mosaic_scroll)*scale);
        rect(fb,left,y,right-left,1,RGB(28,30,35));
    }
    int previous=-1;
    for(int y=66;y<376;++y) {
        double t=ui->mosaic_scroll+(y-66)/scale;
        if(end<=0 || t>end){previous=-1;continue;}
        int x=left+1+(int)lrintf(ts_mosaic_volume_at(m,t/end)*(right-left-2));
        int a=previous<0?x:previous;
        rect(fb,a<x?a:x,y,abs(x-a)+1,1,RGB(91,206,211));previous=x;
    }
    mosaic_button(fb,left,68,15,"R",0,1);
    mosaic_button(fb,left+23,68,15,"",0,1);
    for(int i=0;i<4;++i){rect(fb,left+30-i,73+i,1,1,RGB(210,199,217));rect(fb,left+30+i,73+i,1,1,RGB(210,199,217));}
    mosaic_button(fb,right-15,68,15,"S",0,1);
    mosaic_button(fb,left,359,15,"/",0,1);
    mosaic_button(fb,right-15,359,15,"V",0,1);
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
    char label[100];
    if(ui->mosaic_source_page<ui->mosaic_bank_count)snprintf(label,sizeof(label),"BANK %02d",ui->mosaic_source_page+1);
    else snprintf(label,sizeof(label),"EVENTS %02d",ui->mosaic_source_page-ui->mosaic_bank_count+1);
    text(fb,12,55,label,RGB(133,127,141),1);
    mosaic_button(fb,118,39,42,ui->mosaic_playing?"PAUSE":"PLAY",ui->mosaic_playing,1);
    mosaic_button(fb,164,39,38,"STOP",0,1);
    int rec=ui->file_record_state==TS_PERFORMANCE_FILE_RECORDING;
    mosaic_button(fb,206,39,64,rec?"STOP REC":"REC FILE",rec,1);
    mosaic_button(fb,274,39,44,"REPEAT",m && m->repeat,1);
    snprintf(label,sizeof(label),"%06.2f s",ui->mosaic_time);text(fb,330,44,label,RGB(129,206,200),1);
    rect(fb,408,42,10,10,RGB(112,105,122));rect(fb,409,43,8,8,RGB(26,26,33));
    if(ui->mosaic_follow){rect(fb,411,45,4,4,RGB(129,206,200));}
    text(fb,424,44,"FOLLOW",ui->mosaic_follow?RGB(129,206,200):RGB(166,155,178),1);
    mosaic_button(fb,490,39,52,"FIT ALL",0,1);mosaic_button(fb,548,39,24,"-",0,1);mosaic_button(fb,578,39,24,"+",0,1);
    static TsWaveformCache source_cache[16];
    (void)instrument;
    for(int i=0;i<16;++i) {
        int x=10+(i%2)*49,y=74+(i/2)*34,n=ui->mosaic_source_offset+i;
        const TsSample *sample=n<ui->mosaic_source_count?ui->mosaic_sources[n].sample:NULL;
        int selected=sample && n==ui->mosaic_source_selected;
        uint32_t color=sample?mosaic_color(ui->mosaic_sources[n].hash):RGB(135,171,174);
        rect(fb,x,y,44,28,selected?ui->palette.colors[TS_PALETTE_MOSAIC_HIGHLIGHT]:sample?RGB(40,36,44):RGB(23,23,28));
        if(selected)rect(fb,x+2,y+2,40,24,RGB(51,47,59));
        snprintf(label,sizeof(label),"%02d",n<16?n+1:n-15);text(fb,x+4,y+4,label,sample?color:RGB(74,70,82),1);
        if(sample && sample->data) {
            TsWaveformRequest request={.sample=sample,.first=0,.last=sample->frames,.width=35,.revision=(m?m->epoch:0)^ui->mosaic_sources[n].hash};
            ts_waveform_cache_prepare(&source_cache[i],&request);
            for(int px=0;px<35;++px) {
                const TsWaveformColumn *column=&source_cache[i].columns[px];
                int a=(int)(fminf(1,fabsf(column->minimum))*5),b=(int)(fminf(1,fabsf(column->maximum))*5);
                rect(fb,x+4+px,y+20-b,1,1+a+b,color);
            }
        }
    }
    int pages=ui->mosaic_source_pages;if(pages<1)pages=1;
    mosaic_button(fb,10,353,22,"<",0,1);mosaic_button(fb,86,353,22,">",0,1);
    snprintf(label,sizeof(label),"%02d/%02d",ui->mosaic_source_page+1,pages);text(fb,40,358,label,RGB(150,142,160),1);
    rect(fb,113,66,518,310,RGB(11,13,16));
    snprintf(label,sizeof(label),strlen(ui->status)>48?"%.45s...":"%s",ui->status);text(fb,118,58,label,RGB(133,127,141),1);
    snprintf(label,sizeof(label),"SPEED %.2fX",m?m->speed:1);text(fb,416,58,label,RGB(129,206,200),1);
    mosaic_slider(fb,TS_MOSAIC_SPEED_X,58,TS_MOSAIC_SPEED_W,(log2(m?m->speed:1)+1)*.5,1,1);
    double scale=ui->mosaic_scale>0?ui->mosaic_scale:24;
    double decade=pow(10,floor(log10(40/scale))),step=40/scale/decade;
    double interval=(step<=1?1:step<=2?2:step<=5?5:10)*decade;
    for(double t=ceil(ui->mosaic_scroll/interval)*interval;t<ui->mosaic_scroll+310/scale;t+=interval) {
        int y=66+(int)((t-ui->mosaic_scroll)*scale);
        mosaic_time_label(label,sizeof(label),t);mosaic_ruler_label(fb,y+2,label);
        rect(fb,147,y,484,1,RGB(28,30,35));
    }
    int any_solo=0;
    if(m)for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id)any_solo|=m->events[i].solo;
    if(!mosaic_detail_cards(fb,ui,any_solo,interval)) {
        /* Keep the logical renderer for disabled/unavailable native surfaces. */
        for(int i=0;i<ui->mosaic_ghost_count;++i)mosaic_card(fb,ui,&ui->mosaic_ghosts[i],0,0,1);
        if(m)for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
            const TsMosaicEvent *e=&m->events[i];if(!e->id)continue;
            mosaic_card(fb,ui,e,e->id==ui->mosaic_selected || e->id==ui->mosaic_selection[i],e->muted || (any_solo && !e->solo),0);
        }
    }
    mosaic_detail_sources(fb,ui,interval);
    if(ui->mosaic_box) {
        int x=ui->mosaic_box_x<ui->mosaic_box_end_x?ui->mosaic_box_x:ui->mosaic_box_end_x;
        int y=ui->mosaic_box_y<ui->mosaic_box_end_y?ui->mosaic_box_y:ui->mosaic_box_end_y;
        int w=abs(ui->mosaic_box_x-ui->mosaic_box_end_x),h=abs(ui->mosaic_box_y-ui->mosaic_box_end_y);
        mosaic_canvas_rect(fb,x,y,w,1,RGB(129,206,200));mosaic_canvas_rect(fb,x,y+h,w,1,RGB(129,206,200));
        mosaic_canvas_rect(fb,x,y,1,h,RGB(129,206,200));mosaic_canvas_rect(fb,x+w,y,1,h,RGB(129,206,200));
    }
    mosaic_volume_lane(fb,ui,interval);
    int py=66+(int)((ui->mosaic_time-ui->mosaic_scroll)*scale);
    if(py>=66 && py<376){rect(fb,147,py,484,1,RGB(129,206,200));rect(fb,143,py-2,4,5,RGB(129,206,200));}
    rect(fb,0,380,640,20,RGB(20,19,25));
    mosaic_mix_controls(fb,ui,any_solo);
    if(rec) {
        double seconds=ui->file_record_rate?(double)ui->file_record_frames/ui->file_record_rate:0;
        snprintf(label,sizeof(label),"REC %05.1fs",seconds);text(fb,552,386,label,RGB(231,122,130),1);
    }
}

void ts_ui_render_mosaic_choice(TsFramebuffer *fb,const TsUiState *ui)
{
    if(!fb || !ui || !ui->mosaic_edit_choice)return;
    for(int i=0;i<TS_UI_WIDTH*TS_UI_HEIGHT;++i)fb->pixels[i]=mosaic_mix_color(fb->pixels[i],RGB(9,10,14),.66f);
    rect(fb,136,131,368,140,RGB(111,95,122));rect(fb,138,133,364,136,RGB(31,28,38));
    text(fb,154,149,"RETURN TO MOSAIC",RGB(224,214,233),1);
    text(fb,154,175,"NEW TILE KEEPS THE ORIGINAL AND ADDS A VARIATION.",RGB(178,163,189),1);
    text(fb,154,192,"UPDATE TILE CHANGES ONLY THIS EVENT.",RGB(178,163,189),1);
    text(fb,154,211,"MOSAIC KEEPS PLAYING WHILE YOU CHOOSE.",RGB(129,174,177),1);
    mosaic_button(fb,148,235,106,"NEW TILE",1,0);
    mosaic_button(fb,264,235,116,"UPDATE TILE",0,0);
    mosaic_button(fb,390,235,102,"KEEP EDITING",0,0);
}
