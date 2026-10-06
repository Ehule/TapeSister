/* The editor owns its position when Follow is off. The transport keeps its
   real play mode, row, tick and FastTracks clocks throughout manual editing. */
int ts_tapehead_following(void) {return embed.follow;}
int ts_tapehead_live_editing(void) {
    return songPlaying && !embed.follow &&
        (embed.live_edit || playMode==PLAYMODE_RECPATT || playMode==PLAYMODE_RECSONG);
}
void ts_tapehead_edit_row(int row)
{
    editor.row=(uint8_t)CLAMP(row,0,fastTracksPOCGetExtendedPatternLength(editor.editPattern)-1);
    if(!songPlaying)song.row=editor.row;
    ui.updatePatternEditor=true;
}
void ts_tapehead_navigate_rows(int delta)
{
    int rows=fastTracksPOCGetExtendedPatternLength(editor.editPattern);
    if(rows<1)return;
    int row=editor.row+delta;
    if((config.dontShowAgainFlags&PATT_NAV_MODE_MASK)==PATT_NAV_WRAP)row=(row%rows+rows)%rows;
    ts_tapehead_edit_row(row);
}
void ts_tapehead_mark_anchor(int channel,int row)
{
    embed.mark_valid=1;embed.mark_pattern=editor.editPattern;
    embed.mark_channel=channel;embed.mark_row=row;embed.pointer_mark=1;
}
static void follow_toggle(void)
{
    ts_tapehead_host_lock();
    embed.follow=!embed.follow;
    if(embed.follow) {
        embed.live_edit=0;
        if(songPlaying) {
            editor.editPattern=editor.tmpPattern=(uint8_t)song.pattNum;
            editor.row=(uint8_t)song.row;editor.songPos=song.songPos;
            checkMarkLimits();
        }
    } else if(songPlaying && (playMode==PLAYMODE_RECPATT || playMode==PLAYMODE_RECSONG))embed.live_edit=1;
    ui.updatePatternEditor=ui.updatePosSections=true;
    ts_tapehead_host_unlock();
}
static void selection_arrow(SDL_Keycode key)
{
    int rows=fastTracksPOCGetExtendedPatternLength(editor.editPattern);
    if(rows<1)return;
    int x1=pattMark.markX1,x2=pattMark.markX2,y1=pattMark.markY1,y2=pattMark.markY2-1;
    if(y2<y1) {
        x1=x2=cursor.ch;y1=y2=editor.row;embed.mark_valid=0;
    }
    if(!embed.mark_valid || embed.mark_pattern!=editor.editPattern ||
       embed.mark_channel<x1 || embed.mark_channel>x2 || embed.mark_row<y1 || embed.mark_row>y2) {
        embed.mark_channel=x1;embed.mark_row=y1;embed.mark_pattern=editor.editPattern;embed.mark_valid=1;
    }
    int x=embed.mark_channel==x1?x2:x1,y=embed.mark_row==y1?y2:y1;
    if(key==SDLK_LEFT)--x;if(key==SDLK_RIGHT)++x;
    if(key==SDLK_UP)--y;if(key==SDLK_DOWN)++y;
    x=CLAMP(x,0,song.numChannels-1);y=CLAMP(y,0,rows-1);
    pattMark.markX1=MIN(embed.mark_channel,x);pattMark.markX2=MAX(embed.mark_channel,x);
    pattMark.markY1=MIN(embed.mark_row,y);pattMark.markY2=MAX(embed.mark_row,y)+1;
    checkMarkLimits();ui.updatePatternEditor=true;
}
static int follow_key(const SDL_Event *event)
{
    if(!ui.patternEditorShown || ui.sysReqShown || editor.editTextFlag || ui.configScreenShown ||
       patternNavPopupIsShown() || interpolationPreviewActive())return 0;
    SDL_Keycode key=event->key.keysym.sym;SDL_Keymod mod=(SDL_Keymod)event->key.keysym.mod;
    if(key==SDLK_f && (mod&KMOD_CTRL) && !(mod&(KMOD_SHIFT|KMOD_ALT|KMOD_GUI))) {
        if(!event->key.repeat)follow_toggle();return 1;
    }
    if(embed.mark_valid && embed.mark_pattern==editor.editPattern &&
       (mod&KMOD_ALT) && !(mod&(KMOD_CTRL|KMOD_SHIFT|KMOD_GUI)) &&
       (key==SDLK_UP || key==SDLK_DOWN || key==SDLK_LEFT || key==SDLK_RIGHT)) {
        selection_arrow(key);return 1;
    }
    if(songPlaying && !embed.follow && !(mod&(KMOD_CTRL|KMOD_SHIFT|KMOD_ALT|KMOD_GUI))) {
        if(key==SDLK_SPACE) {
            if(!event->key.repeat) {
                ts_tapehead_host_lock();
                if(ts_tapehead_live_editing()) {stopPlaying();embed.live_edit=0;}
                else embed.live_edit=1;
                ui.updatePatternEditor=ui.updatePosSections=true;
                ts_tapehead_host_unlock();
            }
            return 1;
        }
        if(key==SDLK_HOME || key==SDLK_END) {
            ts_tapehead_edit_row(key==SDLK_HOME?0:fastTracksPOCGetExtendedPatternLength(editor.editPattern)-1);return 1;
        }
    }
    return 0;
}
