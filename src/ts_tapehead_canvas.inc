/* Compact host canvas. Stable tile identity, not bank position, selects an instrument. */
#include "ts_sister_portal_mask.inc"
static int bind_tiles(TsSamplePages *,const TsInstrument *,char *,size_t);
static void tracker_logo(void) {
    drawFramework(112,0,154,32,FRAMEWORK_TYPE1);
    for(int y=0;y<28;++y)for(int x=0;x<42;++x) {
        size_t bit=(size_t)y*42+x;
        /* Keep the complete figure, including both hands and the right profile. */
        if(ts_sister_portal_mask[bit>>3]&(0x80u>>(bit&7)))
            video.frameBuffer[(y+2)*SCREEN_W+x+116]=video.palette[PAL_MOUSEPT];
    }
    const int colors[]={PAL_PATTERN_INSTRUMENT,PAL_PATTERN_NOTE,PAL_PATTERN_EFFECT};
    for(int lane=0;lane<3;++lane)for(int x=0;x<36;++x) {
        int y=15+lane*2-(x/6)%2;
        video.frameBuffer[y*SCREEN_W+119+x]=video.palette[colors[lane]];
        if(x%6==0)video.frameBuffer[(y-1)*SCREEN_W+119+x]=video.palette[colors[lane]];
    }
    textOut(161,5,PAL_FORGRND,"TrackSister");
    textOutTiny(162,21,fastTracksPOCMasterIsEnabled()?"FASTRACKS ON":"FASTRACKS OFF",video.palette[PAL_FORGRND]);
}
enum { CANVAS_X=425, CANVAS_Y=23, CANVAS_COLUMNS=4, CANVAS_ROWS=6,
       CANVAS_TILES=CANVAS_COLUMNS*CANVAS_ROWS, CANVAS_DX=51, CANVAS_DY=25,
       CANVAS_W=49, CANVAS_H=23 };
static size_t canvas_view_count(void) {
    size_t tiles=embed.pages?embed.pages->page_count*TS_BANK_SLOT_COUNT:0;
    return tiles?(tiles+CANVAS_TILES-1)/CANVAS_TILES:1;
}
/* A view spans host Sample pages, so the extra rows expose actual instruments. */
static const TsInstrument *canvas_bank_at(int cell,int *slot) {
    size_t index=embed.canvas_page*CANVAS_TILES+(size_t)cell;
    *slot=(int)(index%TS_BANK_SLOT_COUNT);
    return embed.pages?ts_sample_pages_page(embed.pages,embed.active,index/TS_BANK_SLOT_COUNT):NULL;
}
static int canvas_alias(TsTileId id) {
    if(!id || !embed.model)return 0;
    for(int a=1;a<=128;++a)if(embed.model->aliases[a]==id)return a;
    return 0;
}
static void canvas_page_step(int delta) {
    if(!embed.pages || !embed.pages->page_count)return;
    size_t page=embed.canvas_page;
    if(delta<0 && page) --page;
    else if(delta>0 && page+1<canvas_view_count())++page;
    if(page!=embed.canvas_page) {
        embed.canvas_page=page;embed.canvas_message[0]=0;
        bind_tiles(embed.pages,embed.active,embed.canvas_message,sizeof(embed.canvas_message));
    }
}
static void canvas_click(int x,int y) {
    if(y>=3 && y<19) {if(x>=603 && x<627)canvas_page_step(1);else if(x>=577 && x<601)canvas_page_step(-1);return;}
    x-=CANVAS_X;y-=CANVAS_Y;
    if(x<0 || x>=CANVAS_COLUMNS*CANVAS_DX || y<0 || y>=CANVAS_ROWS*CANVAS_DY ||
       x%CANVAS_DX>=CANVAS_W || y%CANVAS_DY>=CANVAS_H)return;
    int slot;
    const TsInstrument *bank=canvas_bank_at((y/CANVAS_DY)*CANVAS_COLUMNS+x/CANVAS_DX,&slot);
    if(bank && bank->bank[slot].occupied) {
        int alias=canvas_alias(bank->bank[slot].tile_id);
        if(alias) {embed.canvas_message[0]=0;editor.curInstr=alias;editor.curSmp=0;ui.updatePosSections=true;}
        else snprintf(embed.canvas_message,sizeof(embed.canvas_message),"128 INSTRUMENTS USED");
    }
}
static void canvas_tile_button(int x,int y,int w,int h,int selected,int enabled) {
    fillRect(x,y,w,h,selected?PAL_MOUSEPT:PAL_BCKGRND);
    fillRect(x+1,y+1,w-2,h-2,enabled && !selected?PAL_BUTTONS:PAL_DESKTOP);
    int light=selected?PAL_BUTTON2:PAL_BUTTON1,shade=selected?PAL_BUTTON1:PAL_BUTTON2;
    hLine(x+1,y+1,w-3,light);vLine(x+1,y+2,h-4,light);
    hLine(x+1,y+h-2,w-2,shade);vLine(x+w-2,y+1,h-3,shade);
}
static void canvas_draw(void) {
    for(int id=PB_RANGE1;id<=PB_SAMPLE_LIST_DOWN;++id)hidePushButton(id);
    for(int id=TB_INST1;id<=TB_SAMP5;++id)hideTextBox(id);
    hideTextBox(TB_SONG_NAME);
    hideScrollBar(SB_SAMPLE_LIST);
    drawFramework(421,0,211,173,FRAMEWORK_TYPE1);
    char line[80];
    snprintf(line,sizeof(line),"CANVAS %u OF %u",(unsigned)embed.canvas_page+1,(unsigned)canvas_view_count());
    textOutTiny(426,8,embed.canvas_message[0]?embed.canvas_message:line,video.palette[PAL_FORGRND]);
    int previous=embed.canvas_page>0,next=embed.canvas_page+1<canvas_view_count();
    canvas_tile_button(577,3,24,16,0,previous);textOut(585,7,previous?PAL_BTNTEXT:PAL_BUTTON2,"<");
    canvas_tile_button(603,3,24,16,0,next);textOut(611,7,next?PAL_BTNTEXT:PAL_BUTTON2,">");
    for(int cell=0;cell<CANVAS_TILES;++cell) {
        int slot,x=CANVAS_X+(cell%CANVAS_COLUMNS)*CANVAS_DX,y=CANVAS_Y+(cell/CANVAS_COLUMNS)*CANVAS_DY;
        const TsInstrument *bank=canvas_bank_at(cell,&slot);
        const TsBankSlot *tile=bank?&bank->bank[slot]:NULL;
        int alias=tile && tile->occupied?canvas_alias(tile->tile_id):0;
        int selected=alias && alias==editor.curInstr;
        canvas_tile_button(x,y,CANVAS_W,CANVAS_H,selected,tile && tile->occupied);
        const TsSample *sample=tile && tile->occupied?
            (slot==bank->selected_slot && bank->current.data?&bank->current:&tile->sample):NULL;
        if(sample && alias)snprintf(line,sizeof(line),"%02X %.6s",alias,sample->name[0]?sample->name:"TILE");
        else snprintf(line,sizeof(line),"%s",sample?"FULL":"");
        textOutTiny(x+3,y+4,line,video.palette[selected?PAL_MOUSEPT:PAL_BTNTEXT]);
        if(sample && sample->data && sample->frames && sample->channels) {
            fillRect(x+3,y+13,43,7,PAL_DESKTOP);
            /* Bounded preview work: eight probes per waveform column. */
            for(int px=0;px<43;++px) {
                float peak=0;
                for(int probe=0;probe<8;++probe) {
                    size_t f=((size_t)(px*8+probe)*sample->frames)/(43*8);
                    for(unsigned c=0;c<sample->channels;++c)peak=fmaxf(peak,fabsf(sample->data[f*sample->channels+c]));
                }
                int h=(int)(fminf(1,peak)*3);
                fillRect(x+3+px,y+16-h,1,h*2+1,selected?PAL_PATTERN_NOTE:PAL_PATTERN_EFFECT);
            }
        }
    }
}
