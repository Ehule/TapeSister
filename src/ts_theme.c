/* Semantic palette roles. The Original theme uses each renderer's original
   color until a role is explicitly changed; all other presets resolve every
   interface surface, including formerly fixed colors. */
#include "tapesister/palette.h"
#include <string.h>
#define C(r,g,b) (0xff000000u|((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|(uint32_t)(b))
static const char *const group_keys[]={"Overall","Canvas","Sister","Pedalboard","Fallout","Prism","Mosaic","Tracker","Arp","Router","Eq","Spatial","Portal","Browser","Health","Midi","Transport"};
static const char *const group_names[]={"Overall look","Canvas / FM","Sister Machine","Pedalboard","Fallout","Prism / morph","Mosaic","TrackSister","Keyboard / ARP","Routing / inserts","EQ / compressor","Spatial / speakers","CDP Portal","Browser / dialogs","Audio health","MIDI learn","Transport"};
static const char *const role_keys[]={"Background","Panel","Control","Text","Dim","Edge","Grid","Selection","SelectedText","Focus","Activity","Warning","Error","Data1","Data2","Data3","Data4","Data5","Data6","Playhead","Muted","Solo","Record"};
static const char *const role_names[]={"Background","Panel fill","Control fill","Text","Secondary text","Borders","Grid","Selection fill","Selected text","Focus / pointer","Signal / activity","Warning","Error / clip","Warm accent","Cool accent","Blue accent","Green accent","Pink accent","Violet accent","Playhead","Muted / bypass","Solo","Recording"};
static const char *const presets[]={"Original","Soft","High Contrast","Night","Moss","Plum","Ice","Paper"};
/* bg, panel, control, text, dim, warm, cool, blue, green, pink, violet.
   Surface/text pairs are hand picked; state meaning is also carried by labels. */
static const uint32_t schemes[8][11]={
 {0x20201f,0x242125,0x4a3c4a,0xdedad6,0xa8a1ad,0xffae20,0x35ffff,0x147dff,0x18ff00,0xff47e7,0x9966ff},
 {0x222025,0x2d2931,0x514655,0xe6dfea,0xb6aabf,0xd5b37c,0x81cec8,0x8eb3de,0xa2b983,0xcf9bc7,0xada0d4},
 {0x101014,0x1d1d25,0x494454,0xfff9ff,0xc4bfce,0xffcc66,0x76ffff,0x9ec9ff,0xa8ee83,0xffa2dc,0xd5b2ff},
 {0x121016,0x1d1821,0x3d303f,0xc2b6c8,0x9e8ca7,0xbd9c6b,0x72a6a4,0x7c98b9,0x93a57b,0xb583a5,0x9c87b0},
 {0x1d231b,0x293026,0x46523d,0xe9e5cf,0xb8bba1,0xe2bd75,0x93c6b2,0x9cb7c1,0xb6d37a,0xd3a8a1,0xb4aac8},
 {0x221923,0x302331,0x59415a,0xf0dfee,0xc1a5bd,0xe6b776,0x97d1ce,0xaab3de,0xbac998,0xe5a2c7,0xc9a8e9},
 {0x172027,0x222f38,0x3c5260,0xe5f3f5,0xa6bcc8,0xd5c18e,0x86e0df,0xa4caff,0xa1d4bc,0xd4b5d7,0xbab6e0},
 {0xf1ece0,0xe6ded0,0xd2c6b9,0x28242e,0x59505c,0x875100,0x006b70,0x285b99,0x3d662c,0x974078,0x675298}
};
static uint32_t blend(uint32_t a,uint32_t b,int amount) {
    uint32_t c=0xff000000u;if(amount<0)amount=0;if(amount>100)amount=100;
    for(int n=0;n<3;++n){int k=n*8;c|=(uint32_t)((((a>>k)&255)*(100-amount)+((b>>k)&255)*amount)/100)<<k;}return c;
}
const char *ts_theme_group_name(int g){return g>=0&&g<TS_THEME_GROUP_COUNT?group_names[g]:"Overall look";}
const char *ts_theme_group_key(int g){return g>=0&&g<TS_THEME_GROUP_COUNT?group_keys[g]:"Overall";}
const char *ts_theme_role_key(int r){return r>=0&&r<TS_THEME_ROLE_COUNT?role_keys[r]:"Background";}
const char *ts_theme_preset_name(int p){return p>=0&&p<8?presets[p]:"Original";}
const char *ts_theme_role_name(int g,int r) {
    static const char *const data[TS_THEME_GROUP_COUNT][6]={
      [TS_THEME_CANVAS]={"Mono / left wave","Right wave","Loop markers","Mono sum","Zero / edge","Auxiliary"},
      [TS_THEME_SISTER]={"Head 1","Head 2","Head 3","Filter / mix","Input / erase","Auxiliary"},
      [TS_THEME_PEDALBOARD]={"Grains","Delay","Routing / feedback","Auxiliary","Distortion","Reverb"},
      [TS_THEME_FALLOUT]={"Pitch / rise","Mix / master","Bit / LFO","Noise / pan","Drop / skip","Auxiliary"},
      [TS_THEME_PRISM]={"Warm rays","Cool rays","Lens accents","Ready state","Warm ink","Violet ink"},
      [TS_THEME_MOSAIC]={"Source 2","Source 1","Source 5","Source 4","Source 3","Wave accent"},
      [TS_THEME_TRACKER]={"Note fields","Effect fields","Tuning fields","Instrument fields","Volume fields","Empty fields"},
      [TS_THEME_ARP]={"Sounding note","Selected note","LFO","Stored slot","Record accent","Auxiliary"},
      [TS_THEME_ROUTER]={"Send level","Connection","Route accent","Enabled","Feedback accent","Auxiliary"},
      [TS_THEME_EQ]={"EQ curve","Spectrum / nodes","Auxiliary","Level","Auxiliary","Auxiliary"},
      [TS_THEME_PORTAL]={"Source waveform","Result / effect","Loop / parameters","Ready / directory","Process / edge","Auxiliary"}
    };
    if(g>=0&&g<TS_THEME_GROUP_COUNT&&r>=TS_COLOR_DATA1&&r<=TS_COLOR_DATA6&&data[g][r-TS_COLOR_DATA1])return data[g][r-TS_COLOR_DATA1];
    if(g==TS_THEME_ROUTER&&r==TS_COLOR_WARNING)return "Feedback / warnings";
    return r>=0&&r<TS_THEME_ROLE_COUNT?role_names[r]:"Color";
}
static uint32_t factory(int preset,int role) {
    const uint32_t *s=schemes[preset>=0&&preset<8?preset:0];uint32_t bg=s[0]|0xff000000u,text=s[3]|0xff000000u;
    switch(role) {
      case TS_COLOR_BACKGROUND:return bg;case TS_COLOR_PANEL:return s[1]|0xff000000u;
      case TS_COLOR_CONTROL:return s[2]|0xff000000u;case TS_COLOR_TEXT:return text;case TS_COLOR_DIM:case TS_COLOR_MUTED:return s[4]|0xff000000u;
      case TS_COLOR_EDGE:return blend(bg,text,32);case TS_COLOR_GRID:return blend(bg,text,16);
      case TS_COLOR_SELECTION:return blend(bg,s[7]|0xff000000u,preset==7?22:28);
      case TS_COLOR_SELECTEDTEXT:return text;case TS_COLOR_FOCUS:return s[5]|0xff000000u;
      case TS_COLOR_ACTIVITY:case TS_COLOR_PLAYHEAD:return s[5]|0xff000000u;
      case TS_COLOR_WARNING:case TS_COLOR_SOLO:return (preset==7?0x8b5500:0xeabc65)|0xff000000u;
      case TS_COLOR_ERROR:case TS_COLOR_RECORD:return (preset==7?0xa82e38:0xf1848b)|0xff000000u;
      default:return s[5+(role-TS_COLOR_DATA1+6)%6]|0xff000000u;
    }
}
uint32_t ts_theme_original(int g,int r) {
    if(g==TS_THEME_ROUTER||g==TS_THEME_TRANSPORT) {
      switch(r){case TS_COLOR_BACKGROUND:return 0xff151c20u;case TS_COLOR_PANEL:return 0xff202b30u;case TS_COLOR_TEXT:return 0xffdbe8e3u;case TS_COLOR_DIM:return 0xff94aaa7u;case TS_COLOR_DATA2:return 0xff8addb4u;case TS_COLOR_ACTIVITY:return 0xffffce69u;default:break;}
    }
    if(g==TS_THEME_MOSAIC&&r>=TS_COLOR_DATA1&&r<=TS_COLOR_DATA5){const uint32_t c[]={0xffd5b37c,0xff81cec8,0xff8da9cb,0xffa2b983,0xffba93ba};return c[r-TS_COLOR_DATA1];}
    return factory(0,r);
}
static uint32_t resolved(const TsPalette *p,int g,int r,uint32_t original,int legacy) {
    if(!p||g<0||g>=TS_THEME_GROUP_COUNT||r<0||r>=TS_THEME_ROLE_COUNT)return original;
    uint32_t c=p->theme.overrides[g][r];
    if(!c)c=p->theme.overrides[TS_THEME_OVERALL][r];
    if(!c)c=legacy||p->theme.preset==0?original:factory(p->theme.preset,r);
    c|=0xff000000u;
    if(r==TS_COLOR_TEXT||r==TS_COLOR_DIM||r==TS_COLOR_SELECTEDTEXT) {
      int k=(int)p->theme.text_clarity-50;
      uint32_t bg=p->theme.overrides[g][TS_COLOR_BACKGROUND];
      if(!bg)bg=p->theme.overrides[0][TS_COLOR_BACKGROUND];
      if(!bg)bg=factory(p->theme.preset,TS_COLOR_BACKGROUND);
      int light=(((bg>>16)&255)*54+((bg>>8)&255)*183+(bg&255)*19)>160*256;
      if(k)c=blend(c,k<0?bg:(light?0xff000000u:0xffffffffu),k<0?-k:k);
    }
    if(r==TS_COLOR_GRID||r==TS_COLOR_SELECTION) {
      int k=(int)(r==TS_COLOR_GRID?p->theme.grid_visibility:p->theme.selection_strength)-50;
      if(k) {uint32_t bg=p->theme.overrides[g][TS_COLOR_BACKGROUND];if(!bg)bg=p->theme.overrides[0][TS_COLOR_BACKGROUND];
      if(!bg)bg=factory(p->theme.preset,TS_COLOR_BACKGROUND);
        c=blend(c,k<0?bg:(p->theme.preset==7?0xff000000u:0xffffffffu),k<0?-k:k);}
    }
    return c;
}
uint32_t ts_theme_color(const TsPalette *p,int g,int r,uint32_t original){return resolved(p,g,r,original,0);}
uint32_t ts_theme_legacy(const TsPalette *p,int g,int r,TsPaletteColor c){return resolved(p,g,r,p->colors[c],1);}
void ts_theme_override(TsPalette *p,int g,int r,uint32_t c){if(p&&g>=0&&g<TS_THEME_GROUP_COUNT&&r>=0&&r<TS_THEME_ROLE_COUNT)p->theme.overrides[g][r]=c|0xff000000u;}
void ts_theme_reset_group(TsPalette *p,int g){if(p&&g>=0&&g<TS_THEME_GROUP_COUNT)memset(p->theme.overrides[g],0,sizeof(p->theme.overrides[g]));}
int ts_theme_group_custom(const TsPalette *p,int g){if(!p||g<0||g>=TS_THEME_GROUP_COUNT)return 0;for(int r=0;r<TS_THEME_ROLE_COUNT;++r)if(p->theme.overrides[g][r])return 1;return 0;}
void ts_palette_preset(TsPalette *p,int preset) {
    if(!p||preset<0||preset>=8)return;
    ts_palette_default(p);p->theme.preset=(uint8_t)preset;
    if(!preset)return;
    static const int map[TS_PALETTE_COLOR_COUNT]={TS_COLOR_TEXT,TS_COLOR_SELECTION,TS_COLOR_SELECTEDTEXT,TS_COLOR_FOCUS,TS_COLOR_BACKGROUND,TS_COLOR_CONTROL,
    TS_COLOR_DATA1,TS_COLOR_DATA4,TS_COLOR_DATA5,TS_COLOR_DATA3,TS_COLOR_DATA2,TS_COLOR_DATA6,TS_COLOR_SELECTION,TS_COLOR_FOCUS,
    TS_COLOR_DATA1,TS_COLOR_DATA2,TS_COLOR_DATA4,TS_COLOR_DATA1,TS_COLOR_DATA2,TS_COLOR_DATA2,TS_COLOR_DATA1,TS_COLOR_RECORD,TS_COLOR_DATA4,TS_COLOR_ERROR,TS_COLOR_DATA1,TS_COLOR_DATA6,
    TS_COLOR_FOCUS,TS_COLOR_DATA2,TS_COLOR_DATA1,TS_COLOR_DATA5,TS_COLOR_DATA4,TS_COLOR_DATA3};
    for(int i=0;i<TS_PALETTE_COLOR_COUNT;++i)p->colors[i]=factory(preset,map[i]);
}

/* FT2's stable 36-entry tagged palette layout, shared by both tracker renderers. */
uint32_t ts_theme_tracker_color(const TsPalette *p,int i,uint32_t original)
{
    static const int roles[]={TS_COLOR_BACKGROUND,TS_COLOR_TEXT,TS_COLOR_SELECTION,TS_COLOR_SELECTEDTEXT,
        TS_COLOR_PANEL,TS_COLOR_TEXT,TS_COLOR_CONTROL,TS_COLOR_TEXT,TS_COLOR_EDGE,TS_COLOR_EDGE,
        TS_COLOR_EDGE,TS_COLOR_EDGE,TS_COLOR_FOCUS,TS_COLOR_FOCUS,TS_COLOR_FOCUS,TS_COLOR_FOCUS,
        TS_COLOR_DATA3,TS_COLOR_SELECTION,TS_COLOR_SELECTION,TS_COLOR_FOCUS,TS_COLOR_RECORD,TS_COLOR_DATA4,
        TS_COLOR_DATA1,TS_COLOR_DATA4,TS_COLOR_DATA5,TS_COLOR_DATA3,TS_COLOR_DATA2,TS_COLOR_DATA6,
        TS_COLOR_SELECTION,TS_COLOR_PLAYHEAD,TS_COLOR_PLAYHEAD,TS_COLOR_PLAYHEAD,TS_COLOR_ACTIVITY,
        TS_COLOR_WARNING,TS_COLOR_PLAYHEAD,TS_COLOR_PLAYHEAD};
    static const int legacy[]={-1,0,1,2,4,-1,5,-1,-1,-1,-1,-1,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        6,7,8,9,10,11,12,19,20,21,22,23,24,25};
    if(!p||i<0||i>=36)return original;
    int role=roles[i],key=legacy[i];
    TsPalette defaults;ts_palette_default(&defaults);
    if(key>=0 && (p->colors[key]&0xffffffu)!=(defaults.colors[key]&0xffffffu))
        return ts_theme_legacy(p,TS_THEME_TRACKER,role,(TsPaletteColor)key);
    return ts_theme_color(p,TS_THEME_TRACKER,role,original);
}
/* Representative swatches use the same legacy source as the drawing path.
   Original has a few deliberately varied shades within a role. */
uint32_t ts_theme_preview(const TsPalette *p,int g,int r)
{
    int key=-1;
    switch(r){case TS_COLOR_BACKGROUND:key=TS_PALETTE_DESKTOP;break;
      case TS_COLOR_CONTROL:key=TS_PALETTE_BUTTONS;break;case TS_COLOR_TEXT:key=TS_PALETTE_PATTERN_TEXT;break;
      case TS_COLOR_SELECTION:key=TS_PALETTE_BLOCK_MARK;break;case TS_COLOR_SELECTEDTEXT:key=TS_PALETTE_TEXT_ON_BLOCK;break;
      case TS_COLOR_FOCUS:key=TS_PALETTE_MOUSE;break;
      case TS_COLOR_DATA1:key=TS_PALETTE_PATTERN_NOTE;break;case TS_COLOR_DATA2:key=TS_PALETTE_PATTERN_EFFECT;break;
      case TS_COLOR_DATA3:key=TS_PALETTE_PATTERN_TUNING;break;case TS_COLOR_DATA4:key=TS_PALETTE_PATTERN_INSTRUMENT;break;
      case TS_COLOR_DATA5:key=TS_PALETTE_PATTERN_VOLUME;break;case TS_COLOR_DATA6:key=TS_PALETTE_PATTERN_EMPTY;break;default:break;}
    if(g==TS_THEME_MOSAIC && r>=TS_COLOR_DATA1&&r<=TS_COLOR_DATA5){const int keys[]={28,27,31,30,29};key=keys[r-TS_COLOR_DATA1];}
    if(g==TS_THEME_PEDALBOARD&&r==TS_COLOR_DATA6)return ts_theme_color(p,g,r,p->colors[TS_PALETTE_STEREO_WAVE_RIGHT]);
    if(g==TS_THEME_ROUTER||g==TS_THEME_TRANSPORT||g==TS_THEME_HEALTH||g==TS_THEME_EQ||g==TS_THEME_SPATIAL)key=-1;
    return key<0?ts_theme_color(p,g,r,ts_theme_original(g,r)):ts_theme_legacy(p,g,r,(TsPaletteColor)key);
}
