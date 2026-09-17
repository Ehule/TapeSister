/* Cached nebula matter, drawn before the optical diagram. This is a visual
   metaphor, never a waveform, latency display or additional signal model. */
#include "ts_prism_nebula_points.inc"
#include "ts_prism_zoya_points.inc" /* Meditation is only a fleeting apparition. */

static float prism_zoya_unit(float x)
{
    return x<0 ? 0 : x>1 ? 1 : x;
}
static float prism_zoya_ease(float x)
{
    x=prism_zoya_unit(x);return x*x*(3-2*x);
}

typedef struct { int x,y,w,h; } PrismNebulaClear;
typedef struct {
    PrismNebulaClear clear[TS_PRISM_LENSES+2];
    int count;
} PrismNebulaMask;

static int prism_nebula_allowed(const PrismNebulaMask *mask,int x,int y)
{
    if(x<18 || x>=622 || y<108 || y>=229)return 0;
    for(int i=0;i<mask->count;++i) {
        PrismNebulaClear b=mask->clear[i];
        if(x>=b.x && x<b.x+b.w && y>=b.y && y<b.y+b.h)return 0;
    }
    return 1;
}
static void prism_nebula_add(TsFramebuffer *fb,const PrismNebulaMask *mask,
                             int x,int y,uint32_t color,int gain)
{
    if(gain<=0 || !prism_nebula_allowed(mask,x,y))return;
    uint32_t *pixel=&fb->pixels[y*TS_SISTER_UI_WIDTH+x],old=*pixel;
    unsigned r=((old>>16)&255)+(((color>>16)&255)*gain>>8);
    unsigned g=((old>>8)&255)+(((color>>8)&255)*gain>>8);
    unsigned b=(old&255)+((color&255)*gain>>8);
    *pixel=RGB(r>255?255:r,g>255?255:g,b>255?255:b);
}
static void prism_nebula_mote(TsFramebuffer *fb,const PrismNebulaMask *mask,
                              int x,int y,uint32_t color,int gain,int halo)
{
    prism_nebula_add(fb,mask,x,y,color,gain);
    if(!halo)return;
    /* Four low-energy neighbours soften dense matter without a blur pass. */
    int soft=gain/5;
    prism_nebula_add(fb,mask,x-1,y,color,soft);
    prism_nebula_add(fb,mask,x+1,y,color,soft);
    prism_nebula_add(fb,mask,x,y-1,color,soft);
    prism_nebula_add(fb,mask,x,y+1,color,soft);
}
static void prism_nebula_cloud(TsFramebuffer *fb,const PrismNebulaMask *mask,
                               float x,float y,uint32_t color,float gain)
{
    /* Subpixel deposition avoids visible empty rows/columns as a cached field
       stretches. Four taps, fixed work, no offscreen texture or blur. */
    int ix=(int)floorf(x),iy=(int)floorf(y);
    float u=x-ix,v=y-iy;
    prism_nebula_add(fb,mask,ix,iy,color,(int)(gain*(1-u)*(1-v)));
    prism_nebula_add(fb,mask,ix+1,iy,color,(int)(gain*u*(1-v)));
    prism_nebula_add(fb,mask,ix,iy+1,color,(int)(gain*(1-u)*v));
    prism_nebula_add(fb,mask,ix+1,iy+1,color,(int)(gain*u*v));
}
static void prism_zoya_bezier(float t,float x0,float y0,float x1,float y1,
                              float x2,float y2,float x3,float y3,float *x,float *y)
{
    float u=1-t;
    *x=u*u*u*x0+3*u*u*t*x1+3*u*t*t*x2+t*t*t*x3;
    *y=u*u*u*y0+3*u*u*t*y1+3*u*t*t*y2+t*t*t*y3;
}
static void prism_nebula_path(float t,float x,float y,float arrive_y,float exit_y,
                              float join_x,float *px,float *py)
{
    if(t<.08f) {*px=99+49*t/.08f;*py=167;}
    else if(t<.42f)prism_zoya_bezier((t-.08f)/.34f,148,167,180,167,x-34,y,x,y,px,py);
    else if(t<.72f)prism_zoya_bezier((t-.42f)/.30f,x,y,x+34,y,420,arrive_y,444,arrive_y,px,py);
    else if(t<.80f) {float u=(t-.72f)/.08f;*px=444+36*u;*py=arrive_y+(exit_y-arrive_y)*u;}
    else if(t<.95f)prism_zoya_bezier((t-.80f)/.15f,480,exit_y,501,exit_y,join_x-14,167,join_x,167,px,py);
    else {*px=join_x+(572-join_x)*(t-.95f)/.05f;*py=167;}
}

static void prism_zoya_figures(TsFramebuffer *fb,const TsSisterUiModel *m,
                               const TsPrismControls *p,const TsPrismView *v)
{
    const TsPrismZoyaVisual *state=&m->prism_zoya;
    if(m->prism_zoya_pose==TS_PRISM_ZOYA_POSE_OFF || !state->visible || !p->enabled)return;
    float focus=prism_zoya_unit(state->focus),spread=prism_zoya_unit(state->spread*.5f);
    float drift=prism_zoya_unit(state->drift*.5f),body=prism_zoya_unit(state->body/3);
    float color=prism_zoya_unit(state->color),wet=prism_zoya_unit(state->mix);
    float dry=prism_zoya_unit(state->dry),time=state->elapsed_ms/1000.f;
    /* A four-minute continuous cycle keeps precision stable in long sessions.
       Drift changes displacement, not the clock's phase when a knob moves. */
    float age=(state->ambient_ms%240000u)/1000.f;
    PrismNebulaMask mask={0};
    int count=p->lenses;
    if(count<1)count=1;
    if(count>TS_PRISM_LENSES)count=TS_PRISM_LENSES;
    for(int i=0;i<TS_PRISM_LENSES;++i) {
        if(i>=count && v->lens[i].level<=.001f)continue;
        int x,y;ts_sister_ui_prism_point(v->lens[i],&x,&y);
        mask.clear[mask.count++]=(PrismNebulaClear){x-5,y-5,11,18};
    }
    int selected=m->prism_selected-1;
    if(selected>=0 && selected<count) {
        int x,y;ts_sister_ui_prism_point(v->lens[selected],&x,&y);
        mask.clear[mask.count++]=(PrismNebulaClear){x+5,y-5,selected?64:102,11};
    }
    if(selected!=0) {
        int x,y;ts_sister_ui_prism_point(v->lens[0],&x,&y);
        mask.clear[mask.count++]=(PrismNebulaClear){x-22,y+6,46,11};
    }
    float flow_x[8],flow_y[8];
    for(int i=0;i<8;++i) {
        float phase=i*.83f+age*(6.283185307f/240)*(4+i%3);
        flow_x[i]=sinf(phase);flow_y[i]=cosf(phase+i);
    }
    uint32_t palettes[2][256];
    for(int i=0;i<256;++i) {
        /* Warm copper -> amber -> starlight; cool blue -> cyan -> violet/pink. */
        palettes[0][i]=i<128 ? blend_color(RGB(126,29,6),RGB(255,125,26),i*100/128) :
            blend_color(RGB(255,125,26),RGB(255,223,139),(i-128)*100/127);
        uint32_t spectral=i<85 ? blend_color(RGB(41,75,218),RGB(73,204,244),i*100/85) :
            i<170 ? blend_color(RGB(73,204,244),RGB(125,60,226),(i-85)*100/85) :
            blend_color(RGB(125,60,226),RGB(246,78,204),(i-170)*100/85);
        palettes[1][i]=blend_color(RGB(70,119,213),spectral,(unsigned)(35+65*color));
    }
    for(int side=0;side<2;++side) {
        const TsPrismNebulaPoint *points=side?prism_nebula_output:prism_nebula_source;
        int n=side ? (int)(sizeof(prism_nebula_output)/sizeof(*points)) :
                     (int)(sizeof(prism_nebula_source)/sizeof(*points));
        float form=!state->introducing ? 1 : side ? prism_zoya_ease((time-2.05f)/1.4f) : prism_zoya_ease(time/.85f);
        if(form<=0)continue;
        float expansion=side ? 1.04f+.25f*spread-.15f*focus : 1.13f-.07f*focus;
        float motion=(side? .55f+2.2f*drift : .35f+.3f*drift)*(1-.85f*focus);
        float strength=(side ? .30f+.57f*wet : .55f+.20f*dry)*form*(.77f+.32f*body);
        float depart=state->introducing && !side ?
            prism_zoya_ease((time-1.0f)/.6f)*(1-prism_zoya_ease((time-2.0f)/1.2f)):0;
        strength*=1-.25f*depart;
        float cx=side?558:78,cy=168;
        for(int i=0;i<n;++i) {
            const TsPrismNebulaPoint *q=&points[i];
            uint32_t hash=sister_spirit_hash(q->x+91,q->y+113+side*83);
            float mass=q->density/255.f;
            float u=q->x/53.f,w=q->y/62.f;
            int k=side*4;
            float x=cx+q->x*expansion+motion*(flow_x[k]*(1-w*w)+flow_x[k+1]*u*w);
            float y=cy+q->y*(.89f-.10f*focus)+motion*(flow_y[k+2]*(1-u*u)+flow_y[k+3]*u*w);
            if(state->introducing) {
                if(side) {x=482+(x-482)*form;y=167+(y-167)*(.35f+.65f*form);}
                else {
                    x+=(1-form)*((int)((hash>>8)%17)-8);y+=(1-form)*((int)((hash>>16)%17)-8);
                    if((hash&15u)==0) {x+=(148-x)*depart;y+=(167-y)*depart;}
                }
            }
            float density=(.38f+.8f*mass)*strength*(.97f+.03f*flow_x[side*4+1]*q->y/62.f);
            density*=1-focus*.68f*(1-prism_zoya_unit(mass*2));
            if(side)density*=1-.62f*state->apparition;
            int spectrum=side ? (q->spectrum-82)*4 : q->density+25;
            if(side)spectrum+=(int)(8*flow_y[5]*(1-focus));
            if(spectrum<0)spectrum=0;
            if(spectrum>255)spectrum=255;
            uint32_t tone=palettes[side][spectrum];
            if(side && mass>.70f)tone=blend_color(tone,RGB(255,188,115),(unsigned)(prism_zoya_unit((mass-.70f)*3)*100));
            prism_nebula_cloud(fb,&mask,x,y,tone,285*density);
            if(q->density>180 && (hash&31u)==0)
                prism_nebula_mote(fb,&mask,(int)lrintf(x),(int)lrintf(y),RGB(255,224,182),(int)(75*strength),1);
        }
    }
    /* Atmospheric currents share the exact optical paths, with at most six
       lanes. They run behind all glass, lines, handles and numeric readouts. */
    int stride=(count+5)/6;
    for(int lane=0;lane<count;lane+=stride) {
        TsPrismLensView ray=v->lens[lane];float x,y;
        ts_sister_ui_prism_point_f(ray,&x,&y);
        float ay=167+(y-167)*.7f,ey=167+(ts_sister_ui_prism_y(ray.cents)-167)*.7f;
        float join=548+16*ray.pan;
        for(int j=0;j<66;++j) {
            uint32_t hash=sister_spirit_hash(j+37,lane+79);
            float t=j/66.f+age/120;
            t-=floorf(t);
            float intensity=.18f+.12f*wet;
            if(state->introducing) {
                float front=(time-1.05f)/1.6f;
                float distance=front-t;
                if(distance<0 || distance>.24f)continue;
                intensity=(1-distance/.24f)*.9f;
            }
            float px,py;prism_nebula_path(t,x,y,ay,ey,join,&px,&py);
            float fan=(.5f-fabsf(t-.5f))*2;
            py+=((int)((hash>>12)%19)-9)*fan*(.25f+.9f*spread)*(1-.8f*focus);
            px+=((int)((hash>>20)%5)-2)*fan;
            int spectral=(lane*33+j*2)%256;
            uint32_t tone=t<.1f ? palettes[0][190] : palettes[1][spectral];
            prism_nebula_mote(fb,&mask,(int)lrintf(px),(int)lrintf(py),tone,(int)(110*intensity),1);
        }
    }
    if(state->apparition>0) {
        float reveal=prism_zoya_ease(state->apparition);
        for(int i=0;i<TS_PRISM_ZOYA_MEDITATION_POINTS;++i) {
            const TsPrismZoyaPoint *q=&prism_zoya_meditation_points[i];
            uint32_t hash=sister_spirit_hash(q->x+13,q->y+47);
            if((hash&3u)==0)continue;
            float mass=q->density/255.f;
            uint32_t tone=blend_color(RGB(104,129,235),RGB(242,172,229),(unsigned)(mass*100));
            prism_nebula_mote(fb,&mask,610-q->x,100+q->y,tone,(int)(115*reveal*(.2f+mass)),q->density>120);
        }
    }
}
