/* Bounded seed library: 12 immutable motifs, <= 2304 manifestations, one
   <=1280x800 history texture, four 320x200 echoes and a 160x100 veil. No recursion, heap allocation or readback in a
   performance frame. Capture mode is explicitly offline and may read pixels. */
#define M_PI_F 3.14159265358979323846f
enum { M_MOTIFS = 12, M_POINTS = 49, M_INSTANCES = 2304 };
typedef struct { float x, y; } MPoint;
typedef struct { MPoint p[M_POINTS]; int count; } MMotif;
typedef struct {
    float x, y, sx, sy, angle, phase, threshold;
    unsigned motif, color, family;
} MInstance;
typedef struct {
    SDL_Window *window; SDL_Renderer *renderer; SDL_Texture *history, *haze, *veil, *echo[4];
    MMotif motifs[M_MOTIFS]; MInstance instances[M_INSTANCES]; unsigned count;
    TsMandalaConfig config;
    TsMandalaFeatures features;
    TsMandalaEffects effects;
    double echo_at; unsigned echo_write, echo_count;
    int width, height, quality, shed, fullscreen, display, old_x, old_y, old_w, old_h;
    int dirty, diagnostics, controls, frozen;
    Uint32 pointer_time, next_frame;
    double time, cost, max_cost, total_cost; unsigned frames, slow_frames;
    Uint64 rng;
} Mandala;
static float mrand(Mandala *m)
{
    m->rng ^= m->rng >> 12; m->rng ^= m->rng << 25; m->rng ^= m->rng >> 27;
    return (float)((m->rng * UINT64_C(2685821657736338717)) >> 40) / 16777216.f;
}
static float smooth(float x) { x = fmaxf(0, fminf(1, x)); return x*x*(3-2*x); }
static void motif_point(MMotif *m, float x, float y)
{ if (m->count < M_POINTS) m->p[m->count++] = (MPoint){x,y}; }
static void m_instance(Mandala *m, unsigned motif, float x, float y, float sx, float sy, float a, unsigned color, unsigned family)
{
    if (m->count >= M_INSTANCES) return;
    m->instances[m->count++] = (MInstance){x,y,sx,sy,a,mrand(m)*6.2831853f,mrand(m),motif,color,family};
}
static void m_build(Mandala *m, const TsMandalaConfig *config)
{
    m->config = *config; m->rng = config->seed; m->count = 0;
    memset(m->motifs, 0, sizeof(m->motifs));
    for (int k=0;k<M_MOTIFS;++k) {
        MMotif *motive = &m->motifs[k];
        if (k == 0 || k == 5) { /* square / open mechanical bracket */
            static const MPoint square[]={{-1,-1},{1,-1},{1,1},{-1,1},{-1,-1}};
            for (int j=0;j<(k==5?4:5);++j) motif_point(motive,square[j].x,square[j].y);
        } else if (k == 1) {
            motif_point(motive,-1,0); motif_point(motive,1,0);
        } else if (k == 2 || k == 3 || k == 4) {
            int n=k==4?32:24;
            for (int j=0;j<=n;++j) {
                float a=2*M_PI_F*j/n;
                float r=k==2?1:(k==3?.65f+.35f*cosf(3*a):.8f+.14f*sinf(7*a)+.06f*sinf(13*a));
                motif_point(motive,cosf(a)*r,sinf(a)*r);
            }
        } else if (k == 6) {
            for (int j=0;j<=24;++j) {
                float a=-M_PI_F*.5f+M_PI_F*j/24;
                motif_point(motive,cosf(a),sinf(a));
            }
        } else { /* shared fine, imperfect, organic threads */
            float drift=0;
            for(int j=0;j<32;++j) {
                drift=drift*.76f+(mrand(m)-.5f)*.25f;
                float x=j/15.5f-1;
                motif_point(motive,x,sinf(x*(k+1)+k)*.22f+drift);
            }
        }
    }
    unsigned symmetry=config->symmetry;
    /* Concentric segmented machinery: seed changes ring spacing, segmentation,
       facets and open portions, while shared motifs remain cheap. */
    float radius=.18f;
    for (unsigned ring=0;ring<15;++ring) {
        radius += .035f + mrand(m)*.035f;
        unsigned segments=symmetry*(4+ring%7);
        float offset=(ring%2)*M_PI_F/segments;
        unsigned style=(unsigned)(mrand(m)*5);
        for (unsigned j=0;j<segments;++j) {
            float a=2*M_PI_F*j/segments+offset;
            float w=radius*M_PI_F/segments*.82f;
            unsigned motif=style==0?1:style==1?5:style==2?3:0;
            m_instance(m,motif,cosf(a)*radius,sinf(a)*radius,
                .007f+mrand(m)*.016f,w,a,(ring+j/7)%7,0);
            if (ring%3==0) m_instance(m,1,cosf(a)*(radius-.02f),sinf(a)*(radius-.02f),
                .011f+mrand(m)*.014f,.003f,a,0,1);
        }
    }
    /* Architectural satellites and tessellated corner regions. */
    for (unsigned axis=0;axis<symmetry;++axis) {
        float a=2*M_PI_F*axis/symmetry;
        for (unsigned ring=0;ring<4;++ring)
            m_instance(m,2,cosf(a)*1.01f,sinf(a)*1.01f,.05f+ring*.025f,.05f+ring*.025f,a,ring%3,2);
        for (int j=0;j<12;++j) {
            float d=.08f+j*.10f;
            m_instance(m,j%3?1:2,cosf(a)*d,sinf(a)*d,j%3?.035f:.008f,.008f,a,0,2);
        }
    }
    for (int quadrant=0;quadrant<4;++quadrant) {
        float signx=quadrant&1?-1:1,signy=quadrant&2?-1:1;
        for(int x=0;x<13;++x) for(int y=0;y<9;++y) {
            if (mrand(m)<.30f) continue;
            float px=.36f+x*.064f,py=.28f+y*.066f;
            if(px*px+py*py<.63f || px*px+py*py>1.65f)continue;
            float s=.008f+mrand(m)*.022f;
            m_instance(m,0,signx*px,signy*py,s,s,0,1+(unsigned)(mrand(m)*6),3);
        }
        /* Curving memory filaments on reflected wings; shared noise, never particles. */
        for(int j=0;j<24;++j)
            m_instance(m,7+j%5,signx*(.6f+mrand(m)*.4f),signy*(.18f+mrand(m)*.65f),
                .18f+mrand(m)*.2f,.08f+mrand(m)*.10f,signx*signy*(.4f+j*.025f),j%7,4);
    }
    m_instance(m,0,0,0,.73f,.73f,0,0,2);
    m_instance(m,0,0,0,.73f,.73f,M_PI_F/4,1,2);
    for(int i=0;i<4;++i)m_instance(m,2,0,0,.14f+i*.008f,.14f+i*.008f,0,0,2);
    m->dirty=1;
}
static int m_effect_targets(Mandala *m)
{
    if(m->veil)SDL_DestroyTexture(m->veil);
    m->veil=SDL_CreateTexture(m->renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,160,100);
    int ok=m->veil!=NULL;
    if(m->veil)SDL_SetTextureBlendMode(m->veil,SDL_BLENDMODE_BLEND);
    for(int i=0;i<4;++i) {
        if(m->echo[i])SDL_DestroyTexture(m->echo[i]);
        m->echo[i]=SDL_CreateTexture(m->renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,320,200);
        if(!m->echo[i])ok=0;
        else {
            SDL_SetTextureBlendMode(m->echo[i],SDL_BLENDMODE_BLEND);
            SDL_SetRenderTarget(m->renderer,m->echo[i]);SDL_SetRenderDrawColor(m->renderer,4,7,10,255);SDL_RenderClear(m->renderer);
        }
    }
    m->echo_write=m->echo_count=0;m->echo_at=0;
    SDL_SetRenderTarget(m->renderer,NULL);return ok;
}
static int m_target(Mandala *m)
{
    if (m->history)SDL_DestroyTexture(m->history);
    int q=m->quality-m->shed;if(q<0)q=0;
    m->width=(int[]){768,960,1280}[q];
    m->height=(int[]){480,600,800}[q];
    if(m->shed && q==0) { m->width=m->shed==1?640:512; m->height=m->width*5/8; }
    m->history=SDL_CreateTexture(m->renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,m->width,m->height);
    if(!m->history)return 0;
    SDL_SetTextureBlendMode(m->history,SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(m->renderer,m->history);SDL_SetRenderDrawColor(m->renderer,4,7,10,255);SDL_RenderClear(m->renderer);
    SDL_SetRenderTarget(m->renderer,NULL);m->dirty=1;return m_effect_targets(m);
}
static void m_haze(Mandala *m)
{
    if(m->haze)SDL_DestroyTexture(m->haze);
    m->haze=NULL;
    /* One tiny cached soft sprite, reused for every halo. */
    SDL_Surface *haze=SDL_CreateRGBSurfaceWithFormat(0,64,64,32,SDL_PIXELFORMAT_ARGB8888);
    if(haze) {
        for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
            float dx=(x-31.5f)/31.5f,dy=(y-31.5f)/31.5f;
            float fall=fmaxf(0,1-dx*dx-dy*dy);
            ((Uint32*)haze->pixels)[y*64+x]=((Uint32)(fall*fall*32)<<24)|0xADA27E;
        }
        m->haze=SDL_CreateTextureFromSurface(m->renderer,haze);
        if(m->haze)SDL_SetTextureBlendMode(m->haze,SDL_BLENDMODE_BLEND);
        SDL_FreeSurface(haze);
    }
}
static int m_init(Mandala *m, const TsMandalaConfig *config, int quality)
{
    memset(m,0,sizeof(*m));m->quality=quality;m->display=0;m->controls=0;
    m->window=SDL_CreateWindow("TapeSister - Impermanent Mandala",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1152,720,
        SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
    if(!m->window)return 0;
    SDL_SetWindowMinimumSize(m->window,480,300);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"linear");
    m->renderer=SDL_CreateRenderer(m->window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_TARGETTEXTURE);
    if(!m->renderer)m->renderer=SDL_CreateRenderer(m->window,-1,SDL_RENDERER_SOFTWARE|SDL_RENDERER_TARGETTEXTURE);
    if(!m->renderer)return 0;
    SDL_SetRenderDrawBlendMode(m->renderer,SDL_BLENDMODE_BLEND);
    m_build(m,config);
    m_haze(m);
    return m_target(m);
}
static void m_destroy(Mandala *m)
{
    if(m->veil)SDL_DestroyTexture(m->veil);
    for(int i=0;i<4;++i)if(m->echo[i])SDL_DestroyTexture(m->echo[i]);
    if(m->history)SDL_DestroyTexture(m->history);
    if(m->haze)SDL_DestroyTexture(m->haze);
    if(m->renderer)SDL_DestroyRenderer(m->renderer);
    if(m->window)SDL_DestroyWindow(m->window);
}
static void m_scene(Mandala *m, double dt)
{
    static const Uint8 colors[3][7][3]={
        {{213,178,112},{126,163,156},{141,114,150},{181,158,112},{73,112,116},{150,118,90},{92,95,120}},
        {{156,185,197},{155,142,191},{108,158,170},{194,180,160},{74,109,131},{134,147,182},{77,99,114}},
        {{199,148,112},{153,111,110},{131,146,121},{199,183,135},{102,105,88},{159,124,147},{112,89,103}}
    };
    m->time+=dt;
    float level=m->features.level;
    const TsMandalaEffects *fx=&m->effects;
    float distortion=fx->wet[TS_MANDALA_DISTORTION]*(.3f+.7f*fx->size[TS_MANDALA_DISTORTION]);
    float grain=fx->wet[TS_MANDALA_GRAIN],reverb=fx->wet[TS_MANDALA_REVERB],delay=fx->wet[TS_MANDALA_DELAY];
    float prism=fx->wet[TS_MANDALA_PRISM];
    float activity=fminf(1,level*2);distortion*=activity;grain*=activity;reverb*=activity;delay*=activity;prism*=activity;
    float cycle=(float)(m->time/m->config.cycle_seconds*2*M_PI_F);
    float era=.5f+.5f*sinf(cycle-M_PI_F*.5f);
    float age=smooth((float)m->time/18);
    float presence=age*(.35f+.65f*era)*fminf(1,level+.3f*m->features.onset);
    SDL_SetRenderTarget(m->renderer,m->history);
    SDL_SetRenderDrawBlendMode(m->renderer,SDL_BLENDMODE_BLEND);
    /* Frame-rate-independent temporal trace, bounded by one texture. */
    Uint8 fade=(Uint8)fmax(1,fmin(255,255*(1-exp(-dt/(level>.01f?.55+reverb*fx->character[TS_MANDALA_REVERB]*2:1.6)))));
    SDL_SetRenderDrawColor(m->renderer,4,7,10,fade);SDL_RenderFillRect(m->renderer,NULL);
    /* Combine the three time-spaced echoes at 160x100, then one internal-size
       copy. Avoid three large alpha-blended passes on a software renderer. */
    if(delay>.005f && m->echo_count) {
        SDL_SetRenderTarget(m->renderer,m->veil);SDL_SetRenderDrawColor(m->renderer,0,0,0,255);SDL_RenderClear(m->renderer);
        /* Sum of tap gains stays below the history loss, even with long reverb
           traces: no positive-feedback bloom or frame-rate-dependent buildup. */
        for(unsigned j=0;j<3 && j<m->echo_count;++j) {
            SDL_Texture *echo=m->echo[(m->echo_write+3-j)%4];
            float expansion=1+(.016f+.06f*fx->size[TS_MANDALA_DELAY])*(j+1);
            SDL_FRect dst={(160-160*expansion)*.5f,(100-100*expansion)*.5f,160*expansion,100*expansion};
            SDL_SetTextureAlphaMod(echo,(Uint8)(fade*.45f*delay*(.3f+.7f*fx->character[TS_MANDALA_DELAY])/(j+1)));
            SDL_RenderCopyF(m->renderer,echo,NULL,&dst);
        }
        SDL_SetRenderTarget(m->renderer,m->history);SDL_SetTextureAlphaMod(m->veil,255);
        SDL_SetTextureBlendMode(m->veil,SDL_BLENDMODE_ADD);SDL_RenderCopy(m->renderer,m->veil,NULL,NULL);
        SDL_SetTextureBlendMode(m->veil,SDL_BLENDMODE_BLEND);
    }
    float scale=m->height*.46f,cx=m->width*.5f,cy=m->height*.5f;
    if(m->haze && presence>.01f) {
        SDL_SetTextureAlphaMod(m->haze,(Uint8)(presence*(42+110*reverb)));
        for(int i=0;i<12;++i) {
            float a=i*M_PI_F/6,r=.4f+.18f*sinf(cycle+i);
            float fog=.24f+reverb*(.04f+.08f*fx->size[TS_MANDALA_REVERB]);
            SDL_FRect dst={cx+cosf(a)*scale*r-scale*fog,cy+sinf(a)*scale*r-scale*fog,scale*fog*2,scale*fog*2};
            SDL_RenderCopyF(m->renderer,m->haze,NULL,&dst);
        }
    }
    unsigned budget=(unsigned[]){1152,1792,2304}[m->quality];
    if(m->shed)budget=m->shed==1?budget*2/3:budget/2;
    /* Reserve draw cost for the extra refraction strokes. */
    if(prism>.01f)budget=budget*4/5;
    /* Evenly thin the whole layout, preserving outer architecture at Economy. */
    for(unsigned i=0;i<m->count;++i) {
        if((i*budget/m->count)==((i+1)*budget/m->count) && budget<m->count)continue;
        const MInstance *inst=&m->instances[i];
        float threshold=inst->threshold*.78f;
        float life=smooth((presence-threshold+.18f)*4);
        if(inst->family==2)life=level*(.25f+.4f*age);
        if(inst->family==1)life*=.6f+.4f*m->features.peak;
        if(inst->family==4)life*=.26f;
        float drift=sinf((float)m->time*.31f+inst->phase);
        life*=1-distortion*.55f*smooth(inst->threshold+drift*.16f);
        if(life<.006f)continue;
        const Uint8 *rgb=colors[m->config.palette][inst->color];
        Uint8 alpha=(Uint8)fminf(200,(22+80*level)*life);
        SDL_SetRenderDrawColor(m->renderer,rgb[0],rgb[1],rgb[2],alpha);
        float breath=1+.012f*sinf(cycle+inst->phase)+.025f*level;
        float rotation=inst->angle+distortion*.2f*drift+(inst->family==4?.04f*sinf((float)m->time*.012f+inst->phase):0);
        float ca=cosf(rotation),sa=sinf(rotation);
        float px=cx+inst->x*scale*breath,py=cy+inst->y*scale*breath;
        if(inst->family==4)px+=m->features.balance*8;
        px+=distortion*scale*.12f*drift*cosf(inst->phase);
        py+=distortion*scale*.12f*drift*sinf(inst->phase);
        float grow=(.75f+.25f*life)*(1+grain*(-.65f+2.1f*fx->size[TS_MANDALA_GRAIN]));
        grow*=1+grain*.2f*sinf((float)m->time*(.4f+1.2f*fx->character[TS_MANDALA_GRAIN])+inst->phase);
        const MMotif *motif=&m->motifs[inst->motif];
        SDL_FPoint points[M_POINTS];
        for(int j=0;j<motif->count;++j) {
            float x=motif->p[j].x*inst->sx*scale*grow;
            float y=motif->p[j].y*inst->sy*scale*grow;
            float split=distortion*scale*.009f*(j%3-1)*drift;
            points[j]=(SDL_FPoint){px+ca*x-sa*y+split,py+sa*x+ca*y-split*.6f};
        }
        if(inst->motif==0 && inst->family!=2) {
            SDL_Vertex face[4];
            SDL_Color color={rgb[0],rgb[1],rgb[2],(Uint8)(alpha/3)};
            for(int j=0;j<4;++j)face[j]=(SDL_Vertex){points[j],color,{0,0}};
            static const int indices[]={0,1,2,0,2,3};
            SDL_RenderGeometry(m->renderer,NULL,face,4,indices,6);
        }
        SDL_RenderDrawLinesF(m->renderer,points,motif->count);
        if(inst->family==0 && inst->motif==0 && life>.3f) {
            /* Muted mosaic faces. A second inset outline reads as worn metal. */
            SDL_SetRenderDrawColor(m->renderer,rgb[0],rgb[1],rgb[2],alpha/3);
            for(int j=0;j<motif->count;++j) {
                points[j].x=px+(points[j].x-px)*.72f;points[j].y=py+(points[j].y-py)*.72f;
            }
            SDL_RenderDrawLinesF(m->renderer,points,motif->count);
        }
        if(prism>.01f && i%4==0) {
            float shift=prism*(3+21*fx->size[TS_MANDALA_PRISM]);
            SDL_SetRenderDrawColor(m->renderer,91,182,177,(Uint8)(alpha*prism*.65f));
            for(int j=0;j<motif->count;++j){points[j].x+=shift;points[j].y-=shift*.38f;}
            SDL_RenderDrawLinesF(m->renderer,points,motif->count);
            SDL_SetRenderDrawColor(m->renderer,173,107,155,(Uint8)(alpha*prism*.5f));
            for(int j=0;j<motif->count;++j){points[j].x-=2*shift;points[j].y+=shift*.76f;}
            SDL_RenderDrawLinesF(m->renderer,points,motif->count);
        }
        if(inst->family==1 && m->features.width>.05f) {
            SDL_SetRenderDrawColor(m->renderer,102,155,174,(Uint8)(alpha*m->features.width*.5f));
            for(int j=0;j<motif->count;++j){points[j].x+=3*m->features.width;points[j].y+=2*m->features.width;}
            SDL_RenderDrawLinesF(m->renderer,points,motif->count);
        }
    }
    float blur=fminf(1,reverb*.7f+distortion*.55f);
    if(blur>.005f) {
        /* Diffuse at bounded internal resolution, not in full-window passes.
           Blend once into the retained image; no extra full-size texture. */
        SDL_SetRenderTarget(m->renderer,m->veil);SDL_RenderCopy(m->renderer,m->history,NULL,NULL);
        SDL_SetRenderTarget(m->renderer,m->history);
        SDL_SetTextureAlphaMod(m->veil,(Uint8)(blur*110));
        SDL_Rect smear={(int)(distortion*4),(int)(distortion*2),m->width,m->height};
        SDL_RenderCopy(m->renderer,m->veil,NULL,&smear);
    }
    if(delay>.005f && m->time>=m->echo_at) {
        SDL_Texture *echo=m->echo[m->echo_write];
        SDL_SetRenderTarget(m->renderer,echo);SDL_RenderCopy(m->renderer,m->history,NULL,NULL);
        m->echo_write=(m->echo_write+1)%4;if(m->echo_count<4)++m->echo_count;
        m->echo_at=m->time+fmax(.08,2*fx->size[TS_MANDALA_DELAY]);
    } else if(delay<=.005f) {m->echo_count=0;m->echo_at=m->time;}
    SDL_SetRenderTarget(m->renderer,NULL);
}
static void m_text(SDL_Renderer *r,int x,int y,const char *text,Uint8 shade)
{
    SDL_SetRenderDrawColor(r,shade,shade,shade,255);
    for(;*text;++text,x+=6){const char *g=ts_ui_glyph(*text);for(int row=0;row<7;++row)for(int col=0;col<5;++col)
        if(g[row*5+col]=='1'){SDL_Rect pixel={x+col,y+row,1,1};SDL_RenderFillRect(r,&pixel);}}
}
static void m_compose(Mandala *m,SDL_Texture *target,int width,int height)
{
    SDL_SetRenderTarget(m->renderer,target);
    SDL_SetRenderDrawColor(m->renderer,4,7,10,255);SDL_RenderClear(m->renderer);
    float scale=fminf((float)width/m->width,(float)height/m->height);
    SDL_Rect destination={(width-(int)(m->width*scale))/2,(height-(int)(m->height*scale))/2,(int)(m->width*scale),(int)(m->height*scale)};
    SDL_RenderCopy(m->renderer,m->history,NULL,&destination);
    if(m->controls) {
        SDL_Rect bar={8,8,width-16,52};SDL_SetRenderDrawColor(m->renderer,12,18,23,240);SDL_RenderFillRect(m->renderer,&bar);
        m_text(m->renderer,20,18,"ESC EXIT FULLSCREEN / CLOSE   F FULLSCREEN   SPACE FREEZE   D DISPLAY   Q QUALITY",196);
        char line[160];snprintf(line,sizeof(line),"%s%s   DISPLAY %d   %s   CLICK TOP-RIGHT TO EXIT",
            (const char*[]){"ECONOMY 15","STANDARD 30","ENHANCED 60"}[m->quality],m->shed?" (REDUCED)":"",
            m->display+1,m->frozen?"FROZEN":"EVOLVING");
        m_text(m->renderer,20,36,line,161);
        m_text(m->renderer,width-30,18,"X",220);
    }
    if(m->diagnostics) {
        char line[120];snprintf(line,sizeof(line),"%u INSTANCES / %.2F MS / %dX%d / I HIDES DIAGNOSTICS",m->count,m->cost,m->width,m->height);
        m_text(m->renderer,20,height-24,line,175);
    }
    m->dirty=0;
}
static void m_present(Mandala *m)
{
    int width,height;SDL_GetRendererOutputSize(m->renderer,&width,&height);
    m_compose(m,NULL,width,height);SDL_RenderPresent(m->renderer);
}
static void m_window_mode(Mandala *m, VisualShared *s, int topology_changed)
{
    int displays=SDL_GetNumVideoDisplays();
    int display=(int)atomic_load(&s->display),full=(int)atomic_load(&s->fullscreen);
    if(topology_changed || display<0 || display>=displays) {
        display=0;full=0;atomic_store(&s->display,0);atomic_store(&s->fullscreen,0);
        SDL_SetWindowFullscreen(m->window,0);
        SDL_SetWindowSize(m->window,960,600);
        SDL_SetWindowPosition(m->window,SDL_WINDOWPOS_CENTERED_DISPLAY(0),SDL_WINDOWPOS_CENTERED_DISPLAY(0));
    }
    if(full && !m->fullscreen) {
        SDL_GetWindowPosition(m->window,&m->old_x,&m->old_y);SDL_GetWindowSize(m->window,&m->old_w,&m->old_h);
    }
    if(full!=m->fullscreen || display!=m->display || topology_changed) {
        SDL_SetWindowFullscreen(m->window,0);
        if(full || display!=m->display)SDL_SetWindowPosition(m->window,SDL_WINDOWPOS_CENTERED_DISPLAY(display),SDL_WINDOWPOS_CENTERED_DISPLAY(display));
        if(full) {
            if(SDL_SetWindowFullscreen(m->window,SDL_WINDOW_FULLSCREEN_DESKTOP)) {full=0;atomic_store(&s->fullscreen,0);}
        } else if(m->fullscreen && !topology_changed && m->old_w>0) {
            SDL_SetWindowSize(m->window,m->old_w,m->old_h);SDL_SetWindowPosition(m->window,m->old_x,m->old_y);
        }
        m->fullscreen=full;m->display=display;m->dirty=1;
    }
}
static void m_event(Mandala *m,VisualShared *s,const SDL_Event *e)
{
    if(e->type==SDL_QUIT || (e->type==SDL_WINDOWEVENT && e->window.event==SDL_WINDOWEVENT_CLOSE))atomic_store(&s->quit,1);
    if(e->type==SDL_DISPLAYEVENT)m_window_mode(m,s,1);
    if(e->type==SDL_RENDER_TARGETS_RESET || e->type==SDL_RENDER_DEVICE_RESET) {
        if(e->type==SDL_RENDER_DEVICE_RESET)m_haze(m);
        if(!m_target(m))atomic_store(&s->quit,1);
    }
    if(e->type==SDL_WINDOWEVENT) {
        m->dirty=1;
        if(e->window.event==SDL_WINDOWEVENT_MOVED && !m->fullscreen) {
            int display=SDL_GetWindowDisplayIndex(m->window);
            if(display>=0){m->display=display;atomic_store(&s->display,(unsigned)display);}
        }
    }
    if(e->type==SDL_MOUSEMOTION || e->type==SDL_MOUSEBUTTONDOWN) {
        m->pointer_time=SDL_GetTicks();m->controls=1;m->dirty=1;SDL_ShowCursor(SDL_ENABLE);
        int width,height;SDL_GetWindowSize(m->window,&width,&height);
        if(e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_LEFT && e->button.x>width-48 && e->button.y<48) {
            if(m->fullscreen)atomic_store(&s->fullscreen,0);else atomic_store(&s->quit,1);
        }
    }
    if(e->type==SDL_KEYDOWN && !e->key.repeat) {
        switch(e->key.keysym.sym) {
            case SDLK_ESCAPE: if(m->fullscreen)atomic_store(&s->fullscreen,0);else atomic_store(&s->quit,1);break;
            case SDLK_f: atomic_fetch_xor(&s->fullscreen,1);break;
            case SDLK_SPACE: atomic_fetch_xor(&s->freeze,1);break;
            case SDLK_q: atomic_store(&s->quality,(atomic_load(&s->quality)+1)%3);break;
            case SDLK_d: {int n=SDL_GetNumVideoDisplays();if(n>0)atomic_store(&s->display,(atomic_load(&s->display)+1)%(unsigned)n);break;}
            case SDLK_i: m->diagnostics=!m->diagnostics;break;
            default: break;
        }
        m->pointer_time=SDL_GetTicks();m->controls=1;m->dirty=1;
    }
}
static int mandala_run(VisualShared *shared,
#ifdef _WIN32
    HANDLE parent
#else
    pid_t parent
#endif
)
{
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER))return 1;
    VisualMessage message={0};
    if(!shared_latest(shared,&message) || !ts_mandala_valid(&message.config)){SDL_Quit();return 1;}
    Mandala *m=calloc(1,sizeof(*m));
    if(!m || !m_init(m,&message.config,(int)atomic_load(&shared->quality))) {
        if(m){m_destroy(m);free(m);}SDL_Quit();return 1;
    }
    /* Force initial display placement even when the preferred display is zero. */
    m->display=-1;m_window_mode(m,shared,0);
    SDL_RendererInfo info;SDL_GetRendererInfo(m->renderer,&info);
    fprintf(stderr,"Mandala renderer: %s, %dx%d, %u instances, %zu bytes model\n",info.name,m->width,m->height,m->count,sizeof(*m));
    atomic_store(&shared->ready,1);
    Uint64 previous=SDL_GetPerformanceCounter();double frequency=(double)SDL_GetPerformanceFrequency();
    Uint32 fresh=SDL_GetTicks();unsigned serial=message.serial;
    while(!atomic_load(&shared->quit)) {
#ifdef _WIN32
        if(!parent || WaitForSingleObject(parent,0)!=WAIT_TIMEOUT)break;
#else
        if(getppid()!=parent)break;
#endif
        SDL_Event event;while(SDL_PollEvent(&event))m_event(m,shared,&event);
        if(shared_latest(shared,&message)) {
            if(ts_mandala_valid(&message.config) && memcmp(&m->config,&message.config,sizeof(m->config)))m_build(m,&message.config);
            if(serial!=message.serial){serial=message.serial;fresh=SDL_GetTicks();}
        }
        int quality=(int)atomic_load(&shared->quality);
        if(quality<0 || quality>2)quality=0;
        if(quality!=m->quality) {m->quality=quality;m->shed=0;m->slow_frames=0;if(!m_target(m))break;}
        m_window_mode(m,shared,0);
        int frozen=atomic_load(&shared->freeze)!=0;
        if(frozen!=m->frozen){m->frozen=frozen;m->dirty=1;}
        Uint32 now=SDL_GetTicks();
        if(m->controls && now-m->pointer_time>2500) {m->controls=0;m->dirty=1;if(m->fullscreen)SDL_ShowCursor(SDL_DISABLE);}
        int hidden=(SDL_GetWindowFlags(m->window)&(SDL_WINDOW_MINIMIZED|SDL_WINDOW_HIDDEN))!=0;
        Uint64 tick=SDL_GetPerformanceCounter();double dt=fmin(.25,(tick-previous)/frequency);
        int due=(Sint32)(now-m->next_frame)>=0;
        if(m->frozen || hidden)previous=tick;
        if(due && !m->frozen && !hidden) {
            ts_mandala_analyze(now-fresh<250?&message.audio:NULL,&m->features,(float)dt);
            ts_mandala_effects_follow(&m->effects,now-fresh<250?&message.effects:NULL,(float)dt);
            m_scene(m,dt);m->dirty=1;previous=tick;
        }
        int rendered=due && m->dirty && !hidden;
        if(rendered)m_present(m);
        double ms=(SDL_GetPerformanceCounter()-tick)*1000/frequency;
        if(rendered) {
            m->cost=m->cost*.9+ms*.1;m->max_cost=fmax(m->max_cost,ms);m->total_cost+=ms;++m->frames;
        }
        int fps=(int[]){15,30,60}[m->quality];if(m->shed)fps=m->shed==1?15:8;
        if(rendered && !m->frozen && ms>500.0/fps)++m->slow_frames;else if(rendered && m->slow_frames)m->slow_frames--;
        if(m->slow_frames>15 && m->shed<2) {++m->shed;m->slow_frames=0;if(!m_target(m))break;}
        /* No catch-up frames; a slow present only lowers visual cadence. Freeze
           sleeps on events and services control IPC at 10 Hz without drawing. */
        if(due)m->next_frame=now+(unsigned)((m->frozen||hidden)?100:ceil(1000.0/fps));
        int delay=(int)(m->next_frame-SDL_GetTicks());if(delay<1)delay=1;if(delay>100)delay=100;
        if(SDL_WaitEventTimeout(&event,delay))m_event(m,shared,&event);
    }
    atomic_store(&shared->quit,1);atomic_store(&shared->ready,0);
    fprintf(stderr,"Mandala frames=%u mean=%.3fms max=%.3fms shed=%d\n",m->frames,m->frames?m->total_cost/m->frames:0,m->max_cost,m->shed);
    m_destroy(m);free(m);SDL_Quit();return 0;
}
static int mandala_capture(int argc,char **argv)
{
    /* Deterministic diagnostic capture of this exact renderer, synthetic audio
       features; never entered by a normal performance. */
    if(argc<3)return 2;
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER))return 1;
    TsMandalaConfig config;ts_mandala_default(&config);
    config.seed=argc>3?strtoull(argv[3],NULL,0):12345;
    if(!config.seed)config.seed=12345;
    int frames=argc>4?atoi(argv[4]):900;if(frames<1||frames>100000)frames=900;
    Mandala *m=calloc(1,sizeof(*m));int ok=m && m_init(m,&config,1);
    TsMandalaEffects effects={0};
    int effect=argc>5?atoi(argv[5]):-1;
    for(int j=0;j<TS_MANDALA_EFFECTS;++j)if(effect==j || effect==5) {
        effects.wet[j]=effect==5?.7f:1;effects.size[j]=.8f;effects.character[j]=.7f;
    }
    double frequency=(double)SDL_GetPerformanceFrequency(),sum=0,max=0;
    if(ok) {
        for(int i=0;i<frames;++i) {
            m->features=(TsMandalaFeatures){.76f+.15f*sinf(i*.017f),.8f,.6f,.15f*sinf(i*.01f),.12f};
            ts_mandala_effects_follow(&m->effects,&effects,1.0f/15);
            Uint64 before=SDL_GetPerformanceCounter();m_scene(m,1.0/15);m_present(m);
            double cost=(SDL_GetPerformanceCounter()-before)*1000/frequency;sum+=cost;max=fmax(max,cost);
        }
        SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormat(0,m->width,m->height,32,SDL_PIXELFORMAT_ARGB8888);
        SDL_Texture *capture=SDL_CreateTexture(m->renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,m->width,m->height);
        /* The same final compositor, into a bounded offline readback target. */
        if(capture)m_compose(m,capture,m->width,m->height);
        ok=surface && capture && !SDL_RenderReadPixels(m->renderer,NULL,SDL_PIXELFORMAT_ARGB8888,surface->pixels,surface->pitch) && !SDL_SaveBMP(surface,argv[2]);
        if(surface)SDL_FreeSurface(surface);
        SDL_SetRenderTarget(m->renderer,NULL);if(capture)SDL_DestroyTexture(capture);
        SDL_RendererInfo info;SDL_GetRendererInfo(m->renderer,&info);
        printf("capture backend=%s frames=%d mean_ms=%.3f max_ms=%.3f instances=%u model_bytes=%zu history_bytes=%d effect_texture_bytes=1088000\n",info.name,frames,sum/frames,max,m->count,sizeof(*m),m->width*m->height*4);
    }
    if(m){m_destroy(m);free(m);}SDL_Quit();return ok?0:1;
}
