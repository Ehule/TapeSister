/* Embedded tracker preferences. Original checkbox/palette callbacks own edits;
   only their presentation and storage are adapted to TapeSister. */
#define TH_PREFS_BYTES 128u
extern void tapeheadEmbeddedPaletteGet(uint8_t *bytes);
extern void tapeheadEmbeddedPaletteSet(const uint8_t *bytes);
static uint8_t default_preferences[TH_PREFS_BYTES];
static char preferences_message[96];

static void preferences_get(uint8_t p[TH_PREFS_BYTES]) {
    memset(p,0,TH_PREFS_BYTES);memcpy(p,"TPF1",4);
    p[4]=!!(config.specialFlags2&SILENT_REC_ENTRY);p[5]=!!(config.specialFlags&INHERIT_PATT_LEN);
    p[6]=!!(config.specialFlags2&INP_MODE);p[7]=!!(config.specialFlags2&AUTO_PATT_GEN);
    p[8]=config.multiRec;p[9]=config.multiKeyJazz;p[10]=config.multiEdit;p[11]=config.recRelease;
    p[12]=config.recQuant;p[13]=config.recQuantRes;p[14]=config.recTrueInsert;p[15]=config.ptnCutToBuffer;
    p[16]=config.killNotesOnStopPlay;p[17]=config.ptnStretch;p[18]=config.ptnHex;p[19]=config.ptnInstrZero;
    p[20]=config.ptnFrmWrk;p[21]=config.ptnLineLight;p[22]=config.ptnShowVolColumn;p[23]=config.ptnChnNumbers;
    p[24]=config.ptnFont;p[25]=config.ptnAcc;p[26]=config.ptnAlternativeLayout;p[27]=config.cfg_StdPalNum;
    p[28]=tapeheadConfig.patternColorMode;p[29]=tapeheadConfig.fastTracksUseTrackLengths;
    p[30]=!!(config.specialFlags&LINED_SCOPES);p[31]=config.ptnMaxChannels;
    tapeheadEmbeddedPaletteGet(p+32);
    memcpy(p+94,config.multiRecChn,32);p[126]=config.ptnLineLightStep;
}
static void preferences_apply(const uint8_t p[TH_PREFS_BYTES]) {
    config.specialFlags2=(config.specialFlags2&~(SILENT_REC_ENTRY|INP_MODE|AUTO_PATT_GEN))|
        (p[4]?SILENT_REC_ENTRY:0)|(p[6]?INP_MODE:0)|(p[7]?AUTO_PATT_GEN:0);
    config.specialFlags=(config.specialFlags&~(INHERIT_PATT_LEN|LINED_SCOPES))|
        (p[5]?INHERIT_PATT_LEN:0)|(p[30]?LINED_SCOPES:0);
    config.multiRec=p[8];config.multiKeyJazz=p[9];config.multiEdit=p[10];config.recRelease=p[11];
    config.recQuant=p[12];config.recQuantRes=p[13];config.recTrueInsert=p[14];config.ptnCutToBuffer=p[15];
    config.killNotesOnStopPlay=p[16];config.ptnStretch=p[17];config.ptnHex=p[18];config.ptnInstrZero=p[19];
    config.ptnFrmWrk=p[20];config.ptnLineLight=p[21];config.ptnShowVolColumn=p[22];config.ptnChnNumbers=p[23];
    config.ptnFont=p[24];config.ptnAcc=p[25];config.ptnAlternativeLayout=p[26];config.cfg_StdPalNum=p[27];
    tapeheadConfig.patternColorMode=p[28];tapeheadConfig.fastTracksUseTrackLengths=p[29];config.ptnMaxChannels=p[31];
    memcpy(config.multiRecChn,p+94,32);config.ptnLineLightStep=p[126];
    tapeheadEmbeddedPaletteSet(p+32);updatePattFontPtrs();updateChanNums();
    ui.updatePatternEditor=ui.updatePosSections=true;
}
int ts_tapehead_preferences_load(const char *path,char *error,size_t size) {
    SDL_RWops *f=SDL_RWFromFile(path,"rb");if(!f)return 1; /* First run has no defaults. */
    uint8_t bytes[TH_PREFS_BYTES];
    int ok=SDL_RWsize(f)==TH_PREFS_BYTES && SDL_RWread(f,bytes,1,sizeof(bytes))==sizeof(bytes);
    if(SDL_RWclose(f))ok=0;
    if(!ok || !ts_tracker_preferences_validate(bytes,sizeof(bytes)))return fail(error,size,"Invalid tracker defaults");
    memcpy(default_preferences,bytes,sizeof(bytes));preferences_apply(bytes);return 1;
}
int ts_tapehead_preferences_save(const char *path,char *error,size_t size) {
    uint8_t bytes[TH_PREFS_BYTES];preferences_get(bytes);
    size_t length=strlen(path);char *temporary=malloc(length+5);
    if(!temporary)return fail(error,size,"Unable to save tracker defaults");
    memcpy(temporary,path,length);memcpy(temporary+length,".tmp",5);
    SDL_RWops *f=SDL_RWFromFile(temporary,"wb");
    int ok=f && SDL_RWwrite(f,bytes,1,sizeof(bytes))==sizeof(bytes);
    if(f && SDL_RWclose(f))ok=0;
#ifdef _WIN32
    int wn=MultiByteToWideChar(CP_UTF8,0,path,-1,NULL,0);
    int tn=MultiByteToWideChar(CP_UTF8,0,temporary,-1,NULL,0);
    wchar_t *w=wn>0?malloc(wn*sizeof(*w)):NULL,*t=tn>0?malloc(tn*sizeof(*t)):NULL;
    if(!w || !t)ok=0;
    if(w && t) {
        int temporary_valid=MultiByteToWideChar(CP_UTF8,0,temporary,-1,t,tn)>0;
        if(!temporary_valid || !MultiByteToWideChar(CP_UTF8,0,path,-1,w,wn))ok=0;
        if(ok)ok=MoveFileExW(t,w,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
        if(!ok && temporary_valid)DeleteFileW(t);
    }
    free(w);free(t);
#else
    if(ok)ok=rename(temporary,path)==0;
    if(!ok)remove(temporary);
#endif
    free(temporary);
    snprintf(preferences_message,sizeof(preferences_message),ok?"Defaults saved":"Could not save defaults");
    if(!ok)return fail(error,size,preferences_message);
    memcpy(default_preferences,bytes,sizeof(bytes));return 1;
}
static void preferences_save_button(void) {ts_tapehead_request(TS_TH_SAVE_DEFAULTS);}
static void preferences_audio_button(void) {ts_tapehead_request(TS_TH_AUDIO);}
static void preferences_record_page(void) {editor.currConfigScreen=CONFIG_SCREEN_MISCELLANEOUS;ts_tapehead_show_config();}
static void preferences_layout_page(void) {editor.currConfigScreen=CONFIG_SCREEN_LAYOUT;ts_tapehead_show_config();}
static void preferences_font_button(void) {config.ptnFont=(config.ptnFont+1)%4;updatePattFontPtrs();ui.updatePatternEditor=true;ts_tapehead_show_config();}
static void preferences_quant_up(void) {config.recQuantRes=MIN(16,config.recQuantRes*2);ts_tapehead_show_config();}
static void preferences_quant_down(void) {config.recQuantRes=MAX(1,config.recQuantRes/2);ts_tapehead_show_config();}
static void settings_button(int id,int x,int y,int width,const char *caption,void (*fn)(void)) {
    pushButton_t *b=&pushButtons[id];b->x=x;b->y=y;b->w=width;b->h=16;
    b->caption=(char*)caption;b->caption2=NULL;b->callbackFuncOnDown=NULL;b->callbackFuncOnUp=fn;showPushButton(id);
}
static void settings_checkbox(int id,int x,int y,const char *label,int value) {
    checkBox_t *c=&checkBoxes[id];c->x=x;c->y=y;c->clickAreaWidth=184;c->clickAreaHeight=12;
    c->checked=!!value;showCheckBox(id);textOut(x+16,y+2,PAL_FORGRND,label);
}
int ts_tapehead_config_visible(void) {return embed.initialized && ui.configScreenShown;}
void ts_tapehead_show_config(void) {
    if(ui.extendedPatternEditor)exitPatternEditorExtended();
    if(ui.patternEditorOnly)togglePatternEditorOnly();
    int layout=editor.currConfigScreen==CONFIG_SCREEN_LAYOUT;
    hideTopScreen();ui.configScreenShown=true;
    editor.currConfigScreen=layout?CONFIG_SCREEN_LAYOUT:CONFIG_SCREEN_MISCELLANEOUS;
    drawFramework(0,0,394,173,FRAMEWORK_TYPE1);drawFramework(394,0,238,173,FRAMEWORK_TYPE1);
    textOut(8,5,PAL_FORGRND,"Tracker configuration");
    settings_button(PB_CONFIG_LOAD,8,20,91,layout?"Recording":"Recording *",preferences_record_page);
    settings_button(PB_CONFIG_RESET,102,20,91,layout?"Layout *":"Layout",preferences_layout_page);
    settings_button(PB_CONFIG_EXIT,8,153,64,"Done",exitConfigScreen);
    settings_button(PB_CONFIG_SAVE,76,153,115,"Save defaults",preferences_save_button);
    settings_button(PB_CONFIG_AUDIO_RESCAN,195,153,88,"Audio health",preferences_audio_button);
    if(!layout) {
        settings_checkbox(CB_CONF_SILENT_REC_ENTRY,8,41,"Silent record",config.specialFlags2&SILENT_REC_ENTRY);
        settings_checkbox(CB_CONF_INHERIT_PATT_LEN,8,54,"Inherit pattern length",config.specialFlags&INHERIT_PATT_LEN);
        settings_checkbox(CB_CONF_INP_MODE,8,67,"Insert new pattern",config.specialFlags2&INP_MODE);
        settings_checkbox(CB_CONF_AUTO_PATT_GEN,8,80,"Auto pattern generation",config.specialFlags2&AUTO_PATT_GEN);
        settings_checkbox(CB_CONF_MULTICHAN_REC,8,93,"Multichannel record",config.multiRec);
        settings_checkbox(CB_CONF_MULTICHAN_JAZZ,8,106,"Multichannel key jazz",config.multiKeyJazz);
        settings_checkbox(CB_CONF_MULTICHAN_EDIT,8,119,"Multichannel edit",config.multiEdit);
        settings_checkbox(CB_CONF_REC_KEYOFF,8,132,"Record key-off notes",config.recRelease);
        settings_checkbox(CB_CONF_FASTTRACKS_USE_LEN,204,41,"FasTracks uses LEN",tapeheadConfig.fastTracksUseTrackLengths);
        settings_checkbox(CB_CONF_KILL_NOTES_AT_STOP,204,54,"Kill voices at stop",config.killNotesOnStopPlay);
        settings_checkbox(CB_CONF_PATT_CUT_TO_BUF,204,67,"Pattern cut to buffer",config.ptnCutToBuffer);
        settings_checkbox(CB_CONF_CHANGE_PATTLEN_INS_DEL,204,80,"Insert/delete resizes",config.recTrueInsert);
        settings_checkbox(CB_CONF_QUANTIZATION,204,93,"Record quantization",config.recQuant);
        char q[32];snprintf(q,sizeof(q),"Quantize: 1/%d",config.recQuantRes);textOut(220,111,PAL_FORGRND,q);
        settings_button(PB_CONFIG_QUANTIZE_DOWN,341,106,20,"-",preferences_quant_down);
        settings_button(PB_CONFIG_QUANTIZE_UP,363,106,20,"+",preferences_quant_up);
        textOutTiny(204,128,"APG USES INP: RECORD SONG BECOMES REC+",video.palette[PAL_FORGRND]);
        textOutTiny(204,139,"SILENT RECORD: CTRL+GRAVE",video.palette[PAL_FORGRND]);
    } else {
        settings_checkbox(CB_CONF_PATTSTRETCH,8,41,"Pattern stretch",config.ptnStretch);
        settings_checkbox(CB_CONF_HEXCOUNT,8,54,"Hex row numbers",config.ptnHex);
        settings_checkbox(CB_CONF_ACCIDENTAL,8,67,"Accidentals (# / b)",config.ptnAcc);
        settings_checkbox(CB_CONF_SHOWZEROES,8,80,"Show instrument zeroes",config.ptnInstrZero);
        settings_checkbox(CB_CONF_FRAMEWORK,8,93,"Pattern framework",config.ptnFrmWrk);
        settings_checkbox(CB_CONF_LINECOLORS,8,106,"Row number colors",config.ptnLineLight);
        settings_checkbox(CB_CONF_CHANNUMS,8,119,"Channel numbering",config.ptnChnNumbers);
        settings_checkbox(CB_CONF_SHOW_VOLCOL,204,41,"Show volume column",config.ptnShowVolColumn);
        settings_checkbox(CB_CONF_ORIG_FT2_PATT_LAYOUT,204,54,"Original blank fields",!config.ptnAlternativeLayout);
        static const char *fonts[]={"Capitals","Lowercase","Future","Bold"};
        textOut(220,76,PAL_FORGRND,"Pattern font:");
        settings_button(PB_CONFIG_QUANTIZE_UP,220,88,158,fonts[config.ptnFont],preferences_font_button);
        textOutTiny(204,115,"PALETTE LIST: WHEEL TO MORE COLORS",video.palette[PAL_FORGRND]);
        textOutTiny(204,128,"EDIT PRESETS TO MAKE YOUR OWN",video.palette[PAL_FORGRND]);
    }
    /* The palette editor is available on both pages. Its own events are gated
       on configScreenShown; its list coordinates remain upstream's. */
    showPaletteEditor();
    textOutClipX(400,157,PAL_FORGRND,preferences_message,628);
}
