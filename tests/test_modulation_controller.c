#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>
static AudioState audio;
static TsUiState ui;
static TsInstrument bank;
static TsSamplePages pages;
static void mouse(int type,int x,int y,int button,int clicks)
{
    int w,h;SDL_GetWindowSize(modulation_window.window,&w,&h);SDL_Event e;SDL_zero(e);e.type=type;
    if(type==SDL_MOUSEMOTION){e.motion.windowID=modulation_window.id;e.motion.x=x*w/MOD_W;e.motion.y=y*h/MOD_H;e.motion.state=SDL_BUTTON_LMASK;}
    else {e.button.windowID=modulation_window.id;e.button.x=x*w/MOD_W;e.button.y=y*h/MOD_H;e.button.button=button;e.button.clicks=clicks;}
    assert(modulation_event(&e,0,&audio,&ui,&pages,&bank));
}
static void click(int x,int y){mouse(SDL_MOUSEBUTTONDOWN,x,y,SDL_BUTTON_LEFT,1);mouse(SDL_MOUSEBUTTONUP,x,y,SDL_BUTTON_LEFT,1);}
static void drag(int x,int y,int xx,int yy)
{mouse(SDL_MOUSEBUTTONDOWN,x,y,SDL_BUTTON_LEFT,1);mouse(SDL_MOUSEMOTION,xx,yy,0,0);mouse(SDL_MOUSEBUTTONUP,xx,yy,SDL_BUTTON_LEFT,1);}
static void key(SDL_Keycode key,int mod)
{SDL_Event e;SDL_zero(e);e.type=SDL_KEYDOWN;e.key.windowID=modulation_window.id;e.key.keysym.sym=key;e.key.keysym.mod=mod;assert(modulation_event(&e,0,&audio,&ui,&pages,&bank));}
static void wheel(int x,int y,int change,int flipped)
{
    int w,h;SDL_GetWindowSize(modulation_window.window,&w,&h);SDL_Event e;SDL_zero(e);e.type=SDL_MOUSEWHEEL;e.wheel.windowID=modulation_window.id;e.wheel.y=change;
    e.wheel.direction=flipped?SDL_MOUSEWHEEL_FLIPPED:SDL_MOUSEWHEEL_NORMAL;
#if SDL_VERSION_ATLEAST(2,26,0)
    e.wheel.mouseX=x*w/MOD_W;e.wheel.mouseY=y*h/MOD_H;
#else
    SDL_WarpMouseInWindow(modulation_window.window,x*w/MOD_W,y*h/MOD_H);
#endif
    assert(modulation_event(&e,0,&audio,&ui,&pages,&bank));
}
static void type(const char *value)
{SDL_Event e;SDL_zero(e);e.type=SDL_TEXTINPUT;e.text.windowID=modulation_window.id;snprintf(e.text.text,sizeof(e.text.text),"%s",value);assert(modulation_event(&e,0,&audio,&ui,&pages,&bank));}
static void curve_controls(TsModControls *m)
{
    click(690,474);assert(m->lane[0].count==5 && ts_mod_value(&m->lane[0],.25)==1 && ts_mod_value(&m->lane[0],.75)==0);
    click(960,474);assert(m->lane[0].count==6);key(SDLK_z,KMOD_CTRL);assert(m->lane[0].count==5);key(SDLK_y,KMOD_CTRL);assert(m->lane[0].count==6);
    wheel(940,474,-2,0);assert(m->lane[0].count==4);assert(ts_mod_valid(m));
    click(570,90);mouse(SDL_MOUSEBUTTONDOWN,1110,90,SDL_BUTTON_LEFT,2);mouse(SDL_MOUSEBUTTONUP,1110,90,SDL_BUTTON_LEFT,2);
    assert(modulation_window.edit==3 && modulation_window.all);type("2");key(SDLK_RETURN,0);assert(m->lane[0].seconds==.5f);
    mouse(SDL_MOUSEBUTTONDOWN,1110,90,SDL_BUTTON_LEFT,2);mouse(SDL_MOUSEBUTTONUP,1110,90,SDL_BUTTON_LEFT,2);
    uint32_t on[50],off[50];SDL_Rect caret={404+modulation_window.cursor*18,350,2,25};
    modulation_window.all=0;modulation_window.blink=SDL_GetTicks();modulation_window.painted=SDL_GetTicks()-40;modulation_paint(0,&audio,&ui);
    assert(!SDL_RenderReadPixels(modulation_window.renderer,&caret,SDL_PIXELFORMAT_ARGB8888,on,2*sizeof(uint32_t)));
    modulation_window.blink=SDL_GetTicks()-500;modulation_window.painted=SDL_GetTicks()-40;modulation_paint(0,&audio,&ui);
    assert(!SDL_RenderReadPixels(modulation_window.renderer,&caret,SDL_PIXELFORMAT_ARGB8888,off,2*sizeof(uint32_t)));assert(memcmp(on,off,sizeof(on)));
    key(SDLK_HOME,0);type("1");assert(!strcmp(modulation_window.text,"12") && modulation_window.cursor==1);
    key(SDLK_DELETE,0);assert(!strcmp(modulation_window.text,"1"));key(SDLK_ESCAPE,0);assert(m->lane[0].seconds==.5f);
    click(310,474);TsModLane before=m->lane[0];
    mouse(SDL_MOUSEBUTTONDOWN,MOD_GX,MOD_GY+MOD_GH,SDL_BUTTON_LEFT,1);
    TsModLane published=ui.config.matrix.effect[0].modulation.lane[0];
    mouse(SDL_MOUSEMOTION,MOD_GX+MOD_GW/2,MOD_GY+MOD_GH/2,0,0);
    assert(modulation_window.pending && !memcmp(&published,&ui.config.matrix.effect[0].modulation.lane[0],sizeof(published)));
    TsModLane dirty=m->lane[0];
    mx_capture_activity(0,&audio,&pages,&bank,50);
    assert(!memcmp(&dirty,&m->lane[0],sizeof(dirty)));
    /* A release can carry a position not preceded by a motion event. */
    mouse(SDL_MOUSEBUTTONUP,MOD_GX+MOD_GW,MOD_GY,SDL_BUTTON_LEFT,1);
    assert(!modulation_window.pending && m->lane[0].point[63].y==1);
    assert(!memcmp(&m->lane[0],&ui.config.matrix.effect[0].modulation.lane[0],sizeof(before)));
    key(SDLK_z,KMOD_CTRL);assert(!memcmp(&before,&m->lane[0],sizeof(before)));
    /* Repeated short strokes must not resample untouched freehand points. */
    drag(MOD_GX,MOD_GY+180,MOD_GX+MOD_GW,MOD_GY+80);before=m->lane[0];
    drag(MOD_GX+20,MOD_GY+190,MOD_GX+30,MOD_GY+185);
    for(unsigned i=4;i<63;++i)assert(!memcmp(&before.point[i],&m->lane[0].point[i],sizeof(TsModPoint)));
    click(420,474);before=m->lane[0];
    int px=MOD_GX+(int)(before.point[10].x*MOD_GW),py=MOD_GY+(int)((1-before.point[10].y)*MOD_GH);
    mouse(SDL_MOUSEBUTTONDOWN,px,py,SDL_BUTTON_RIGHT,1);mouse(SDL_MOUSEBUTTONUP,px,py,SDL_BUTTON_RIGHT,1);
    assert(m->lane[0].count==before.count-1 && !modulation_window.before);key(SDLK_z,KMOD_CTRL);assert(!memcmp(&before,&m->lane[0],sizeof(before)));
}
static void shot(const char *path)
{
    modulation_window.painted=SDL_GetTicks()-40;modulation_paint(0,&audio,&ui);
    int w,h;SDL_GetRendererOutputSize(modulation_window.renderer,&w,&h);
    SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);assert(s);
    assert(!SDL_RenderReadPixels(modulation_window.renderer,NULL,SDL_PIXELFORMAT_ARGB8888,s->pixels,s->pitch));
    assert(!SDL_SaveBMP(s,path));SDL_FreeSurface(s);
}
int main(int argc,char **argv)
{
    SDL_SetMainReady();SDL_setenv("SDL_VIDEODRIVER","dummy",1);assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER));
    ts_ui_init(&ui);ts_instrument_init(&bank);char error[256];assert(ts_sample_pages_init(&pages,error,sizeof(error)));
    ts_sister_runtime_init(&audio.sister);audio.output_rate=48000;audio.output_device_channels=2;
    TsMatrixControls c=audio.sister.matrix.controls;c.enabled=1;
    int mod=ts_matrix_effect_add(&c,TS_MATRIX_FX_MODULATOR),board=ts_matrix_effect_add(&c,TS_MATRIX_FX_PEDALBOARD);
    assert(mod==0 && board==1);c.effect[board].board.slot[0].type=TS_SISTER_FX_FILTER;c.effect[board].board.slot[1].type=TS_SISTER_FX_REVERB;
    assert(ts_matrix_effects_restore(&audio.sister.matrix_effects,&c,48000));assert(ts_matrix_set(&audio.sister.matrix,&c,48000));
    matrix_window.controls=ui.config.matrix=c;matrix_window.palette=ui.palette;matrix_window.layout=&audio.sister.matrix_layout;
    modulation_open(c.effect[mod].id);assert(modulation_window.visible);SDL_SetWindowSize(modulation_window.window,MOD_W,MOD_H);
    TsModControls *m=&matrix_window.controls.effect[mod].modulation;TsModLane saved=m->lane[0];
    click(780,474);assert(memcmp(&saved,&m->lane[0],sizeof(saved)));TsModLane random=m->lane[0];
    key(SDLK_z,KMOD_CTRL);assert(!memcmp(&saved,&m->lane[0],sizeof(saved)));key(SDLK_y,KMOD_CTRL);assert(!memcmp(&random,&m->lane[0],sizeof(random)));
    drag(1100,474,1160,474);assert(m->lane[0].smooth>.8f);
    click(430,90);assert(m->lane[0].mode==TS_MOD_ONESHOT);click(670,90);assert(m->lane[0].timing==TS_MOD_TEMPO);
    mouse(SDL_MOUSEBUTTONDOWN,800,90,SDL_BUTTON_LEFT,2);mouse(SDL_MOUSEBUTTONUP,800,90,SDL_BUTTON_LEFT,2);assert(modulation_window.edit==2);
    key(SDLK_a,KMOD_CTRL);SDL_Event text;SDL_zero(text);text.type=SDL_TEXTINPUT;text.text.windowID=modulation_window.id;strcpy(text.text.text,"7.5");assert(modulation_event(&text,0,&audio,&ui,&pages,&bank));key(SDLK_RETURN,0);assert(m->lane[0].beats==7.5f);
    click(310,474);assert(modulation_window.draw);drag(MOD_GX+100,MOD_GY+180,MOD_GX+600,MOD_GY+50);
    assert(m->lane[0].count==64 && ts_mod_valid(m));assert(m->lane[0].point[38].y>m->lane[0].point[8].y);
    click(420,474);assert(!modulation_window.draw);
    /* Available destination list includes the board and its individual pedals. */
    click(1130,542);assert(modulation_window.menu==1);click(500,200);assert(m->map_count==1 && m->map[0].child==c.effect[board].board.slot[0].id && modulation_window.menu==2);
    ModChoice choices[64];int count=mod_choices(choices,64,m),cut=-1;
    for(int i=0;i<count;++i)if(choices[i].field==TS_MOD_FILTER+TS_CF_L_CUT)cut=i;
    assert(cut>=0);click(500,164+cut*32);assert(!modulation_window.menu && m->map[0].field==TS_MOD_FILTER+TS_CF_L_CUT);
    drag(960,612,1020,612);drag(1080,612,990,612);assert(m->map[0].low>.19f && m->map[0].high<.71f);
    float high=m->map[0].high;key(SDLK_z,KMOD_CTRL);assert(m->map[0].high==1);key(SDLK_y,KMOD_CTRL);assert(m->map[0].high==high);
    /* Shared lanes have independently inverted assignment ranges. */
    click(1130,542);click(500,232);assert(m->map_count==2 && m->map[1].child==c.effect[board].board.slot[1].id);click(500,164);assert(!modulation_window.menu);
    drag(960,642,1230,642);drag(1080,642,840,642);assert(m->map[1].low>m->map[1].high);
    click(1180,612);assert(!m->map[0].enabled);click(1180,612);assert(m->map[0].enabled);
    click(890,32);assert(m->retrigger==1);click(90,625);assert(m->count==5 && modulation_window.first==4);key(SDLK_z,KMOD_CTRL);assert(m->count==4);
    modulation_window.lane=modulation_window.first=0;
    curve_controls(m);
    float seconds=m->lane[0].seconds;wheel(800,90,1,0);assert(m->lane[0].seconds>seconds);
    wheel(1110,90,1,0);assert(fabsf(m->lane[0].seconds-seconds)<1e-6f);
    drag(60,736,210,736);assert(matrix_window.controls.effect[mod].mix==1);
    drag(210,736,60,736);assert(matrix_window.controls.effect[mod].mix==.5f);key(SDLK_z,KMOD_CTRL);
    click(1150,137);assert(m->lane[0].unlinked);float frequency=m->lane[0].frequency;seconds=m->lane[0].seconds;
    wheel(800,90,2,0);assert(m->lane[0].seconds>seconds && m->lane[0].frequency==frequency);seconds=m->lane[0].seconds;
    wheel(1110,90,2,0);assert(m->lane[0].frequency>frequency && m->lane[0].seconds==seconds);
    click(1150,137);assert(!m->lane[0].unlinked && m->lane[0].count==64);
    float low=m->map[0].low;wheel(960,612,1,0);assert(fabsf(m->map[0].low-low-.01f)<1e-6f);
    wheel(960,612,1,1);assert(fabsf(m->map[0].low-low)<1e-6f);
    SDL_SetModState(KMOD_SHIFT);wheel(960,612,1,0);SDL_SetModState(KMOD_NONE);assert(fabsf(m->map[0].low-low-.001f)<1e-6f);
    drag(978,624,1003,624);assert(fabsf(m->map[0].low-.75f)<1e-6f);key(SDLK_z,KMOD_CTRL);assert(fabsf(m->map[0].low-low-.001f)<1e-6f);
    drag(1073,624,1068,624);assert(fabsf(m->map[0].high-.2f)<1e-6f);
    assert(ts_matrix_valid(&matrix_window.controls));
    /* Stable identities remain the destination even when the pedal moves. */
    TsPedalboardControls *b=&matrix_window.controls.effect[board].board;
    TsSisterFxSlotControls temp=b->slot[0];b->slot[0]=b->slot[3];b->slot[3]=temp;
    assert(ts_mod_target(&matrix_window.controls,&m->map[0])==board);mx_commit_controls(0,&audio,&ui);
    snprintf(m->lane[0].name,sizeof(m->lane[0].name),"SLOW WEATHER");m->lane[0].timing=TS_MOD_FREE;m->lane[0].seconds=90;m->lane[0].mode=TS_MOD_LOOP;ts_mod_shape(&m->lane[0],2,8);
    mx_commit_controls(0,&audio,&ui);
    if(argc>1){m->map[0].low=.2f;m->map[0].high=.7f;mx_commit_controls(0,&audio,&ui);shot(argv[1]);}
    SDL_Event audition;SDL_zero(audition);audition.type=SDL_KEYDOWN;audition.key.windowID=modulation_window.id;audition.key.keysym.sym=SDLK_q;assert(!modulation_event(&audition,0,&audio,&ui,&pages,&bank));
    const int sizes[][2]={{960,600},{1600,1000},{1000,900}};
    for(int i=0;i<3;++i){SDL_SetWindowSize(modulation_window.window,sizes[i][0],sizes[i][1]);click(430,90);assert(m->lane[0].mode==TS_MOD_ONESHOT);click(310,90);assert(!m->lane[0].mode);}
    key(SDLK_ESCAPE,0);assert(!modulation_window.visible);
    assert(modulation_connect(c.effect[mod].id,TS_MATRIX_EFFECT_BASE+board,0,&audio,&ui,&pages,&bank));assert(!modulation_window.visible && !modulation_window.menu && m->map_count==3);
    int other=ts_matrix_effect_add(&matrix_window.controls,TS_MATRIX_FX_MODULATOR);assert(other>=0);uint32_t other_id=matrix_window.controls.effect[other].id;mx_commit_controls(0,&audio,&ui);
    modulation_open(c.effect[mod].id);click(755,32);assert(modulation_window.node==other_id);click(640,32);assert(modulation_window.node==c.effect[mod].id);
    drag(50,667,200,667);assert(matrix_window.controls.effect[mod].modulation.audio_amount==.5f);key(SDLK_z,KMOD_CTRL);
    modulation_hide();assert(modulation_connect(other_id,TS_MATRIX_EFFECT_BASE+mod,0,&audio,&ui,&pages,&bank));assert(!modulation_window.visible && matrix_window.controls.effect[other].modulation.map_count==1);
    assert(modulation_connect(c.effect[mod].id,TS_MATRIX_EFFECT_BASE+other,0,&audio,&ui,&pages,&bank));assert(matrix_window.controls.effect[mod].modulation.map_count==3 && ts_matrix_valid(&matrix_window.controls));
    modulation_open(c.effect[mod].id);ts_matrix_effect_remove(&matrix_window.controls,mod);assert(!modulation_resolve() && !modulation_window.visible);
    modulation_close();matrix_history_clear();ts_sister_runtime_disable(&audio.sister);ts_sample_pages_free(&pages);ts_instrument_free(&bank);SDL_Quit();
    puts("Modulation native curve edits, menus, ranges, undo, resizing, stable IDs, connect gesture and Escape passed.");return 0;
}
