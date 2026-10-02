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
static void tracker_eject(TsFramebuffer *fb,int x,int y,uint32_t color)
{
    /* Tapehead's seven-pixel CONTROL symbol. */
    rect(fb,x+3,y,1,1,color);rect(fb,x+2,y+1,3,1,color);
    rect(fb,x+1,y+2,5,1,color);rect(fb,x,y+4,7,2,color);
}
static void tracker_note_draw(TsFramebuffer *fb,int x,int y,const TsTrackerCell *cell,uint32_t color)
{
    static const uint8_t letters[12]={12,12,13,13,14,15,15,16,16,10,10,11};
    if(cell->note_kind==TS_TRACKER_NOTE_PITCH) {
        tracker_glyph(fb,x,y,tracker_font4[letters[cell->note%12]],8,8,color);
        int sharp=cell->note%12==1 || cell->note%12==3 || cell->note%12==6 || cell->note%12==8 || cell->note%12==10;
        tracker_glyph(fb,x+8,y,tracker_font4[sharp?37:36],8,8,color);
        int octave=cell->note/12-1;
        if(octave<0)tracker_tiny(fb,x+16,y,"-1",color);
        else tracker_glyph(fb,x+16,y,tracker_font4[octave],8,8,color);
    } else if(cell->note_kind==TS_TRACKER_NOTE_OFF) {
        for(int i=0;i<3;++i)tracker_glyph(fb,x+i*8,y,tracker_font4[40+i],8,8,color);
    } else if(cell->note_kind==TS_TRACKER_NOTE_CUT)tracker_tiny(fb,x+6,y,"CUT",color);
    else for(int i=0;i<3;++i)rect(fb,x+3+i*8,y+3,1,1,color);
}
static unsigned tracker_alias(const TsSisterTracker *t, TsTileId id)
{
    if (id) for (unsigned i=1;i<TS_TRACKER_ALIASES;++i) if(t->aliases[i]==id)return i;
    return 0;
}
static void tracker_render(TsFramebuffer *fb,const TsUiState *ui,const TsInstrument *instrument)
{
    const TsSisterTracker *t=ui->tracker;
    const TsTrackerPattern *p=ts_sister_tracker_pattern_const(t,t->editor_pattern);
    clear(fb,PAL_DESKTOP);
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
    char line[128];
    tracker_button(fb,10,34,20,"<",0,1);tracker_button(fb,144,34,20,">",0,1);
    snprintf(line,sizeof(line),"PATTERN %03u",p?p->id:0);text(fb,36,39,line,PAL_TEXT,1);
    tracker_button(fb,168,34,36,"NEW",0,1);
    snprintf(line,sizeof(line),"BPM %03u",t->bpm);tracker_button(fb,212,34,78,line,0,1);
    snprintf(line,sizeof(line),"TPL %02u",t->ticks_per_line);tracker_button(fb,294,34,56,line,0,1);
    snprintf(line,sizeof(line),"ROWS %03u",p?p->rows:0);tracker_button(fb,354,34,72,line,0,1);
    tracker_button(fb,430,34,56,"LOOP",t->loop,1);
    snprintf(line,sizeof(line),"HEARD %03u %03u",ui->tracker_heard_pattern,ui->tracker_row);
    text(fb,494,39,line,ui->tracker_running?PAL_NOTE:PAL_TEXT,1);
    for(int lane=0;lane<TS_TRACKER_LANES;++lane) {
        int x=TS_TRACKER_LANE_X+lane*TS_TRACKER_LANE_WIDTH;
        /* Tapehead's separate LEN and Fast Tracks strips. These show saved
           definitions; this stage's playback still uses the Standard clock. */
        static const char *ratios[TS_TRACKER_RATIOS]={"1:2","2:3","3:4","4:5","5:6","7:8","15:16",
            "1:1","17:16","8:7","6:5","5:4","4:3","3:2","2:1","3:1","5:1"};
        const TsTrackerLane *definition=&t->lanes[lane];
        uint32_t header_color=palette_blend(PAL_DESKTOP,PAL_BLOCK_TEXT,75);
        frame(fb,x-1,TS_TRACKER_LEN_Y-2,TS_TRACKER_LANE_WIDTH,TS_TRACKER_GRID_BOTTOM-TS_TRACKER_LEN_Y+4,RGB(0,0,0),0);
        rect(fb,x+1,TS_TRACKER_LEN_Y-1,TS_TRACKER_LANE_WIDTH-3,TS_TRACKER_GRID_Y-TS_TRACKER_LEN_Y+1,PAL_DESKTOP);
        snprintf(line,sizeof(line),"%d",lane+1);tracker_tiny(fb,x+2,TS_TRACKER_LEN_Y,line,PAL_MOUSE);
        if(definition->length)snprintf(line,sizeof(line),"LEN%u",definition->length);
        else snprintf(line,sizeof(line),"LEN OFF");
        tracker_tiny(fb,x+10,TS_TRACKER_LEN_Y,line,header_color);
        tracker_eject(fb,x+TS_TRACKER_LANE_WIDTH-11,TS_TRACKER_LEN_Y,
            t->control_lane==lane?PAL_BLOCK_TEXT:header_color);
        snprintf(line,sizeof(line),"FT %s %c",ratios[definition->ratio<TS_TRACKER_RATIOS?definition->ratio:7],
            definition->mode==TS_TRACKER_PATTERN?'P':definition->mode==TS_TRACKER_SONG?'S':'-');
        tracker_tiny(fb,x+2,TS_TRACKER_RATIO_Y,line,header_color);
        snprintf(line,sizeof(line),"%.15s",definition->name);tracker_tiny(fb,x+2,TS_TRACKER_NAME_Y,line,PAL_MOUSE);
        tracker_button(fb,x+2,TS_TRACKER_MIX_Y,20,"M",definition->muted,1);
        tracker_button(fb,x+24,TS_TRACKER_MIX_Y,20,"S",ui->tracker_solo&(1u<<lane),1);
        snprintf(line,sizeof(line),"%.1f",definition->trim);tracker_tiny(fb,x+49,TS_TRACKER_MIX_Y+5,line,PAL_VOLUME);
        tracker_tiny(fb,x+2,TS_TRACKER_FIELDS_Y,"NOTE",PAL_NOTE);
        tracker_tiny(fb,x+27,TS_TRACKER_FIELDS_Y,"TI",PAL_INSTRUMENT);
        tracker_tiny(fb,x+36,TS_TRACKER_FIELDS_Y,"VL",PAL_VOLUME);
        tracker_tiny(fb,x+45,TS_TRACKER_FIELDS_Y,"M/N",PAL_TUNING);
        tracker_tiny(fb,x+58,TS_TRACKER_FIELDS_Y,"FX",PAL_EFFECT);
    }
    TsTrackerRegion selection=ts_tracker_edit_region(ui->tracker_edit,t);
    if(p)for(int visible=0;visible<TS_TRACKER_VISIBLE_ROWS;++visible) {
        int row=ui->tracker_scroll+visible,y=TS_TRACKER_GRID_Y+visible*TS_TRACKER_ROW_HEIGHT;
        if(row>=p->rows)break;
        int heard=ui->tracker_running && ui->tracker_heard_pattern==p->id && ui->tracker_row==(unsigned)row;
        int current=row==t->editor_row;
        if(current)rect(fb,4,y,632,12,PAL_DESKTOP);
        if(heard) {
            rect(fb,TS_TRACKER_LANE_X,y,TS_TRACKER_LANE_RIGHT-TS_TRACKER_LANE_X,1,PAL_NOTE);
            rect(fb,TS_TRACKER_LANE_X,y+11,TS_TRACKER_LANE_RIGHT-TS_TRACKER_LANE_X,1,PAL_NOTE);
        }
        snprintf(line,sizeof(line),"%02X",row);
        uint32_t row_color=current?RGB(255,255,255):PAL_TEXT;
        for(int side=0;side<2;++side)for(int digit=0;digit<2;++digit)
            tracker_glyph(fb,(side?TS_TRACKER_LANE_RIGHT+7:9)+digit*8,y+2,
                tracker_font4[row>>(digit?0:4)&15],8,8,row_color);
        for(int lane=0;lane<TS_TRACKER_LANES;++lane) {
            const TsTrackerCell *c=&p->cells[row][lane];int x=TS_TRACKER_LANE_X+lane*TS_TRACKER_LANE_WIDTH;
            if(ui->tracker_edit && ui->tracker_edit->selected && row>=selection.row0 && row<=selection.row1 &&
               lane>=selection.lane0 && lane<=selection.lane1)rect(fb,x+1,y,TS_TRACKER_LANE_WIDTH-3,12,PAL_BLOCK);
            if(row==t->editor_row && lane==t->editor_lane) {
                int start=x+ts_tracker_field_offset(ui->tracker_field),width=ts_tracker_field_width(ui->tracker_field);
                uint32_t focus=ui->tracker_hex_digit?RGB(255,224,32):RGB(40,224,255);
                rect(fb,start-1,y,width+1,12,palette_blend(PAL_DESKTOP,focus,20));
                rect(fb,start-1,y,1,12,focus);rect(fb,start+width-1,y,1,12,focus);
                rect(fb,start-1,y,width+1,1,focus);
                if(ui->tracker_cursor_visible) {
                    int caret=start+(ui->tracker_field?ui->tracker_hex_digit*4:0);
                    rect(fb,caret,y+10,ui->tracker_field==0?23:3,2,RGB(255,224,32));
                }
            }
            char tile[8],volume[8],tune[8],fx[8];
            unsigned alias=tracker_alias(t,c->tile_id);
            snprintf(tile,sizeof(tile),c->tile_id?(alias?"%02X":"??"):"..",alias);
            snprintf(volume,sizeof(volume),c->has_volume?"%02X":"..",c->volume);
            snprintf(tune,sizeof(tune),c->tune_command?"%c%02X":"...",c->tune_command,c->tune_value);
            snprintf(fx,sizeof(fx),c->fx_command?"%c%02X":"...",c->fx_command,c->fx_value);
            if(row==t->editor_row && lane==t->editor_lane && ui->tracker_hex_digit) {
                if(ui->tracker_field==1)snprintf(tile,sizeof(tile),"%X.",((unsigned)ui->tracker_hex_value>>4)&15u);
                if(ui->tracker_field==2)snprintf(volume,sizeof(volume),"%X.",((unsigned)ui->tracker_hex_value>>4)&15u);
                if(ui->tracker_field==4)snprintf(tune+1,sizeof(tune)-1,"%X.",((unsigned)ui->tracker_hex_value>>4)&15u);
                if(ui->tracker_field==6)snprintf(fx+1,sizeof(fx)-1,"%X.",((unsigned)ui->tracker_hex_value>>4)&15u);
            }
            uint32_t empty=active_palette()->colors[TS_PALETTE_PATTERN_EMPTY];
            int selected=ui->tracker_edit && ui->tracker_edit->selected && row>=selection.row0 && row<=selection.row1 && lane>=selection.lane0 && lane<=selection.lane1;
            uint32_t colors[]={c->note_kind?PAL_NOTE:empty,c->tile_id?PAL_INSTRUMENT:empty,c->has_volume?PAL_VOLUME:empty,c->tune_command?PAL_TUNING:empty,c->fx_command?PAL_EFFECT:empty};
            for(int field=0;field<5;++field) {
                if(selected)colors[field]=PAL_BLOCK_TEXT;
                else if(t->lanes[lane].muted)colors[field]=palette_blend(RGB(0,0,0),colors[field],40);
            }
            tracker_note_draw(fb,x+2,y+2,c,colors[0]);
            tracker_tiny(fb,x+27,y+2,tile,colors[1]);tracker_tiny(fb,x+36,y+2,volume,colors[2]);
            tracker_tiny(fb,x+45,y+2,tune,colors[3]);tracker_tiny(fb,x+58,y+2,fx,colors[4]);
        }
    }
    const TsTrackerCell *c=p?&p->cells[t->editor_row][t->editor_lane]:NULL;
    snprintf(line,sizeof(line),"ROW %03u LANE %u  %s  TILE %02X  %s",t->editor_row,t->editor_lane+1,
        (const char*[]){"NOTE","TILE","VOLUME","TUNE COMMAND","TUNE VALUE","FX COMMAND","FX VALUE"}[ui->tracker_field],c?tracker_alias(t,c->tile_id):0,
        ui->tracker_missing?"MISSING TILE IN PLAYBACK":"STANDARD; LEN / FT / COMMAND AUDIO INACTIVE");
    tracker_tiny(fb,10,317,line,ui->tracker_missing?PAL_VOLUME:PAL_TEXT);
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
    text(fb,10,373,"SHIFT+ARROWS SELECT / CTRL C X V / CTRL Z Y / TAB FX / F9 ROUTER / F10 SAMPLE",PAL_TEXT,1);
    text(fb,10,387,ui->status,PAL_TEXT,1);
    if(ui->tracker_edit && ui->tracker_edit->tools_open) {
        const TsTrackerEdit *e=ui->tracker_edit;
        static const char *labels[]={"COPY","CUT","PASTE","MIX PASTE","UNDO","REDO",
            "INSERT ROW","DELETE ROW","NOTE -1","NOTE +1","NOTE -12","NOTE +12",
            "VOL RAMP","FILL DOWN","REVERSE","REPEAT BLOCK","SELECT LANE","SELECT ALL","CLONE PAT"};
        static const char *masks[]={"NOTE","TILE","VOLUME","TUNE","FX"};
        rect(fb,18,106,604,216,RGB(9,11,17));rect(fb,18,106,604,2,RGB(232,208,255));
        text(fb,28,116,"PATTERN EDITING",PAL_NOTE,1);tracker_button(fb,536,111,76,"CLOSE",0,1);
        for(int i=0;i<19;++i)tracker_button(fb,28+(i%6)*98,138+(i/6)*22,94,labels[i],0,1);
        text(fb,28,231,"FIELDS FOR CLIPBOARD / CLEAR BLOCK / FILL / REVERSE:",PAL_TEXT,1);
        for(int i=0;i<5;++i)tracker_button(fb,28+i*98,245,94,masks[i],!!(e->mask&(1u<<i)),1);
        snprintf(line,sizeof(line),"BLOCK ROWS %03d-%03d LANES %d-%d | CLIPBOARD %u X %u",selection.row0,selection.row1,
            selection.lane0+1,selection.lane1+1,e->clipboard_rows,e->clipboard_lanes);text(fb,28,272,line,PAL_TEXT,1);
        text(fb,28,286,"SHIFT/CTRL/ALT + F3 F4 F5: CUT COPY PASTE LANE / PATTERN / BLOCK",PAL_TEXT,1);
        text(fb,28,299,"CTRL+SHIFT+V VOL RAMP / SHIFT+INSERT OR BACKSPACE: ALL LANES",PAL_TEXT,1);
        text(fb,28,312,"TRANSPOSE: CTRL+UP/DOWN (+SHIFT FOR OCTAVE) / ESC CLOSE",PAL_TEXT,1);
    }
}
