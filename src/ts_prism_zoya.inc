/* A small visual manifestation, not a second signal model. Only the brief
   activation story uses UI time. Settled registration reads the same live lens
   snapshot as the optics; no idle oscillator, particle simulation or textures. */
#include "ts_prism_zoya_points.inc"

static float prism_zoya_unit(float x)
{
    return x<0 ? 0 : x>1 ? 1 : x;
}
static float prism_zoya_ease(float x)
{
    x=prism_zoya_unit(x);return x*x*(3-2*x);
}
static void prism_zoya_pixel(TsFramebuffer *fb,int side,float x,float y,uint32_t color)
{
    int px=(int)lrintf(x),py=(int)lrintf(y);
    /* Reserve every label, glass hit area, numbered lens and control. */
    if(py<108 || py>=230 || px<(side?510:18) || px>=(side?622:122))return;
    rect(fb,px,py,1,1,color);
}

static void prism_zoya_figures(TsFramebuffer *fb,const TsSisterUiModel *m,
                               const TsPrismControls *p,const TsPrismView *v)
{
    if(m->prism_zoya_pose==TS_PRISM_ZOYA_POSE_OFF ||
       !m->prism_zoya.visible || !p->enabled)return;
    int standing=m->prism_zoya_pose==TS_PRISM_ZOYA_POSE_STANDING;
    const TsPrismZoyaPoint *points=standing ? prism_zoya_standing_points :
                                           prism_zoya_meditation_points;
    int point_count=standing ? TS_PRISM_ZOYA_STANDING_POINTS :
                               TS_PRISM_ZOYA_MEDITATION_POINTS;
    const uint32_t background=RGB(7,11,15);
    const uint32_t tones[3]={RGB(108,211,236),RGB(226,122,212),RGB(246,198,139)};
    float focus=prism_zoya_unit(p->focus);
    float body=prism_zoya_unit(v->lens[0].level/3);
    float spread=prism_zoya_unit(p->spread*.5f)*(1-focus);
    float motion[TS_PRISM_LENSES]={0};
    if(v->valid) {
        /* Subtract the stationary centers, leaving DSP wandering/settling.
           During A/B, use both drift-free endpoints at the live morph position. */
        TsPrismControls still=*p;still.drift=still.a.drift=still.b.drift=0;
        still.morph=v->morph;
        TsPrismView center=ts_prism_control_view(&still);
        for(int i=0;i<p->lenses;++i) {
            float delta=v->lens[i].refraction_cents-center.lens[i].refraction_cents;
            motion[i]=(1-focus)*7*delta/(45+fabsf(delta));
        }
    }
    int intro=m->prism_zoya.introducing;
    float time=m->prism_zoya.elapsed_ms/1000.f;
    float dissolve=intro ? prism_zoya_ease((time-1.1f)/.7f)*(1-prism_zoya_ease((time-2.1f)/1.3f)) : 0;
    for(int side=0;side<2;++side) {
        float form=!intro ? 1 : side ? prism_zoya_ease((time-2.25f)/1.05f) : prism_zoya_ease(time/.85f);
        if(form<=0)continue;
        float coherent=!intro ? 0 : side ?
            prism_zoya_ease((time-2.6f)/.4f)*(1-prism_zoya_ease((time-3.1f)/.5f)) :
            prism_zoya_ease((time-.55f)/.3f)*(1-prism_zoya_ease((time-1.05f)/.6f));
        float density=(64+14*body+16*focus+22*coherent)*form;
        if(!side)density*=1-.48f*dissolve;
        float strength=(side ? .31f+.20f*prism_zoya_unit(v->wet) :
                                  .36f+.12f*prism_zoya_unit(v->dry))+.18f*coherent;
        int echoes=side && spread>.06f ? 2 : 0;
        /* Two decimated echoes maximum; never one character per lens. */
        for(int copy=echoes;copy>=0;--copy)for(int i=0;i<point_count;++i) {
            const TsPrismZoyaPoint *point=&points[i];
            uint32_t hash=sister_spirit_hash(point->x+137,point->y+31);
            if((hash%100u)>=density || (copy && (hash&3u)))continue;
            float mass=point->density/255.f;
            int lens=(point->region*3+(int)((hash>>9)%3u))%p->lenses;
            float wander=side ? motion[lens] : 0;
            float x=side ? 610-point->x : 22+point->x;
            /* In meditation, a fixed, modest lift makes
               OUT feel suspended without adding an idle bob or new clock. */
            float y=110+point->y-(side && !standing?10:0);
            x+=wander*((hash>>12)&1u?1:-1);
            y+=wander*.4f*((hash>>13)&1u?1:-1);
            if(copy) {
                x+=(copy==1?1:-1)*(2+5*spread);
                y+=(copy==1?-1:1)*2*spread;
            }
            if(intro) {
                if(side) {
                    x=602+(x-602)*form;
                    y=167+(y-167)*form;
                } else {
                    x+=(1-form)*((int)((hash>>16)%31u)-22);
                    y+=(1-form)*((int)((hash>>21)%35u)-17);
                    if((hash%5u)==0 && time>1.1f && time<2.05f) {
                        float depart=prism_zoya_ease((time-1.1f)/.95f);
                        x+=(148-x)*depart;y+=(167-y)*depart;
                    }
                }
            }
            /* Warm dense matter, cool diffuse matter: volume, never contours.
               Color adds chromatic variation to the transformed manifestation. */
            int tone=mass>.57f ? 2 : 0;
            if(((hash>>18)%100u)<(unsigned)(12+side*prism_zoya_unit(p->color)*60))
                tone=1;
            if(copy)tone=copy==1?0:1;
            float gain=strength*form*(.18f+1.12f*mass)*(copy ? .28f*spread : 1);
            /* Focus resolves the dense body by diminishing its diffuse fringe. */
            gain*=1-focus*.75f*(1-prism_zoya_unit(mass*2));
            gain*=.78f+.44f*((hash>>24)&255u)/255.f;
            uint32_t color=blend_color(background,tones[tone],(int)lrintf(100*gain));
            prism_zoya_pixel(fb,side,x,y,color);
        }
    }
}

static void prism_zoya_bezier(float t,float x0,float y0,float x1,float y1,
                              float x2,float y2,float x3,float y3,float *x,float *y)
{
    float u=1-t;
    *x=u*u*u*x0+3*u*u*t*x1+3*u*t*t*x2+t*t*t*x3;
    *y=u*u*u*y0+3*u*u*t*y1+3*u*t*t*y2+t*t*t*y3;
}

/* At most six lanes, two short packets each, using the EXISTING curve geometry.
   This 3.6 s introduction is a visual metaphor, never a latency/scope display. */
static void prism_zoya_ray(TsFramebuffer *fb,const TsSisterUiModel *m,int lens,int count,
                           float x,float y,float arrive_y,float exit_y,float join_x,uint32_t color)
{
    if(m->prism_zoya_pose==TS_PRISM_ZOYA_POSE_OFF ||
       !m->prism_zoya.visible || !m->prism_zoya.introducing)return;
    int stride=(count+5)/6;
    if(stride<1)stride=1;
    if(lens%stride)return;
    for(int packet=0;packet<2;++packet) {
        float t=(m->prism_zoya.elapsed_ms-1150.f-lens*8.f-packet*140.f)/1550.f;
        if(t<0 || t>1)continue;
        float px,py;
        if(t<.08f) { if(lens)continue;px=109+39*t/.08f;py=167; }
        else if(t<.42f)prism_zoya_bezier((t-.08f)/.34f,148,167,180,167,x-34,y,x,y,&px,&py);
        else if(t<.72f)prism_zoya_bezier((t-.42f)/.30f,x,y,x+34,y,420,arrive_y,444,arrive_y,&px,&py);
        else if(t<.80f) { float u=(t-.72f)/.08f;px=444+36*u;py=arrive_y+(exit_y-arrive_y)*u; }
        else if(t<.95f)prism_zoya_bezier((t-.80f)/.15f,480,exit_y,501,exit_y,join_x-14,167,join_x,167,&px,&py);
        else {px=join_x+(602-join_x)*(t-.95f)/.05f;py=167;}
        /* Draw before lens handles so the established editing affordances win. */
        rect(fb,(int)lrintf(px),(int)lrintf(py),2,1,
            blend_color(RGB(7,11,15),color,packet?45:75));
    }
}
