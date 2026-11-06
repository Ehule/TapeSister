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
static SDL_Window *main_window;
static int event(SDL_Event *e)
{return matrix_event(e,main_window,0,&audio,&ui,&pages,&bank);}
static void pointer(Uint32 type,int x,int y,int button,int clicks)
{
    int width,height;SDL_GetWindowSize(matrix_window.window,&width,&height);
    SDL_Event e;SDL_zero(e);e.type=type;
    if(type==SDL_MOUSEMOTION){e.motion.windowID=matrix_window.id;e.motion.x=x*width/MATRIX_W;e.motion.y=y*height/MATRIX_H;e.motion.state=SDL_BUTTON_LMASK;}
    else {e.button.windowID=matrix_window.id;e.button.x=x*width/MATRIX_W;e.button.y=y*height/MATRIX_H;e.button.button=button;e.button.clicks=clicks;}
    assert(event(&e));
}
static void click(int x,int y,int count)
{pointer(SDL_MOUSEBUTTONDOWN,x,y,SDL_BUTTON_LEFT,count);pointer(SDL_MOUSEBUTTONUP,x,y,SDL_BUTTON_LEFT,count);}
static void key(SDL_Keycode sym,int mod)
{SDL_Event e;SDL_zero(e);e.type=SDL_KEYDOWN;e.key.windowID=matrix_window.visible?matrix_window.id:SDL_GetWindowID(main_window);e.key.keysym.sym=sym;e.key.keysym.mod=mod;assert(event(&e));}
static FxCanvasNode *node(int row)
{for(int i=0;i<fx_canvas.count;++i)if(fx_canvas.node[i].row.kind==2 && fx_canvas.node[i].row.index==row)return &fx_canvas.node[i];return NULL;}
static FxCanvasNode *instance(uint32_t id)
{for(int i=0;i<fx_canvas.count;++i)if(fx_canvas.node[i].row.kind==4 && fx_canvas.node[i].row.tile==id)return &fx_canvas.node[i];return NULL;}
static void screenshot(const char *path)
{
    matrix_window.palette=ui.palette;
    fx_paint(&audio,&pages,&bank);
    SDL_Surface *s=SDL_CreateRGBSurfaceFrom(matrix_window.pixels,MATRIX_W,MATRIX_H,32,MATRIX_W*4,0xff0000,0xff00,0xff,0);
    assert(s && !SDL_SaveBMP(s,path));SDL_FreeSurface(s);
}
static void view_codec(void)
{
    static TsMatrixLayout read;
    TsMatrixLayout *l=matrix_window.layout;FILE *f=tmpfile();assert(f && ts_matrix_layout_write(f,l));rewind(f);ts_matrix_layout_default(&read);
    char line[4096];while(fgets(line,sizeof(line),f)){line[strcspn(line,"\r\n")]=0;char *eq=strchr(line,'=');assert(eq);*eq++=0;assert(ts_matrix_layout_read(&read,line,eq)==1);}fclose(f);
    assert(ts_matrix_layout_valid(&read) && !memcmp(l,&read,sizeof(read)));
    assert(ts_matrix_layout_read(&read,"FxCanvas.View","nan,0,1")==-1);
    assert(ts_matrix_layout_read(&read,"FxCanvas.View","0,0,0")==-1);
    assert(ts_matrix_layout_read(&read,"FxCanvas.Node9999","4,1,0,0")==-1);
}
int main(int argc,char **argv)
{
    SDL_SetMainReady();SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER));
    main_window=SDL_CreateWindow("Mosaic",0,0,1280,800,0);assert(main_window);
    ts_ui_init(&ui);ts_instrument_init(&bank);char error[256];assert(ts_sample_pages_init(&pages,error,sizeof(error)));
    ui.mosaic=pages.mosaic;ui.mosaic_open=1;ts_sister_runtime_init(&audio.sister);audio.output_rate=48000;audio.output_device_channels=2;
    TsMatrixControls c=audio.sister.matrix.controls;c.enabled=1;
    int rev=ts_matrix_effect_add(&c,TS_SISTER_FX_REVERB),trans=ts_matrix_effect_add(&c,TS_SISTER_FX_TRANSIENT),
        grain=ts_matrix_effect_add(&c,TS_SISTER_FX_GRAIN),dist=ts_matrix_effect_add(&c,TS_SISTER_FX_DISTORTION),
        meta=ts_matrix_effect_add(&c,TS_MATRIX_FX_META),comp=ts_matrix_effect_add(&c,TS_MATRIX_FX_COMPRESSOR);
    assert(comp>=0);snprintf(c.effect[meta].meta.name,32,"ELECTRICAL MASS");
    memset(c.row[TS_MATRIX_MOSAIC].matrix_gain,0,sizeof(c.row[0].matrix_gain));
    c.row[TS_MATRIX_MOSAIC].matrix_gain[TS_MATRIX_EFFECT_BASE+rev]=710;
    c.row[TS_MATRIX_MOSAIC].matrix_gain[TS_MATRIX_EFFECT_BASE+grain]=710;
    c.row[TS_MATRIX_EFFECT_BASE+rev].matrix_gain[TS_MATRIX_EFFECT_BASE+trans]=1000;
    c.row[TS_MATRIX_EFFECT_BASE+trans].matrix_gain[TS_MATRIX_EFFECT_BASE+meta]=1000;
    c.row[TS_MATRIX_EFFECT_BASE+grain].matrix_gain[TS_MATRIX_EFFECT_BASE+dist]=1000;
    c.row[TS_MATRIX_EFFECT_BASE+dist].matrix_gain[TS_MATRIX_EFFECT_BASE+meta]=1000;
    c.row[TS_MATRIX_EFFECT_BASE+meta].matrix_gain[TS_MATRIX_MAIN]=1000;
    assert(ts_matrix_effects_restore(&audio.sister.matrix_effects,&c,48000));assert(ts_matrix_set(&audio.sister.matrix,&c,48000));ui.config.matrix=c;
    TsMatrixEffectNode *history=audio.sister.matrix_effects.current[rev];
    key(SDLK_F9,KMOD_SHIFT);assert(matrix_window.visible && matrix_window.canvas);assert(audio.sister.matrix_effects.current[rev]==history);
    fx_build(&pages,&bank);fx_fit();assert(node(TS_MATRIX_MOSAIC) && instance(c.effect[meta].id));
    assert(fx_canvas.dest[TS_MATRIX_EFFECT_SC_BASE+rev]==-1);
    assert(fx_canvas.dest[TS_MATRIX_EFFECT_SC_BASE+comp]>=0);
    if(argc>1)screenshot(argv[1]);
    if(argc>3) {
        static TsFramebuffer fb;ts_ui_render(&fb,&ui,&bank);
        SDL_Surface *s=SDL_CreateRGBSurfaceFrom(fb.pixels,640,400,32,640*4,0xff0000,0xff00,0xff,0);assert(s && !SDL_SaveBMP(s,argv[3]));SDL_FreeSurface(s);
    }
    /* A spatial drag is saved/undoable without replacing a live processor. */
    FxCanvasNode *n=instance(c.effect[meta].id);int x=fx_x(n->x)+20,y=fx_y(n->y)+20;
    pointer(SDL_MOUSEBUTTONDOWN,x,y,SDL_BUTTON_LEFT,1);pointer(SDL_MOUSEMOTION,x+30,y+20,0,0);pointer(SDL_MOUSEBUTTONUP,x+30,y+20,SDL_BUTTON_LEFT,1);
    assert(matrix_window.layout->canvas_count==1 && audio.sister.matrix_effects.current[rev]==history);
    view_codec();key(SDLK_z,KMOD_CTRL);assert(!matrix_window.layout->canvas_count && audio.sister.matrix_effects.current[rev]==history);
    key(SDLK_y,KMOD_CTRL);assert(matrix_window.layout->canvas_count==1);fx_build(&pages,&bank);
    n=instance(c.effect[meta].id);click(fx_x(n->x)+20,fx_y(n->y)+20,2);assert(fx_canvas.meta==c.effect[meta].id);
    float before=matrix_window.controls.effect[meta].meta.macro[0];click(230,744,1);
    assert(matrix_window.controls.effect[meta].meta.macro[0]>before && audio.sister.matrix_effects.current[rev]==history);
    if(argc>2)screenshot(argv[2]);key(SDLK_ESCAPE,0);assert(!fx_canvas.meta && matrix_window.visible);
    /* A cable gesture changes the SAME graph viewed by F9. */
    fx_build(&pages,&bank);FxCanvasNode *source=node(TS_MATRIX_MOSAIC),*target=instance(c.effect[comp].id);assert(source && target);
    int sx=fx_x(source->x+FX_NODE_W),sy=fx_y(source->y+FX_NODE_H*.5f),tx=fx_x(target->x),ty=fx_y(target->y+FX_NODE_H*.5f);
    pointer(SDL_MOUSEBUTTONDOWN,sx,sy,SDL_BUTTON_LEFT,1);assert(matrix_window.drag==41);
    pointer(SDL_MOUSEBUTTONUP,tx,ty,SDL_BUTTON_LEFT,1);
    assert(audio.sister.matrix.controls.row[TS_MATRIX_MOSAIC].matrix_gain[TS_MATRIX_EFFECT_BASE+comp]==1000);
    fx_canvas.source=source->row;fx_canvas.wire=0;fx_canvas.destination=TS_MATRIX_EFFECT_BASE+comp;
    key(SDLK_DELETE,0);assert(!audio.sister.matrix.controls.row[TS_MATRIX_MOSAIC].matrix_gain[TS_MATRIX_EFFECT_BASE+comp]);
    key(SDLK_z,KMOD_CTRL);assert(audio.sister.matrix.controls.row[TS_MATRIX_MOSAIC].matrix_gain[TS_MATRIX_EFFECT_BASE+comp]==1000);
    /* A self-loop is rejected by the shared validator, not silently delayed. */
    fx_build(&pages,&bank);FxCanvasNode *rn=instance(c.effect[rev].id);assert(rn);
    pointer(SDL_MOUSEBUTTONDOWN,fx_x(rn->x+FX_NODE_W),fx_y(rn->y+FX_NODE_H*.5f),SDL_BUTTON_LEFT,1);
    pointer(SDL_MOUSEBUTTONUP,fx_x(rn->x),fx_y(rn->y+FX_NODE_H*.5f),SDL_BUTTON_LEFT,1);
    assert(!audio.sister.matrix.controls.row[TS_MATRIX_EFFECT_BASE+rev].matrix_gain[TS_MATRIX_EFFECT_BASE+rev]);
    key(SDLK_F9,0);assert(!matrix_window.canvas && matrix_window.visible);
    assert(matrix_window.controls.row[TS_MATRIX_MOSAIC].matrix_gain[TS_MATRIX_EFFECT_BASE+comp]==1000);
    key(SDLK_F9,KMOD_SHIFT);assert(matrix_window.canvas && audio.sister.matrix_effects.current[rev]==history);
    fx_canvas.wire=-1;float zoom=matrix_window.layout->canvas_zoom;
    SDL_Event wheel;SDL_zero(wheel);wheel.type=SDL_MOUSEWHEEL;wheel.wheel.windowID=matrix_window.id;wheel.wheel.y=1;
#if SDL_VERSION_ATLEAST(2,26,0)
    wheel.wheel.mouseX=800;wheel.wheel.mouseY=400;
#else
    SDL_WarpMouseInWindow(matrix_window.window,800,400);
#endif
    assert(event(&wheel));assert(matrix_window.layout->canvas_zoom>zoom && audio.sister.matrix_effects.current[rev]==history);
    SDL_Event note;SDL_zero(note);note.type=SDL_KEYDOWN;note.key.windowID=matrix_window.id;note.key.keysym.sym=SDLK_q;
    assert(!event(&note) && note.key.windowID==SDL_GetWindowID(main_window));
    key(SDLK_F9,KMOD_SHIFT);assert(!matrix_window.visible);
    /* Scaled Mosaic header entry; no transport/key command is synthesized. */
    SDL_Event e;SDL_zero(e);e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=SDL_GetWindowID(main_window);e.button.button=SDL_BUTTON_LEFT;
    e.button.x=(TS_MOSAIC_FX_X+10)*2;e.button.y=(TS_MOSAIC_FX_Y+5)*2;assert(event(&e));assert(matrix_window.visible && matrix_window.canvas);
    SDL_SetWindowSize(matrix_window.window,1001,701);fx_paint(&audio,&pages,&bank);
    matrix_close();meta_fx_close();ts_sister_runtime_free(&audio.sister);ts_sample_pages_free(&pages);ts_instrument_free(&bank);SDL_DestroyWindow(main_window);SDL_Quit();
    puts("FX Canvas graph sharing, view history, stable identities, nested chain, macros and entry points passed.");return 0;
}
