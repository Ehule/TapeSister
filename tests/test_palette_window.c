#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>
static TsUiState u;
static TsInstrument bank;
static SisterWindow sister;
static TsFramebuffer fb;
static SDL_Window *host;
static void click(int x,int y)
{
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=palette_window.id;e.button.button=SDL_BUTTON_LEFT;e.button.x=x;e.button.y=y;
    assert(palette_window_event(&e,host,&u,&sister));e.type=SDL_MOUSEBUTTONUP;assert(palette_window_event(&e,host,&u,&sister));
}
static void key(SDL_Keycode sym,Uint16 mod)
{
    SDL_Event e={0};e.type=SDL_KEYDOWN;e.key.windowID=palette_window.id;e.key.keysym.sym=sym;e.key.keysym.mod=mod;
    assert(palette_window_event(&e,host,&u,&sister));
}
static void hex(const char *value)
{
    click(355,410);key(SDLK_a,KMOD_CTRL);SDL_Event e={0};e.type=SDL_TEXTINPUT;e.text.windowID=palette_window.id;
    snprintf(e.text.text,sizeof(e.text.text),"%s",value);assert(palette_window_event(&e,host,&u,&sister));key(SDLK_RETURN,0);
}
static void snapshot(const char *path,const uint32_t *pixels,int width,int height)
{
    SDL_Surface *s=SDL_CreateRGBSurfaceFrom((void*)pixels,width,height,32,width*4,0xff0000,0xff00,0xff,0);
    assert(s&&!SDL_SaveBMP(s,path));SDL_FreeSurface(s);
}
static void screenshots(void)
{
    char path[100],error[160];assert(ts_instrument_generate(&bank,TS_GENERATOR_METALLIC,777,error,sizeof(error)));
    for(int i=0;i<8;++i){ts_palette_preset(&u.palette,i);ts_ui_render(&fb,&u,&bank);snprintf(path,sizeof(path),"palette-canvas-%d.bmp",i);snapshot(path,fb.pixels,640,400);
        sister.model.fx_page=1;ts_sister_ui_render(&fb,&sister.model,&u.palette);snprintf(path,sizeof(path),"palette-pedals-%d.bmp",i);snapshot(path,fb.pixels,640,400);
        matrix_window.palette=u.palette;matrix_window.pixels=calloc(MATRIX_W*MATRIX_H,4);assert(matrix_window.pixels);
        static AudioState a;static TsSamplePages pages;ts_matrix_default(&matrix_window.controls);matrix_window.controls.enabled=1;matrix_window.outputs=4;matrix_window.inputs=2;matrix_window.processors=0xffffffff;
        assert(ts_sample_pages_init(&pages,error,sizeof(error)));mx_rows(&pages,&bank);matrix_window.selected=0;
        mx_paint(&a,&pages,&bank);snprintf(path,sizeof(path),"palette-router-%d.bmp",i);snapshot(path,matrix_window.pixels,MATRIX_W,MATRIX_H);free(matrix_window.pixels);matrix_window.pixels=NULL;ts_sample_pages_free(&pages);
    }
    ts_palette_preset(&u.palette,1);palette_window.page=2;palette_window.group=TS_THEME_PEDALBOARD;palette_window.role=TS_COLOR_DATA6;
    pw_hex(&u.palette);pw_paint(&u);snapshot("palette-editor.bmp",palette_window.pixels,PALETTE_W,PALETTE_H);
    palette_window.page=1;pw_paint(&u);snapshot("palette-readability.bmp",palette_window.pixels,PALETTE_W,PALETTE_H);
}
int main(int argc,char **argv)
{
    SDL_SetMainReady();SDL_setenv("SDL_VIDEODRIVER","dummy",1);assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER));
    host=SDL_CreateWindow("Palette fixture",0,0,640,400,0);assert(host);
    ts_ui_init(&u);ts_instrument_init(&bank);ts_sister_ui_model_init(&sister.model,&u.config);
    TsPalette original=u.palette;u.config_open=1;begin_palette(&u);
    assert(palette_window.visible&&!u.palette_open&&!u.config_open&&!ui_dialog_open(&u));
    click(205,58);assert(u.palette.theme.preset==1); /* Soft */
    click(30,566);assert(!memcmp(&u.palette,&original,sizeof(original))); /* Undo */
    click(110,566);assert(u.palette.theme.preset==1); /* Redo */
    click(210,566);assert(!memcmp(&u.palette,&original,sizeof(original))); /* Before */
    click(210,566);assert(u.palette.theme.preset==1);
    click(495,123);click(40,202);assert(palette_window.group==TS_THEME_PEDALBOARD&&!palette_window.follow);
    hex("102030");assert(u.palette.theme.overrides[TS_THEME_PEDALBOARD][TS_COLOR_DATA1]==0xff102030);
    click(80,123);hex("abcdef");assert(u.palette.theme.overrides[0][TS_COLOR_BACKGROUND]==0xffabcdef);
    click(333,440);assert(u.palette.theme.overrides[0][TS_COLOR_BACKGROUND]!=0xffabcdef);
    key(SDLK_z,KMOD_CTRL);assert(u.palette.theme.overrides[0][TS_COLOR_BACKGROUND]==0xffabcdef); /* whole drag is one history step */
    click(590,566);assert(palette_window.page==3);hex("aabbcc");assert(u.palette.colors[TS_PALETTE_PATTERN_TEXT]==0xffaabbcc);
    click(400,18);assert(palette_window.follow);u.tracker_open=1;pw_follow(host,&u,&sister,SDL_GetWindowID(host));assert(palette_window.group==TS_THEME_TRACKER);
    SDL_Event outside={0};outside.type=SDL_MOUSEBUTTONDOWN;outside.button.windowID=SDL_GetWindowID(host);
    assert(!palette_window_event(&outside,host,&u,&sister)); /* modeless event ownership */
    TsPalette edited=u.palette;click(210,566);palette_window_hide(&u,0);assert(!memcmp(&u.palette,&edited,sizeof(edited))); /* close Before keeps edited */
    palette_window_show(&u);click(360,566);assert(!memcmp(&u.palette,&edited,sizeof(edited)));
    char err[160],saved_path[TS_BROWSER_PATH_MAX];TsPalette loaded;
    click(220,599);assert(palette_window.browser.mode==TS_BROWSER_TRACKER_PALETTE_EXPORT);
    key(SDLK_a,KMOD_CTRL);SDL_Event name={0};name.type=SDL_TEXTINPUT;name.text.windowID=palette_window.id;
    strcpy(name.text.text,"Palette Window Fixture.pal");assert(palette_window_event(&name,host,&u,&sister));
    assert(ts_browser_destination_path(&palette_window.browser,saved_path,sizeof(saved_path)));remove(saved_path);
    click(40,590);if(palette_window.browser.mode!=TS_BROWSER_CLOSED)fprintf(stderr,"Palette file action: %s\n",palette_window.browser.message);assert(palette_window.browser.mode==TS_BROWSER_CLOSED);
    assert(ts_palette_load(&loaded,saved_path,err,sizeof(err)));assert(!memcmp(&loaded,&edited,sizeof(loaded)));
    click(220,599);snprintf(palette_window.browser.filename,sizeof(palette_window.browser.filename),"Palette Window Fixture.pal");
    click(40,590);assert(palette_window.browser.overwrite_armed);click(40,590);if(palette_window.browser.mode!=TS_BROWSER_CLOSED)fprintf(stderr,"Palette file action: %s\n",palette_window.browser.message);assert(palette_window.browser.mode==TS_BROWSER_CLOSED);
    click(205,58);assert(u.palette.theme.preset==1);
    click(40,599);TsBrowser *browser=&palette_window.browser;int file=-1;
    for(int i=0;i<browser->entry_count;++i)if(!strcmp(browser->entries[i].name,"Palette Window Fixture.pal"))file=i;
    assert(file>=0);ts_browser_select(browser,file);click(40,590);assert(!memcmp(&u.palette,&edited,sizeof(edited)));remove(saved_path);
    SDL_setenv("TAPESISTER_PALETTE","palette-shared-fixture.pal",1);remove("palette-shared-fixture.pal");
    assert(shared_palette_path(&u,saved_path,sizeof(saved_path))&&!strcmp(saved_path,"palette-shared-fixture.pal"));
    click(360,599);assert(ts_palette_load(&loaded,"palette-shared-fixture.pal",err,sizeof(err)));assert(!memcmp(&loaded,&edited,sizeof(loaded)));
    click(360,599);assert(palette_window.overwrite_shared);click(360,599);assert(!palette_window.overwrite_shared);remove("palette-shared-fixture.pal");
    palette_window_hide(&u,0);SDL_Event shortcut={0};shortcut.type=SDL_KEYDOWN;shortcut.key.windowID=SDL_GetWindowID(host);shortcut.key.keysym.sym=SDLK_p;
    shortcut.key.keysym.mod=KMOD_LCTRL|KMOD_LSHIFT;assert(!palette_window_event(&shortcut,host,&u,&sister)); /* CDP shortcut stays available */
    shortcut.key.keysym.mod=KMOD_LCTRL|KMOD_LALT;assert(palette_window_event(&shortcut,host,&u,&sister));assert(palette_window.visible);
    /* Native application hook changes pixels, not saved tracker preferences. */
    TsTapeHeadHost native={.window=host};assert(ts_tapehead_init(&native,48000,err,sizeof(err)));
    static TsSamplePages native_pages;assert(ts_sample_pages_init(&native_pages,err,sizeof(err)));
    assert(ts_tapehead_sync(&native_pages,&bank,48000,err,sizeof(err)));
    extern void tapeheadEmbeddedPaletteCurrent(uint8_t*);uint8_t native_before[62],native_after[62];
    tapeheadEmbeddedPaletteCurrent(native_before);ts_palette_preset(&u.palette,7);ts_tapehead_theme_apply(&u.palette);
    tapeheadEmbeddedPaletteCurrent(native_after);assert(!memcmp(native_before,native_after,62));
    ts_tapehead_service(1);const uint32_t *pixels=ts_tapehead_frame();assert(pixels);
    size_t paper=0;for(int i=290*632;i<632*400;++i)if((pixels[i]&0xffffff)==0xf1ece0)++paper;assert(paper>30000);
    if(argc>1){snapshot("palette-tracker-paper.bmp",pixels,632,400);u.tracker_open=0;screenshots();}
    ts_tapehead_theme_apply(&original);tapeheadEmbeddedPaletteCurrent(native_after);assert(!memcmp(native_before,native_after,62));
    ts_tapehead_close();ts_sample_pages_free(&native_pages);palette_window_close();SDL_DestroyWindow(host);ts_instrument_free(&bank);SDL_Quit();
    puts("Floating palette event ownership, editing, history and native tracker passed");return 0;
}
