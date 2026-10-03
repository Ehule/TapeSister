/* Native pattern editor. Running definitions and editor focus are independent. */
#include "ts_tracker_fonts.inc"
/* Adapted from Tapehead's pattCharOut/textOutTiny. Packed rows replace its
   global bitmap loader; rect provides framebuffer clipping at every edge. */
static void tracker_glyph(TsFramebuffer *fb,int x,int y,const uint8_t *rows,int w,int h,uint32_t color)
{
    for(int row=0;row<h;++row)for(int bit=0;bit<w;++bit)
        if(rows[row]&(1u<<bit))rect(fb,x+bit,y+row,1,1,color);
}
static int tracker_text_width(const char *value)
{
    int width=0;while(*value)width+=tracker_font1_widths[(unsigned char)*value++&127];
    return width?width-1:0;
}
static void tracker_text(TsFramebuffer *fb,int x,int y,const char *value,uint32_t color)
{
    while(*value) {unsigned ch=(unsigned char)*value++&127;
        tracker_glyph(fb,x,y,tracker_font1[ch],8,10,color);x+=tracker_font1_widths[ch];}
}
static void tracker_tiny(TsFramebuffer *fb,int x,int y,const char *value,uint32_t color)
{
    while(*value) {
        unsigned ch=(unsigned char)*value++;int glyph=-1;
        if(ch>='0' && ch<='9')glyph=ch-'0';
        else if(ch>='A' && ch<='Z')glyph=ch-'A'+10;
        else if(ch>='a' && ch<='z')glyph=ch-'a'+10;
        else if(ch=='-')glyph=36;
        else if(ch=='+')glyph=37;
        if(glyph>=0)tracker_glyph(fb,x,y,tracker_font3[glyph],4,7,color);
        else if(ch=='.')rect(fb,x+1,y+5,1,1,color);
        else if(ch==':'){rect(fb,x+1,y+2,1,1,color);rect(fb,x+1,y+5,1,1,color);}
        else if(ch=='/')for(int i=0;i<4;++i)rect(fb,x+3-i,y+i+1,1,1,color);
        else if(ch=='|')rect(fb,x+1,y,1,7,color);
        else if(ch=='?'){rect(fb,x,y,3,1,color);rect(fb,x+2,y+1,1,2,color);rect(fb,x+1,y+3,1,1,color);rect(fb,x+1,y+5,1,1,color);}
        x+=4;
    }
}
static void tracker_button(TsFramebuffer *fb,int x,int y,int w,const char *label,int active,int compact)
{
    uint32_t light=contrast_color(PAL_BUTTON,active_palette()->buttons_contrast,1.5f);
    uint32_t dark=contrast_color(PAL_BUTTON,active_palette()->buttons_contrast,.5f);
    int h=compact?16:24;
    bevel_frame(fb,x,y,w,h,active?PAL_BLOCK:PAL_BUTTON,light,dark);
    int width=tracker_text_width(label);
    if(width<=w-6)tracker_text(fb,x+(w-width)/2,y+(h-10)/2,label,active?PAL_BLOCK_TEXT:RGB(0,0,0));
    else tracker_tiny(fb,x+(w-(int)strlen(label)*4)/2,y+(h-7)/2,label,active?PAL_BLOCK_TEXT:RGB(0,0,0));
}
static void tracker_switch(TsFramebuffer *fb,int x,int y,const char *label,int active)
{
    rect(fb,x,y,10,10,active?PAL_BLOCK:PAL_DESKTOP);
    tracker_tiny(fb,x+3,y+1,label,active?PAL_BLOCK_TEXT:PAL_TEXT);
    rect(fb,x,y+9,10,1,active?PAL_BLOCK_TEXT:PAL_BUTTON);
}
static unsigned tracker_alias(const TsSisterTracker *t, TsTileId id)
{
    if (id) for (unsigned i=1;i<TS_TRACKER_ALIASES;++i) if(t->aliases[i]==id)return i;
    return 0;
}
static void tracker_render(TsFramebuffer *fb,const TsUiState *ui,const TsInstrument *instrument)
{
    const TsSisterTracker *t=ui->tracker;
    TsTrackerLayout g=ts_tracker_layout(t,ui->tracker_expanded);
    char line[180];
    const TsTrackerPattern *p=ts_sister_tracker_pattern_const(t,t->editor_pattern);
    clear(fb,PAL_DESKTOP);
    if(!g.expanded) {
    frame(fb,2,2,162,28,PAL_DESKTOP,0);
    tracker_text(fb,9,10,"SisterTracker",PAL_NOTE);
    tracker_button(fb,170,4,42,"PLAY",ui->tracker_running==1,0);
    tracker_button(fb,216,4,50,"PAUSE",ui->tracker_running==2,0);
    tracker_button(fb,270,4,42,"STOP",!ui->tracker_running,0);
    tracker_button(fb,316,4,42,"BACK",0,0);tracker_button(fb,362,4,44,"SAVE",0,0);
    tracker_button(fb,410,4,56,"FOLLOW",t->follow && !ui->tracker_follow_hold,0);
    tracker_button(fb,466,4,24,"FX",0,0);
    tracker_button(fb,494,5,28,"EQ",ui->master_eq_open,0);
    master_output_fader(fb,ui->master_output.gain,528,5);
    master_output_meter(fb,&ui->master_output,580,8);
    tracker_button(fb,10,34,20,"<",0,1);tracker_button(fb,144,34,20,">",0,1);
    snprintf(line,sizeof(line),"PATTERN %03u",p?p->id:0);text(fb,36,39,line,PAL_TEXT,1);
    tracker_button(fb,168,34,36,"NEW",0,1);
    snprintf(line,sizeof(line),"BPM %03u",t->bpm);tracker_button(fb,212,34,78,line,0,1);
    snprintf(line,sizeof(line),"TPL %02u",t->ticks_per_line);tracker_button(fb,294,34,56,line,0,1);
    snprintf(line,sizeof(line),"ROWS %03u",p?p->rows:0);tracker_button(fb,354,34,72,line,0,1);
    tracker_button(fb,430,34,56,"LOOP",t->loop,1);
    snprintf(line,sizeof(line),"HEARD %03u %03u",ui->tracker_heard_pattern,ui->tracker_master_row);
    text(fb,494,39,line,ui->tracker_running?PAL_NOTE:PAL_TEXT,1);
    }
    tracker_tiny(fb,5,g.len_y,t->length_bypass?"OFF":"LEN",t->length_bypass?PAL_TEXT:PAL_NOTE);
    tracker_tiny(fb,5,g.ratio_y,t->fasttracks_uses_length?"FTL":"PAT",PAL_TEXT);
    for(int lane=0;lane<TS_TRACKER_LANES;++lane) {
        int x=TS_TRACKER_LANE_X+lane*TS_TRACKER_LANE_WIDTH;
        const TsTrackerLane *definition=&t->lanes[lane];
        frame(fb,x-1,g.len_y-2,TS_TRACKER_LANE_WIDTH,g.bottom-g.len_y+4,RGB(0,0,0),0);
        rect(fb,x+1,g.len_y-1,TS_TRACKER_LANE_WIDTH-3,g.grid_y-g.len_y+1,PAL_DESKTOP);
        if(g.name_y>=0 && ts_tracker_custom_name(definition,lane)) {
            snprintf(line,sizeof(line),"%.16s",definition->name);tracker_tiny(fb,x+2,g.name_y,line,PAL_MOUSE);
        }
        tracker_switch(fb,x+2,g.mix_y,"M",definition->muted);
        tracker_switch(fb,x+14,g.mix_y,"S",ui->tracker_solo&(1u<<lane));
        if(ui->tracker_running && ui->tracker_heard_pattern==(p?p->id:0) &&
           (!ui->tracker_block || (lane>=(int)(ui->tracker_block_lanes&255) && lane<=(int)(ui->tracker_block_lanes>>8)))) {
            unsigned head=ui->tracker_lane_row[lane];
            if(head<TS_TRACKER_ROWS)snprintf(line,sizeof(line),"%02X",head);
            else snprintf(line,sizeof(line),"--");
            tracker_tiny(fb,x+31,g.mix_y+1,line,PAL_TEXT);
        }
        snprintf(line,sizeof(line),"%.1f",definition->trim);tracker_tiny(fb,x+49,g.mix_y+1,line,PAL_VOLUME);
    }
    TsTrackerRegion selection=ts_tracker_edit_region(ui->tracker_edit,t);
    ts_tracker_tapehead_render(fb,ui);
    const TsTrackerCell *c=p?&p->cells[t->editor_row][t->editor_lane]:NULL;
    snprintf(line,sizeof(line),"ROW %03u LANE %u  %s  TILE %02X  %s",t->editor_row,t->editor_lane+1,
        (const char*[]){"NOTE","TILE","VOLUME","TUNE COMMAND","TUNE VALUE","FX COMMAND","FX VALUE"}[ui->tracker_field],c?tracker_alias(t,c->tile_id):0,
        ui->tracker_missing?"MISSING TILE IN PLAYBACK":t->lanes[t->editor_lane].mode==TS_TRACKER_SONG?
            "SONG SAVED ONLY: PLAYS STANDARD":"LEN / PATTERN FT ACTIVE; TUNE / FX STORED ONLY");
    if(ui->tracker_hover_x>=TS_TRACKER_LANE_X && ui->tracker_hover_x<TS_TRACKER_LANE_RIGHT &&
       ui->tracker_hover_y>=g.len_y && ui->tracker_hover_y<g.grid_y) {
        int lane=(ui->tracker_hover_x-TS_TRACKER_LANE_X)/TS_TRACKER_LANE_WIDTH;
        if(ui->tracker_hover_y>=g.mix_y && ui->tracker_hover_y<g.mix_y+10)
            snprintf(line,sizeof(line),"LANE %d %.31s - M MUTE / S SOLO / RIGHT NUMBER: TRIM 0-2 - CLICK OR WHEEL",lane+1,t->lanes[lane].name);
        else if(ui->tracker_hover_y<g.ratio_y-1)
            snprintf(line,sizeof(line),"LEN: CLICK +/- OR WHEEL / SHIFT 8 / CTRL OFF / EJECT SYMBOL SETS CONTROL");
        else snprintf(line,sizeof(line),"FT: ENABLE / RATIO: CLICK-WHEEL / R-B: DIRECTION / LED BANK: DISABLE / RIGHT CLICK: DIRECTION");
    } else if(ui->tracker_hover_x>=4 && ui->tracker_hover_x<TS_TRACKER_LANE_X-2 &&
              ui->tracker_hover_y>=g.len_y-1 && ui->tracker_hover_y<g.ratio_y+9) {
        snprintf(line,sizeof(line),ui->tracker_hover_y<g.ratio_y-1?
            "LEN: BYPASS / RESTORE ALL LEN AND CONTROL - SAVED LENGTHS RETAINED":
            "FTL: FASTTRACKS USE LANE LENGTHS / PAT: FASTTRACKS USE PHYSICAL PATTERN ROWS");
    } else if(ui->tracker_hover_y>=g.grid_y && ui->tracker_hover_y<g.bottom &&
              ui->tracker_hover_x>=TS_TRACKER_LANE_X && ui->tracker_hover_x<TS_TRACKER_LANE_RIGHT) {
        static const char *help[]={"NOTE: QWERTY ENTRY / OFF OR CUT", "TILE: TWO HEX DIGITS / 00 INHERITS", "VOLUME: 00-40 HEX / EMPTY INHERITS",
            "TUNING COMMAND: M ROOT / N FINE - STORED ONLY", "TUNING VALUE: TWO HEX DIGITS - STORED ONLY",
            "FX COMMAND: 0-9 A-Z - STORED ONLY", "FX VALUE: TWO HEX DIGITS - STORED ONLY"};
        int f=ts_tracker_field_at((ui->tracker_hover_x-TS_TRACKER_LANE_X)%TS_TRACKER_LANE_WIDTH);
        snprintf(line,sizeof(line),"ROW %03d LANE %d %s",ts_tracker_view_row(ui,p,(ui->tracker_hover_x-TS_TRACKER_LANE_X)/TS_TRACKER_LANE_WIDTH,ui->tracker_hover_y),(ui->tracker_hover_x-TS_TRACKER_LANE_X)/TS_TRACKER_LANE_WIDTH+1,help[f]);
    }
    if(g.expanded && (ui->file_record_state==TS_PERFORMANCE_FILE_RECORDING || ui->file_record_state==TS_PERFORMANCE_FILE_STOPPING))
        snprintf(line,sizeof(line),"FILE CAPTURE %.1F S / F7 STOP AT SEAM / CTRL SHIFT F STOP NOW / CTRL ALT BACKSPACE VIEW",
            ui->file_record_rate?(double)ui->file_record_frames/ui->file_record_rate:0.0);
    tracker_tiny(fb,10,g.help_y,line,ui->tracker_missing?PAL_VOLUME:PAL_TEXT);
    if(!g.expanded) {
    tracker_button(fb,10,332,74,"USE TILE",0,0);tracker_button(fb,88,332,24,"<",0,0);
    tracker_button(fb,116,332,24,">",0,0);tracker_button(fb,146,332,32,"OFF",0,0);
    tracker_button(fb,182,332,32,"CUT",0,0);tracker_button(fb,218,332,48,"CLEAR",0,0);
    snprintf(line,sizeof(line),"STEP %02u",t->edit_step);tracker_button(fb,270,332,66,line,0,0);
    snprintf(line,sizeof(line),"OCT %d",ui->keyboard_base_note/12-1);tracker_button(fb,340,332,72,line,0,0);
    static const char *field_labels[]={"NTE","TI","VL","M/N","FX"};
    static const int field_ids[]={0,1,2,3,5};
    for(int group=0;group<5;++group)tracker_button(fb,418+group*32,332,28,field_labels[group],
        ui->tracker_field==field_ids[group] || (group>=3 && ui->tracker_field==field_ids[group]+1),0);
    tracker_button(fb,578,332,52,"EDIT",ui->tracker_edit && ui->tracker_edit->tools_open,0);
    TsTileId inherited=0;
    if(p)for(int row=0;row<=t->editor_row;++row)if(p->cells[row][t->editor_lane].tile_id)inherited=p->cells[row][t->editor_lane].tile_id;
    snprintf(line,sizeof(line),"CELL TILE %02X %s | USE TILE: SAMPLE %02X %.30s",tracker_alias(t,inherited),
        c && c->tile_id?"EXPLICIT":"INHERITED",instrument->selected_slot+1,
        instrument->selected_slot>=0?instrument->current.name:"SELECT A SAMPLE FIRST");
    text(fb,10,361,line,PAL_INSTRUMENT,1);
    text(fb,10,373,"CTRL L LOOP / F7 PERF / F8 EXTRACT-CAPTURE / CTRL ALT BACKSPACE EXPAND / TAB FX",PAL_TEXT,1);
    text(fb,10,387,ui->status,PAL_TEXT,1);
    }
    if(ui->tracker_edit && ui->tracker_edit->tools_open) {
        const TsTrackerEdit *e=ui->tracker_edit;
        static const char *labels[]={"COPY","CUT","PASTE","MIX PASTE","UNDO","REDO",
            "INSERT ROW","DELETE ROW","NOTE -1","NOTE +1","NOTE -12","NOTE +12",
            "VOL RAMP","FILL DOWN","REVERSE","REPEAT BLOCK","SELECT LANE","SELECT ALL","CLONE PAT","EXTRACT F8","EXPAND X2","SHRINK /2"};
        static const char *masks[]={"NOTE","TILE","VOLUME","TUNE","FX"};
        rect(fb,18,106,604,216,RGB(9,11,17));rect(fb,18,106,604,2,RGB(232,208,255));
        text(fb,28,116,"PATTERN EDITING",PAL_NOTE,1);tracker_button(fb,536,111,76,"CLOSE",0,1);
        for(int i=0;i<22;++i)tracker_button(fb,28+(i%6)*98,138+(i/6)*22,94,labels[i],0,1);
        text(fb,28,231,"FIELDS FOR CLIPBOARD / CLEAR BLOCK / FILL / REVERSE:",PAL_TEXT,1);
        for(int i=0;i<5;++i)tracker_button(fb,28+i*98,245,94,masks[i],!!(e->mask&(1u<<i)),1);
        snprintf(line,sizeof(line),"BLOCK ROWS %03d-%03d LANES %d-%d | CLIPBOARD %u X %u",selection.row0,selection.row1,
            selection.lane0+1,selection.lane1+1,e->clipboard_rows,e->clipboard_lanes);text(fb,28,272,line,PAL_TEXT,1);
        text(fb,28,286,"SHIFT/CTRL/ALT + F3 F4 F5: CUT COPY PASTE LANE / PATTERN / BLOCK",PAL_TEXT,1);
        text(fb,28,299,"CTRL+SHIFT+V VOL RAMP / SHIFT+INSERT OR BACKSPACE: ALL LANES",PAL_TEXT,1);
        text(fb,28,312,"TRANSPOSE: CTRL+UP/DOWN (+SHIFT FOR OCTAVE) / ESC CLOSE",PAL_TEXT,1);
    }
}
