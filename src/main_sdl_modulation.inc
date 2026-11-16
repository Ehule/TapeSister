/* Parameter curves share Matrix identity, persistence and history. Audio never
   depends on this window being open, and painting never edits control data. */
#include "tapesister/matrix_modulation.h"
#define MOD_BG (ts_theme_color(&matrix_window.palette,TS_THEME_MOSAIC,TS_COLOR_BACKGROUND,0xff171c22u))
#define MOD_PANEL (ts_theme_color(&matrix_window.palette,TS_THEME_MOSAIC,TS_COLOR_PANEL,0xff293039u))
#define MOD_TEXT (ts_theme_color(&matrix_window.palette,TS_THEME_MOSAIC,TS_COLOR_TEXT,0xffe4e7e9u))
#define MOD_DIM (ts_theme_color(&matrix_window.palette,TS_THEME_MOSAIC,TS_COLOR_DIM,0xff9aa7b0u))
#define MOD_YELLOW 0xffffce69u
enum { MOD_W=1280,MOD_H=800,MOD_GX=292,MOD_GY=174,MOD_GW=942,MOD_GH=244,MOD_ROWS=5 };
static struct {
    SDL_Window *window; SDL_Renderer *renderer;
    Uint32 id,return_id,painted; uint32_t node;
    int visible,lane,first,page,draw,drag,point,origin_x,last_x,last_y;
    int menu,menu_row,menu_first,edit,cursor,all,pending; Uint32 blink; float origin;
    char text[40],status[128]; MatrixSnapshot *before;
} modulation_window={.drag=-1};
static int modulation_editing(void)
{return modulation_window.visible && (modulation_window.drag>=0 || modulation_window.pending || modulation_window.before);}
static const uint32_t mod_ink[]={0xffc4a4dcu,0xff91cbd3u,0xffe4bf8bu,0xffa9c7a4u};
/* Coalesce pointer edits once per UI frame, then flush on release/focus loss.
   The visible curve follows every event; audio receives complete curves. */
static void modulation_flush(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    if(!modulation_window.pending)return;
    modulation_window.pending=0;mx_commit_controls(device,audio,ui);
}
static void mod_edit_begin(int field,const char *text)
{
    modulation_window.edit=field;snprintf(modulation_window.text,sizeof(modulation_window.text),"%.23s",text);
    modulation_window.cursor=(int)strlen(modulation_window.text);modulation_window.all=1;
    modulation_window.blink=SDL_GetTicks();SDL_StartTextInput();
}
static void modulation_history_finish(SDL_AudioDeviceID device,AudioState *audio,TsSamplePages *pages,TsInstrument *bank)
{
    if(!modulation_window.before)return;
    MatrixSnapshot *after=mx_snapshot(device,audio,pages,bank);
    if(after && !mx_equal(modulation_window.before,after))mx_push(modulation_window.before);
    else free(modulation_window.before);
    free(after);modulation_window.before=NULL;
}
static void modulation_hide(void)
{
    modulation_window.visible=0;modulation_window.drag=-1;modulation_window.edit=modulation_window.menu=0;
    SDL_CaptureMouse(SDL_FALSE);SDL_StopTextInput();
    free(modulation_window.before);modulation_window.before=NULL;
    if(modulation_window.window)SDL_HideWindow(modulation_window.window);
}
static void modulation_open(uint32_t id)
{
    SDL_Window *origin=SDL_GetKeyboardFocus();
    if(origin && origin!=modulation_window.window)modulation_window.return_id=SDL_GetWindowID(origin);
    if(!modulation_window.window) {
        modulation_window.window=SDL_CreateWindow("TapeSister / Modulation",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
            MOD_W,MOD_H,TS_WORKSPACE_WINDOW_FLAGS);
        if(!modulation_window.window)return;
        modulation_window.renderer=SDL_CreateRenderer(modulation_window.window,-1,SDL_RENDERER_SOFTWARE);
        if(!modulation_window.renderer){SDL_DestroyWindow(modulation_window.window);modulation_window.window=NULL;return;}
        SDL_SetWindowMinimumSize(modulation_window.window,960,600);
        modulation_window.id=SDL_GetWindowID(modulation_window.window);
    }
    modulation_window.node=id;modulation_window.visible=1;modulation_window.lane=modulation_window.first=modulation_window.page=0;
    modulation_window.menu=modulation_window.edit=0;modulation_window.drag=-1;modulation_window.painted=SDL_GetTicks()-40;
    snprintf(modulation_window.status,sizeof(modulation_window.status),"DRAW A CURVE, THEN ASSIGN A PARAMETER. EACH ASSIGNMENT HAS ITS OWN RANGE.");
    SDL_ShowWindow(modulation_window.window);SDL_RaiseWindow(modulation_window.window);
}
static TsMatrixEffectControls *modulation_resolve(void)
{
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(matrix_window.controls.effect[i].id==modulation_window.node &&
        matrix_window.controls.effect[i].type==TS_MATRIX_FX_MODULATOR) return &matrix_window.controls.effect[i];
    modulation_hide();return NULL;
}
static void mod_rect(int x,int y,int w,int h,uint32_t color)
{
    SDL_SetRenderDrawColor(modulation_window.renderer,(color>>16)&255,(color>>8)&255,color&255,255);
    SDL_Rect r={x,y,w,h};SDL_RenderFillRect(modulation_window.renderer,&r);
}
static void mod_text(int x,int y,const char *s,uint32_t color,int scale)
{
    for(;*s;++s,x+=6*scale) {
        const char *g=ts_ui_glyph(*s);
        for(int r=0;r<7;++r)for(int c=0;c<5;++c)if(g[r*5+c]=='1')mod_rect(x+c*scale,y+r*scale,scale,scale,color);
    }
}
static void mod_line(int x,int y,int xx,int yy,uint32_t color)
{
    SDL_SetRenderDrawColor(modulation_window.renderer,(color>>16)&255,(color>>8)&255,color&255,255);
    SDL_RenderDrawLine(modulation_window.renderer,x,y,xx,yy);
}
static void mod_button(int x,int y,int w,const char *s,int selected)
{
    mod_rect(x,y,w,30,selected?0xff55435fu:MOD_PANEL);
    mod_text(x+10,y+8,s,selected?mod_ink[0]:MOD_TEXT,2);
}
static void mod_curve(const TsModLane *lane,int x,int y,int w,int h,uint32_t color,int shade)
{
    int last=y+h-(int)lroundf(ts_mod_value(lane,0)*h);
    for(int i=1;i<=w;++i) {
        int next=y+h-(int)lroundf(ts_mod_value(lane,(double)i/w)*h);
        if(shade)mod_line(x+i,next,x+i,y+h,0xff34303fu);
        mod_line(x+i-1,last,x+i,next,color);
        if(shade)mod_line(x+i-1,last+1,x+i,next+1,color);
        last=next;
    }
}
static int mod_map_at(const TsModControls *c,int row)
{
    int n=0;
    for(unsigned i=0;i<c->map_count;++i)if(c->map[i].lane==(unsigned)modulation_window.lane) {
        if(n++==row+modulation_window.page*MOD_ROWS)return i;
    }
    return -1;
}
static int mod_map_count(const TsModControls *c)
{int n=0;for(unsigned i=0;i<c->map_count;++i)n+=c->map[i].lane==(unsigned)modulation_window.lane;return n;}
static void mod_effect_label(const TsMatrixEffectControls *v,char *s,size_t n)
{
    if(matrix_window.layout)for(int i=0;i<matrix_window.layout->canvas_count;++i) {
        const TsFxCanvasPosition *p=&matrix_window.layout->canvas[i];
        if(p->kind==4 && p->id==v->id && p->name[0]){snprintf(s,n,"%s",p->name);return;}
    }
    snprintf(s,n,"%s #%u",ts_matrix_effect_name(v->type),v->id);
}
static void mod_target_label(const TsMatrixEffectControls *v,uint32_t child,char *s,size_t n)
{
    char label[48];mod_effect_label(v,label,sizeof(label));
    if(child) {
        for(int i=0;i<v->board.count;++i)if(v->board.slot[i].id==child) {
            snprintf(s,n,"%.13s / %d %.10s",label,i+1,ts_sister_fx_type_name(v->board.slot[i].type));return;
        }
    }
    snprintf(s,n,"%.29s",label);
}
/* Menus contain actual eligible destinations/parameters, including children of
   independent pedalboards. Each parameter has one owner to avoid hidden sums. */
typedef struct {uint32_t target,child;unsigned field;char name[48];} ModChoice;
static int mod_owned(const TsModMap *map,int skip)
{
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i) {
        const TsMatrixEffectControls *v=&matrix_window.controls.effect[i];
        if(v->type!=TS_MATRIX_FX_MODULATOR || !v->id)continue;
        for(unsigned j=0;j<v->modulation.map_count;++j) {
            const TsModMap *m=&v->modulation.map[j];
            if(v->id==modulation_window.node && (int)j==skip)continue;
            if(m->target==map->target && m->child==map->child && m->type==map->type && m->field==map->field)return 1;
        }
    }
    return 0;
}
static int mod_first_field(const TsMatrixEffectControls *v,uint32_t child,int skip)
{
    /* Mix is a useful default for every eligible effect. */
    for(unsigned j=0;j<85;++j) {
        unsigned field=j<5?(j+3)%5:j;
        TsModMap m={.target=v->id,.child=child,.type=ts_mod_target_type(v,child),.field=field};
        if(ts_mod_field_available(v,child,field) && !mod_owned(&m,skip))return (int)field;
    }
    return -1;
}
static int mod_choices(ModChoice *out,int capacity,const TsModControls *c)
{
    int n=0,at=modulation_window.menu_row;
    if(modulation_window.menu==2 && at>=0 && at<(int)c->map_count) {
        const TsModMap *m=&c->map[at];int target=ts_mod_target(&matrix_window.controls,m);
        if(target<0)return 0;
        const TsMatrixEffectControls *v=&matrix_window.controls.effect[target];
        for(unsigned f=0;f<85 && n<capacity;++f)if(ts_mod_field_available(v,m->child,f)) {
            TsModMap next=*m;next.field=f;if(mod_owned(&next,at))continue;
            out[n]=(ModChoice){.target=v->id,.child=m->child,.field=f};
            ts_mod_parameter_name(v,m->child,f,out[n].name,sizeof(out[n].name));++n;
        }
    } else for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i) {
        const TsMatrixEffectControls *v=&matrix_window.controls.effect[i];
        if(!v->id || v->type==TS_MATRIX_FX_MODULATOR)continue;
        for(int k=-1;k<(v->type==TS_MATRIX_FX_PEDALBOARD?v->board.count:0) && n<capacity;++k) {
            uint32_t child=k<0?0:v->board.slot[k].id;int f=mod_first_field(v,child,at);
            if(f<0)continue;
            out[n]=(ModChoice){.target=v->id,.child=child,.field=(unsigned)f};
            mod_target_label(v,child,out[n].name,sizeof(out[n].name));++n;
        }
    }
    return n;
}
static void modulation_paint(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    if(SDL_GetTicks()-modulation_window.painted>=33)modulation_flush(device,audio,ui);
    if(!modulation_window.visible || !window_drawable(modulation_window.window) || SDL_GetTicks()-modulation_window.painted<33)return;
    TsMatrixEffectControls *v=modulation_resolve();if(!v)return;
    TsModControls *c=&v->modulation;
    modulation_window.lane=SDL_clamp(modulation_window.lane,0,(int)c->count-1);
    modulation_window.first=SDL_clamp(modulation_window.first,0,((int)c->count-1)/4*4);
    modulation_window.page=SDL_clamp(modulation_window.page,0,SDL_max(0,(mod_map_count(c)-1)/MOD_ROWS));
    TsModLane *l=&c->lane[modulation_window.lane];uint32_t color=mod_ink[modulation_window.lane%4];
    int w,h;SDL_GetRendererOutputSize(modulation_window.renderer,&w,&h);
    SDL_RenderSetScale(modulation_window.renderer,w/(float)MOD_W,h/(float)MOD_H);
    mod_rect(0,0,MOD_W,MOD_H,MOD_BG);mod_rect(0,0,MOD_W,65,0xff302736u);
    mod_text(24,20,"MODULATION",mod_ink[0],3);
    char title[48];mod_effect_label(v,title,sizeof(title));title[27]=0;mod_text(270,26,title,MOD_TEXT,2);
    int instance=0;for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(matrix_window.controls.effect[i].id && matrix_window.controls.effect[i].type==TS_MATRIX_FX_MODULATOR){++instance;if(matrix_window.controls.effect[i].id==v->id)break;}
    char nav[24];snprintf(nav,sizeof(nav),"< M%d >",instance);mod_button(620,18,164,nav,0);
    mod_button(800,18,190,"RETRIGGER",0);
    mod_button(1006,18,108,v->enabled?"ON":"BYPASS",v->enabled);
    mod_button(1128,18,128,"< BACK",0);
    mod_text(24,88,"CURVE LANES",MOD_DIM,2);
    mod_button(178,78,34,"<",0);mod_button(218,78,34,">",0);
    double phase[TS_MOD_LANES];
    if(device)SDL_LockAudioDevice(device);
    for(unsigned i=0;i<c->count;++i)phase[i]=ts_matrix_modulation_phase(&audio->sister.matrix_effects,v->id,i);
    if(device)SDL_UnlockAudioDevice(device);
    for(int r=0;r<4;++r) {
        int i=modulation_window.first+r;if(i>=(int)c->count)break;
        int y=126+r*117;uint32_t ink=mod_ink[i%4];
        mod_rect(16,y,240,105,MOD_PANEL);mod_rect(16,y,4,105,i==modulation_window.lane?ink:MOD_PANEL);
        if(i==modulation_window.lane)mod_rect(20,y,236,2,ink);
        char s[40];snprintf(s,sizeof(s),"%02d %.14s",i+1,c->lane[i].name);mod_text(30,y+14,s,ink,2);
        mod_rect(230,y+11,14,14,c->lane[i].enabled?ink:MOD_DIM);
        mod_curve(&c->lane[i],30,y+43,210,35,ink,0);
        snprintf(s,sizeof(s),"%s / %g %s",c->lane[i].mode?"ONE SHOT":"LOOP",c->lane[i].timing?c->lane[i].beats:c->lane[i].seconds,c->lane[i].timing?"BEATS":"SEC");
        mod_text(30,y+88,s,MOD_DIM,1);
    }
    mod_button(24,612,226,"+ ADD LANE",0);
    mod_text(24,660,"DOUBLE-CLICK NAME TO RENAME",MOD_DIM,1);
    mod_text(24,680,"LANES RUN WHEN ASSIGNED",MOD_DIM,1);
    mod_text(24,696,"TO AN ACTIVE EFFECT.",MOD_DIM,1);
    char influence[32];snprintf(influence,sizeof(influence),"INFLUENCE %.0f%%",v->mix*100);mod_button(24,724,226,influence,0);mod_rect(34,751,(int)(206*v->mix),3,mod_ink[0]);
    mod_button(276,78,90,"LOOP",!l->mode);mod_button(374,78,130,"ONE SHOT",l->mode);
    mod_button(530,78,90,"FREE",!l->timing);mod_button(628,78,100,"TEMPO",l->timing);
    char s[160];snprintf(s,sizeof(s),"%g %s",l->timing?l->beats:l->seconds,l->timing?"BEATS":"SEC");
    mod_button(744,78,244,s,0);
    mod_rect(754,105,(int)(224*logf((l->timing?l->beats:l->seconds)/(l->timing?.0625f:.05f))/logf(l->timing?16384.f:288000.f)),3,mod_ink[0]);
    if(!l->timing){snprintf(s,sizeof(s),"FREQ %.3g HZ",1.f/l->seconds);mod_button(1006,78,250,s,0);mod_rect(1016,105,(int)(230*logf((1.f/l->seconds)*14400.f)/logf(288000.f)),3,mod_ink[0]);}
    snprintf(s,sizeof(s),"%.23s  /  %s",l->name,l->mode?"ONE SHOT HOLDS LAST VALUE":"LOOPING CURVE");mod_text(278,132,s,color,2);
    mod_rect(MOD_GX-16,MOD_GY-12,MOD_GW+32,MOD_GH+34,0xff1b2026u);
    for(int i=0;i<=4;++i) {
        int yy=MOD_GY+i*MOD_GH/4;mod_line(MOD_GX,yy,MOD_GX+MOD_GW,yy,0xff353c43u);
        int xx=MOD_GX+i*MOD_GW/4;mod_line(xx,MOD_GY,xx,MOD_GY+MOD_GH,0xff353c43u);
    }
    mod_curve(l,MOD_GX,MOD_GY,MOD_GW,MOD_GH,color,1);
    if(!modulation_window.draw)for(unsigned i=0;i<l->count;++i)mod_rect(MOD_GX+(int)(l->point[i].x*MOD_GW)-3,MOD_GY+MOD_GH-(int)(l->point[i].y*MOD_GH)-3,7,7,MOD_TEXT);
    int cursor=MOD_GX+(int)(phase[modulation_window.lane]*MOD_GW);mod_line(cursor,MOD_GY,cursor,MOD_GY+MOD_GH,0xfff0e9dfu);
    mod_text(MOD_GX,MOD_GY+MOD_GH+12,"START",MOD_DIM,1);mod_text(MOD_GX+MOD_GW-18,MOD_GY+MOD_GH+12,"END",MOD_DIM,1);
    mod_button(276,460,90,"DRAW",modulation_window.draw);mod_button(374,460,100,"POINTS",!modulation_window.draw);
    mod_button(492,460,72,"SINE",0);mod_button(572,460,72,"TRI",0);mod_button(652,460,96,"SQUARE",0);mod_button(756,460,104,"RANDOM",0);
    mod_text(878,448,"POINT COUNT",MOD_DIM,1);snprintf(s,sizeof(s),"- %u +",l->count);mod_button(872,460,112,s,0);
    snprintf(s,sizeof(s),"SMOOTH %.0f%%",l->smooth*100);mod_button(996,460,260,s,0);
    mod_rect(1006,487,(int)(240*l->smooth),3,color);
    mod_text(278,505,modulation_window.draw?"DRAG TO DRAW. SMOOTHING ROUNDS EACH SEGMENT.":"DRAG POINTS. DOUBLE-CLICK TO ADD. RIGHT-CLICK TO REMOVE.",MOD_DIM,1);
    mod_text(278,540,"PARAMETER ASSIGNMENTS",color,2);
    mod_button(900,530,44,"<",0);snprintf(s,sizeof(s),"%d/%d",modulation_window.page+1,SDL_max(1,(mod_map_count(c)+MOD_ROWS-1)/MOD_ROWS));mod_text(958,540,s,MOD_DIM,2);mod_button(1020,530,44,">",0);
    mod_button(1080,530,176,"+ ASSIGN",0);
    mod_text(284,580,"DESTINATION",MOD_DIM,1);mod_text(660,580,"PARAMETER",MOD_DIM,1);mod_text(932,580,"LOW",MOD_DIM,1);mod_text(1052,580,"HIGH",MOD_DIM,1);
    for(int r=0;r<MOD_ROWS;++r) {
        int at=mod_map_at(c,r),y=600+r*30;
        if(at<0) {if(!r)mod_text(290,y+8,"NO ASSIGNMENTS. CHOOSE + ASSIGN TO CONNECT A PARAMETER.",MOD_DIM,1);break;}
        TsModMap *m=&c->map[at];int target=ts_mod_target(&matrix_window.controls,m);
        const TsMatrixEffectControls *d=target>=0?&matrix_window.controls.effect[target]:NULL;
        mod_rect(276,y,980,27,MOD_PANEL);
        if(d)mod_target_label(d,m->child,s,sizeof(s));else snprintf(s,sizeof(s),"MISSING TARGET #%u",m->target);
        mod_text(284,y+7,s,d?MOD_TEXT:MOD_YELLOW,2);
        if(d)ts_mod_parameter_name(d,m->child,m->field,s,sizeof(s));else snprintf(s,sizeof(s),"UNAVAILABLE");mod_text(660,y+7,s,MOD_TEXT,2);
        if(d)ts_mod_parameter_value(d,m->child,m->field,m->low,s,sizeof(s));else snprintf(s,sizeof(s),"%.1f%%",m->low*100);mod_text(932,y+7,s,color,2);
        if(d)ts_mod_parameter_value(d,m->child,m->field,m->high,s,sizeof(s));else snprintf(s,sizeof(s),"%.1f%%",m->high*100);mod_text(1052,y+7,s,color,2);
        /* Sliders share the normalized parameter range used by the wheel. */
        for(int high=0;high<2;++high){int sx=high?1048:928;float amount=high?m->high:m->low;
            mod_rect(sx,y+24,100,2,MOD_DIM);mod_rect(sx,y+24,(int)(100*amount),2,color);
            mod_rect(sx+(int)(100*amount)-2,y+21,4,6,color);}
        mod_text(1168,y+7,m->enabled?"ON":"--",m->enabled?color:MOD_DIM,2);mod_text(1220,y+7,"X",MOD_DIM,2);
    }
    mod_rect(0,766,1280,34,0xff302736u);mod_text(24,778,modulation_window.status,MOD_TEXT,1);
    if(modulation_window.edit) {
        mod_rect(380,292,520,140,0xff55435fu);mod_text(404,312,modulation_window.edit==1?"LANE NAME":modulation_window.edit==3?"FREQUENCY (HZ)":l->timing?"DURATION (BEATS)":"DURATION (SECONDS)",MOD_TEXT,2);
        if(modulation_window.all)mod_rect(402,348,SDL_max(4,(int)strlen(modulation_window.text)*18),29,MOD_PANEL);
        mod_text(404,352,modulation_window.text,mod_ink[0],3);
        if((SDL_GetTicks()-modulation_window.blink)/500%2==0)mod_rect(404+modulation_window.cursor*18,350,2,25,MOD_TEXT);mod_text(404,406,"ENTER: APPLY / ESC: CANCEL",MOD_TEXT,1);
    }
    if(modulation_window.menu) {
        ModChoice choices[TS_MATRIX_EFFECT_CAPACITY*(TS_SISTER_FX_SLOT_COUNT+1)];
        int count=mod_choices(choices,(int)(sizeof(choices)/sizeof(choices[0])),c);
        modulation_window.menu_first=SDL_clamp(modulation_window.menu_first,0,SDL_max(0,count-15));
        mod_rect(360,100,620,580,0xff41354bu);mod_text(380,120,modulation_window.menu==2?"CHOOSE PARAMETER":"CHOOSE DESTINATION",mod_ink[0],2);
        for(int r=0;r<15 && r+modulation_window.menu_first<count;++r) {
            mod_rect(376,156+r*32,588,29,MOD_PANEL);mod_text(388,164+r*32,choices[r+modulation_window.menu_first].name,MOD_TEXT,2);
        }
        mod_text(380,654,count?"SCROLL FOR MORE / ESC: CANCEL":"NO UNASSIGNED PARAMETERS AVAILABLE",MOD_DIM,1);
    }
    SDL_RenderPresent(modulation_window.renderer);modulation_window.painted=SDL_GetTicks();
}
static void mod_remove_map(TsModControls *c,int at)
{memmove(&c->map[at],&c->map[at+1],(c->map_count-at-1)*sizeof(c->map[0]));memset(&c->map[--c->map_count],0,sizeof(c->map[0]));}
static void mod_draw(TsModLane *l,int x,int y,int first)
{
    x=SDL_clamp(x,MOD_GX,MOD_GX+MOD_GW);y=SDL_clamp(y,MOD_GY,MOD_GY+MOD_GH);
    if(first) {
        ts_mod_curve_changed(l);
        /* Do not re-smooth already sampled freehand data on each new stroke. */
        int uniform=l->count==TS_MOD_POINTS;
        for(unsigned i=0;uniform && i<l->count;++i)uniform=fabsf(l->point[i].x-i/(float)(TS_MOD_POINTS-1))<.000001f;
        if(!uniform){TsModLane old=*l;l->count=TS_MOD_POINTS;
            for(unsigned i=0;i<l->count;++i)l->point[i]=(TsModPoint){i/(float)(TS_MOD_POINTS-1),ts_mod_value(&old,i/(double)(TS_MOD_POINTS-1))};}
        modulation_window.last_x=x;modulation_window.last_y=y;
    }
    float from=(modulation_window.last_x-MOD_GX)/(float)MOD_GW,to=(x-MOD_GX)/(float)MOD_GW;
    float start=1-(modulation_window.last_y-MOD_GY)/(float)MOD_GH,value=1-(y-MOD_GY)/(float)MOD_GH;
    int a=(int)lroundf(from*(TS_MOD_POINTS-1)),b=(int)lroundf(to*(TS_MOD_POINTS-1));
    for(int i=SDL_min(a,b);i<=SDL_max(a,b);++i) {
        float t=from==to?1:SDL_clamp((l->point[i].x-from)/(to-from),0,1);
        l->point[i].y=start+(value-start)*t;
    }
    if(!l->mode && (b==0 || b==TS_MOD_POINTS-1))l->point[b?0:TS_MOD_POINTS-1].y=l->point[b].y;
    modulation_window.last_x=x;modulation_window.last_y=y;
}
static void mod_point_move(TsModLane *l,int x,int y)
{
    int i=modulation_window.point;if(i<0 || i>=(int)l->count)return;
    ts_mod_curve_changed(l);
    l->point[i].y=SDL_clamp(1-(y-MOD_GY)/(float)MOD_GH,0,1);
    if(i && i<(int)l->count-1)l->point[i].x=SDL_clamp((x-MOD_GX)/(float)MOD_GW,l->point[i-1].x+.00001f,l->point[i+1].x-.00001f);
    if(!l->mode && (i==0 || i==(int)l->count-1))l->point[i?0:l->count-1].y=l->point[i].y;
}
static int modulation_event(const SDL_Event *e,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    if(!modulation_window.visible || event_window_id(e)!=modulation_window.id)return 0;
    TsMatrixEffectControls *v=modulation_resolve();if(!v)return 1;
    TsModControls *c=&v->modulation;
    modulation_window.lane=SDL_clamp(modulation_window.lane,0,(int)c->count-1);
    modulation_window.first=SDL_clamp(modulation_window.first,0,((int)c->count-1)/4*4);
    modulation_window.page=SDL_clamp(modulation_window.page,0,SDL_max(0,(mod_map_count(c)-1)/MOD_ROWS));
    TsModLane *l=&c->lane[modulation_window.lane];
    int w,h,x=0,y=0;SDL_GetWindowSize(modulation_window.window,&w,&h);
    if(e->type==SDL_MOUSEMOTION){x=e->motion.x;y=e->motion.y;}
    else if(e->type==SDL_MOUSEBUTTONDOWN || e->type==SDL_MOUSEBUTTONUP){x=e->button.x;y=e->button.y;}
    else if(e->type==SDL_MOUSEWHEEL){
#if SDL_VERSION_ATLEAST(2,26,0)
        x=e->wheel.mouseX;y=e->wheel.mouseY;
#else
        SDL_GetMouseState(&x,&y);
#endif
    }
    x=x*MOD_W/SDL_max(1,w);y=y*MOD_H/SDL_max(1,h);
    if(e->type==SDL_WINDOWEVENT && (e->window.event==SDL_WINDOWEVENT_CLOSE || e->window.event==SDL_WINDOWEVENT_FOCUS_LOST)) {
        modulation_flush(device,audio,ui);modulation_history_finish(device,audio,pages,bank);modulation_window.drag=-1;SDL_CaptureMouse(SDL_FALSE);
        if(e->window.event==SDL_WINDOWEVENT_CLOSE){modulation_hide();editor_return_window(modulation_window.return_id);}return 1;
    }
    if(e->type==SDL_MOUSEBUTTONUP) {
        if(e->button.button==SDL_BUTTON_LEFT && modulation_window.drag==0){mod_draw(l,x,y,0);modulation_window.pending=1;}
        if(e->button.button==SDL_BUTTON_LEFT && modulation_window.drag==1){mod_point_move(l,x,y);modulation_window.pending=1;}
        modulation_flush(device,audio,ui);modulation_history_finish(device,audio,pages,bank);modulation_window.drag=-1;SDL_CaptureMouse(SDL_FALSE);return 1;
    }
    if(e->type==SDL_KEYDOWN && e->key.keysym.sym==SDLK_ESCAPE) {
        if(modulation_window.edit){modulation_window.edit=0;SDL_StopTextInput();return 1;}
        if(modulation_window.menu){modulation_window.menu=0;return 1;}
        modulation_flush(device,audio,ui);modulation_history_finish(device,audio,pages,bank);modulation_hide();editor_return_window(modulation_window.return_id);return 1;
    }
    if(modulation_window.edit) {
        if(e->type==SDL_TEXTINPUT || e->type==SDL_KEYDOWN)modulation_window.blink=SDL_GetTicks();
        char *text=modulation_window.text;int at=modulation_window.cursor,n=(int)strlen(text);
        if(e->type==SDL_TEXTINPUT)for(const char *p=e->text.text;*p;++p)if(*p>=32 && *p<127 && *p!='=') {
            if(modulation_window.all){text[0]=0;n=at=0;modulation_window.all=0;}
            if(n>=23)break;memmove(text+at+1,text+at,(size_t)(n-at+1));text[at++]=*p;++n;
            modulation_window.cursor=at;
        }
        if(e->type==SDL_KEYDOWN) {
            int key=e->key.keysym.sym;
            if(key==SDLK_a && (e->key.keysym.mod&KMOD_CTRL))modulation_window.all=1;
            if(key==SDLK_BACKSPACE || key==SDLK_DELETE) {
                if(modulation_window.all){text[0]=0;at=0;}
                else if(key==SDLK_BACKSPACE && at){memmove(text+at-1,text+at,(size_t)(n-at+1));--at;}
                else if(key==SDLK_DELETE && at<n)memmove(text+at,text+at+1,(size_t)(n-at));
                modulation_window.cursor=at;modulation_window.all=0;
            }
            if(key==SDLK_LEFT || key==SDLK_RIGHT || key==SDLK_HOME || key==SDLK_END) {
                modulation_window.cursor=key==SDLK_HOME?0:key==SDLK_END?n:SDL_clamp(at+(key==SDLK_LEFT?-1:1),0,n);modulation_window.all=0;
            }
            if((key==SDLK_RETURN || key==SDLK_KP_ENTER) && mx_editable()) {
                modulation_window.before=mx_snapshot(device,audio,pages,bank);
                if(modulation_window.edit==1)snprintf(l->name,sizeof(l->name),"%.23s",modulation_window.text);
                else {char *end;float value=strtof(modulation_window.text,&end);
                    if(end!=modulation_window.text && !*end && isfinite(value)) {
                        if(modulation_window.edit==3){if(value>0)l->seconds=1.f/SDL_clamp(value,1.f/14400.f,20.f);}
                        else if(l->timing)l->beats=SDL_clamp(value,.0625f,1024.f);else l->seconds=SDL_clamp(value,.05f,14400.f);
                    }}
                modulation_window.edit=0;SDL_StopTextInput();mx_commit_controls(device,audio,ui);ui->project_modified=1;
                modulation_history_finish(device,audio,pages,bank);
            }
        }
        return 1;
    }
    if(e->type==SDL_KEYDOWN && (e->key.keysym.mod&KMOD_CTRL) && (e->key.keysym.sym==SDLK_z || e->key.keysym.sym==SDLK_y)) {
        modulation_flush(device,audio,ui);modulation_history_finish(device,audio,pages,bank);modulation_window.menu=0;modulation_window.drag=-1;SDL_CaptureMouse(SDL_FALSE);
        mx_undo(device,audio,ui,pages,bank,e->key.keysym.sym==SDLK_y || (e->key.keysym.mod&KMOD_SHIFT));return 1;
    }
    if(e->type==SDL_KEYDOWN || e->type==SDL_KEYUP){modulation_flush(device,audio,ui);return 0;} /* Preserve live audition / transport. */
    int click=e->type==SDL_MOUSEBUTTONDOWN,left=click && e->button.button==SDL_BUTTON_LEFT;
    if(modulation_window.menu) {
        if(e->type==SDL_MOUSEWHEEL){modulation_window.menu_first=SDL_max(0,modulation_window.menu_first-e->wheel.y*3);return 1;}
        if(!left)return 1;
        if(x<376 || x>=964 || y<156 || y>=636){modulation_window.menu=0;return 1;}
        ModChoice choices[TS_MATRIX_EFFECT_CAPACITY*(TS_SISTER_FX_SLOT_COUNT+1)];int n=mod_choices(choices,(int)(sizeof(choices)/sizeof(choices[0])),c);
        int selected=(y-156)/32+modulation_window.menu_first;if(selected>=n || !mx_editable())return 1;
        if(!modulation_window.before)modulation_window.before=mx_snapshot(device,audio,pages,bank);
        int at=modulation_window.menu_row;
        if(at<0){if(c->map_count>=TS_MOD_MAPS)return 1;at=c->map_count++;c->map[at]=(TsModMap){.enabled=1,.lane=modulation_window.lane,.low=0,.high=1};}
        TsModMap *m=&c->map[at];m->target=choices[selected].target;m->child=choices[selected].child;m->field=choices[selected].field;
        for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(matrix_window.controls.effect[i].id==m->target)m->type=ts_mod_target_type(&matrix_window.controls.effect[i],m->child);
        int menu=modulation_window.menu;modulation_window.menu=menu==1?2:0;modulation_window.menu_row=at;modulation_window.menu_first=0;
        mx_commit_controls(device,audio,ui);ui->project_modified=1;return 1;
    }
    if(!mx_editable())return 1;
    if(left && x>=620 && x<784 && y>=18 && y<48) {
        uint32_t ids[TS_MATRIX_EFFECT_CAPACITY];int count=0,at=0;
        for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(matrix_window.controls.effect[i].id && matrix_window.controls.effect[i].type==TS_MATRIX_FX_MODULATOR){if(matrix_window.controls.effect[i].id==v->id)at=count;ids[count++]=matrix_window.controls.effect[i].id;}
        if(count){modulation_flush(device,audio,ui);modulation_history_finish(device,audio,pages,bank);modulation_open(ids[(at+(x<702?-1:1)+count)%count]);}return 1;
    }
    if(left && x>=1128 && y>=18 && y<48) {modulation_flush(device,audio,ui);modulation_history_finish(device,audio,pages,bank);modulation_hide();editor_return_window(modulation_window.return_id);return 1;}
    if(left && x<260) {
        if(y>=78 && y<108 && x>=178){modulation_window.first=SDL_clamp(modulation_window.first+(x>=218?4:-4),0,((int)c->count-1)/4*4);return 1;}
        int r=(y-126)/117,i=modulation_window.first+r;
        if(y>=126 && r<4 && i<(int)c->count && y<126+r*117+105) {
            modulation_window.lane=i;modulation_window.page=0;l=&c->lane[i];
            if(x<224) {
                if(e->button.clicks>=2){mod_edit_begin(1,l->name);}
                return 1;
            }
        }
    }
    if(left && x>=900 && x<1064 && y>=530 && y<560) {
        modulation_window.page=SDL_clamp(modulation_window.page+(x>=1020?1:x<944?-1:0),0,SDL_max(0,(mod_map_count(c)-1)/MOD_ROWS));return 1;
    }
    if(left && x>=1080 && y>=530 && y<560) {modulation_window.menu=1;modulation_window.menu_row=-1;modulation_window.menu_first=0;return 1;}
    if(left && y>=600 && y<750 && x>=276 && x<916) {
        int at=mod_map_at(c,(y-600)/30);if(at>=0){modulation_window.menu=x<650?1:2;modulation_window.menu_row=at;modulation_window.menu_first=0;}return 1;
    }
    if(left && x>=744 && x<1256 && (x<988 || (!l->timing && x>=1006)) && y>=78 && y<108 && e->button.clicks>=2) {
        char text[40];int hz=!l->timing && x>=1006;snprintf(text,sizeof(text),"%g",hz?1.f/l->seconds:l->timing?l->beats:l->seconds);mod_edit_begin(hz?3:2,text);return 1;
    }
    if(e->type==SDL_MOUSEWHEEL && modulation_window.drag<0) {
        int change=e->wheel.y*(e->wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-1:1),changed=0;
        if(!change)return 1;
        modulation_window.before=mx_snapshot(device,audio,pages,bank);
        if(y>=600 && y<750 && x>=916 && x<1160) {
            int at=mod_map_at(c,(y-600)/30);if(at>=0){float *value=x>=1044?&c->map[at].high:&c->map[at].low;
                *value=SDL_clamp(*value+change*((SDL_GetModState()&KMOD_SHIFT)?.001f:.01f),0,1);changed=1;}
        } else if(y>=78 && y<108 && x>=744 && (x<988 || (!l->timing && x>=1006))) {
            float factor=powf(2,change*((SDL_GetModState()&KMOD_SHIFT)?.01f:.1f));
            if(l->timing)l->beats=SDL_clamp(l->beats*factor,.0625f,1024.f);
            else l->seconds=SDL_clamp(l->seconds*(x>=1006?1/factor:factor),.05f,14400.f);changed=1;
        } else if(y>=460 && y<490 && x>=872 && x<984){ts_mod_resize(l,SDL_clamp((int)l->count+change,2,TS_MOD_POINTS));changed=1;}
        if(changed){mx_commit_controls(device,audio,ui);ui->project_modified=1;}
        modulation_history_finish(device,audio,pages,bank);return 1;
    }
    if(click && !modulation_window.before)modulation_window.before=mx_snapshot(device,audio,pages,bank);
    int changed=0;
    if(left) {
        if(y>=18 && y<48 && x>=800 && x<990){++c->retrigger;changed=1;}
        else if(y>=18 && y<48 && x>=1006 && x<1114){v->enabled=!v->enabled;changed=1;}
        else if(x<260) {
            int r=(y-126)/117,i=modulation_window.first+r;
            if(y>=126 && r<4 && i<(int)c->count && x>=224 && y<126+r*117+105){c->lane[i].enabled=!c->lane[i].enabled;changed=1;}
            else if(y>=724 && y<754){modulation_window.drag=5;modulation_window.origin=v->mix;}
            else if(y>=612 && y<642 && c->count<TS_MOD_LANES){ts_mod_lane_default(&c->lane[c->count],c->count);modulation_window.lane=c->count++;modulation_window.first=modulation_window.lane/4*4;modulation_window.page=0;changed=1;}
        } else if(y>=78 && y<108) {
            if(x<504){l->mode=x>=374;changed=1;if(!l->mode){l->point[l->count-1].y=l->point[0].y;if(l->detail_count)l->detail[l->detail_count-1].y=l->detail[0].y;}}
            else if(x>=530 && x<728){l->timing=x>=628;changed=1;}
            else if(x>=744 && (x<988 || (!l->timing && x>=1006))){int hz=!l->timing && x>=1006;modulation_window.drag=hz?4:2;modulation_window.origin=hz?1.f/l->seconds:l->timing?l->beats:l->seconds;}
        } else if(y>=460 && y<490) {
            if(x<474)modulation_window.draw=x<374;
            else if(x>=492 && x<860){ts_mod_shape(l,x<572?0:x<652?1:x<756?3:2,l->count);changed=1;}
            else if(x>=872 && x<984){ts_mod_resize(l,SDL_clamp((int)l->count+(x<928?-1:1),2,TS_MOD_POINTS));changed=1;}
            else if(x>=996){modulation_window.drag=3;modulation_window.origin=l->smooth;}
        } else if(y>=600 && y<750 && x>=916) {
            int at=mod_map_at(c,(y-600)/30);
            if(at>=0) {
                if(x>=1210){mod_remove_map(c,at);changed=1;}
                else if(x>=1160){c->map[at].enabled=!c->map[at].enabled;changed=1;}
                else {int high=x>=1044,slider=(y-600)%30>=19;
                    modulation_window.drag=(slider?300:100)+at*2+high;modulation_window.origin=high?c->map[at].high:c->map[at].low;
                    if(slider){float value=SDL_clamp((x-(high?1048:928))/100.f,0,1);if(high)c->map[at].high=value;else c->map[at].low=value;changed=1;}}
            }
        }
        modulation_window.origin_x=x;
    }
    if(click && x>=MOD_GX && x<=MOD_GX+MOD_GW && y>=MOD_GY && y<=MOD_GY+MOD_GH) {
        if(modulation_window.draw && left){modulation_window.drag=0;mod_draw(l,x,y,1);changed=1;}
        else if(!modulation_window.draw) {
            int nearest=-1;double dist=144;
            for(unsigned i=0;i<l->count;++i){double dx=MOD_GX+l->point[i].x*MOD_GW-x,dy=MOD_GY+(1-l->point[i].y)*MOD_GH-y,d=dx*dx+dy*dy;if(d<dist){dist=d;nearest=i;}}
            if(left && nearest<0 && e->button.clicks>=2 && l->count<TS_MOD_POINTS) {
                float pos=(x-MOD_GX)/(float)MOD_GW;
                for(unsigned i=1;i<l->count;++i)if(pos>l->point[i-1].x+.0001f && pos<l->point[i].x-.0001f) {
                    memmove(&l->point[i+1],&l->point[i],(l->count-i)*sizeof(l->point[0]));++l->count;nearest=i;l->point[i].x=pos;break;
                }
            }
            if(nearest>=0) {
                if(left){modulation_window.point=nearest;modulation_window.drag=1;mod_point_move(l,x,y);changed=1;}
                else if(e->button.button==SDL_BUTTON_RIGHT && nearest>0 && nearest<(int)l->count-1) {
                    ts_mod_curve_changed(l);memmove(&l->point[nearest],&l->point[nearest+1],(l->count-nearest-1)*sizeof(l->point[0]));memset(&l->point[--l->count],0,sizeof(l->point[0]));changed=1;
                }
            }
        }
    }
    if(left && modulation_window.drag>=0)SDL_CaptureMouse(SDL_TRUE);
    if(e->type==SDL_MOUSEMOTION && modulation_window.drag>=0) {
        int drag=modulation_window.drag;float delta=(x-modulation_window.origin_x)/300.f;
        if(SDL_GetModState()&KMOD_SHIFT)delta*=.1f;
        if(drag==0)mod_draw(l,x,y,0);
        else if(drag==1)mod_point_move(l,x,y);
        else if(drag==2){float value=modulation_window.origin*powf(10,delta*2);if(l->timing)l->beats=SDL_clamp(value,.0625f,1024.f);else l->seconds=SDL_clamp(value,.05f,14400.f);}
        else if(drag==4)l->seconds=1.f/SDL_clamp(modulation_window.origin*powf(10,delta*2),1.f/14400.f,20.f);
        else if(drag==5)v->mix=SDL_clamp(modulation_window.origin+delta,0,1);
        else if(drag==3)l->smooth=SDL_clamp(modulation_window.origin+delta,0,1);
        else if(drag>=300){TsModMap *m=&c->map[(drag-300)/2];float value=SDL_clamp((x-((drag&1)?1048:928))/100.f,0,1);if(drag&1)m->high=value;else m->low=value;}
        else if(drag>=100){TsModMap *m=&c->map[(drag-100)/2];float value=SDL_clamp(modulation_window.origin+delta,0,1);if(drag&1)m->high=value;else m->low=value;}
        changed=1;
    }
    if(changed) {
        if(e->type==SDL_MOUSEMOTION)modulation_window.pending=1;
        else mx_commit_controls(device,audio,ui);ui->project_modified=1;
        snprintf(modulation_window.status,sizeof(modulation_window.status),"CTRL+Z: UNDO / SHIFT-DRAG: FINE / DOUBLE-CLICK TIME / HZ: TYPE VALUE / ESC: RETURN");
    }
    return 1;
}
static void modulation_close(void)
{
    modulation_hide();if(modulation_window.renderer)SDL_DestroyRenderer(modulation_window.renderer);
    if(modulation_window.window)SDL_DestroyWindow(modulation_window.window);
    modulation_window.renderer=NULL;modulation_window.window=NULL;
}
static int modulation_connect(uint32_t id,int destination,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    TsMatrixEffectControls *source=NULL;
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(matrix_window.controls.effect[i].id==id && matrix_window.controls.effect[i].type==TS_MATRIX_FX_MODULATOR)source=&matrix_window.controls.effect[i];
    if(!source)return 0;
    int slot=ts_matrix_effect_index(destination);
    if(slot<0 || !matrix_window.controls.effect[slot].id || !mx_editable()) {
        snprintf(matrix_window.status,sizeof(matrix_window.status),"MODULATION: CONNECT TO AN ADDED EFFECT OR PEDALBOARD.");return 1;
    }
    MatrixSnapshot *before=matrix_window.pending?NULL:mx_snapshot(device,audio,pages,bank);
    TsMatrixEffectControls *target=&matrix_window.controls.effect[slot];TsModControls *c=&source->modulation;
    int field=mod_first_field(target,0,-1);
    if(field<0 || c->map_count>=TS_MOD_MAPS){free(before);return 1;}
    int at=c->map_count++;
    c->map[at]=(TsModMap){.target=target->id,.type=(unsigned)target->type,.field=(unsigned)field,.lane=0,.enabled=1,.low=0,.high=1};
    mx_commit_controls(device,audio,ui);ui->project_modified=1;
    if(before)mx_push(before);
    snprintf(matrix_window.status,sizeof(matrix_window.status),"MODULATION CONNECTED / DOUBLE-CLICK MODULATOR TO EDIT ASSIGNMENTS / CTRL+Z: UNDO");
    return 1;
}
