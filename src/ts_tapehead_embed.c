#include "tapesister/profile.h"
/* TapeHead runs as an embedded subsystem. This file is the host boundary;
   editing, undo, selection, timing, effects and transport stay upstream. */
#include "tapesister/tapehead_embed.h"
#include "tapesister/audition.h"
#include "tapesister/palette.h"
#include "ft2_scrollbars.h"
#include "ft2_interpolation.h"
#include "ft2_header.h"
#include "ft2_structs.h"
#include "ft2_video.h"
#include "ft2_gui.h"
#include "ft2_audio.h"
#include "ft2_bmp.h"
#include "ft2_config.h"
#include "ft2_palette.h"
#include "ft2_checkboxes.h"
#include "ft2_tables.h"
#include "ft2_keyboard.h"
#include "ft2_mouse.h"
#include "ft2_pattern_ed.h"
#include "ft2_pattern_draw.h"
#include "ft2_edit.h"
#include "ft2_sample_ed.h"
#include "ft2_pushbuttons.h"
#include "ft2_undo.h"
#include "ft2_hpc.h"
#include "ft2_random.h"
#include "ft2_sample_launcher.h"
#include "ft2_pattern_launcher_ui.h"
#include "ft2_multichannel.h"
#include "ft2_textboxes.h"
#include "mixer/ft2_windowed_sinc.h"
#include "scopes/ft2_scopes.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

extern void tapeheadEmbeddedConfigDefaults(void);
extern bool tapeheadEmbeddedAudioPrepare(uint32_t,uint32_t);
extern void tapeheadEmbeddedAudioRender(float *,uint32_t);
extern void tapeheadEmbeddedScopeTick(void);
extern void tapeheadEmbeddedClearClipboard(void);
extern bool tapeheadEmbeddedTileInUse(const float *);
extern void freeTextBoxes(void);
extern void windUpFTHelp(void);

#define TH_FRAMES 8192u
#define TH_HEADER 52u
#define TH_LANE_BYTES 10u
#define TH_PATTERN_BYTES (7u+256u*8u*7u)
typedef struct RetiredTile {float *data;struct RetiredTile *next;} RetiredTile;
#define TH_MAX_SCORE (TH_HEADER+8u*TH_LANE_BYTES+256u+256u*TH_PATTERN_BYTES)
static struct {
    TsTapeHeadHost host;
    SDL_Cursor *host_cursor;int host_cursor_visible;
    atomic_int ready;
    int initialized, action, lock_depth, loading;
    TsSisterTracker *model;
    uint64_t model_hash;
    TsPatternId ids[256];
    TsTileId selected_tile;
    TsSamplePages *pages;
    const TsInstrument *active;
    size_t canvas_page,host_page; /* 24-tile viewport; 16-tile host Sample page. */
    uint64_t tile_stamp[129];
    TsTileLocation tile_location[129];
    unsigned tile_rate[129];
    float *tile_data[129];
    RetiredTile *retired;
    uint8_t midi_held[16][128];
    uint8_t key_held[SDL_NUM_SCANCODES],key_note[SDL_NUM_SCANCODES];
    float output[TH_FRAMES*2u];
    uint8_t capture_flags[TH_FRAMES];
    int capture_seam_pending;
    unsigned rate;
    double wheel_fraction;
    int wheel_target;
    char status[160],canvas_message[80];
    unsigned capture;
    TsPerformanceFileState file_state;
    uint64_t file_seconds;
    uint32_t record_color;
    int record_flash;
    int follow, live_edit;
    int mark_valid, mark_pattern, mark_channel, mark_row;
    int pointer_mark, pointer_x, pointer_y;
} embed;

static int fail(char *e,size_t n,const char *s) { if(e&&n)snprintf(e,n,"%s",s);return 0; }
void ts_tapehead_host_lock(void) {
    if(embed.lock_depth++==0 && embed.host.lock)embed.host.lock(embed.host.context);
}
void ts_tapehead_host_unlock(void) {
    if(embed.lock_depth>0 && --embed.lock_depth==0 && embed.host.unlock)embed.host.unlock(embed.host.context);
}
void ts_tapehead_host_present(void) {
    if(embed.host.present)embed.host.present(embed.host.context,video.frameBuffer);
}
void ts_tapehead_host_mouse(const SDL_Event *event) {
    int x,y,width,height;
    if(!embed.host.window)return;
    SDL_GetWindowSize(embed.host.window,&width,&height);
    if(width<=0 || height<=0)return;
    if(event) {
        Uint32 window=SDL_GetWindowID(embed.host.window);
        if(event->type==SDL_MOUSEMOTION && event->motion.windowID==window) {
            x=event->motion.x;y=event->motion.y;
        } else if((event->type==SDL_MOUSEBUTTONDOWN || event->type==SDL_MOUSEBUTTONUP) &&
                  event->button.windowID==window) {
            x=event->button.x;y=event->button.y;
        } else return;
    } else {
        if(mouse.setPosFlag) {
            SDL_WarpMouseInWindow(embed.host.window,(mouse.setPosX+4)*width/640,mouse.setPosY*height/400);
            mouse.setPosFlag=false;
        }
        int window_x,window_y;
        mouse.buttonState=SDL_GetGlobalMouseState(&x,&y);
        mouse.absX=x;mouse.absY=y;
        SDL_GetWindowPosition(embed.host.window,&window_x,&window_y);
        x-=window_x;y-=window_y;
    }
    mouse.rawX=x;mouse.rawY=y;
    mouse.x=(int)floor((double)x*640/width)-4;
    mouse.y=(int)floor((double)y*400/height);
}
void ts_tapehead_request(int action) { embed.action=action; }
int ts_tapehead_action(void) { int a=embed.action;embed.action=0;return a; }
int ts_tapehead_interpolation_active(void) {return embed.initialized && interpolationPreviewActive();}
int ts_tapehead_running(void) { return atomic_load(&embed.ready)&&songPlaying; }
int ts_tapehead_block_active(void) { return embed.initialized&&tapeheadBlockLoopIsActive(); }
unsigned ts_tapehead_capture_flags(unsigned frame) {return frame<TH_FRAMES?embed.capture_flags[frame]:0;}
void ts_tapehead_audio_span(unsigned offset,unsigned frames,int seam) {
    unsigned flags=(tapeheadBlockLoopIsActive()?1u:0u)|(songPlaying?4u:0u);
    if(offset+frames<=TH_FRAMES && frames) {
        memset(embed.capture_flags+offset,flags,frames);
        if(embed.capture_seam_pending) {embed.capture_flags[offset]|=2;embed.capture_seam_pending=0;}
    }
    if(seam)embed.capture_seam_pending=1;
}
const uint32_t *ts_tapehead_frame(void) { return embed.initialized?video.frameBuffer:NULL; }

/* The original sequencer/voice envelopes supply period, panning and ramps.
   Only sample reads change: immutable host tile snapshots preserve float stereo. */
double ts_tapehead_tile_begin(void *ptr) {
    voice_t *v=ptr;
    return ts_audition_loop_begin(v->loopStart,v->sampleEnd,(TsLoopMode)v->tileLoopMode,
                                 &v->tileDirection,&v->tileIntro);
}
void ts_tapehead_mix_tile(void *ptr,unsigned offset,unsigned count) {
    voice_t *v=ptr;
    if(!v->tileData || v->sampleEnd<1)return;
    TsSample sample={0};sample.data=(float*)v->tileData;
    sample.frames=v->tileFrames;sample.channels=v->tileChannels;
    for(unsigned i=0;i<count && v->active;++i) {
        TsStereoFrame value;
        if(v->loopType!=LOOP_DISABLED && !v->oneShot) {
            value=ts_audition_loop_frame(&sample,&v->tilePosition,v->loopStart,v->sampleEnd,
                v->tileCrossfade,(TsLoopMode)v->tileLoopMode,&v->tileDirection,&v->tileIntro);
        } else {
            if(v->tilePosition<0 || v->tilePosition>=v->sampleEnd) {v->active=false;break;}
            value=ts_audition_read_frame(&sample,v->tilePosition,v->sampleEnd);
        }
        /* FT2's equal-power pan is -3 dB at center. Host tiles already carry
           their stereo level: make center unity while retaining the pan law,
           note/global volume, envelopes and ramps (including fade voices). */
        const float center_gain=1.41421356237f;
        audio.fMixBufferL[offset+i]+=value.l*v->fCurrVolumeL*center_gain;
        audio.fMixBufferR[offset+i]+=value.r*v->fCurrVolumeR*center_gain;
        if(v->volumeRampLength) {
            v->fCurrVolumeL+=v->fVolumeLDelta;v->fCurrVolumeR+=v->fVolumeRDelta;
            if(--v->volumeRampLength==0) {
                if(v->isFadeOutVoice){v->active=false;break;}
                v->fCurrVolumeL=v->fTargetVolumeL;v->fCurrVolumeR=v->fTargetVolumeR;
            }
        }
        v->tilePosition+=((double)v->delta/4294967296.0)*v->tileRateCorrection*v->tileDirection;
        v->position=(int)v->tilePosition;
        v->positionFrac=(uint64_t)((v->tilePosition-floor(v->tilePosition))*4294967296.0);
    }
}
static void collect_tiles(void) {
    RetiredTile **link=&embed.retired;
    while(*link) {
        RetiredTile *tile=*link;
        if(tapeheadEmbeddedTileInUse(tile->data))link=&tile->next;
        else {*link=tile->next;free(tile->data);free(tile);}
    }
}

void ts_tapehead_status(const TsUiState *host_ui,unsigned capture) {
    snprintf(embed.status,sizeof(embed.status),"%s",host_ui->status);embed.capture=capture;
    embed.file_state=host_ui->file_record_state;
    embed.file_seconds=host_ui->file_record_rate?host_ui->file_record_frames/host_ui->file_record_rate:0;
    embed.record_color=host_ui->palette.colors[TS_PALETTE_PATTERN_VOLUME]|0xff000000u;
    embed.record_flash=host_ui->text_cursor_visible;
}
static int file_recording(void) {
    return embed.file_state==TS_PERFORMANCE_FILE_RECORDING || embed.file_state==TS_PERFORMANCE_FILE_STOPPING;
}
static void record_outline(int x,int y,int w,int h) {
    if(!file_recording() || !embed.record_flash)return;
    /* Use the host's pink file-recording color; stay inside the tightly
       spaced tracker button so adjacent recording controls remain legible. */
    for(int row=0;row<h;++row)for(int col=0;col<w;++col)
        if(row<2 || row>=h-2 || col<2 || col>=w-2)
            video.frameBuffer[(y+row)*SCREEN_W+x+col]=embed.record_color;
}

#include "ts_tapehead_settings.inc"

static void fx_button(void) {ts_tapehead_request(TS_TH_FX);}
static void prism_button(void) {ts_tapehead_request(TS_TH_PRISM);}
static void sister_button(void) {ts_tapehead_request(TS_TH_SISTER);}
static void fallout_button(void) {ts_tapehead_request(TS_TH_FALLOUT);}
static void canvas_button(void) {ts_tapehead_request(TS_TH_CANVAS);}
static void router_button(void) {ts_tapehead_request(TS_TH_ROUTER);}
static void project_save_button(void) {ts_tapehead_request(TS_TH_SAVE_PROJECT);}
static void project_open_button(void) {ts_tapehead_request(TS_TH_OPEN_PROJECT);}
static void capture_button(void) {ts_tapehead_request(TS_TH_CAPTURE);}
#include "ts_tapehead_canvas.inc"
static void length_bypass_button(void) {fastTracksPOCSetLengthTopologyBypassed(!fastTracksPOCLengthTopologyIsBypassed());}
static int main_panel_visible(void) {
    return !ui.extendedPatternEditor && !ui.patternEditorOnly && !ui.configScreenShown &&
           !ui.helpScreenShown && !ui.aboutScreenShown && !ui.nibblesShown;
}
#include "ts_tapehead_follow.inc"
static void menu(void) {
    if(ui.configScreenShown) {palette_buttons();textOutClipX(400,157,PAL_FORGRND,preferences_message,628);return;}
    pushButtons[PB_DISK_OP].x=294;pushButtons[PB_DISK_OP].y=36;
    pushButtons[PB_ZAP].y=53;pushButtons[PB_TRIM].y=70;
    pushButtons[PB_EXTEND_VIEW].x=359;pushButtons[PB_EXTEND_VIEW].y=87;
    pushButtons[PB_EXTEND_VIEW].caption=embed.file_state==TS_PERFORMANCE_FILE_STOPPING?"File wait":
        embed.capture || file_recording()?"Stop file":"Rec file";
    pushButtons[PB_EXTEND_VIEW].callbackFuncOnUp=capture_button;
    pushButtons[PB_DISK_OP].caption="Load";pushButtons[PB_DISK_OP].callbackFuncOnUp=project_open_button;
    pushButtons[PB_INST_ED].caption="FX";pushButtons[PB_INST_ED].callbackFuncOnUp=fx_button;
    pushButtons[PB_SMP_ED].caption="Prism";pushButtons[PB_SMP_ED].callbackFuncOnUp=prism_button;
    pushButtons[PB_CONFIG].caption="Config";
    pushButtons[PB_ABOUT].caption="Zap";pushButtons[PB_ABOUT].callbackFuncOnUp=pbZap;
    pushButtons[PB_INST_ED_EXT].caption="Sister";pushButtons[PB_INST_ED_EXT].callbackFuncOnUp=sister_button;
    pushButtons[PB_SMP_ED_EXT].caption="Canvas";pushButtons[PB_SMP_ED_EXT].callbackFuncOnUp=canvas_button;
    /* These mutate an independent sample library; tiles belong to the host. */
    hidePushButton(PB_ADD_CHANNELS);hidePushButton(PB_SUB_CHANNELS);
    pushButtons[PB_NIBBLES].caption="Fallout";pushButtons[PB_NIBBLES].callbackFuncOnUp=fallout_button;
    pushButtons[PB_ZAP].caption="Save";pushButtons[PB_ZAP].callbackFuncOnUp=project_save_button;
    pushButtons[PB_TRIM].caption="Router";pushButtons[PB_TRIM].callbackFuncOnUp=router_button;
    if(main_panel_visible()) {
        drawPushButton(PB_ABOUT);drawPushButton(PB_NIBBLES);drawPushButton(PB_TRIM);drawPushButton(PB_CONFIG);
        showPushButton(PB_ZAP);drawPushButton(PB_DISK_OP);drawPushButton(PB_EXTEND_VIEW);
        record_outline(359,87,59,16);
        if(ui.scopesShown) {
            tracker_logo();
            pushButtons[PB_BADGE].bitmapFlag=pushButtons[PB_BADGE].bitmap32Flag=false;
            pushButtons[PB_BADGE].caption=pushButtons[PB_BADGE].caption2=NULL;
            pushButtons[PB_BADGE].callbackFuncOnUp=length_bypass_button;drawPushButton(PB_BADGE);
            textOutTiny(273,7,"LEN",video.palette[PAL_FORGRND]);
            textOutTiny(273,20,fastTracksPOCLengthTopologyIsBypassed()?"OFF":"ON",video.palette[PAL_FORGRND]);
        }
        canvas_draw();
        char octave[12];snprintf(octave,sizeof(octave),"OCT %u",editor.curOctave);
        drawFramework(294,155,59,16,FRAMEWORK_TYPE1);textOut(300,159,PAL_FORGRND,octave);
        drawFramework(359,155,59,16,FRAMEWORK_TYPE1);
        textOutTiny(363,160,embed.follow?"FOLLOW ON":"FOLLOW OFF",video.palette[embed.follow?PAL_FORGRND:PAL_PATTEXT]);
    }
}
void ts_tapehead_host_redraw(void) {menu();}

int ts_tapehead_init(const TsTapeHeadHost *host,unsigned rate,char *error,size_t size) {
    if(embed.initialized)return 1;
    memset(&embed,0,sizeof(embed));atomic_init(&embed.ready,0);
    embed.host=*host;embed.host_cursor=SDL_GetCursor();embed.host_cursor_visible=SDL_ShowCursor(SDL_QUERY);embed.rate=rate?rate:48000;
    memset(&video,0,sizeof(video));memset(&keyb,0,sizeof(keyb));
    memset(&mouse,0,sizeof(mouse));memset(&editor,0,sizeof(editor));
    memset(&ui,0,sizeof(ui));memset(&song,0,sizeof(song));
    memset((void*)&pattMark,0,sizeof(pattMark));
    memset(&audio,0,sizeof(audio));
    memset(&pattSync,0,sizeof(pattSync));memset(&chSync,0,sizeof(chSync));
    audio.linearPeriodsFlag=true;randomize();
    for(int i=0;i<MAX_CHANNELS;++i){lastChInstr[i].instrNum=255;lastChInstr[i].smpNum=255;}
    video.window=host->window;video.dMouseXMul=video.dMouseYMul=1;
    video.mouseCursorUpscaleFactor=1;video.windowModeUpscaleFactor=1;
    video.frameBuffer=calloc(SCREEN_W*SCREEN_H,sizeof(uint32_t));
    if(!video.frameBuffer)return fail(error,size,"Unable to allocate tracker framebuffer");
    embed.initialized=1;embed.follow=1;
    editor.programRunning=true;editor.editRowSkip=1;editor.curOctave=4;
    editor.curInstr=editor.srcInstr=1;editor.copyMaskEnable=true;
    editor.ptnJumpPos[0]=0;editor.ptnJumpPos[1]=16;editor.ptnJumpPos[2]=32;editor.ptnJumpPos[3]=48;
    memset(editor.copyMask,1,sizeof(editor.copyMask));memset(editor.pasteMask,1,sizeof(editor.pasteMask));
    mouse.lastUsedObjectID=OBJECT_ID_NONE;ui.sampleDataOrLoopDrag=-1;
    hpc_Init();hpc_SetDurationInHz(&video.vblankHpc,VBLANK_HZ);
    calcMiscReplayerVars();
    if(!loadBMPs() || !setupSprites() || !setupWindowedSincTables())goto bad;
    tapeheadEmbeddedConfigDefaults();loadTapeheadConfig();
    tapeheadConfig.patternColorMode=PATTERN_COLOR_ALWAYS;
    config.cfg_AutoSave=false;config.specialFlags2|=HARDWARE_MOUSE;
    if(embed.host_cursor)SDL_SetCursor(embed.host_cursor);
    SDL_ShowCursor(embed.host_cursor_visible);
    config.ptnMaxChannels=2;ui.maxVisibleChannels=8;
    if(!setupReplayer() || !tapeheadEmbeddedAudioPrepare(embed.rate,TH_FRAMES) || !setupGUI())goto bad;
    undoInit();undoLoadConfig();
    playMode=PLAYMODE_EDIT;audio.locked=false;
    palette_default();preferences_get(default_preferences);
    menu();drawGUIOnRunTime();menu();
    return 1;
bad:
    ts_tapehead_close();return fail(error,size,"Unable to initialize embedded TapeHead");
}

void ts_tapehead_close(void) {
    if(!embed.initialized)return;
    ts_tapehead_host_lock();atomic_store(&embed.ready,0);
    editor.programRunning=false;stopVoices();stopAllScopes();
    closeAudio();closeReplayer();undoClose();
    collect_tiles();
    for(int a=1;a<=128;++a){free(embed.tile_data[a]);embed.tile_data[a]=NULL;}
    video.window=NULL;closeVideo(); /* The host window is never owned here. */
    freeSprites();freeTextBoxes();windUpFTHelp();freeMouseCursors();freeBMPs();freeWindowedSincTables();
    if(embed.host_cursor)SDL_SetCursor(embed.host_cursor);
    SDL_ShowCursor(embed.host_cursor_visible);
    free(editor.tmpFilenameU);free(editor.tmpInstrFilenameU);
    embed.initialized=0;ts_tapehead_host_unlock();
}
void ts_tapehead_stop(void) { if(embed.initialized){ts_tapehead_host_lock();stopPlaying();stopVoices();embed.capture_seam_pending=0;ts_tapehead_host_unlock();} }
int ts_tapehead_midi(const TsMidiEvent *event,int allow_on) {
    if(!embed.initialized || !event)return 0;
    ts_tapehead_host_lock();
    if(event->action==TS_MIDI_ACTION_PANIC) {
        for(int ch=0;ch<16;++ch)if(event->channel<0 || event->channel==ch)
            for(int note=12;note<=107;++note)if(embed.midi_held[ch][note]) {
                embed.midi_held[ch][note]=0;recordNote(note-11,0);
            }
        ts_tapehead_host_unlock();return 0; /* Host voices also need panic. */
    }
    int ch=event->note.channel,note=event->note.midi_note;
    if(ch<0 || ch>=16 || note<12 || note>107) {
        ts_tapehead_host_unlock();
        return allow_on && event->action==TS_MIDI_ACTION_NOTE_ON;
    }
    int owned=embed.midi_held[ch][note];
    if(event->action==TS_MIDI_ACTION_NOTE_ON && allow_on) {
        embed.midi_held[ch][note]=1;
        recordNote(note-11,(int8_t)((event->note.velocity*64+63)/127));owned=1;
    } else if(event->action==TS_MIDI_ACTION_NOTE_OFF && owned) {
        embed.midi_held[ch][note]=0;
        int held=0;for(int c=0;c<16;++c)held|=embed.midi_held[c][note];
        if(!held)recordNote(note-11,0);
    }
    ts_tapehead_host_unlock();return owned;
}
void ts_tapehead_focus_lost(void) {
    if(!embed.initialized)return;
    if(embed.host_cursor)SDL_SetCursor(embed.host_cursor);
    SDL_ShowCursor(embed.host_cursor_visible);
    if(editor.editTextFlag)exitTextEditing();
    for(int s=0;s<SDL_NUM_SCANCODES;++s)if(embed.key_held[s]) {
        SDL_Event release;SDL_zero(release);release.type=SDL_KEYUP;
        release.key.keysym.scancode=(SDL_Scancode)s;release.key.keysym.sym=SDL_GetKeyFromScancode((SDL_Scancode)s);
        ts_tapehead_event(&release,mouse.x,mouse.y);
    }
    mouseButtonUpHandler(SDL_BUTTON_LEFT);mouseButtonUpHandler(SDL_BUTTON_RIGHT);mouseButtonUpHandler(SDL_BUTTON_MIDDLE);
    TsMidiEvent panic={0};panic.action=TS_MIDI_ACTION_PANIC;panic.channel=-1;
    ts_tapehead_midi(&panic,0);
    memset(&keyb,0,sizeof(keyb));
}
void ts_tapehead_service(int draw) {
    if(!embed.initialized)return;
    if(!songPlaying)embed.live_edit=0;
    ts_tapehead_host_lock();collect_tiles();ts_tapehead_host_unlock();
    if(draw)eraseSprites();
    readKeyModifiers();setSyncedReplayerVars();
    handleLastGUIObjectDown();handleRecPlusExhaustion();handlePolyMatrixQHandoff();
    handlePatternLauncherStop();
    if(draw)handlePatternLauncherPanelRefresh();
    if(draw) {
        uint64_t started=ts_profile_begin(TS_PROF_SCOPES);
        tapeheadEmbeddedScopeTick();ts_profile_end(TS_PROF_SCOPES,started);
        handleRedrawing();menu();renderSprites();
    }
    ++editor.framesPassed;
}
void ts_tapehead_tick(void) { ts_tapehead_service(1); }
static int fasttracks_header_lane(int x,int y) {
    if(!ui.patternEditorShown || !fastTracksPOCMasterIsEnabled() || x<30 || !ui.patternChannelWidth)return -1;
    const pattCoord2_t *p=&pattCoord2Table[config.ptnStretch][ui.pattChanScrollShown][getPatternEditorView()];
    if(y<p->upperRowsY+10 || y>=p->upperRowsY+18)return -1;
    int visible=(x-30)/ui.patternChannelWidth,lane=visible+ui.channelOffset;
    return visible<ui.numChannelsShown && lane<song.numChannels?lane:-1;
}
static void ratio_adjust(int lane,int delta) {
    if(!fastTracksPOCIsSelected(lane))fastTracksPOCSetMode(lane,FAST_TRACKS_MODE_PATTERN);
    int count=fastTracksPOCGetRatioCount();
    fastTracksPOCSetRatioIndex(lane,(fastTracksPOCGetRatioIndex(lane)+count+delta)%count);
    setSongModifiedFlag();
}
int ts_tapehead_event(const SDL_Event *event,int x,int y) {
    if(!embed.initialized)return 0;
    mouse.x=mouse.rawX=x;mouse.y=mouse.rawY=y;
    switch(event->type) {
    case SDL_KEYDOWN:
        if(event->key.keysym.sym==SDLK_ESCAPE && ui.configScreenShown) {exitConfigScreen();return 1;}
        if(follow_key(event))return 1;
        /* MIDI and project commands belong to the host. Instrument destructive
           shortcuts cannot modify the host's tile registry. */
        if(event->key.keysym.scancode==SDL_SCANCODE_KP_PERIOD)return 1;
        if(event->key.keysym.sym==SDLK_F7 && !(event->key.keysym.mod&(KMOD_CTRL|KMOD_ALT|KMOD_SHIFT))) {
            ts_tapehead_request(TS_TH_CAPTURE);return 1;
        }
        if(event->key.keysym.sym==SDLK_F8 && tapeheadBlockLoopIsActive() && !(event->key.keysym.mod&(KMOD_CTRL|KMOD_ALT|KMOD_SHIFT))) {
            ts_tapehead_request(TS_TH_CYCLE_CAPTURE);return 1;
        }
        {
            int sc=event->key.keysym.scancode;
            int note=cursor.object==CURSOR_NOTE && !(event->key.keysym.mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI))?
                     scancodeKeyToNote((SDL_Scancode)sc):0;
            if(sc>=0 && sc<SDL_NUM_SCANCODES)embed.key_held[sc]=1;
            keyDownHandler((SDL_Scancode)sc,event->key.keysym.sym,(SDL_Keymod)event->key.keysym.mod,event->key.repeat);
            if(!songPlaying)embed.live_edit=0;
            if(note>0 && note<=96 && sc>=0 && sc<SDL_NUM_SCANCODES)
                for(int ch=0;ch<8;++ch)if(editor.keyOnTab[ch]==note)embed.key_note[sc]=note;
            return 1;
        }
    case SDL_KEYUP: {
        int sc=event->key.keysym.scancode;
        if(sc<0 || sc>=SDL_NUM_SCANCODES || !embed.key_held[sc])return 0;
        int note=embed.key_note[sc];embed.key_held[sc]=embed.key_note[sc]=0;
        bool modifier=keyb.keyModifierDown;keyb.keyModifierDown=true;
        keyUpHandler((SDL_Scancode)sc,event->key.keysym.sym);keyb.keyModifierDown=modifier;
        if(note)recordNote(note,0);
        return 1;
    }
    case SDL_MOUSEBUTTONDOWN: {
        readKeyModifiers();
        if(main_panel_visible() && !ui.sysReqShown && !editor.editTextFlag &&
           x>=359 && x<418 && y>=155 && y<171) {
            if(event->button.button==SDL_BUTTON_LEFT)follow_toggle();
            return 1;
        }
        int lane=fasttracks_header_lane(x,y);
        if(lane>=0 && !ui.sysReqShown && !editor.editTextFlag &&
           (event->button.button==SDL_BUTTON_LEFT || event->button.button==SDL_BUTTON_RIGHT)) {
            int local=(x-30)%ui.patternChannelWidth;
            int ratio_width=fastTracksPOCGetRatioNumerator(lane)>=10 || fastTracksPOCGetRatioDenominator(lane)>=10?16:12;
            if(SDL_GetModState()&KMOD_SHIFT || (event->button.button==SDL_BUTTON_LEFT && local>=ui.patternChannelWidth-17))
                fastTracksPOCSetMode(lane,(fastTracksPOCGetMode(lane)+1)%3);
            else if(event->button.button==SDL_BUTTON_RIGHT || (local>=ratio_width+4 && local<ratio_width+10)) {
                if(!fastTracksPOCIsSelected(lane))fastTracksPOCSetMode(lane,FAST_TRACKS_MODE_PATTERN);
                fastTracksPOCSetDirection(lane,(fastTracksPOCGetDirection(lane)+1)%3);
            } else if(!fastTracksPOCIsSelected(lane))fastTracksPOCSetMode(lane,FAST_TRACKS_MODE_PATTERN);
            else ratio_adjust(lane,1);
            setSongModifiedFlag();return 1;
        }
        if(main_panel_visible() && x>=421 && x<632 && y<173) {
            if(!ui.sysReqShown && !editor.editTextFlag && event->button.button==SDL_BUTTON_LEFT)canvas_click(x,y);
            return 1;
        }
        if(main_panel_visible() && x>=294 && x<353 && y>=155 && y<171) {
            editor.curOctave=CLAMP(editor.curOctave+(event->button.button==SDL_BUTTON_RIGHT?-1:1),0,7);return 1;
        }
        embed.pointer_mark=0;embed.pointer_x=x;embed.pointer_y=y;
        mouse.buttonState|=SDL_BUTTON(event->button.button);mouseButtonDownHandler(event->button.button);return 1;
    }
    case SDL_MOUSEBUTTONUP:
        if(event->button.button==SDL_BUTTON_LEFT && embed.pointer_mark && !embed.follow &&
           abs(x-embed.pointer_x)<3 && abs(y-embed.pointer_y)<3)ts_tapehead_edit_row(embed.mark_row);
        embed.pointer_mark=0;
        mouse.buttonState&=~SDL_BUTTON(event->button.button);mouseButtonUpHandler(event->button.button);
        if(!songPlaying)embed.live_edit=0;
        return 1;
    case SDL_MOUSEMOTION:return 1;
    case SDL_MOUSEWHEEL: {
        readKeyModifiers();
        double d=event->wheel.preciseY?event->wheel.preciseY:event->wheel.y;
        if(event->wheel.direction==SDL_MOUSEWHEEL_FLIPPED)d=-d;
        int lane_target=fasttracks_header_lane(x,y),target=400;
        if(lane_target>=0)target=100+lane_target;
        else if(ui.patternEditorShown && x>=30 && ui.patternChannelWidth) {
            const pattCoord2_t *p=&pattCoord2Table[config.ptnStretch][ui.pattChanScrollShown][getPatternEditorView()];
            if(y>=p->upperRowsY+2 && y<p->upperRowsY+10)target=200+(x-30)/ui.patternChannelWidth;
        }
        if(main_panel_visible() && x>=125 && x<168 && y>=62 && y<78)target=300;
        if(main_panel_visible() && x>=294 && x<353 && y>=155 && y<171)target=301;
        if(main_panel_visible() && x>=421 && x<632 && y<173)target=600;
        if(ui.configScreenShown)target=500;
        if(target!=embed.wheel_target || d*embed.wheel_fraction<0)embed.wheel_fraction=0;
        embed.wheel_target=target;embed.wheel_fraction+=d;
        int ticks=(int)embed.wheel_fraction;embed.wheel_fraction-=ticks;
        for(int i=0;i<abs(ticks);++i) {
            int lane=fasttracks_header_lane(x,y);
            if(ui.sysReqShown || editor.editTextFlag)break;
            if(lane>=0)ratio_adjust(lane,ticks>0?1:-1);
            else if(target==600)canvas_page_step(ticks>0?-1:1);
            else if(main_panel_visible() && x>=125 && x<168 && y>=62 && y<78) {if(ticks>0)pbIncAdd();else pbDecAdd();}
            else if(main_panel_visible() && x>=294 && x<353 && y>=155 && y<171)
                editor.curOctave=CLAMP(editor.curOctave+(ticks>0?1:-1),0,7);
            else mouseWheelHandler(ticks>0?MOUSE_WHEEL_UP:MOUSE_WHEEL_DOWN);
        }
        return 1;
    }
    case SDL_TEXTINPUT: {
        if(editor.editTextFlag) {char input[SDL_TEXTINPUTEVENT_TEXT_SIZE];memcpy(input,event->text.text,sizeof(input));input[sizeof(input)-1]=0;char *s=utf8ToCp850(input,false);if(s){for(char *c=s;*c;++c)handleTextEditInputChar(*c);free(s);}}
        return 1;
    }
    default:return 0;
    }
}
const float *ts_tapehead_render(unsigned frames,unsigned rate) {
    if(!atomic_load_explicit(&embed.ready,memory_order_acquire) || frames>TH_FRAMES || rate!=embed.rate)return NULL;
    memset(embed.capture_flags,0,frames);
    tapeheadEmbeddedAudioRender(embed.output,frames);return embed.output;
}

static uint64_t hash_bytes(uint64_t h,const void *p,size_t n) {const uint8_t *b=p;while(n--){h^=*b++;h*=UINT64_C(1099511628211);}return h;}
#define HASH(v) h=hash_bytes(h,&(v),sizeof(v))
static int bind_tiles(TsSamplePages *pages,const TsInstrument *active,char *e,size_t size) {
    TsSisterTracker *t=&pages->tracker;
    /* Register the active bank and populated visible tiles without reassigning
       existing aliases. A missing/deleted tile never resolves by slot number. */
    for(int pass=0;pass<2;++pass) {
    for(int cell=0;cell<(pass?CANVAS_TILES:TS_BANK_SLOT_COUNT);++cell) {
    int slot=cell;
    const TsInstrument *bank=pass?canvas_bank_at(cell,&slot):active;
    if(!bank)continue;
    if(bank->bank[slot].occupied) {
        TsTileId id=bank->bank[slot].tile_id;int a=1;
        for(;a<=128 && t->aliases[a]!=id;++a);
        if(a>128){for(a=1;a<=128 && t->aliases[a];++a);if(a<=128)t->aliases[a]=id;}
        if(a>128 && !pass && slot==active->selected_slot)return fail(e,size,"Tracker's 128 tile aliases are occupied");
        if(a<=128 && !pass && slot==active->selected_slot && embed.selected_tile!=id) {editor.curInstr=a;embed.selected_tile=id;ui.updatePosSections=true;updateInstrumentSwitcher();}
    }
    }
    }
    for(int a=1;a<=128;++a) {
        TsTileLocation loc={0};const TsBankSlot *slot=t->aliases[a]?ts_sample_pages_find_tile(pages,active,t->aliases[a],&loc):NULL;
        const TsInstrument *bank=slot?ts_sample_pages_page(pages,active,loc.page):NULL;
        const TsSample *src=slot?(bank->selected_slot==loc.slot&&bank->current.data?&bank->current:&slot->sample):NULL;
        int live=src&&src==&bank->current;
        TsTuning tuning=slot?(live?bank->audible_tuning:slot->audible_tuning):(TsTuning){60,0};
        int loop=slot?(live?bank->has_loop:slot->has_loop):0;
        size_t first=slot?(live?bank->loop_first:slot->loop_first):0,last=slot?(live?bank->loop_last:slot->loop_last):0;
        float fade_ms=slot?(live?bank->loop_crossfade_ms:slot->loop_crossfade_ms):0;
        TsLoopMode mode=slot?(live?bank->loop_mode:slot->loop_mode):TS_LOOP_FORWARD;
        uint64_t h=UINT64_C(14695981039346656037);HASH(t->aliases[a]);HASH(src);HASH(tuning.root_note);HASH(tuning.fine_tune_cents);HASH(loop);HASH(first);HASH(last);HASH(mode);HASH(fade_ms);
        if(src){HASH(src->name);HASH(src->data);HASH(src->visual_revision);HASH(src->frames);HASH(src->channels);HASH(src->sample_rate);if(live)HASH(bank->generation);}
        embed.tile_location[a]=loc;embed.tile_rate[a]=src?src->sample_rate:0;
        if(h==embed.tile_stamp[a])continue;
        sample_t prepared={0};float *data=NULL;
        if(src && src->data && src->frames>0) {
            if(src->frames>MAX_SAMPLE_LEN || src->channels<1 || src->channels>2)return fail(e,size,"Tile exceeds tracker sample capacity");
            data=malloc(src->frames*src->channels*sizeof(float));
            if(!data || !allocateSmpData(&prepared,(int32_t)src->frames,true)) {free(data);freeSmpData(&prepared);return fail(e,size,"Unable to prepare tracker tile");}
            memcpy(data,src->data,src->frames*src->channels*sizeof(float));
            for(size_t f=0;f<src->frames;++f) {
                float mono=data[f*src->channels];if(src->channels==2)mono=(mono+data[f*2+1])*.5f;
                ((int16_t*)prepared.dataPtr)[f]=(int16_t)lrintf(fmaxf(-1,fminf(1,mono))*32767);
            }
            prepared.tileData=data;prepared.tileChannels=src->channels;
            prepared.length=(int32_t)src->frames;
            prepared.tileLoopMode=mode;
            TsAuditionPlan plan={src,first,last};
            prepared.tileCrossfade=loop?ts_audition_crossfade_frames(&plan,fade_ms):0;
            prepared.volume=64;prepared.panning=128;prepared.flags=SAMPLE_16BIT;
            snprintf(prepared.name,sizeof(prepared.name),"%.22s",src->name);
            if(loop && first<last && last<=src->frames) {
                prepared.loopStart=(int)first;prepared.loopLength=(int)(last-first);
                prepared.flags|=(mode==TS_LOOP_PING_PONG||mode==TS_LOOP_START_PING_PONG)?LOOP_PINGPONG:LOOP_FORWARD;
                if(mode==TS_LOOP_REVERSE||mode==TS_LOOP_START_REVERSE)prepared.flags|=SAMPLE_REVERSE_LOOP;
            }
            double c4_rate=src->sample_rate*exp2((60-tuning.root_note)/12.0+tuning.fine_tune_cents/1200.0);
            setSampleC4Hz(&prepared,c4_rate);
            prepared.tileRateCorrection=c4_rate/getSampleC4Hz(&prepared);
            fixSample(&prepared);
        }
        RetiredTile *retired=embed.tile_data[a]?malloc(sizeof(*retired)):NULL;
        if(embed.tile_data[a]&&!retired) {free(data);freeSmpData(&prepared);return fail(e,size,"Unable to retain sounding tile");}
        ts_tapehead_host_lock();
        if(!instr[a] && !allocateInstr(a)) {ts_tapehead_host_unlock();free(retired);free(data);freeSmpData(&prepared);return fail(e,size,"Unable to bind tracker tile");}
        sample_t *old=&instr[a]->smp[0];
        stopAllScopes();freeSmpData(old);
        if(retired) {retired->data=embed.tile_data[a];retired->next=embed.retired;embed.retired=retired;}
        *old=prepared;embed.tile_data[a]=data;embed.tile_stamp[a]=h;
        if(src&&src->name[0])snprintf(song.instrName[a],sizeof(song.instrName[a]),"%.22s",src->name);
        else if(src)snprintf(song.instrName[a],sizeof(song.instrName[a]),"TILE %02X",a);
        else snprintf(song.instrName[a],sizeof(song.instrName[a]),"%s",t->aliases[a]?"MISSING TILE":"");
        updateInstrumentSwitcher();
        collect_tiles();ts_tapehead_host_unlock();
    }
    return 1;
}

static void put(uint8_t **p,uint32_t v,int width) {while(width--){*(*p)++=(uint8_t)v;v>>=8;}}
static uint32_t get(const uint8_t **p,int width) {uint32_t v=0;for(int i=0;i<width;++i)v|=(uint32_t)*(*p)++<<(8*i);return v;}
static int native_import(TsSisterTracker *t,char *e,size_t n) {
    if(!ts_sister_tracker_validate(t,e,n))return 0;
    if(t->bpm>255)return fail(e,n,"Existing score tempo exceeds TapeHead's 255 BPM limit");
    if(!t->pattern_count && !ts_sister_tracker_add_pattern(t,64,&t->editor_pattern,e,n))return 0;
    note_t *prepared[256]={0};
    for(int i=0;i<t->pattern_count;++i) {
        prepared[i]=calloc(MAX_PATT_LEN*TRACK_WIDTH+16,1);
        if(!prepared[i])goto allocation_failed;
    }
    for(int i=0;i<t->pattern_count;++i) {
        const TsTrackerPattern *p=t->patterns[i];
        for(int row=0;row<256;++row)for(int lane=0;lane<8;++lane) {
            const TsTrackerCell *c=&p->cells[row][lane];note_t *d=&prepared[i][row*MAX_CHANNELS+lane];
            if(c->note_kind==TS_TRACKER_NOTE_PITCH) {
                if(c->note<12 || c->note>107) {fail(e,n,"Existing score contains pitches outside TapeHead C-0 to B-7");goto import_failed;}
                d->note=c->note-11;
            } else if(c->note_kind==TS_TRACKER_NOTE_OFF)d->note=NOTE_OFF;
            else if(c->note_kind==TS_TRACKER_NOTE_CUT){d->efx=0xE;d->efxData=0xC0;}
            if(c->tile_id) {
                int a=1;for(;a<=128&&t->aliases[a]!=c->tile_id;++a);
                if(a>128) {fail(e,n,"Existing score uses a tile alias beyond TapeHead's 128 instruments");goto import_failed;}
                d->instr=a;
            }
            if(c->has_volume)d->vol=c->volume+0x10;
            if(c->tune_command)d->tuneType=c->tune_command=='M'?0x16:0x17;
            d->tuneData=c->tune_value;
            if(c->fx_command){d->efx=c->fx_command<='9'?c->fx_command-'0':c->fx_command-'A'+10;d->efxData=c->fx_value;}
        }
    }
    ts_tapehead_focus_lost();stopPlaying();undoClear();embed.capture_seam_pending=0;
    tapeheadEmbeddedClearClipboard();memset((void*)&pattMark,0,sizeof(pattMark));
    fastTracksPOCResetForLoadedModule();
    for(int i=0;i<256;++i) {
        free(pattern[i]);pattern[i]=prepared[i];embed.ids[i]=i<t->pattern_count?t->patterns[i]->id:0;
        patternNumRows[i]=i<t->pattern_count?t->patterns[i]->rows:64;
        if(embed.ids[i]==t->editor_pattern)editor.editPattern=i;
    }
    song.numChannels=8;
    song.songLength=t->order_count?t->order_count:1;song.songLoopStart=t->restart_order;
    for(int i=0;i<t->order_count;++i)for(int p=0;p<t->pattern_count;++p)if(t->orders[i]==embed.ids[p])song.orders[i]=p;
    editor.row=t->editor_row;cursor.ch=t->editor_lane;editor.editRowSkip=t->edit_step;
    editor.BPM=song.BPM=t->bpm;editor.speed=song.speed=song.initialSpeed=t->ticks_per_line;
    setMixerBPM(song.BPM);fastTracksPOCSetMasterEnabled(true);
    fastTracksPOCSetUsesTrackLengths(t->fasttracks_uses_length);
    fastTracksPOCSetLengthTopologyBypassed(t->length_bypass);
    for(int lane=0;lane<8;++lane) {
        TsTrackerLane *l=&t->lanes[lane];fastTracksPOCSetTrackLength(0,lane,l->length);
        fastTracksPOCSetMode(lane,(fastTracksMode_t)l->mode);fastTracksPOCSetRatioIndex(lane,l->ratio);
        fastTracksPOCSetDirection(lane,l->direction);
        channelVolumeTrim[lane]=(uint16_t)lrintf(l->trim*256);setChannelMute(lane,l->muted);
    }
    fastTracksPOCSetControlTrack(0,t->control_lane);playMode=PLAYMODE_EDIT;
    song.row=editor.row;song.pattNum=editor.editPattern;song.currNumRows=patternNumRows[editor.editPattern];
    fastTracksPOCResetForLoadedModule();fastTracksPOCSetLengthTopologyBypassed(t->length_bypass);
    ui.updatePatternEditor=ui.updatePosSections=true;return 1;
allocation_failed:
    fail(e,n,"Unable to import tracker pattern");
import_failed:
    for(int i=0;i<256;++i)free(prepared[i]);
    return 0;
}

int ts_tapehead_export(TsSisterTracker *t,char *e,size_t n) {
    if(!embed.initialized || !t)return 1;
    int count=0;uint8_t used[256]={0};
    used[editor.editPattern]=1;
    for(int i=0;i<song.songLength;++i)used[song.orders[i]]=1;
    for(int i=0;i<256;++i)if(pattern[i]||embed.ids[i]||used[i])++count;
    size_t size=TH_HEADER+8*TH_LANE_BYTES+256+(size_t)count*TH_PATTERN_BYTES+TH_PREFS_BYTES;
    uint8_t *bytes=calloc(1,size);if(!bytes)return fail(e,n,"Unable to save embedded tracker score");
    uint8_t *p=bytes;memcpy(p,"STH2\0\0\0\0",8);p+=8;
    put(&p,count,2);put(&p,editor.BPM,2);put(&p,editor.speed,1);put(&p,editor.globalVolume,1);
    put(&p,song.songLength,2);put(&p,song.songLoopStart,2);put(&p,editor.editPattern,1);put(&p,editor.row,2);
    put(&p,editor.curOctave,1);put(&p,editor.curInstr,1);put(&p,editor.editRowSkip,1);
    put(&p,fastTracksPOCMasterIsEnabled(),1);put(&p,fastTracksPOCUsesTrackLengths(),1);put(&p,fastTracksPOCLengthTopologyIsBypassed(),1);
    put(&p,ui.patternEditorOnly,1);put(&p,ui.extendedPatternEditor,1);put(&p,tapeheadConfig.patternColorMode,1);
    memcpy(p,song.name,21);p+=21;put(&p,fastTracksPOCGetControlTrack(0)+1,1);
    for(int lane=0;lane<8;++lane) {
        put(&p,fastTracksPOCGetTrackLength(0,lane),2);put(&p,fastTracksPOCGetMode(lane),1);
        put(&p,fastTracksPOCGetRatioIndex(lane),1);put(&p,fastTracksPOCGetDirection(lane),1);
        put(&p,fastTracksPOCIsSelected(lane),1);put(&p,editor.channelMuted[lane],1);
        put(&p,channelVolumeTrim[lane],2);put(&p,0,1);
    }
    memcpy(p,song.orders,256);p+=256;
    for(int i=0;i<256;++i)if(pattern[i]||embed.ids[i]||used[i]) {
        if(!embed.ids[i]) {
            TsPatternId id;
            if(!ts_sister_tracker_add_pattern(t,patternNumRows[i],&id,e,n)){free(bytes);return 0;}
            embed.ids[i]=id;
        }
        TsTrackerPattern *native=ts_sister_tracker_pattern(t,embed.ids[i]);
        if(!native){free(bytes);return fail(e,n,"Embedded pattern identity is missing");}
        native->rows=patternNumRows[i];
        put(&p,i,1);put(&p,patternNumRows[i],2);put(&p,embed.ids[i],4);
        for(int row=0;row<256;++row)for(int lane=0;lane<8;++lane) {
            note_t c=pattern[i]?pattern[i][row*MAX_CHANNELS+lane]:(note_t){0};
            put(&p,c.note,1);put(&p,c.instr,1);put(&p,c.vol,1);put(&p,c.efx,1);put(&p,c.efxData,1);put(&p,c.tuneType,1);put(&p,c.tuneData,1);
            TsTrackerCell *d=&native->cells[row][lane];memset(d,0,sizeof(*d));
            if(c.note==NOTE_OFF)d->note_kind=TS_TRACKER_NOTE_OFF;
            else if(c.note){d->note_kind=TS_TRACKER_NOTE_PITCH;d->note=c.note+11;}
            d->tile_id=c.instr?t->aliases[c.instr]:0;
            if(c.vol>=0x10&&c.vol<=0x50){d->has_volume=1;d->volume=c.vol-0x10;}
            if(c.tuneType==0x16||c.tuneType==0x17){d->tune_command=c.tuneType==0x16?'M':'N';d->tune_value=c.tuneData;}
            if(c.efx||c.efxData){d->fx_command=c.efx<10?'0'+c.efx:'A'+c.efx-10;d->fx_value=c.efxData;}
        }
    }
    preferences_get(bytes+size-TH_PREFS_BYTES);
    t->bpm=editor.BPM;t->ticks_per_line=editor.speed;t->order_count=song.songLength;t->restart_order=song.songLoopStart;
    for(int i=0;i<song.songLength;++i)t->orders[i]=embed.ids[song.orders[i]];
    t->editor_pattern=embed.ids[editor.editPattern];t->editor_row=editor.row;t->editor_lane=cursor.ch;t->edit_step=editor.editRowSkip;
    t->follow=embed.follow;
    t->fasttracks_uses_length=fastTracksPOCUsesTrackLengths();t->length_bypass=fastTracksPOCLengthTopologyIsBypassed();t->control_lane=fastTracksPOCGetControlTrack(0);
    for(int lane=0;lane<8;++lane) {
        TsTrackerLane *l=&t->lanes[lane];l->length=fastTracksPOCGetTrackLength(0,lane);
        l->mode=fastTracksPOCGetMode(lane);l->ratio=fastTracksPOCGetRatioIndex(lane);
        l->direction=fastTracksPOCGetDirection(lane);
        l->trim=channelVolumeTrim[lane]/256.f;l->muted=editor.channelMuted[lane];
    }
    uint8_t *previous=t->embedded_data;uint32_t previous_size=t->embedded_size;
    t->embedded_data=bytes;t->embedded_size=(uint32_t)size;
    if(!ts_sister_tracker_validate(t,e,n)) {
        t->embedded_data=previous;t->embedded_size=previous_size;free(bytes);return 0;
    }
    free(previous);
    embed.model_hash=ts_sister_tracker_hash(t);return 1;
}

static int score_import(TsSisterTracker *t,char *e,size_t n) {
    if(!t->embedded_size) {preferences_apply(default_preferences);return native_import(t,e,n);}
    if(!ts_tracker_embedded_validate(t->embedded_data,t->embedded_size))return fail(e,n,"Invalid embedded tracker score");
    note_t *prepared[256]={0};
    const uint8_t *records=t->embedded_data+TH_HEADER+8*TH_LANE_BYTES+256;
    int total=t->embedded_data[8]|(t->embedded_data[9]<<8);
    for(int k=0;k<total;++k) {
        int i=records[k*TH_PATTERN_BYTES];
        prepared[i]=calloc(MAX_PATT_LEN*TRACK_WIDTH+16,1);
        if(!prepared[i]) {
            for(int j=0;j<256;++j)free(prepared[j]);
            return fail(e,n,"Unable to load embedded tracker pattern");
        }
    }
    const uint8_t *p=t->embedded_data+8;int count=get(&p,2);
    ts_tapehead_focus_lost();stopPlaying();undoClear();embed.capture_seam_pending=0;
    tapeheadEmbeddedClearClipboard();memset((void*)&pattMark,0,sizeof(pattMark));
    for(int i=0;i<256;++i){free(pattern[i]);pattern[i]=prepared[i];patternNumRows[i]=64;embed.ids[i]=0;}
    editor.BPM=song.BPM=get(&p,2);editor.speed=song.speed=get(&p,1);song.initialSpeed=MAX(song.speed,1);
    editor.globalVolume=song.globalVolume=get(&p,1);song.songLength=get(&p,2);song.songLoopStart=get(&p,2);
    editor.editPattern=get(&p,1);editor.row=get(&p,2);editor.curOctave=get(&p,1);editor.curInstr=get(&p,1);editor.editRowSkip=get(&p,1);
    int master=get(&p,1),uses=get(&p,1),bypass=get(&p,1),only=get(&p,1),expanded=get(&p,1),color=get(&p,1);
    memcpy(song.name,p,21);p+=21;int control=(int)get(&p,1)-1;
    fastTracksPOCResetForLoadedModule();fastTracksPOCSetMasterEnabled(master);
    fastTracksPOCSetUsesTrackLengths(uses);fastTracksPOCSetLengthTopologyBypassed(bypass);
    for(int lane=0;lane<8;++lane) {
        unsigned len=get(&p,2),mode=get(&p,1),ratio=get(&p,1),reverse=get(&p,1),selected=get(&p,1),muted=get(&p,1),trim=get(&p,2);get(&p,1);
        fastTracksPOCSetTrackLength(0,lane,len);fastTracksPOCSetMode(lane,mode);fastTracksPOCSetRatioIndex(lane,ratio);
        fastTracksPOCSetDirection(lane,reverse);
        if(selected!=fastTracksPOCIsSelected(lane))fastTracksPOCToggle(lane);
        channelVolumeTrim[lane]=trim;setChannelMute(lane,muted);
    }
    fastTracksPOCSetControlTrack(0,control);memcpy(song.orders,p,256);p+=256;
    for(int k=0;k<count;++k) {
        int i=get(&p,1);int rows=get(&p,2);embed.ids[i]=get(&p,4);
        patternNumRows[i]=rows;
        for(int row=0;row<256;++row)for(int lane=0;lane<8;++lane) {
            note_t *c=&pattern[i][row*MAX_CHANNELS+lane];
            c->note=get(&p,1);c->instr=get(&p,1);c->vol=get(&p,1);c->efx=get(&p,1);c->efxData=get(&p,1);c->tuneType=get(&p,1);c->tuneData=get(&p,1);
        }
    }
    preferences_apply(t->embedded_data[3]=='2'?t->embedded_data+t->embedded_size-TH_PREFS_BYTES:default_preferences);
    /* STH1 already stored these two settings, before the preferences tail. */
    fastTracksPOCSetUsesTrackLengths(uses);tapeheadConfig.patternColorMode=color;
    cursor.ch=t->editor_lane;
    setMixerBPM(song.BPM);song.numChannels=8;playMode=PLAYMODE_EDIT;
    song.pattNum=editor.editPattern;song.row=editor.row;song.currNumRows=patternNumRows[editor.editPattern];
    fastTracksPOCResetForLoadedModule();fastTracksPOCSetLengthTopologyBypassed(bypass);
    if(ui.patternEditorOnly)togglePatternEditorOnly();
    if(ui.extendedPatternEditor)togglePatternEditorExtended();
    if(only)togglePatternEditorOnly();else if(expanded)togglePatternEditorExtended();
    ui.updatePatternEditor=ui.updatePosSections=true;return 1;
}
int ts_tapehead_sync(TsSamplePages *pages,const TsInstrument *active,unsigned rate,char *e,size_t n) {
    if(!embed.initialized)return 1;
    TsSisterTracker *t=&pages->tracker;uint64_t h=ts_sister_tracker_hash(t);
    if(embed.model!=t || h!=embed.model_hash) {
        ts_tapehead_host_lock();atomic_store(&embed.ready,0);
        if(!score_import(t,e,n)){ts_tapehead_host_unlock();return 0;}
        embed.follow=t->follow;embed.live_edit=embed.mark_valid=embed.pointer_mark=0;
        embed.model=t;embed.model_hash=ts_sister_tracker_hash(t);memset(embed.tile_stamp,0,sizeof(embed.tile_stamp));
        ts_tapehead_host_unlock();
    }
    if(rate && rate!=embed.rate) {
        ts_tapehead_host_lock();atomic_store(&embed.ready,0);
        int ok=tapeheadEmbeddedAudioPrepare(rate,TH_FRAMES);embed.rate=rate;ts_tapehead_host_unlock();
        if(!ok)return fail(e,n,"Unable to prepare tracker audio rate");
    }
    int selected=active->selected_slot;
    if(embed.pages!=pages || embed.host_page!=pages->active_page ||
       (selected>=0 && selected<TS_BANK_SLOT_COUNT && active->bank[selected].occupied &&
        active->bank[selected].tile_id!=embed.selected_tile))
        embed.canvas_page=(pages->active_page*TS_BANK_SLOT_COUNT+(selected>=0?(size_t)selected:0))/CANVAS_TILES;
    embed.pages=pages;embed.active=active;embed.host_page=pages->active_page;
    if(embed.canvas_page>=canvas_view_count())embed.canvas_page=canvas_view_count()-1;
    if(!bind_tiles(pages,active,e,n))return 0;
    embed.model_hash=ts_sister_tracker_hash(t);atomic_store_explicit(&embed.ready,1,memory_order_release);
    return 1;
}
