/* Uses the same fixed-pixel drawing and palette as the main instrument. */
static void spatial_line(TsFramebuffer *fb,int x0,int y0,int x1,int y1,uint32_t color)
{
    int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,e=dx+dy;
    for(;;){rect(fb,x0,y0,1,1,color);if(x0==x1&&y0==y1)break;int e2=e*2;if(e2>=dy){e+=dy;x0+=sx;}if(e2<=dx){e+=dx;y0+=sy;}}
}
static void spatial_circle(TsFramebuffer *fb,int cx,int cy,int r,uint32_t color)
{
    for(int i=0;i<96;++i){float a=(float)(i*2*3.14159265358979323846/96),b=(float)((i+1)*2*3.14159265358979323846/96);
        spatial_line(fb,cx+(int)(r*sinf(a)),cy-(int)(r*cosf(a)),cx+(int)(r*sinf(b)),cy-(int)(r*cosf(b)),color);}
}
static void spatial_grid(TsFramebuffer *fb,int x,int y)
{
    uint32_t grid=RGB(55,52,62);
    spatial_circle(fb,x,y,80,grid);spatial_circle(fb,x,y,40,grid);
    spatial_line(fb,x-84,y,x+84,y,grid);spatial_line(fb,x,y-84,x,y+84,grid);
    text(fb,x-15,y-96,"FRONT",PAL_TEXT,1);
    text(fb,x-12,y+62,"REAR",RGB(140,137,146),1);
}
static void spatial_bar(TsFramebuffer *fb,int x,int y,int width,float base,float live,uint32_t color)
{
    rect(fb,x,y,width,8,RGB(16,16,22));
    int b=(int)(fmaxf(0,fminf(1,base))*(width-1));
    int l=(int)(fmaxf(0,fminf(1,live))*(width-1));
    rect(fb,x,y,width,1,RGB(66,60,71));rect(fb,x+b-1,y-2,3,12,PAL_TEXT);
    rect(fb,x+l,y,2,8,color);
}
static void spatial_speakers(TsFramebuffer *fb,const TsSpatialUi *m,int cx,int cy)
{
    const TsSpatialControls *c=&m->controls;
    spatial_grid(fb,cx,cy);
    int l=c->stereo_pair,r=(l+1)%c->speakers;
    if(!c->enabled){
        float a=c->azimuth[l]*3.14159265358979323846/180,b=c->azimuth[r]*3.14159265358979323846/180;
        spatial_line(fb,cx-(int)(80*sinf(a)),cy-(int)(80*cosf(a)),
            cx-(int)(80*sinf(b)),cy-(int)(80*cosf(b)),PAL_EFFECT);
    }
    for(int i=0;i<c->speakers;++i){
        float a=c->azimuth[i]*3.14159265358979323846/180;
        int x=cx-(int)(80*sinf(a)),y=cy-(int)(80*cosf(a));
        uint32_t color=m->speaker==i?PAL_MOUSE:PAL_TEXT;
        if(!c->enabled&&(i==l||i==r))color=PAL_EFFECT;
        if(m->view.test_speaker==i)color=PAL_NOTE;
        frame(fb,x-9,y-8,18,16,RGB(25,24,30),color);
        char label[16];snprintf(label,sizeof(label),"%d",i+1);text(fb,x-(i<9?3:6),y-3,label,color,1);
        snprintf(label,sizeof(label),"OUT %d",c->output[i]+1);
        text(fb,x-(int)(strlen(label)*3),y+13,label,RGB(153,150,160),1);
        int level=(int)(fminf(1,m->view.peak[i])*24);
        if(level)rect(fb,x-12,y-13,level,2,PAL_WAVE_LEFT);
    }
}
int ts_spatial_ui_parameter(int x,int y)
{ return x>=12&&x<628&&y>=273&&y<330?(x-12)/125:-1; }
void ts_spatial_ui_render(TsFramebuffer *fb,const TsSpatialUi *m,const TsPalette *palette)
{
    render_palette=palette;
    const TsSpatialControls *c=&m->controls;const TsSpatialView *v=&m->view;
    uint32_t dim=RGB(158,150,165),edge=RGB(57,50,63);char label[112];
    rect(fb,0,0,640,400,RGB(24,21,29));frame(fb,6,6,628,388,RGB(30,26,34),edge);
    text(fb,18,17,"AMBISONICS",PAL_NOTE,1);
    button(fb,208,9,96,c->enabled?"ENABLED":"STEREO",c->enabled);
    snprintf(label,sizeof(label),"STEREO %d-%d",c->stereo_pair+1,(c->stereo_pair+1)%c->speakers+1);
    button(fb,312,9,130,label,0);button(fb,450,9,124,m->learn?"LEARN ON":"MIDI LEARN",m->learn);
    button(fb,586,9,40,"X",0);
    button(fb,12,40,96,"FIELD",m->page==0);button(fb,114,40,96,"ARRAY",m->page==1);button(fb,216,40,96,"MOTION",m->page==2);
    snprintf(label,sizeof(label),"%d SPEAKERS / FOA / %u OUTPUTS",c->speakers,v->channels);
    text(fb,336,48,label,dim,1);
    if(m->page==0){
        spatial_grid(fb,160,164);spatial_speakers(fb,m,480,164);
        for(int i=0;i<48;++i){float a=(float)(i*2*3.14159265358979323846/48),f[4]={.70710678f,cosf(a),sinf(a),0};
            ts_spatial_transform(f,v->value,c->transform);
            float denom=fmaxf(.05f,1.41421356f*fabsf(f[0]));
            float px=fmaxf(-1.2f,fminf(1.2f,f[2]/denom)),py=fmaxf(-1.2f,fminf(1.2f,f[1]/denom));
            rect(fb,159-(int)(72*px),163-(int)(72*py),2,2,PAL_EFFECT);
        }
        for(int ch=0;ch<(c->mono?1:2);++ch){
            float a=c->mono?0:(ch?-1.f:1.f)*v->value[TS_SPATIAL_SPREAD]*3.14159265358979323846;
            float f[4]={.70710678f,cosf(a),sinf(a),0};ts_spatial_transform(f,v->value,c->transform);
            float d=fmaxf(.05f,1.41421356f*fabsf(f[0]));
            int x=160-(int)(70*fmaxf(-1.2f,fminf(1.2f,f[2]/d))),y=164-(int)(70*fmaxf(-1.2f,fminf(1.2f,f[1]/d)));
            rect(fb,x-3,y-3,7,7,ch?PAL_WAVE_RIGHT:PAL_WAVE_LEFT);
            text(fb,x+7,y-3,c->mono?"M":ch?"R":"L",PAL_TEXT,1);
        }
        float a=(c->value[TS_SPATIAL_AIM]-.5f)*2*3.14159265358979323846;
        int bx=160-(int)(72*c->value[TS_SPATIAL_AMOUNT]*sinf(a)),by=164-(int)(72*c->value[TS_SPATIAL_AMOUNT]*cosf(a));
        spatial_circle(fb,bx,by,4,dim);
        a=(v->value[TS_SPATIAL_AIM]-.5f)*2*3.14159265358979323846;
        int ax=160-(int)(72*v->value[TS_SPATIAL_AMOUNT]*sinf(a)),ay=164-(int)(72*v->value[TS_SPATIAL_AMOUNT]*cosf(a));
        spatial_line(fb,160,164,ax,ay,PAL_MOUSE);rect(fb,ax-2,ay-2,5,5,PAL_MOUSE);
        button(fb,14,245,100,c->mono?"MONO":"STEREO IN",c->mono);
        button(fb,122,245,110,ts_spatial_transform_name(c->transform),0);
        text(fb,339,250,"SPEAKERS STAY FIXED / FIELD MOVES",dim,1);
        for(int i=0;i<5;++i){int x=12+125*i;
            text(fb,x,278,ts_spatial_parameter_name(i),m->learn?PAL_MOUSE:PAL_TEXT,1);
            if(i==0||i==2)snprintf(label,sizeof(label),"%+.1F DEG",(c->value[i]-.5f)*360);
            else if(i==1)snprintf(label,sizeof(label),"%.1F DEG",c->value[i]*360);
            else snprintf(label,sizeof(label),"%.1F%%",c->value[i]*100);
            text(fb,x,294,label,PAL_EFFECT,1);spatial_bar(fb,x,314,112,c->value[i],v->value[i],PAL_MOUSE);
        }
        text(fb,18,343,"MAP: DRAG DIRECTION/AMOUNT   SHIFT: ROTATE   WHEEL: SPREAD",dim,1);
        text(fb,18,358,"WIDE MARK: BASE     THIN MARK: MODULATED   CTRL+F9: OPEN/CLOSE",dim,1);
    }else if(m->page==1){
        spatial_speakers(fb,m,160,174);
        button(fb,326,80,138,"SPEAKERS - / +",0);
        snprintf(label,sizeof(label),"%s",c->point?"POINT":"FLAT");button(fb,474,80,142,label,0);
        const char *dec[]={"MAX RE","IN PHASE","BASIC"};snprintf(label,sizeof(label),"DECODER: %s",dec[c->decoder]);button(fb,326,116,290,label,0);
        snprintf(label,sizeof(label),"<    SPEAKER %02d    >",m->speaker+1);button(fb,326,157,290,label,0);
        const char *names[]={"HARDWARE OUTPUT","AZIMUTH","TRIM DB","DELAY MS"};
        int sp=m->speaker<c->speakers?m->speaker:0;
        for(int i=0;i<4;++i){int y=204+i*29;text(fb,326,y,names[i],PAL_TEXT,1);
            if(i==0)snprintf(label,sizeof(label),"%d",c->output[sp]+1);
            if(i==1)snprintf(label,sizeof(label),"%+.1F",c->azimuth[sp]);
            if(i==2)snprintf(label,sizeof(label),"%+.1F",c->trim_db[sp]);
            if(i==3)snprintf(label,sizeof(label),"%.2F",c->delay_ms[sp]);
            button(fb,500,y-6,116,label,0);
        }
        button(fb,326,328,140,"TEST 1 SECOND",v->test_speaker==sp);button(fb,474,328,142,"STOP TEST",0);
        text(fb,18,298,"CLICK A SPEAKER TO SELECT",dim,1);
        text(fb,18,314,"NUMBERS RUN AROUND THE ROOM",dim,1);
        text(fb,18,330,"STEREO PAIR: LEFT THEN RIGHT",dim,1);
        text(fb,18,358,"WHEEL: ADJUST   SHIFT: FINE   L/R CLICK: + / -   TEST: -24 DB",dim,1);
    }else{
        text(fb,18,81,"LFO / CONTINUOUS MOTION",PAL_NOTE,1);
        snprintf(label,sizeof(label),"PERIOD %.2F S",1.f/ts_sister_fallout_lfo_hz(c->lfo_rate));text(fb,18,105,label,PAL_TEXT,1);
        spatial_bar(fb,18,123,276,c->lfo_rate,c->lfo_rate,PAL_MOUSE);
        snprintf(label,sizeof(label),"DEPTH %.1F%%",c->lfo_depth*100);text(fb,326,105,label,PAL_TEXT,1);
        spatial_bar(fb,326,123,288,c->lfo_depth,c->lfo_depth,PAL_MOUSE);
        for(int i=0;i<5;++i)button(fb,14+i*124,147,118,ts_spatial_parameter_name(i),(c->lfo_targets&(1u<<i))!=0);
        text(fb,18,190,"RISE / ONE-SHOT OR LOOP",PAL_NOTE,1);
        snprintf(label,sizeof(label),"LENGTH %.2F S",ts_sister_fallout_rise_seconds(c->rise_length));text(fb,18,213,label,PAL_TEXT,1);
        spatial_bar(fb,18,231,276,c->rise_length,c->rise_length,PAL_MOUSE);
        snprintf(label,sizeof(label),"DEPTH %.1F%%",c->rise_depth*100);text(fb,326,213,label,PAL_TEXT,1);
        spatial_bar(fb,326,231,288,c->rise_depth,c->rise_depth,PAL_MOUSE);
        for(int i=0;i<5;++i)button(fb,14+i*124,255,118,ts_spatial_parameter_name(i),(c->rise_targets&(1u<<i))!=0);
        button(fb,14,286,100,c->rise_loop?"LOOP":"ONE SHOT",c->rise_loop);button(fb,122,286,108,"TRIGGER",0);
        spatial_bar(fb,246,293,368,v->rise,v->rise,PAL_EFFECT);
        button(fb,14,324,86,"CAP A",c->captured&1);button(fb,108,324,86,"TO A",0);
        button(fb,202,324,86,"CAP B",c->captured&2);button(fb,296,324,86,"TO B",0);
        snprintf(label,sizeof(label),"MORPH %.1F S",c->morph_seconds);button(fb,392,324,222,label,v->morph_active);
        text(fb,18,358,"CAP STORES BASE FIELD / TO MORPHS / WHEEL MORPH TIME / SHIFT FINE",dim,1);
    }
    const char *status=m->message;
    if(!status[0])status=v->conflict&&c->enabled?"SPATIAL BLOCKED: A SPEAKER OUTPUT IS RESERVED BY THE EXTERNAL INSERT":
        c->enabled&&!v->available?"SPATIAL UNAVAILABLE: CHECK OUTPUT CHANNELS, DUPLICATES AND ARRAY GEOMETRY":
        c->enabled?"MAIN MIX > ENCODE > FIELD TRANSFORM > ARRAY DECODE / FILE OUT: STEREO BEFORE SPATIAL":
        !v->stereo_available?"SELECTED STEREO PAIR UNAVAILABLE / USING HARDWARE OUTPUTS 1-2":
        "AMBISONICS OFF / ORDINARY STEREO ON THE SELECTED SPEAKER PAIR";
    snprintf(label,sizeof(label),"%.101s",status);text(fb,14,379,label,PAL_EFFECT,1);
    render_palette=NULL;
}
