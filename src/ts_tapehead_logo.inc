/* UI-only cached portrait. Full-resolution pixels are reused by the host at
   display DPI; the embedded framebuffer keeps a small software fallback. */
#include "ts_tracker_logo.inc"
static uint32_t tracker_logo_pixels[128*128],tracker_logo_small[28*28];
static uint32_t tracker_logo_theme[4];
static unsigned tracker_logo_revision;
static uint32_t logo_mix(uint32_t a,uint32_t b,unsigned amount) {
    uint32_t c=0xff000000u;
    for(int s=0;s<24;s+=8)c|=((((a>>s)&255)*(255-amount)+((b>>s)&255)*amount+127)/255)<<s;
    return c;
}
static void tracker_logo_prepare(void) {
    uint32_t colors[]={video.palette[PAL_BCKGRND],video.palette[PAL_DESKTOP],
        video.palette[PAL_PATTERN_VOLUME],video.palette[PAL_FORGRND]};
    if(tracker_logo_revision && !memcmp(colors,tracker_logo_theme,sizeof(colors)))return;
    memcpy(tracker_logo_theme,colors,sizeof(colors));
    uint32_t ramp[16];
    for(unsigned t=0;t<16;++t)ramp[t]=t<=11?logo_mix(colors[0],colors[2],t*255/11):
        logo_mix(colors[2],colors[3],(t-11)*255/4);
    for(unsigned i=0;i<128*128;++i) {
        unsigned packed=ts_tracker_logo[i];
        tracker_logo_pixels[i]=logo_mix(colors[1],ramp[packed>>4],(packed&15)*17);
    }
    for(unsigned y=0;y<28;++y)for(unsigned x=0;x<28;++x) {
        unsigned sums[3]={0},count=0;
        for(unsigned sy=y*128/28;sy<(y+1)*128/28;++sy)
            for(unsigned sx=x*128/28;sx<(x+1)*128/28;++sx) {
                uint32_t c=tracker_logo_pixels[sy*128+sx];
                for(int n=0;n<3;++n)sums[n]+=(c>>(n*8))&255;
                ++count;
            }
        uint32_t c=(uint32_t)PAL_DESKTOP<<24;
        for(int n=0;n<3;++n)c|=((sums[n]+count/2)/count)<<(n*8);
        tracker_logo_small[y*28+x]=c;
    }
    if(!++tracker_logo_revision)++tracker_logo_revision;
}
static int tracker_logo_pressed(void) {return pushButtons[PB_LOGO].state==PUSHBUTTON_PRESSED;}
static void tracker_logo_draw_portrait(void) {
    tracker_logo_prepare();int shift=tracker_logo_pressed();
    for(int y=0;y<28;++y)memcpy(video.frameBuffer+(y+2+shift)*SCREEN_W+121+shift,
        tracker_logo_small+y*28,28*sizeof(uint32_t));
}
unsigned ts_tapehead_logo(const uint32_t **pixels,SDL_Rect *rect) {
    if(!embed.initialized || !main_panel_visible() || !ui.scopesShown || ui.sysReqShown ||
        !pushButtons[PB_LOGO].visible)return 0;
    tracker_logo_prepare();int shift=tracker_logo_pressed();
    if(pixels)*pixels=tracker_logo_pixels;
    if(rect)*rect=(SDL_Rect){121+shift,2+shift,28,28};
    return tracker_logo_revision;
}
