static void mosaic_arrange_overlay(TsFramebuffer *fb,const TsUiState *ui)
{
    if(!ui->mosaic_scatter_open && !ui->mosaic_spacing_confirm)return;
    uint32_t panel=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_PANEL,RGB(31,28,38));
    uint32_t ink=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_TEXT,RGB(224,214,233));
    uint32_t accent=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_DATA2,RGB(129,206,200));
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
    if(ui->mosaic_scatter_field==1)snprintf(label,sizeof(label),"%s_",ui->mosaic_scatter_text);
    mosaic_button(fb,130,115,160,label,ui->mosaic_scatter_field==1,0);
    snprintf(label,sizeof(label),"TO %.3f",ui->mosaic_scatter_end);
    if(ui->mosaic_scatter_field==2)snprintf(label,sizeof(label),"%s_",ui->mosaic_scatter_text);
    mosaic_button(fb,310,115,180,label,ui->mosaic_scatter_field==2,0);
    snprintf(label,sizeof(label),"COUNT %d",ui->mosaic_scatter_count);
    if(ui->mosaic_scatter_field==3)snprintf(label,sizeof(label),"%s_",ui->mosaic_scatter_text);
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
