#include "../third_party/tapehead/application/src/ft2_video.h"
#include "../third_party/tapehead/application/src/ft2_palette.h"
/* Visual defaults must not become musical state or erase custom palettes. */
static void check_tracker_logo_render(int width,int height,const char *image) {
    SDL_Surface *screen=SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_ARGB8888);assert(screen);
    SDL_Renderer *renderer=SDL_CreateSoftwareRenderer(screen);assert(renderer);
    TsFramebuffer *fb=malloc(sizeof(*fb));assert(fb);ts_ui_render(fb,test_ui,test_bank);
    SDL_Surface *base=SDL_CreateRGBSurfaceFrom(fb->pixels,640,400,32,640*4,0xff0000,0xff00,0xff,0xff000000);assert(base);
    SDL_Texture *texture=SDL_CreateTextureFromSurface(renderer,base);assert(texture);
    main_surface_copy(renderer,texture);SDL_RenderPresent(renderer);
    size_t bytes=(size_t)screen->pitch*height;void *before=malloc(bytes);assert(before);memcpy(before,screen->pixels,bytes);
    embedded_logo_copy(renderer,0);SDL_RenderPresent(renderer);assert(!memcmp(before,screen->pixels,bytes));
    embedded_logo_copy(renderer,1);SDL_RenderPresent(renderer);
    SDL_Rect rect;assert(ts_tapehead_logo(NULL,&rect));rect.x+=4;
    int left=rect.x*width/640,right=(rect.x+rect.w)*width/640;
    int top=rect.y*height/400,bottom=(rect.y+rect.h)*height/400,changed=0;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        uint32_t a=*(uint32_t*)((char*)before+y*screen->pitch+x*4);
        uint32_t b=*(uint32_t*)((char*)screen->pixels+y*screen->pitch+x*4);
        if(a!=b){assert(x>=left && x<right && y>=top && y<bottom);++changed;}
    }
    assert(changed);SDL_Texture *cached=embedded_host.logo_texture;
    embedded_logo_copy(renderer,1);assert(embedded_host.logo_texture==cached);
    if(image)assert(!SDL_SaveBMP(screen,image));
    SDL_DestroyTexture(embedded_host.logo_texture);embedded_host.logo_texture=NULL;embedded_host.logo_revision=0;
    free(before);SDL_DestroyTexture(texture);SDL_FreeSurface(base);free(fb);SDL_DestroyRenderer(renderer);SDL_FreeSurface(screen);
}
static void test_tracker_appearance(const char *image) {
    assert((video.palette[PAL_BCKGRND]&0xffffffu)==0x0b0d10);
    assert((video.palette[PAL_FORGRND]&0xffffffu)==0xe0d6e9);
    assert((video.palette[PAL_PATTERN_NOTE]&0xffffffu)==0xd5b37c);
    assert((video.palette[PAL_PATTERN_EFFECT]&0xffffffu)==0x81cec8);
    const uint32_t *portrait;SDL_Rect rect;
    unsigned revision=ts_tapehead_logo(&portrait,&rect);assert(revision && portrait);
    assert(rect.x==121 && rect.y==2 && rect.w==28 && rect.h==28);
    assert(ts_tapehead_logo(&portrait,&rect)==revision);
    uint64_t score=ts_sister_tracker_hash(&test_pages->tracker);
    TsPalette changed=test_ui->palette;
    ts_theme_override(&changed,TS_THEME_TRACKER,TS_COLOR_DATA5,0xffc08ddd);
    ts_tapehead_theme_apply(&changed);
    assert(ts_tapehead_logo(&portrait,&rect)!=revision);
    ts_tapehead_theme_apply(&test_ui->palette);
    checkpoint();assert(ts_sister_tracker_hash(&test_pages->tracker)==score);
    int enabled=fastTracksPOCMasterIsEnabled();click(200,16);
    assert(fastTracksPOCMasterIsEnabled()!=enabled);click(200,16);
    assert(fastTracksPOCMasterIsEnabled()==enabled);
    ts_tapehead_show_config();assert(!ts_tapehead_logo(&portrait,&rect));
    press(SDLK_ESCAPE,SDL_SCANCODE_ESCAPE,KMOD_NONE);ts_tapehead_tick();
    assert(ts_tapehead_logo(&portrait,&rect));
    check_tracker_logo_render(640,400,NULL);
    check_tracker_logo_render(800,500,NULL);
    check_tracker_logo_render(960,600,NULL);
    check_tracker_logo_render(1280,800,image);
    check_tracker_logo_render(1366,768,NULL);
    TsPalette custom;ts_palette_tracker_default(&custom);custom.colors[TS_PALETTE_PATTERN_NOTE]=0x336699;
    assert(ts_palette_save(&custom,"appearance-custom.pal",test_error,sizeof(test_error)));
    assert(ts_tapehead_palette_import("appearance-custom.pal",test_error,sizeof(test_error)));
    assert((video.palette[PAL_PATTERN_NOTE]&0xffffffu)==0x35659a);
    assert(ts_tapehead_preferences_save("appearance-custom.cfg",test_error,sizeof(test_error)));
    ts_tapehead_theme_apply(&changed);ts_tapehead_theme_apply(&test_ui->palette);
    assert(ts_tapehead_preferences_load("appearance-custom.cfg",test_error,sizeof(test_error)));
    assert((video.palette[PAL_PATTERN_NOTE]&0xffffffu)==0x35659a);
    remove("appearance-custom.pal");remove("appearance-custom.cfg");
    ts_tapehead_close();ts_sample_pages_free(test_pages);ts_instrument_free(test_bank);SDL_DestroyWindow(test_window);SDL_Quit();
    puts("Mosaic tracker default, custom palette persistence, theme-only logo caching, visibility and FASTRACKS hitbox passed.");
}
