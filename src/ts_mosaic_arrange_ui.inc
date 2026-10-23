static void mosaic_scatter_button(TsFramebuffer *fb,int x,int y,int w,const char *label,int active,int enabled)
{
    if(enabled){mosaic_button(fb,x,y,w,label,active,0);return;}
    rect(fb,x,y,w,24,ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_PANEL,RGB(31,28,38)));
    text(fb,x+(w-(int)strlen(label)*6)/2,y+8,label,ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_DIM,RGB(100,100,100)),1);
}
static void mosaic_arrange_overlay(TsFramebuffer *fb,const TsUiState *ui)
{
    if(!ui->mosaic_scatter_open && !ui->mosaic_spacing_confirm && !ui->mosaic_loop_open && !ui->mosaic_align_open)return;
    uint32_t panel=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_PANEL,RGB(31,28,38));
    uint32_t ink=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_TEXT,RGB(224,214,233));
    uint32_t accent=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_DATA2,RGB(129,206,200));
    if(ui->mosaic_align_open) {
        rect(fb,150,146,340,114,accent);rect(fb,152,148,336,110,panel);
        text(fb,170,166,"SELECT MORE THAN ONE CARD",ink,1);
        text(fb,170,187,"SHIFT CLICK ADDS CARDS TO THE SELECTION.",ink,1);
        mosaic_button(fb,250,220,140,"OK",1,0);return;
    }
    if(ui->mosaic_loop_open) {
        rect(fb,120,128,400,158,accent);rect(fb,122,130,396,154,panel);
        text(fb,140,142,"TIME RANGE / SECONDS",ink,1);
        text(fb,140,160,"START",ink,1);text(fb,330,160,"END",ink,1);
        for(int i=0;i<2;++i) {
            char value[34];snprintf(value,sizeof(value),"%.24s%s",ui->mosaic_loop_text[i],ui->mosaic_loop_field==i+1?(ui->text_cursor_visible?"_":" "):"");
            mosaic_button(fb,140+190*i,173,170,value,ui->mosaic_loop_field==i+1,0);
        }
        double a=strtod(ui->mosaic_loop_text[0],NULL),b=strtod(ui->mosaic_loop_text[1],NULL);
        char duration[64];snprintf(duration,sizeof(duration),"DURATION %.6F S",isfinite(a) && isfinite(b)?b-a:0);
        text(fb,140,208,duration,ink,1);
        text(fb,140,225,ui->mosaic_loop_status,ink,1);
        mosaic_button(fb,170,246,140,"APPLY",1,0);mosaic_button(fb,330,246,140,"CANCEL",0,0);return;
    }
    if(ui->mosaic_spacing_confirm) {
        rect(fb,120,140,400,120,accent);rect(fb,122,142,396,116,panel);
        text(fb,140,158,"ENABLE AUTO-SPACING?",ink,1);
        text(fb,140,180,"MOVE CARDS HORIZONTALLY TO REMOVE OVERLAPS.",ink,1);
        text(fb,140,195,"TIMES STAY FIXED. CTRL+Z CAN UNDO THIS.",ink,1);
        mosaic_button(fb,180,220,126,"ENABLE",1,0);mosaic_button(fb,330,220,126,"CANCEL",0,0);return;
    }
    rect(fb,80,30,456,350,accent);rect(fb,82,32,452,346,panel);
    text(fb,96,44,"SCATTER / CLICK OR WHEEL / SHIFT REVERSES",ink,1);
    char label[64];
    int keep=ui->mosaic_scatter_keep_positions,pitched=ui->mosaic_scatter_pitch.mode!=TS_MOSAIC_PITCH_KEEP;
    const TsMosaicPitchSettings *p=&ui->mosaic_scatter_pitch;
    snprintf(label,sizeof(label),"FROM %.3f S",ui->mosaic_scatter_start);
    if(ui->mosaic_scatter_field==1)snprintf(label,sizeof(label),"%.24s%s",ui->mosaic_scatter_text,ui->text_cursor_visible?"_":" ");
    mosaic_scatter_button(fb,96,64,204,label,ui->mosaic_scatter_field==1,!keep);
    snprintf(label,sizeof(label),"TO %.3f S",ui->mosaic_scatter_end);
    if(ui->mosaic_scatter_field==2)snprintf(label,sizeof(label),"%.24s%s",ui->mosaic_scatter_text,ui->text_cursor_visible?"_":" ");
    mosaic_scatter_button(fb,316,64,204,label,ui->mosaic_scatter_field==2,!keep);
    snprintf(label,sizeof(label),"COUNT %d",ui->mosaic_scatter_count);
    if(!ui->mosaic_scatter_copies)snprintf(label,sizeof(label),"SELECTED CARDS");
    if(ui->mosaic_scatter_field==3)snprintf(label,sizeof(label),"%.15s%s",ui->mosaic_scatter_text,ui->text_cursor_visible?"_":" ");
    mosaic_scatter_button(fb,96,94,108,label,ui->mosaic_scatter_field==3,ui->mosaic_scatter_copies);
    mosaic_scatter_button(fb,220,94,140,ui->mosaic_scatter_copies?"CREATE COPIES":"EDIT SELECTION",ui->mosaic_scatter_copies,!ui->mosaic_source_pick_count);
    mosaic_scatter_button(fb,376,94,144,"KEEP POSITIONS",keep,!ui->mosaic_scatter_copies);
    mosaic_scatter_button(fb,96,124,204,ui->mosaic_scatter_cluster?"CLUSTERED":"EVEN SCATTER",ui->mosaic_scatter_cluster,!keep);
    mosaic_scatter_button(fb,316,124,204,ui->mosaic_scatter_overlap?"OVERLAP ON":"OVERLAP OFF",ui->mosaic_scatter_overlap,!keep);
    mosaic_scatter_button(fb,96,156,204,ts_mosaic_pitch_mode_name(p->mode),pitched,1);
    static const char *roots[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    snprintf(label,sizeof(label),"ROOT %s",roots[p->root]);
    mosaic_scatter_button(fb,316,156,204,label,0,pitched);
    snprintf(label,sizeof(label),"%s %s",p->mode==TS_MOSAIC_PITCH_SPREAD_SCALE?"SCALE":"CHORD",
        p->mode==TS_MOSAIC_PITCH_SPREAD_SCALE?ts_fm_pitch_scale_name(p->scale):ts_mosaic_chord_name(p->chord));
    if(p->mode!=TS_MOSAIC_PITCH_SPREAD_SCALE && p->chord==TS_MOSAIC_CHORD_PRIMARY)snprintf(label,sizeof(label),"PRIMARY CHORD");
    mosaic_scatter_button(fb,96,186,204,label,0,pitched);
    mosaic_scatter_button(fb,316,186,204,ts_mosaic_pitch_order_name(p->order),0,pitched);
    snprintf(label,sizeof(label),"LOW C%d",p->low_octave);mosaic_scatter_button(fb,96,216,204,label,0,pitched);
    snprintf(label,sizeof(label),"HIGH %s%d",p->high_octave==9?"G":"B",p->high_octave);mosaic_scatter_button(fb,316,216,204,label,0,pitched);
    text(fb,96,247,ui->mosaic_scatter_notes,ink,1);
    rect(fb,96,264,424,28,RGB(13,15,20));
    double first=ui->mosaic_scatter_start,last=ui->mosaic_scatter_end;
    if(keep && ui->mosaic_ghost_count) {
        first=last=ui->mosaic_ghosts[0].start;
        for(int i=0;i<ui->mosaic_ghost_count;++i){first=fmin(first,ui->mosaic_ghosts[i].start);last=fmax(last,ui->mosaic_ghosts[i].start);}
    }
    double span=fmax(.001,last-first);
    for(int i=0;i<ui->mosaic_ghost_count;++i) {
        const TsMosaicEvent *e=&ui->mosaic_ghosts[i];
        int x=98+(int)(fmax(0,fmin(1,(e->start-first)/span))*416);
        rect(fb,x,268+(i%3)*6,3,4,mosaic_color(e->source->hash));
    }
    mosaic_scatter_button(fb,96,302,204,"ROLL POSITIONS",0,!keep);
    mosaic_scatter_button(fb,316,302,204,"ROLL NOTES",0,pitched && (p->order==TS_MOSAIC_PITCH_RANDOM || p->order==TS_MOSAIC_PITCH_SHUFFLED));
    mosaic_scatter_button(fb,96,334,204,"APPLY",ui->mosaic_scatter_valid,ui->mosaic_scatter_valid);
    mosaic_button(fb,316,334,204,"CANCEL",0,0);
    text(fb,96,366,ui->mosaic_scatter_status,ink,1);
}
