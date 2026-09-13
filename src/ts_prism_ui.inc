/* Subpixel optical geometry inside the native framebuffer. Text and controls
   retain the application's pixel style; the optical paths use coverage blending. */
static void prism_pixel(TsFramebuffer *fb, int x, int y, uint32_t color, float coverage)
{
    if(x<10 || x>=630 || y<96 || y>=250 || coverage<=0)return;
    int opacity=(int)lrintf(fminf(1,coverage)*100);
    fb->pixels[y*TS_UI_WIDTH+x]=blend_color(fb->pixels[y*TS_UI_WIDTH+x],color,opacity);
}

/* Accumulate coverage across a whole curve before compositing. Blending each
   short segment separately leaves dark seams at the shared endpoints. */
typedef struct {
    unsigned char coverage[620*154];
    int x,y,w,h;
} PrismStroke;

static void prism_stroke_begin(PrismStroke *s,float x0,float y0,float x1,float y1)
{
    s->x=(int)fmaxf(10,floorf(x0)-1);s->y=(int)fmaxf(96,floorf(y0)-1);
    s->w=(int)fminf(630,ceilf(x1)+2)-s->x;
    s->h=(int)fminf(250,ceilf(y1)+2)-s->y;
    if(s->w<0)s->w=0;
    if(s->h<0)s->h=0;
    memset(s->coverage,0,(size_t)s->w*s->h);
}

static void prism_stroke_pixel(TsFramebuffer *fb,PrismStroke *s,int x,int y,uint32_t color,float amount)
{
    if(!s){prism_pixel(fb,x,y,color,amount);return;}
    x-=s->x;y-=s->y;
    if(x<0 || y<0 || x>=s->w || y>=s->h)return;
    int i=y*s->w+x,alpha=s->coverage[i]+(int)lrintf(amount*255);
    s->coverage[i]=(unsigned char)(alpha>255?255:alpha);
}

static void prism_stroke_end(TsFramebuffer *fb,const PrismStroke *s,uint32_t color)
{
    for(int y=0;y<s->h;++y)for(int x=0;x<s->w;++x)
        if(s->coverage[y*s->w+x])prism_pixel(fb,s->x+x,s->y+y,color,s->coverage[y*s->w+x]/255.f);
}

static void prism_stroke_line(TsFramebuffer *fb,PrismStroke *stroke,
                              float x0,float y0,float x1,float y1,uint32_t color)
{
    int steep=fabsf(y1-y0)>fabsf(x1-x0);
    if(steep){float t=x0;x0=y0;y0=t;t=x1;x1=y1;y1=t;}
    if(x0>x1){float t=x0;x0=x1;x1=t;t=y0;y0=y1;y1=t;}
    float dx=x1-x0;
    if(dx<.00001f)return;
    float gradient=(y1-y0)/dx;
    for(int x=(int)floorf(x0);x<=(int)ceilf(x1);++x) {
        float first=fmaxf(x-.5f,x0),last=fminf(x+.5f,x1);
        if(last<=first)continue;
        float y=y0+gradient*((first+last)*.5f-x0);
        int iy=(int)floorf(y);float fraction=y-iy,weight=last-first;
        prism_stroke_pixel(fb,stroke,steep?iy:x,steep?x:iy,color,weight*(1-fraction));
        prism_stroke_pixel(fb,stroke,steep?iy+1:x,steep?x:iy+1,color,weight*fraction);
    }
}

static void prism_line(TsFramebuffer *fb,float x0,float y0,float x1,float y1,uint32_t color)
{
    prism_stroke_line(fb,NULL,x0,y0,x1,y1,color);
}

static void prism_curve(TsFramebuffer *fb,float x0,float y0,float x1,float y1,
                         float x2,float y2,float x3,float y3,uint32_t color)
{
    PrismStroke stroke;
    prism_stroke_begin(&stroke,fminf(fminf(x0,x1),fminf(x2,x3)),fminf(fminf(y0,y1),fminf(y2,y3)),
                       fmaxf(fmaxf(x0,x1),fmaxf(x2,x3)),fmaxf(fmaxf(y0,y1),fmaxf(y2,y3)));
    float px=x0,py=y0;
    int steps=(int)ceilf((fabsf(x1-x0)+fabsf(y1-y0)+fabsf(x2-x1)+fabsf(y2-y1)+
                         fabsf(x3-x2)+fabsf(y3-y2))/5);
    if(steps<8)steps=8;
    if(steps>160)steps=160;
    for(int i=1;i<=steps;++i) {
        float t=(float)i/steps,u=1-t;
        float x=u*u*u*x0+3*u*u*t*x1+3*u*t*t*x2+t*t*t*x3;
        float y=u*u*u*y0+3*u*u*t*y1+3*u*t*t*y2+t*t*t*y3;
        prism_stroke_line(fb,&stroke,px,py,x,y,color);px=x;py=y;
    }
    prism_stroke_end(fb,&stroke,color);
}

static void prism_profile(int shape,float t,float *left,float *right)
{
    float bulge=1-t*t;
    switch(shape) {
    case TS_PRISM_PLANO_CONVEX: *left=-3;*right=3+15*bulge;break;
    case TS_PRISM_MENISCUS_POSITIVE: *left=-11+11*bulge;*right=-4+20*bulge;break;
    case TS_PRISM_BICONCAVE: *left=-17+12*bulge;*right=17-12*bulge;break;
    case TS_PRISM_PLANO_CONCAVE: *left=-17+12*bulge;*right=4;break;
    case TS_PRISM_MENISCUS_NEGATIVE: *left=-17+20*bulge;*right=-4+11*bulge;break;
    default: *left=-3-14*bulge;*right=3+14*bulge;break;
    }
}

static void prism_lens_outline(TsFramebuffer *fb,int x,int shape,uint32_t color)
{
    PrismStroke stroke;prism_stroke_begin(&stroke,x-20,110,x+20,224);
    float pl,pr;prism_profile(shape,-1,&pl,&pr);
    prism_stroke_line(fb,&stroke,x+pl,111,x+pr,111,color);
    for(int j=-56;j<56;++j) {
        float l,r,nl,nr;prism_profile(shape,j/56.f,&l,&r);prism_profile(shape,(j+1)/56.f,&nl,&nr);
        for(int px=(int)floorf(x+l)+1;px<x+r;++px)
            prism_pixel(fb,px,167+j,RGB(27,42,49),.75f);
        prism_stroke_line(fb,&stroke,x+pl,167+j,x+nl,168+j,color);
        prism_stroke_line(fb,&stroke,x+pr,167+j,x+nr,168+j,color);pl=nl;pr=nr;
    }
    prism_stroke_line(fb,&stroke,x+pl,223,x+pr,223,color);
    prism_stroke_end(fb,&stroke,color);
}

static void sister_prism_render(TsFramebuffer *fb,const TsSisterUiModel *model)
{
    const TsPrismControls *p=&model->parameters.prism;
    TsPrismView v=model->routing.prism.valid?model->routing.prism:ts_prism_control_view(p);
    const uint32_t background=RGB(7,11,15),ink=RGB(140,179,192),original=RGB(229,232,212);
    const uint32_t colors[12]={RGB(236,224,159),RGB(241,148,108),RGB(239,193,101),
        RGB(195,215,120),RGB(126,215,171),RGB(92,207,212),RGB(106,168,238),
        RGB(159,146,229),RGB(207,139,206),RGB(205,177,122),RGB(187,153,110),RGB(227,188,124)};
    char label[96];
    rect(fb,10,40,620,232,background);
    button(fb,16,48,96,p->enabled?"PRISM ON":"PRISM OFF",p->enabled);
    button(fb,122,48,116,ts_prism_mode_name(p->mode),1);
    snprintf(label,sizeof(label),"%d SHIFTED + 01 BODY",p->lenses-1);text(fb,250,55,label,ink,1);
    text(fb,544,55,v.valid?"LIVE DSP":"SETTINGS",ink,1);
    text(fb,28,85,"SOURCE",ink,1);text(fb,118,85,"INPUT",ink,1);
    text(fb,290,85,"ALTER",ink,1);text(fb,440,85,"OUTPUT",ink,1);text(fb,574,85,"OUT",ink,1);
    prism_lens_outline(fb,148,p->input_shape,RGB(114,181,203));
    prism_lens_outline(fb,462,p->output_shape,RGB(114,181,203));
    int shape[2]={p->input_shape,p->output_shape};
    for(int stage=0;stage<2;++stage) {
        int cx=stage?462:148;
        const char *name=ts_prism_shape_name(shape[stage]);
        text(fb,cx-(int)strlen(name)*3,229,name,ink,1);
        const char *tone=ts_prism_shape_color_name(shape[stage]);
        text(fb,cx-(int)strlen(tone)*3,99,tone,blend_color(background,ink,80),1);
    }
    /* A separate original lane makes Dry Level distinct from the wet anchor. */
    uint32_t dry=blend_color(background,original,(int)lrintf(8+92*fminf(1,v.dry)));
    prism_curve(fb,65,167,82,167,73,242,100,242,dry);
    prism_line(fb,100,242,566,242,dry);
    prism_curve(fb,566,242,592,242,582,167,602,167,dry);
    rect(fb,266,237,86,11,background);
    snprintf(label,sizeof(label),"DRY %03d",(int)lrintf(v.dry*100));text(fb,273,239,label,dry,1);
    prism_line(fb,30,167,148,167,original);
    prism_line(fb,90,163,98,167,original);prism_line(fb,90,171,98,167,original);
    for(int i=TS_PRISM_LENSES-1;i>=0;--i) {
        TsPrismLensView ray=v.lens[i];if(i>=p->lenses && ray.level<=.001f)continue;
        float x,y;ts_sister_ui_prism_point_f(ray,&x,&y);
        float strength=ray.level/(1+ray.level);
        uint32_t color=blend_color(background,colors[i],(int)lrintf(8+strength*(12+80*v.wet)));
        float arrive_y=167+(y-167)*.7f;
        float exit_y=167+(ts_sister_ui_prism_y(ray.cents)-167)*.7f;
        float join_x=548+16*ray.pan;
        prism_curve(fb,148,167,180,167,x-34,y,x,y,color);
        prism_curve(fb,x,y,x+34,y,420,arrive_y,444,arrive_y,color);
        prism_line(fb,444,arrive_y,480,exit_y,color);
        prism_curve(fb,480,exit_y,501,exit_y,join_x-14,167,join_x,167,color);
        prism_line(fb,join_x,167,602,167,color);
        if(i==0) {
            rect(fb,(int)lrintf(x)-2,(int)lrintf(y)-2,5,5,color);
            if(model->prism_selected!=1)text(fb,(int)x-20,(int)y+8,"01 BODY",color,1);
        } else {
            prism_line(fb,x-3,y-3,x+3,y-3,color);prism_line(fb,x+3,y-3,x+3,y+3,color);
            prism_line(fb,x+3,y+3,x-3,y+3,color);prism_line(fb,x-3,y+3,x-3,y-3,color);
        }
        if(model->prism_selected==i+1) {
            if(i==0)snprintf(label,sizeof(label),"01 BODY %+.0FDB",p->trim_db[0]);
            else snprintf(label,sizeof(label),"%02d %+.0FDB",i+1,p->trim_db[i]);
            text(fb,(int)x+7,(int)y-3,label,RGB(240,240,220),1);
        }
        if((p->mute_mask|p->solo_mask)&(1<<i))text(fb,(int)x-3,(int)y+6,(p->mute_mask&(1<<i))?"M":"S",ink,1);
    }
    prism_line(fb,602,167,609,167,original);
    prism_line(fb,601,163,609,167,original);prism_line(fb,601,171,609,167,original);
    text(fb,24,252,"DRAG: PITCH/PAN  WHEEL: TRIM  SHIFT MUTE / CTRL SOLO",ink,1);
    text(fb,24,262,"BIG LENS: CLICK NEXT / RIGHT PREVIOUS  PRESET RIGHT: STOCK",ink,1);
    static const char *const names[8] = {
        "LENSES", "SPREAD", "DRIFT", "FOCUS", "STEREO", "BODY", "WET", "OUTPUT"
    };
    float amounts[8] = {(p->lenses - 2) / 10.f, p->spread / 2, p->drift / 2, p->focus,
                        p->stereo, p->body / 3, p->mix, (p->output_db + 12) / 24};
    for (int i = 0; i < 8; ++i) {
        int x = 16 + (i % 4) * 154, y = 280 + (i / 4) * 28;
        int locked = ts_sister_ui_parameter_locked(model, TS_SISTER_UI_PARAM_PRISM_LENSES + i);
        sister_percent_parameter_state(fb, x, y, 144, names[i], amounts[i],
                                       i==1 || i==2 ? 200 : i==5 ? 300 : 100, colors[i], locked);
        if (i == 0 || i == 7) {
            rect(fb, x + 2, y + 2, 140, 11, RGB(24,23,25));
            if (i == 0) snprintf(label, sizeof(label), "LENSES %02d", p->lenses);
            else snprintf(label, sizeof(label), "OUTPUT %+.1F DB", p->output_db);
            text(fb, x + 3, y + 3, label, locked ? sister_dim_color(colors[i]) : colors[i], 1);
        }
    }
    sister_percent_parameter_state(fb,16,332,144,"DRY LEVEL",p->dry_level/2,200,colors[0],
        ts_sister_ui_parameter_locked(model,TS_SISTER_UI_PARAM_PRISM_DRY));
    sister_percent_parameter_state(fb,170,332,144,"COLOR",p->color,100,colors[5],
        ts_sister_ui_parameter_locked(model,TS_SISTER_UI_PARAM_PRISM_COLOR));
    text(fb,326,338,model->routing.enabled ? "TO TAPE + FX" : "TO FX",ink,1);
    button(fb, TS_SISTER_UI_FX_REC_X, TS_SISTER_UI_FX_REC_Y, TS_SISTER_UI_FX_REC_W,
        model->file_capture_state == TS_PERFORMANCE_FILE_STOPPING ? "FILE WAIT" :
        model->file_capture_state == TS_PERFORMANCE_FILE_RECORDING ? "STOP FILE" : "REC FILE",
        model->file_capture_state == TS_PERFORMANCE_FILE_RECORDING && model->text_cursor_visible);
}
