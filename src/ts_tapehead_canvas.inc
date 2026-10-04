/* Compact host canvas. Stable tile identity, not bank position, selects an instrument. */
#include "ts_sister_portal_mask.inc"
static int bind_tiles(TsSamplePages *,const TsInstrument *,char *,size_t);
static void tracker_logo(void) {
    drawFramework(112,0,154,32,FRAMEWORK_TYPE1);
    for(int y=0;y<28;++y)for(int x=0;x<42;++x) {
        size_t bit=(size_t)y*42+x;
        /* Keep the Sister Machine silhouette; replace the wave at her hands with tracker steps. */
        if(x<25 && (ts_sister_portal_mask[bit>>3]&(0x80u>>(bit&7))))
            video.frameBuffer[(y+2)*SCREEN_W+x+116]=video.palette[PAL_MOUSEPT];
    }
    const int colors[]={PAL_PATTERN_INSTRUMENT,PAL_PATTERN_NOTE,PAL_PATTERN_EFFECT};
    for(int lane=0;lane<3;++lane)for(int x=0;x<19;++x) {
        int y=12+lane*4-(x/5)%2;
        video.frameBuffer[y*SCREEN_W+139+x]=video.palette[colors[lane]];
        if(x%5==0)video.frameBuffer[(y-1)*SCREEN_W+139+x]=video.palette[colors[lane]];
    }
    textOut(161,5,PAL_FORGRND,"TrackSister");
    textOutTiny(162,21,fastTracksPOCMasterIsEnabled()?"FASTRACKS ON":"FASTRACKS OFF",video.palette[PAL_FORGRND]);
}
static const TsInstrument *canvas_bank(void) {
    return embed.pages?ts_sample_pages_page(embed.pages,embed.active,embed.canvas_page):NULL;
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
    else if(delta>0 && page+1<embed.pages->page_count)++page;
    if(page!=embed.canvas_page) {
        embed.canvas_page=page;embed.canvas_message[0]=0;
        bind_tiles(embed.pages,embed.active,embed.canvas_message,sizeof(embed.canvas_message));
    }
}
static void canvas_click(int x,int y) {
    if(y<20) {if(x>=602)canvas_page_step(1);else if(x>=578)canvas_page_step(-1);return;}
    if(x<427 || x>=627 || y<23 || y>=127)return;
    int slot=((y-23)/26)*4+(x-427)/50;
    const TsInstrument *bank=canvas_bank();
    if(bank && bank->bank[slot].occupied) {
        int alias=canvas_alias(bank->bank[slot].tile_id);
        if(alias) {embed.canvas_message[0]=0;editor.curInstr=alias;editor.curSmp=0;ui.updatePosSections=true;}
        else snprintf(embed.canvas_message,sizeof(embed.canvas_message),"128 INSTRUMENTS USED - TILE UNAVAILABLE");
    }
}
static void canvas_draw(void) {
    for(int id=PB_RANGE1;id<=PB_SAMPLE_LIST_DOWN;++id)hidePushButton(id);
    for(int id=TB_INST1;id<=TB_SAMP5;++id)hideTextBox(id);
    hideScrollBar(SB_SAMPLE_LIST);
    drawFramework(424,0,208,173,FRAMEWORK_TYPE1);
    char line[80];
    snprintf(line,sizeof(line),"CANVAS  %u OF %u",(unsigned)embed.canvas_page+1,
             embed.pages?(unsigned)embed.pages->page_count:1);
    textOutTiny(429,8,line,video.palette[PAL_FORGRND]);
    drawFramework(578,3,22,16,FRAMEWORK_TYPE1);textOut(585,7,PAL_FORGRND,"<");
    drawFramework(602,3,25,16,FRAMEWORK_TYPE1);textOut(610,7,PAL_FORGRND,">");
    const TsInstrument *bank=canvas_bank();
    for(int slot=0;slot<16;++slot) {
        int x=427+(slot%4)*50,y=23+(slot/4)*26;
        const TsBankSlot *tile=bank?&bank->bank[slot]:NULL;
        int alias=tile && tile->occupied?canvas_alias(tile->tile_id):0;
        int selected=alias && alias==editor.curInstr;
        fillRect(x,y,49,24,selected?PAL_MOUSEPT:PAL_DSKTOP2);
        fillRect(x+1,y+1,47,22,PAL_DESKTOP);
        const TsSample *sample=tile && tile->occupied?
            (slot==bank->selected_slot && bank->current.data?&bank->current:&tile->sample):NULL;
        snprintf(line,sizeof(line),"%02X %.6s",slot+1,sample?(sample->name[0]?sample->name:"TILE"):"-");
        textOutTiny(x+3,y+3,line,video.palette[selected?PAL_MOUSEPT:PAL_FORGRND]);
        if(sample && sample->data && sample->frames && sample->channels) {
            /* Bounded preview work: eight probes per waveform column. */
            for(int px=0;px<43;++px) {
                float peak=0;
                for(int probe=0;probe<8;++probe) {
                    size_t f=((size_t)(px*8+probe)*sample->frames)/(43*8);
                    for(unsigned c=0;c<sample->channels;++c)peak=fmaxf(peak,fabsf(sample->data[f*sample->channels+c]));
                }
                int h=(int)(fminf(1,peak)*5);
                fillRect(x+3+px,y+16-h,1,h*2+1,selected?PAL_PATTERN_NOTE:PAL_PATTERN_EFFECT);
            }
        }
    }
    int a=editor.curInstr;sample_t *sample=a && instr[a]?&instr[a]->smp[0]:NULL;
    snprintf(line,sizeof(line),"INST %02X  %.27s",a,a?song.instrName[a]:"SELECT A TILE");
    textOutTiny(429,132,line,video.palette[PAL_FORGRND]);
    if(embed.canvas_message[0]) {
        snprintf(line,sizeof(line),"%.47s",embed.canvas_message);
        textOutTiny(429,143,line,video.palette[PAL_FORGRND]);
    } else if(sample && sample->tileData) {
        TsTileLocation loc=embed.tile_location[a];
        snprintf(line,sizeof(line),"P%u S%02u  %uHZ %uCH %uMS",(unsigned)loc.page+1,
                 (unsigned)loc.slot+1,embed.tile_rate[a],sample->tileChannels,
                 embed.tile_rate[a]?(unsigned)((uint64_t)sample->length*1000/embed.tile_rate[a]):0);
        textOutTiny(429,143,line,video.palette[PAL_FORGRND]);
    }
    textOutTiny(430,159,"SONG",video.palette[PAL_FORGRND]);
    drawFramework(450,155,179,16,FRAMEWORK_TYPE2);drawTextBox(TB_SONG_NAME);
}
