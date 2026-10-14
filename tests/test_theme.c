#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/palette.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
static double linear(double v){v/=255;return v<=.04045?v/12.92:pow((v+.055)/1.055,2.4);}
static double luminance(uint32_t c){return .2126*linear((c>>16)&255)+.7152*linear((c>>8)&255)+.0722*linear(c&255);}
int main(void)
{
    TsPalette p,loaded,original;char error[160];ts_palette_default(&original);p=original;
    for(int g=0;g<TS_THEME_GROUP_COUNT;++g)for(int r=0;r<TS_THEME_ROLE_COUNT;++r)
        assert(ts_theme_color(&p,g,r,0xff123456)==0xff123456);
    ts_theme_override(&p,TS_THEME_OVERALL,TS_COLOR_TEXT,0xffedcba9);
    assert(ts_theme_color(&p,TS_THEME_PRISM,TS_COLOR_TEXT,0xff123456)==0xffedcba9);
    ts_theme_override(&p,TS_THEME_ROUTER,TS_COLOR_TEXT,0xff112233);
    assert(ts_theme_color(&p,TS_THEME_ROUTER,TS_COLOR_TEXT,0xff123456)==0xff112233);
    ts_theme_reset_group(&p,TS_THEME_ROUTER);
    assert(ts_theme_color(&p,TS_THEME_ROUTER,TS_COLOR_TEXT,0xff123456)==0xffedcba9);
    /* A black explicit override is distinct from inheritance. Reverb is local. */
    ts_theme_override(&p,TS_THEME_PEDALBOARD,TS_COLOR_DATA6,0);
    assert(ts_theme_legacy(&p,TS_THEME_PEDALBOARD,TS_COLOR_DATA6,TS_PALETTE_STEREO_WAVE_RIGHT)==0xff000000);
    assert(ts_theme_legacy(&p,TS_THEME_CANVAS,TS_COLOR_DATA2,TS_PALETTE_STEREO_WAVE_RIGHT)==original.colors[TS_PALETTE_STEREO_WAVE_RIGHT]);
    for(int preset=0;preset<TS_THEME_PRESET_COUNT;++preset){
        ts_palette_preset(&p,preset);assert(p.theme.preset==preset);
        if(preset){double text=luminance(ts_theme_color(&p,TS_THEME_OVERALL,TS_COLOR_TEXT,0)),bg=luminance(ts_theme_color(&p,TS_THEME_OVERALL,TS_COLOR_CONTROL,0));
            assert((fmax(text,bg)+.05)/(fmin(text,bg)+.05)>=4.5);}
        for(int g=0;g<TS_THEME_GROUP_COUNT;++g)for(int r=0;r<TS_THEME_ROLE_COUNT;++r)
            ts_theme_override(&p,g,r,(uint32_t)((g+1)*12345+r*45678));
        p.theme.text_clarity=70;p.theme.grid_visibility=22;p.theme.selection_strength=80;
        assert(ts_palette_save(&p,"theme-roundtrip.pal",error,sizeof(error)));
        assert(ts_palette_load(&loaded,"theme-roundtrip.pal",error,sizeof(error)));
        assert(!memcmp(&p,&loaded,sizeof(p)));
    }
    /* Rejected metadata must not mutate the live palette. */
    FILE *f=fopen("theme-roundtrip.pal","ab");assert(f);fputs("ThemeGrid=101\n",f);fclose(f);
    assert(!ts_palette_load(&loaded,"theme-roundtrip.pal",error,sizeof(error)));
    assert(!memcmp(&p,&loaded,sizeof(p)));remove("theme-roundtrip.pal");
    ts_palette_preset(&p,7);assert(!strcmp(ts_theme_preset_name(7),"Sister"));
    assert(luminance(ts_theme_color(&p,TS_THEME_OVERALL,TS_COLOR_BACKGROUND,0))<.02);
    uint32_t before=ts_theme_color(&p,TS_THEME_ROUTER,TS_COLOR_TEXT,0);
    p.theme.text_clarity=100;assert(luminance(ts_theme_color(&p,TS_THEME_ROUTER,TS_COLOR_TEXT,0))>luminance(before));
    p.theme.text_clarity=0;assert(luminance(ts_theme_color(&p,TS_THEME_ROUTER,TS_COLOR_TEXT,0))<luminance(before));
    /* Custom light backgrounds still reverse clarity toward dark ink. */
    ts_theme_override(&p,TS_THEME_OVERALL,TS_COLOR_BACKGROUND,0xffeee8df);
    ts_theme_override(&p,TS_THEME_OVERALL,TS_COLOR_TEXT,0xff504050);p.theme.text_clarity=50;
    before=ts_theme_color(&p,TS_THEME_ROUTER,TS_COLOR_TEXT,0);
    p.theme.text_clarity=100;assert(luminance(ts_theme_color(&p,TS_THEME_ROUTER,TS_COLOR_TEXT,0))<luminance(before));
    p.theme.text_clarity=0;assert(luminance(ts_theme_color(&p,TS_THEME_ROUTER,TS_COLOR_TEXT,0))>luminance(before));
    ts_palette_preset(&p,0);assert(!memcmp(&p,&original,sizeof(p)));
    /* Native tracker Original uses its own saved color, including index zero. */
    for(int i=0;i<36;++i)assert((ts_theme_tracker_color(&p,i,0x102030)&0xffffff)==0x102030);
    puts("Theme inheritance, presets, contrast, compatibility and persistence passed");return 0;
}
