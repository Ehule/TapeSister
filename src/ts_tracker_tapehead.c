/* SisterTracker boundary for the extracted Tapehead pattern renderer.
 * Rendering runs on the UI thread. No FT2 song, replay engine, SDL window or
 * mutable audio state is imported. See third_party/tapehead/UPSTREAM.md. */
#include "tapesister/ui.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "ts_tracker_fonts.inc"
#include "../third_party/tapehead/transport_visuals.h"

#define SCREEN_W TS_UI_WIDTH
#define SCREEN_H TS_UI_HEIGHT
#define MAX_CHANNELS TS_TRACKER_LANES
#define MAX_PATT_LEN TS_TRACKER_ROWS
#define MAX_PATTERNS 1 /* Stable native pattern IDs are bound to this render slot. */
#define ASSERT assert
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define ABS(a) ((a)<0?-(a):(a))
#define RGB32_R(c) (((c)>>16)&255u)
#define RGB32_G(c) (((c)>>8)&255u)
#define RGB32_B(c) ((c)&255u)
#define RGB32(r,g,b) (0xff000000u|((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|(uint32_t)(b))
#define FONT3_CHAR_W 4
#define FONT3_CHAR_H 7
#define FONT3_WIDTH (43*FONT3_CHAR_W)
#define FONT4_CHAR_W 8
#define FONT4_CHAR_H 8
#define FONT4_WIDTH (78*FONT4_CHAR_W)
enum { FONT_TYPE3, FONT_TYPE4 };
enum { CURSOR_NOTE, CURSOR_INST1, CURSOR_INST2, CURSOR_VOL1, CURSOR_VOL2,
       CURSOR_TUNE, CURSOR_TUNE1, CURSOR_TUNE2, CURSOR_EFX, CURSOR_EFX1, CURSOR_EFX2 };
enum { PLAYMODE_IDLE, PLAYMODE_EDIT, PLAYMODE_RECPATT, PLAYMODE_RECSONG };
enum { NOTE_OFF=97, HOST_PLACEHOLDER=254, HOST_MISSING_ALIAS=255 };
/* Original Tapehead palette roles, mapped to the host's opaque RGBA palette. */
enum { PAL_BCKGRND, PAL_PATTEXT, PAL_BLCKMRK, PAL_BLCKTXT, PAL_DESKTOP,
       PAL_FORGRND, PAL_BUTTONS, PAL_BTNTEXT, PAL_DSKTOP2, PAL_DSKTOP1,
       PAL_BUTTON2, PAL_BUTTON1, PAL_MOUSEPT, PAL_PIANOXOR1, PAL_PIANOXOR2,
       PAL_PIANOXOR3, PAL_LOOPPIN, PAL_TEXTMRK, PAL_BOXSLCT, PAL_CURSOR_NAV,
       PAL_CURSOR_EDIT, PAL_TRACKTRIM_GREEN, PAL_PATTERN_NOTE,
       PAL_PATTERN_INSTRUMENT, PAL_PATTERN_VOLUME, PAL_PATTERN_TUNING,
       PAL_PATTERN_EFFECT, PAL_PATTERN_EMPTY, PAL_WAVE_SELECTION,
       PAL_TRACK_LENGTH_PLAYHEAD, PAL_FASTTRACKS_PLAYHEAD, PAL_CONTROL_PLAYHEAD,
       PAL_FASTTRACKS_SYNC, PAL_FASTTRACKS_PHASE, PAL_FASTTRACKS_SONG,
       PAL_FASTTRACKS_LENGTH_PLAYHEAD, PAL_NUM };
typedef enum { FAST_TRACKS_MODE_STANDARD, FAST_TRACKS_MODE_PATTERN,
               FAST_TRACKS_MODE_SONG } fastTracksMode_t;
/* Snapshot layout copied from ft2_fasttracks.h. Audio ownership stays native. */
typedef struct {
    fastTracksMode_t mode;
    bool enabled, selected, clutched, reversed, masterAligned;
    int16_t sourceOrder, sourcePattern;
    int32_t sourceRow;
    uint8_t ratioNumerator, ratioDenominator;
} fastTracksTrackSnapshot_t;
typedef struct {
    bool masterEnabled, transmissionClutchLatched;
    fastTracksTrackSnapshot_t tracks[MAX_CHANNELS];
} fastTracksSnapshot_t;
typedef struct {
    uint8_t note,instr,vol,tuneType,tuneData,efx,efxData;
    /* Only representation differences from FT2's note_t live here. */
    uint8_t hasEffect,missingAlias,nativeNoteKind,nativeNote,pendingObject,pendingHighNibble;
} note_t;
typedef struct { int upperRowsTextY,numUpperRows,numLowerRows; } pattCoord_t;
static struct { uint32_t *frameBuffer,palette[PAL_NUM]; } video;
static struct { uint16_t numChannelsShown,maxVisibleChannels; int channelOffset,patternChannelWidth; } ui;
static struct { int ptnShowVolColumn,ptnLineLight,ptnHex,ptnAlternativeLayout,ptnAcc,ptnChnNumbers; } config={.ptnShowVolColumn=1};
static struct { int numChannels,pattNum,row; } song;
static struct { int ptnCursorY; } editor;
static struct { int object,ch; } cursor;
static struct { uint8_t font3[FONT3_WIDTH*FONT3_CHAR_H]; } bmp;
static uint8_t hostFont4[FONT4_WIDTH*FONT4_CHAR_H];
static bool songPlaying;
static int playMode;
static note_t hostCells[MAX_PATT_LEN*MAX_CHANNELS];
static note_t *pattern[MAX_PATTERNS]={hostCells};
static uint16_t patternNumRows[MAX_PATTERNS];
static struct {
    const TsUiState *state;
    const TsSisterTracker *tracker;
    TsTrackerLayout layout;
    TsTrackerView view;
    TsTrackerRegion selection;
    int selected;
    pattCoord_t coordinates;
    fastTracksSnapshot_t snapshot;
} host;

static void hostRect(int x,int y,int w,int h,uint32_t color)
{
    assert(x>=0 && y>=0 && w>=0 && h>=0 && x+w<=SCREEN_W && y+h<=SCREEN_H);
    for(int yy=y;yy<y+h;++yy)for(int xx=x;xx<x+w;++xx)
        video.frameBuffer[yy*SCREEN_W+xx]=color;
}
static void fillRect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint8_t palette)
{ hostRect(x,y,w,h,video.palette[palette]); }
static void hostGlyph(int x,int y,const uint8_t *rows,int w,int h,uint32_t color)
{
    for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx)
        if(rows[yy]&(1u<<xx))hostRect(x+xx,y+yy,1,1,color);
}
static void hostQuestionMark(int x,int y,uint32_t color)
{
    hostRect(x,y,3,1,color);hostRect(x+2,y+1,1,2,color);
    hostRect(x+1,y+3,1,1,color);hostRect(x+1,y+5,1,1,color);
}
/* Original ft2_gui.c textOutTiny plus the transport header's colon wrapper. */
#include "../third_party/tapehead/tiny_draw.inc"
static uint16_t fastTracksPOCGetTrackLength(uint16_t p,int lane)
{ (void)p;return host.tracker->lanes[lane].length; }
static bool fastTracksPOCLengthTopologyIsBypassed(void)
{ return host.tracker->length_bypass || host.state->tracker_block; }
static bool fastTracksPOCLengthTopologyIsActive(uint16_t p)
{ (void)p;return !fastTracksPOCLengthTopologyIsBypassed(); }
static bool fastTracksPOCUsesTrackLengths(void)
{ return host.tracker->fasttracks_uses_length; }
static int fastTracksPOCGetControlTrack(uint16_t p)
{ (void)p;return host.tracker->control_lane; }
static uint16_t fastTracksPOCGetExtendedPatternLength(uint16_t p)
{ (void)p;return (uint16_t)host.view.extent; }
static uint16_t fastTracksPOCGetEffectiveTrackLength(uint16_t p,int lane)
{
    (void)p;
    if(fastTracksPOCLengthTopologyIsBypassed())return patternNumRows[0];
    return host.tracker->lanes[lane].length?host.tracker->lanes[lane].length:(uint16_t)host.view.transport_rows[lane];
}
static uint16_t fastTracksPOCGetFastTrackLength(uint16_t p,int lane)
{ (void)p;return (uint16_t)host.view.transport_rows[lane]; }
static int32_t fastTracksPOCResolveMasterSourceRow(uint16_t p,int lane,int row)
{ (void)p;(void)row;return (int32_t)host.state->tracker_lane_row[lane]; }
static void fastTracksPOCGetSnapshot(fastTracksSnapshot_t *snapshot)
{ *snapshot=host.snapshot; }
static bool patternFieldColorsActive(void) { return true; }
static uint32_t patternFieldColor(uint8_t field,bool populated)
{
    return video.palette[populated?PAL_PATTERN_NOTE+MIN(field,4):PAL_PATTERN_EMPTY];
}
static char *hostDirectionBadge(int lane)
{ return host.tracker->lanes[lane].direction==TS_TRACKER_PING_PONG?"B":"R"; }
static bool hostDrawExtendedNote(int x,int y,const note_t *n,uint32_t color)
{
    if(n->nativeNoteKind==TS_TRACKER_NOTE_CUT) {textOutTiny(x+6,y,"CUT",color);return true;}
    if(n->nativeNoteKind!=TS_TRACKER_NOTE_PITCH || (n->nativeNote>=12 && n->nativeNote<108))return false;
    /* FT2 covers MIDI 12..107. Preserve the host's complete MIDI 0..127 range. */
    static const uint8_t letters[12]={12,12,13,13,14,15,15,16,16,10,10,11};
    int pitch=n->nativeNote%12;
    hostGlyph(x,y,tracker_font4[letters[pitch]],8,8,color);
    hostGlyph(x+8,y,tracker_font4[pitch==1||pitch==3||pitch==6||pitch==8||pitch==10?37:36],8,8,color);
    if(n->nativeNote<12){hostRect(x+16,y+3,3,1,color);textOutTiny(x+20,y,"1",color);}
    else hostGlyph(x+16,y,tracker_font4[n->nativeNote/12-1],8,8,color);
    return true;
}
static uint8_t hostCellChar(const note_t *n,int object,uint8_t glyph)
{
    if(n->missingAlias && (object==CURSOR_INST1 || object==CURSOR_INST2))return HOST_MISSING_ALIAS;
    if(n->pendingObject && object==n->pendingObject-1)return n->pendingHighNibble;
    if(object==CURSOR_TUNE && !n->tuneType)return HOST_PLACEHOLDER;
    if(object==CURSOR_EFX && !n->hasEffect)return HOST_PLACEHOLDER;
    return n->pendingObject==object?HOST_PLACEHOLDER:glyph;
}
static void drawPatternBorders(void)
{
    /* Native window chrome has already drawn each lane's recessed panel. */
    int row=host.view.master_row-host.view.master_top;
    if(row>=0 && row<host.layout.rows)
        fillRect(4,(uint16_t)(host.layout.grid_y+row*TS_TRACKER_ROW_HEIGHT),632,TS_TRACKER_ROW_HEIGHT,PAL_DESKTOP);
}
static void hostCellSelection(int lane,int row,int x,int textY,uint32_t *note,
    uint32_t *inst,uint32_t *vol,uint32_t *tune,uint32_t *effect)
{
    /* FT2 marks XOR palette indices stored in pixel alpha. SisterTracker uses
       opaque RGBA and logical lane selections, so mark before the glyph pass. */
    TsTrackerRegion r=host.selection;
    if(host.selected && row>=r.row0 && row<=r.row1 && lane>=r.lane0 && lane<=r.lane1) {
        fillRect(x+1,textY-1,TS_TRACKER_LANE_WIDTH-3,TS_TRACKER_ROW_HEIGHT,PAL_BLCKMRK);
        *note=*inst=*vol=*tune=*effect=video.palette[PAL_BLCKTXT];
    }
    const TsUiState *state=host.state;
    if(state->tracker_block && host.view.playing &&
       row>=(int)(state->tracker_block_rows&255) && row<=(int)(state->tracker_block_rows>>8) &&
       lane>=(int)(state->tracker_block_lanes&255) && lane<=(int)(state->tracker_block_lanes>>8))
        fillRect(x,textY-1,1,TS_TRACKER_ROW_HEIGHT,PAL_PATTERN_VOLUME);
}
static uint32_t hostMuteColor(int lane,uint32_t c)
{
    if(!host.tracker->lanes[lane].muted)return c;
    return RGB32(RGB32_R(c)*40/100,RGB32_G(c)*40/100,RGB32_B(c)*40/100);
}

#include "../third_party/tapehead/pattern_draw.inc"

int ts_tracker_tapehead_field_at(int local)
{
    /* The original hit-test and renderer use the very same layout entry. */
    static const uint8_t fields[11]={0,1,1,2,2,3,4,4,5,6,6};
    return fields[patternXToCursorObject(local)];
}
static void hostBindFonts(void)
{
    static int ready;
    if(ready)return;
    (void)tracker_font1;(void)tracker_font1_widths;
    for(int ch=0;ch<43;++ch)for(int y=0;y<7;++y)for(int x=0;x<4;++x)
        bmp.font3[y*FONT3_WIDTH+ch*4+x]=(tracker_font3[ch][y]>>x)&1;
    for(int ch=0;ch<78;++ch)for(int y=0;y<8;++y)for(int x=0;x<8;++x)
        hostFont4[y*FONT4_WIDTH+ch*8+x]=(tracker_font4[ch][y]>>x)&1;
    font4Ptr=hostFont4;ready=1;
}
static uint8_t hostCommand(uint8_t value)
{ return value>='0'&&value<='9'?value-'0':value>='A'&&value<='Z'?value-'A'+10:0; }
void ts_tracker_tapehead_render(TsFramebuffer *fb,const TsUiState *state)
{
    const TsSisterTracker *t=state->tracker;
    const TsTrackerPattern *p=ts_sister_tracker_pattern_const(t,t->editor_pattern);
    if(!p)return;
    hostBindFonts();
    host.state=state;host.tracker=t;host.layout=ts_tracker_layout(t,state->tracker_expanded);
    host.view=ts_tracker_view(state,p);host.selection=ts_tracker_edit_region(state->tracker_edit,t);
    host.selected=ts_tracker_edit_selected(state->tracker_edit,t);
    host.coordinates=(pattCoord_t){host.layout.grid_y+1,host.layout.rows/2,host.layout.rows-host.layout.rows/2-1};
    video.frameBuffer=fb->pixels;
    static const int paletteMap[PAL_NUM]={
        -1,TS_PALETTE_PATTERN_TEXT,TS_PALETTE_BLOCK_MARK,TS_PALETTE_TEXT_ON_BLOCK,
        TS_PALETTE_DESKTOP,-1,TS_PALETTE_BUTTONS,-1,-1,-1,-1,-1,TS_PALETTE_MOUSE,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,
        TS_PALETTE_PATTERN_NOTE,TS_PALETTE_PATTERN_INSTRUMENT,TS_PALETTE_PATTERN_VOLUME,
        TS_PALETTE_PATTERN_TUNING,TS_PALETTE_PATTERN_EFFECT,TS_PALETTE_PATTERN_EMPTY,
        TS_PALETTE_WAVE_SELECTION,TS_PALETTE_TRACK_LENGTH_PLAYHEAD,TS_PALETTE_FASTTRACKS_PLAYHEAD,
        TS_PALETTE_CONTROL_PLAYHEAD,TS_PALETTE_FASTTRACKS_SYNC,TS_PALETTE_FASTTRACKS_PHASE,
        TS_PALETTE_FASTTRACKS_SONG,TS_PALETTE_FASTTRACKS_LENGTH_PLAYHEAD};
    for(int i=0;i<PAL_NUM;++i)video.palette[i]=paletteMap[i]>=0?state->palette.colors[paletteMap[i]]:RGB32(0,0,0);
    video.palette[PAL_FORGRND]=RGB32(255,255,255);
    video.palette[PAL_CURSOR_NAV]=RGB32(40,224,255);video.palette[PAL_CURSOR_EDIT]=RGB32(255,224,32);
    ui.numChannelsShown=ui.maxVisibleChannels=MAX_CHANNELS;ui.channelOffset=0;ui.patternChannelWidth=TS_TRACKER_LANE_WIDTH;
    config.ptnShowVolColumn=config.ptnHex=config.ptnAlternativeLayout=config.ptnChnNumbers=1;
    song.numChannels=MAX_CHANNELS;song.pattNum=0;song.row=host.view.master_row;
    songPlaying=host.view.playing;playMode=state->tracker_hex_digit?PLAYMODE_EDIT:PLAYMODE_IDLE;
    cursorBreatheFrame=(uint8_t)(state->tracker_breathe_frame%CURSOR_BREATHE_FRAMES);
    static const uint8_t objects[7]={CURSOR_NOTE,CURSOR_INST1,CURSOR_VOL1,CURSOR_TUNE,CURSOR_TUNE1,CURSOR_EFX,CURSOR_EFX1};
    cursor.object=objects[state->tracker_field];
    if(state->tracker_hex_digit && (state->tracker_field==1 || state->tracker_field==2 || state->tracker_field==4 || state->tracker_field==6))cursor.object++;
    cursor.ch=t->editor_lane;
    editor.ptnCursorY=host.layout.grid_y+((int)t->editor_row-host.view.top[t->editor_lane])*TS_TRACKER_ROW_HEIGHT;
    memset(hostCells,0,sizeof(hostCells));patternNumRows[0]=p->rows;
    for(int row=0;row<p->rows;++row)for(int lane=0;lane<MAX_CHANNELS;++lane) {
        const TsTrackerCell *c=&p->cells[row][lane];note_t *n=&hostCells[row*MAX_CHANNELS+lane];
        n->nativeNoteKind=c->note_kind;n->nativeNote=c->note;
        n->note=c->note_kind==TS_TRACKER_NOTE_OFF?NOTE_OFF:c->note_kind==TS_TRACKER_NOTE_PITCH && c->note>=12 && c->note<108?c->note-11:c->note_kind?128:0;
        if(c->tile_id)for(int alias=1;alias<TS_TRACKER_ALIASES;++alias)if(t->aliases[alias]==c->tile_id){n->instr=(uint8_t)alias;break;}
        n->missingAlias=c->tile_id && !n->instr;
        if(n->missingAlias)n->instr=255;
        n->vol=c->has_volume?c->volume+0x10:0;n->tuneType=hostCommand(c->tune_command);n->tuneData=c->tune_value;
        n->efx=hostCommand(c->fx_command);n->efxData=c->fx_value;n->hasEffect=c->fx_command!=0;
        if(row==t->editor_row && lane==t->editor_lane && state->tracker_hex_digit) {
            unsigned value=state->tracker_hex_value;
            n->pendingObject=(uint8_t)cursor.object;
            n->pendingHighNibble=(value>>4)&15;
            if(state->tracker_field==1){n->instr=value;n->missingAlias=0;}
            if(state->tracker_field==2)n->vol=0x10;
            if(state->tracker_field==4)n->tuneData=value;
            if(state->tracker_field==6)n->efxData=value;
        }
    }
    memset(&host.snapshot,0,sizeof(host.snapshot));host.snapshot.masterEnabled=true;
    static const uint8_t numerator[TS_TRACKER_RATIOS]={1,2,3,4,5,7,15,1,17,8,6,5,4,3,2,3,5};
    static const uint8_t denominator[TS_TRACKER_RATIOS]={2,3,4,5,6,8,16,1,16,7,5,4,3,2,1,1,1};
    for(int lane=0;lane<MAX_CHANNELS;++lane) {
        const TsTrackerLane *l=&t->lanes[lane];fastTracksTrackSnapshot_t *s=&host.snapshot.tracks[lane];
        unsigned ratio=l->ratio<TS_TRACKER_RATIOS?l->ratio:7;
        s->enabled=host.view.fast[lane];s->mode=s->enabled?FAST_TRACKS_MODE_PATTERN:FAST_TRACKS_MODE_STANDARD;
        s->reversed=l->direction!=TS_TRACKER_FORWARD;s->sourceRow=state->tracker_lane_row[lane];
        s->masterAligned=state->tracker_lane_phase[lane]==2;
        s->ratioNumerator=numerator[ratio];s->ratioDenominator=denominator[ratio];
    }
    writePattern(t->editor_row,0);
    /* A saved-only Song assignment must never appear to be a live Song clock. */
    for(int lane=0;lane<MAX_CHANNELS;++lane)if(t->lanes[lane].mode==TS_TRACKER_SONG)
        textOutTiny(TS_TRACKER_LANE_X+lane*TS_TRACKER_LANE_WIDTH+30,host.layout.ratio_y,"S",video.palette[PAL_FASTTRACKS_SONG]);
    host.state=NULL;host.tracker=NULL;video.frameBuffer=NULL;
}
