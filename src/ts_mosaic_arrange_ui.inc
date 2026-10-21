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
    rect(fb,110,78,400,251,accent);rect(fb,112,80,396,247,panel);
    text(fb,130,92,"SCATTER / START TIMES IN SECONDS",ink,1);
    char label[64];
    snprintf(label,sizeof(label),"FROM %.3f",ui->mosaic_scatter_start);
    if(ui->mosaic_scatter_field==1)snprintf(label,sizeof(label),"%.24s%s",ui->mosaic_scatter_text,ui->text_cursor_visible?"_":" ");
    mosaic_button(fb,130,115,160,label,ui->mosaic_scatter_field==1,0);
    snprintf(label,sizeof(label),"TO %.3f",ui->mosaic_scatter_end);
    if(ui->mosaic_scatter_field==2)snprintf(label,sizeof(label),"%.24s%s",ui->mosaic_scatter_text,ui->text_cursor_visible?"_":" ");
    mosaic_button(fb,310,115,180,label,ui->mosaic_scatter_field==2,0);
    snprintf(label,sizeof(label),"COUNT %d",ui->mosaic_scatter_count);
    if(ui->mosaic_scatter_field==3)snprintf(label,sizeof(label),"%.15s%s",ui->mosaic_scatter_text,ui->text_cursor_visible?"_":" ");
    mosaic_button(fb,130,151,110,label,ui->mosaic_scatter_field==3,0);
    mosaic_button(fb,260,151,230,ui->mosaic_scatter_copies?"CREATE COPIES":"MOVE SELECTION",ui->mosaic_scatter_copies,0);
    mosaic_button(fb,130,186,170,ui->mosaic_scatter_cluster?"CLUSTERED":"EVEN SCATTER",ui->mosaic_scatter_cluster,0);
    mosaic_button(fb,320,186,170,ui->mosaic_scatter_overlap?"OVERLAP ON":"OVERLAP OFF",ui->mosaic_scatter_overlap,0);
    rect(fb,130,222,360,34,RGB(13,15,20));
    double span=fmax(.001,ui->mosaic_scatter_end-ui->mosaic_scatter_start);
    for(int i=0;i<ui->mosaic_ghost_count;++i) {
        const TsMosaicEvent *e=&ui->mosaic_ghosts[i];
        int x=132+(int)((e->start-ui->mosaic_scatter_start)/span*352);
        rect(fb,x,226+(i%4)*6,3,4,mosaic_color(e->source->hash));
    }
    mosaic_button(fb,130,272,100,"REROLL",0,0);
    mosaic_button(fb,250,272,100,"APPLY",ui->mosaic_scatter_valid,0);
    mosaic_button(fb,370,272,120,"CANCEL",0,0);
    text(fb,130,307,ui->mosaic_scatter_status,ink,1);
}
