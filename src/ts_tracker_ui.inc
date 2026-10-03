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
/* These are Tapehead's dimPatternColor / breatheColorToward calculations,
   not alpha overlays: inactive notes and empty fields dim by the same rule. */
static uint32_t tracker_dim(uint32_t color)
{
    return RGB((((color>>16)&255)+2*((PAL_DESKTOP>>16)&255))/3,
        (((color>>8)&255)+2*((PAL_DESKTOP>>8)&255))/3,
        ((color&255)+2*(PAL_DESKTOP&255))/3);
}
static uint32_t tracker_breathe(uint32_t color,const TsUiState *ui)
{
    unsigned level=ui->tracker_breathe?ui->tracker_breathe:255,inverse=255-level;
    return RGB((((color>>16)&255)*level+((PAL_DESKTOP>>16)&255)*inverse)/255,
        (((color>>8)&255)*level+((PAL_DESKTOP>>8)&255)*inverse)/255,
        ((color&255)*level+(PAL_DESKTOP&255)*inverse)/255);
}
static void tracker_outline(TsFramebuffer *fb,int x,int y,int w,int h,uint32_t color)
{
    rect(fb,x,y,w,1,color);rect(fb,x,y+h-1,w,1,color);
    rect(fb,x,y,1,h,color);rect(fb,x+w-1,y,1,h,color);
}
static void tracker_phase_led(TsFramebuffer *fb,int x,int y,uint32_t color)
{
    rect(fb,x+1,y,1,3,color);rect(fb,x,y+1,3,1,color);
}
static uint32_t tracker_head_color(const TsSisterTracker *t,int lane,int fast)
{
    TsPaletteColor key=fast?TS_PALETTE_FASTTRACKS_PLAYHEAD:TS_PALETTE_TRACK_LENGTH_PLAYHEAD;
    if(fast && t->lanes[lane].length && t->fasttracks_uses_length)key=TS_PALETTE_FASTTRACKS_LENGTH_PLAYHEAD;
    if(t->control_lane==lane)key=TS_PALETTE_CONTROL_PLAYHEAD;
    uint32_t color=active_palette()->colors[key];
    return t->length_bypass?tracker_dim(color):color;
}
static void tracker_render(TsFramebuffer *fb,const TsUiState *ui,const TsInstrument *instrument)
{
    const TsSisterTracker *t=ui->tracker;
    TsTrackerLayout g=ts_tracker_layout(t,ui->tracker_expanded);
    char line[180];
    const TsTrackerPattern *p=ts_sister_tracker_pattern_const(t,t->editor_pattern);
    TsTrackerView view=ts_tracker_view(ui,p);
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
        /* Tapehead's separate LEN and Fast Tracks strips, with live heads. */
        static const char *ratios[TS_TRACKER_RATIOS]={"1:2","2:3","3:4","4:5","5:6","7:8","15:16",
            "1:1","17:16","8:7","6:5","5:4","4:3","3:2","2:1","3:1","5:1"};
        const TsTrackerLane *definition=&t->lanes[lane];
        uint32_t header_color=tracker_breathe(PAL_BLOCK_TEXT,ui);
        frame(fb,x-1,g.len_y-2,TS_TRACKER_LANE_WIDTH,g.bottom-g.len_y+4,RGB(0,0,0),0);
        rect(fb,x+1,g.len_y-1,TS_TRACKER_LANE_WIDTH-3,g.grid_y-g.len_y+1,PAL_DESKTOP);
        snprintf(line,sizeof(line),"%d",lane+1);tracker_tiny(fb,x+2,g.len_y,line,PAL_MOUSE);
        if(definition->length)snprintf(line,sizeof(line),"LEN%u",definition->length);
        else snprintf(line,sizeof(line),"LEN OFF");
        uint32_t length_color=definition->length?PAL_BLOCK_TEXT:header_color;
        uint32_t control_color=t->control_lane==lane?active_palette()->colors[TS_PALETTE_CONTROL_PLAYHEAD]:header_color;
        tracker_tiny(fb,x+10,g.len_y,line,t->length_bypass?tracker_dim(length_color):length_color);
        tracker_eject(fb,x+TS_TRACKER_LANE_WIDTH-11,g.len_y,t->length_bypass?tracker_dim(control_color):control_color);
        if(definition->mode==TS_TRACKER_PATTERN) {
            tracker_tiny(fb,x+2,g.ratio_y,ratios[definition->ratio<TS_TRACKER_RATIOS?definition->ratio:7],PAL_BLOCK_TEXT);
            if(definition->direction!=TS_TRACKER_FORWARD) {
                rect(fb,x+29,g.ratio_y-1,6,9,PAL_BLOCK);
                tracker_tiny(fb,x+30,g.ratio_y,definition->direction==TS_TRACKER_REVERSE?"R":"B",
                    active_palette()->colors[TS_PALETTE_FASTTRACKS_PHASE]);
            }
            for(int led=0;led<3;++led) {
                uint32_t color=header_color;
                if(view.playing && (ui->tracker_lane_phase[lane]&(1u<<led)))
                    color=tracker_breathe(active_palette()->colors[led==1?TS_PALETTE_FASTTRACKS_SYNC:TS_PALETTE_FASTTRACKS_PHASE],ui);
                tracker_phase_led(fb,x+TS_TRACKER_LANE_WIDTH-15+led*4,g.ratio_y+2,color);
            }
        } else {
            tracker_tiny(fb,x+2,g.ratio_y,"FT",header_color);
            if(definition->mode==TS_TRACKER_SONG)tracker_tiny(fb,x+30,g.ratio_y,"S",active_palette()->colors[TS_PALETTE_FASTTRACKS_SONG]);
        }
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
            uint32_t head_color=(!ui->tracker_block && (view.fast[lane] || (!t->length_bypass && definition->length)))?
                tracker_head_color(t,lane,view.fast[lane]):RGB(255,255,255);
            tracker_tiny(fb,x+31,g.mix_y+1,line,head<(p?p->rows:0)?head_color:tracker_dim(head_color));
        }
        snprintf(line,sizeof(line),"%.1f",definition->trim);tracker_tiny(fb,x+49,g.mix_y+1,line,PAL_VOLUME);
    }
    TsTrackerRegion selection=ts_tracker_edit_region(ui->tracker_edit,t);
    int has_selection=ts_tracker_edit_selected(ui->tracker_edit,t);
    /* Tapehead's fixed desktop band belongs to the normal master stream.
       Private lanes retain their own source row at that screen position. */
    if(p)for(int visible=0;visible<g.rows;++visible) {
        int master=view.master_top+visible,y=g.grid_y+visible*TS_TRACKER_ROW_HEIGHT;
        int band=master==view.master_row;
        if(band)rect(fb,4,y,632,TS_TRACKER_ROW_HEIGHT,PAL_DESKTOP);
        if(master>=0 && master<view.extent) {
            uint32_t row_color=band?RGB(255,255,255):PAL_TEXT;
            for(int side=0;side<2;++side)for(int digit=0;digit<2;++digit)
                tracker_glyph(fb,(side?TS_TRACKER_LANE_RIGHT+7:9)+digit*8,y+2,
                    tracker_font4[(master>>(digit?0:4))&15],8,8,row_color);
        }
        for(int lane=0;lane<TS_TRACKER_LANES;++lane) {
            int row=view.top[lane]+visible;
            if(row<0 || (!view.independent[lane] && row>=view.extent))continue;
            static const TsTrackerCell blank={0};
            const TsTrackerCell *c=row<p->rows?&p->cells[row][lane]:&blank;
            const TsTrackerLane *definition=&t->lanes[lane];
            int x=TS_TRACKER_LANE_X+lane*TS_TRACKER_LANE_WIDTH;
            int selected=has_selection && row>=selection.row0 && row<=selection.row1 &&
                lane>=selection.lane0 && lane<=selection.lane1;
            if(selected)rect(fb,x+1,y,TS_TRACKER_LANE_WIDTH-3,TS_TRACKER_ROW_HEIGHT,PAL_BLOCK);
            if(ui->tracker_block && ui->tracker_heard_pattern==p->id &&
               row>=(int)(ui->tracker_block_rows&255) && row<=(int)(ui->tracker_block_rows>>8) &&
               lane>=(int)(ui->tracker_block_lanes&255) && lane<=(int)(ui->tracker_block_lanes>>8))
                rect(fb,x,y,1,TS_TRACKER_ROW_HEIGHT,PAL_VOLUME);
            char tile[8],volume[8],tune[8],fx[8];
            unsigned alias=tracker_alias(t,c->tile_id);
            snprintf(tile,sizeof(tile),c->tile_id?(alias?"%02X":"??"):"00",alias);
            snprintf(volume,sizeof(volume),c->has_volume?"%02X":"..",c->volume);
            snprintf(tune,sizeof(tune),c->tune_command?"%c%02X":"...",c->tune_command,c->tune_value);
            snprintf(fx,sizeof(fx),c->fx_command?"%c%02X":"...",c->fx_command,c->fx_value);
            int cursor=row==t->editor_row && lane==t->editor_lane;
            if(cursor && ui->tracker_hex_digit) {
                if(ui->tracker_field==1)snprintf(tile,sizeof(tile),"%X.",((unsigned)ui->tracker_hex_value>>4)&15u);
                if(ui->tracker_field==2)snprintf(volume,sizeof(volume),"%X.",((unsigned)ui->tracker_hex_value>>4)&15u);
                if(ui->tracker_field==4)snprintf(tune+1,sizeof(tune)-1,"%X.",((unsigned)ui->tracker_hex_value>>4)&15u);
                if(ui->tracker_field==6)snprintf(fx+1,sizeof(fx)-1,"%X.",((unsigned)ui->tracker_hex_value>>4)&15u);
            }
            uint32_t empty=active_palette()->colors[TS_PALETTE_PATTERN_EMPTY];
            int populated[]={c->note_kind!=0,c->tile_id!=0,c->has_volume!=0,c->tune_command!=0,c->fx_command!=0};
            uint32_t colors[]={populated[0]?PAL_NOTE:empty,populated[1]?PAL_INSTRUMENT:empty,
                populated[2]?PAL_VOLUME:empty,populated[3]?PAL_TUNING:empty,populated[4]?PAL_EFFECT:empty};
            int inactive=!t->length_bypass && !ui->tracker_block && definition->length &&
                (!view.fast[lane] || t->fasttracks_uses_length) && row>=definition->length;
            for(int field=0;field<5;++field) {
                if(view.fast[lane] && populated[field])colors[field]=PAL_BLOCK_TEXT;
                if(selected)colors[field]=PAL_BLOCK_TEXT;
                if(inactive)colors[field]=tracker_dim(colors[field]);
                if(definition->muted)colors[field]=palette_blend(RGB(0,0,0),colors[field],40);
            }
            tracker_note_draw(fb,x+2,y+2,c,colors[0]);
            tracker_tiny(fb,x+27,y+2,tile,colors[1]);tracker_tiny(fb,x+36,y+2,volume,colors[2]);
            tracker_tiny(fb,x+45,y+2,tune,colors[3]);tracker_tiny(fb,x+58,y+2,fx,colors[4]);
            int lane_heard=view.playing && ui->tracker_lane_row[lane]==(unsigned)row &&
                !ui->tracker_block && (view.fast[lane] || (!t->length_bypass && definition->length));
            if(lane_heard)tracker_outline(fb,x+1,y,TS_TRACKER_LANE_WIDTH-2,TS_TRACKER_ROW_HEIGHT,
                tracker_head_color(t,lane,view.fast[lane]));
            /* Locator is drawn last without painting over the pattern text.
               It belongs to the logical edit cell, never a private screen slot. */
            if(cursor) {
                int start=x+ts_tracker_field_offset(ui->tracker_field),width=ts_tracker_field_width(ui->tracker_field);
                uint32_t focus=tracker_breathe(ui->tracker_hex_digit?RGB(255,224,32):RGB(40,224,255),ui);
                tracker_outline(fb,start-1,y,width+1,TS_TRACKER_ROW_HEIGHT,focus);
                if(ui->tracker_cursor_visible) {
                    int caret=start+(ui->tracker_field?ui->tracker_hex_digit*4:0);
                    rect(fb,caret,y+10,ui->tracker_field==0?23:3,1,focus);
                }
            }
        }
    }
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
