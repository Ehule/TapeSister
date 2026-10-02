/* Native pattern editor. Running definitions and editor focus are independent. */
static unsigned tracker_alias(const TsSisterTracker *t, TsTileId id)
{
    if (id) for (unsigned i=1;i<TS_TRACKER_ALIASES;++i) if(t->aliases[i]==id)return i;
    return 0;
}
static void tracker_note_text(char *out,size_t size,const TsTrackerCell *c)
{
    static const char *names[]={"C-","C#","D-","D#","E-","F-","F#","G-","G#","A-","A#","B-"};
    if(c->note_kind==TS_TRACKER_NOTE_PITCH)snprintf(out,size,"%s%d",names[c->note%12],c->note/12-1);
    else snprintf(out,size,"%s",c->note_kind==TS_TRACKER_NOTE_OFF?"OFF":c->note_kind==TS_TRACKER_NOTE_CUT?"CUT":"...");
}
static void tracker_render(TsFramebuffer *fb,const TsUiState *ui,const TsInstrument *instrument)
{
    const TsSisterTracker *t=ui->tracker;
    const TsTrackerPattern *p=ts_sister_tracker_pattern_const(t,t->editor_pattern);
    clear(fb,PAL_DESKTOP);
    rect(fb,0,0,640,32,RGB(12,12,12));
    text(fb,10,13,"SISTERTRACKER",PAL_INSTRUMENT,1);
    button(fb,170,4,42,"PLAY",ui->tracker_running==1);
    button(fb,216,4,50,"PAUSE",ui->tracker_running==2);
    button(fb,270,4,42,"STOP",!ui->tracker_running);
    button(fb,316,4,42,"BACK",0);button(fb,362,4,44,"SAVE",0);
    button(fb,410,4,56,"FOLLOW",t->follow && !ui->tracker_follow_hold);
    button(fb,466,4,24,"FX",0);
    button(fb,494,5,28,"EQ",ui->master_eq_open);
    master_output_fader(fb,ui->master_output.gain,528,5);
    master_output_meter(fb,&ui->master_output,580,8);
    char line[128];
    mosaic_button(fb,10,34,20,"<",0,1);mosaic_button(fb,144,34,20,">",0,1);
    snprintf(line,sizeof(line),"PATTERN %03u",p?p->id:0);text(fb,36,39,line,PAL_TEXT,1);
    mosaic_button(fb,168,34,36,"NEW",0,1);
    snprintf(line,sizeof(line),"BPM %03u",t->bpm);mosaic_button(fb,212,34,78,line,0,1);
    snprintf(line,sizeof(line),"TPL %02u",t->ticks_per_line);mosaic_button(fb,294,34,56,line,0,1);
    snprintf(line,sizeof(line),"ROWS %03u",p?p->rows:0);mosaic_button(fb,354,34,72,line,0,1);
    mosaic_button(fb,430,34,56,"LOOP",t->loop,1);
    snprintf(line,sizeof(line),"HEARD %03u %03u",ui->tracker_heard_pattern,ui->tracker_row);
    text(fb,494,39,line,ui->tracker_running?PAL_NOTE:PAL_TEXT,1);
    for(int lane=0;lane<TS_TRACKER_LANES;++lane) {
        int x=34+lane*74;
        /* Tapehead's separate LEN and Fast Tracks strips. These show saved
           definitions; this stage's playback still uses the Standard clock. */
        static const char *ratios[TS_TRACKER_RATIOS]={"1:2","2:3","3:4","4:5","5:6","7:8","15:16",
            "1:1","17:16","8:7","6:5","5:4","4:3","3:2","2:1","3:1","5:1"};
        const TsTrackerLane *definition=&t->lanes[lane];
        rect(fb,x,TS_TRACKER_LEN_Y-1,72,11,RGB(27,27,34));
        rect(fb,x,TS_TRACKER_RATIO_Y-1,72,11,RGB(20,25,31));
        if(definition->length)snprintf(line,sizeof(line),"LEN%u",definition->length);
        else snprintf(line,sizeof(line),"LEN OFF");
        text(fb,x+3,TS_TRACKER_LEN_Y,line,RGB(174,174,182),1);
        if(t->control_lane==lane)text(fb,x+63,TS_TRACKER_LEN_Y,"C",RGB(174,174,182),1);
        snprintf(line,sizeof(line),"FT %s %c",ratios[definition->ratio<TS_TRACKER_RATIOS?definition->ratio:7],
            definition->mode==TS_TRACKER_PATTERN?'P':definition->mode==TS_TRACKER_SONG?'S':'-');
        text(fb,x+3,TS_TRACKER_RATIO_Y,line,RGB(149,171,185),1);
        snprintf(line,sizeof(line),"%d %.9s",lane+1,definition->name);text(fb,x+2,TS_TRACKER_NAME_Y,line,PAL_TEXT,1);
        mosaic_button(fb,x+2,TS_TRACKER_MIX_Y,20,"M",definition->muted,1);
        mosaic_button(fb,x+24,TS_TRACKER_MIX_Y,20,"S",ui->tracker_solo&(1u<<lane),1);
        snprintf(line,sizeof(line),"%.1f",definition->trim);text(fb,x+49,TS_TRACKER_MIX_Y+5,line,PAL_VOLUME,1);
        text(fb,x+3,TS_TRACKER_FIELDS_Y,"NTE TI VL",PAL_TEXT,1);
    }
    TsTrackerRegion selection=ts_tracker_edit_region(ui->tracker_edit,t);
    if(p)for(int visible=0;visible<TS_TRACKER_VISIBLE_ROWS;++visible) {
        int row=ui->tracker_scroll+visible,y=TS_TRACKER_GRID_Y+visible*TS_TRACKER_ROW_HEIGHT;
        if(row>=p->rows)break;
        int heard=ui->tracker_running && ui->tracker_heard_pattern==p->id && ui->tracker_row==(unsigned)row;
        rect(fb,6,y,624,12,heard?RGB(42,63,61):(row%4?RGB(22,21,25):RGB(31,29,35)));
        snprintf(line,sizeof(line),"%03d",row);text(fb,10,y+2,line,heard?PAL_NOTE:PAL_TEXT,1);
        for(int lane=0;lane<TS_TRACKER_LANES;++lane) {
            const TsTrackerCell *c=&p->cells[row][lane];int x=34+lane*74;
            if(ui->tracker_edit && ui->tracker_edit->selected && row>=selection.row0 && row<=selection.row1 &&
               lane>=selection.lane0 && lane<=selection.lane1)rect(fb,x,y,72,12,RGB(48,57,85));
            if(row==t->editor_row && lane==t->editor_lane) {
                int offsets[]={2,30,50},widths[]={25,18,18};
                int start=x+offsets[ui->tracker_field],width=widths[ui->tracker_field];
                rect(fb,start,y,width,12,RGB(75,59,84));
                rect(fb,start,y,width,1,RGB(232,208,255));
                rect(fb,start,y,1,12,RGB(232,208,255));
                rect(fb,start+width-1,y,1,12,RGB(232,208,255));
                if(ui->tracker_cursor_visible) {
                    int caret=ui->tracker_field==0?x+3:x+(ui->tracker_field==1?31:51)+ui->tracker_hex_digit*6;
                    rect(fb,caret,y+10,ui->tracker_field==0?18:5,2,RGB(255,245,164));
                }
            }
            char note[8],tile[8],volume[8];tracker_note_text(note,sizeof(note),c);
            unsigned alias=tracker_alias(t,c->tile_id);
            snprintf(tile,sizeof(tile),c->tile_id?(alias?"%02X":"??"):"..",alias);
            snprintf(volume,sizeof(volume),c->has_volume?"%02X":"..",c->volume);
            if(row==t->editor_row && lane==t->editor_lane && ui->tracker_hex_digit) {
                if(ui->tracker_field==1)snprintf(tile,sizeof(tile),"%X.",((unsigned)ui->tracker_hex_value>>4)&15u);
                if(ui->tracker_field==2)snprintf(volume,sizeof(volume),"%X.",((unsigned)ui->tracker_hex_value>>4)&15u);
            }
            text(fb,x+3,y+2,note,PAL_NOTE,1);text(fb,x+31,y+2,tile,PAL_INSTRUMENT,1);
            text(fb,x+51,y+2,volume,PAL_VOLUME,1);
        }
    }
    const TsTrackerCell *c=p?&p->cells[t->editor_row][t->editor_lane]:NULL;
    snprintf(line,sizeof(line),"ROW %03u LANE %u  %s  TILE %02X  %s",t->editor_row,t->editor_lane+1,
        ui->tracker_field==0?"NOTE":ui->tracker_field==1?"TILE":"VOLUME",c?tracker_alias(t,c->tile_id):0,
        ui->tracker_missing?"MISSING TILE IN PLAYBACK":"STANDARD CLOCK; LEN/FT READOUTS INACTIVE");
    text(fb,10,317,line,ui->tracker_missing?PAL_VOLUME:PAL_TEXT,1);
    mosaic_button(fb,10,332,74,"USE TILE",0,0);mosaic_button(fb,88,332,24,"<",0,0);
    mosaic_button(fb,116,332,24,">",0,0);mosaic_button(fb,146,332,32,"OFF",0,0);
    mosaic_button(fb,182,332,32,"CUT",0,0);mosaic_button(fb,218,332,48,"CLEAR",0,0);
    snprintf(line,sizeof(line),"STEP %02u",t->edit_step);mosaic_button(fb,270,332,66,line,0,0);
    snprintf(line,sizeof(line),"OCT %d",ui->keyboard_base_note/12-1);mosaic_button(fb,340,332,72,line,0,0);
    mosaic_button(fb,424,332,46,"NOTE",ui->tracker_field==0,0);
    mosaic_button(fb,474,332,46,"TILE",ui->tracker_field==1,0);
    mosaic_button(fb,524,332,46,"VOL",ui->tracker_field==2,0);
    mosaic_button(fb,578,332,52,"EDIT",ui->tracker_edit && ui->tracker_edit->tools_open,0);
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
        text(fb,28,116,"PATTERN EDITING",PAL_NOTE,1);mosaic_button(fb,536,111,76,"CLOSE",0,1);
        for(int i=0;i<19;++i)mosaic_button(fb,28+(i%6)*98,138+(i/6)*22,94,labels[i],0,1);
        text(fb,28,231,"FIELDS FOR CLIPBOARD / CLEAR BLOCK / FILL / REVERSE:",PAL_TEXT,1);
        for(int i=0;i<5;++i)mosaic_button(fb,28+i*98,245,94,masks[i],!!(e->mask&(1u<<i)),1);
        snprintf(line,sizeof(line),"BLOCK ROWS %03d-%03d LANES %d-%d | CLIPBOARD %u X %u",selection.row0,selection.row1,
            selection.lane0+1,selection.lane1+1,e->clipboard_rows,e->clipboard_lanes);text(fb,28,272,line,PAL_TEXT,1);
        text(fb,28,286,"SHIFT/CTRL/ALT + F3 F4 F5: CUT COPY PASTE LANE / PATTERN / BLOCK",PAL_TEXT,1);
        text(fb,28,299,"CTRL+SHIFT+V VOL RAMP / SHIFT+INSERT OR BACKSPACE: ALL LANES",PAL_TEXT,1);
        text(fb,28,312,"TRANSPOSE: CTRL+UP/DOWN (+SHIFT FOR OCTAVE) / ESC CLOSE",PAL_TEXT,1);
    }
}
