int ts_ui_mosaic_marker_y(const TsUiState *ui,int index)
{
    if(!ui || !ui->mosaic || index<0 || index>=TS_MOSAIC_MARKERS || !ui->mosaic->markers[index].stored)return -1;
    double time=ui->mosaic_marker_drag==index+1?ui->mosaic_marker_preview:ui->mosaic->markers[index].time;
    double y=66+(time-ui->mosaic_scroll)*(ui->mosaic_scale>0?ui->mosaic_scale:24);
    if(y<66 || y>=TS_MOSAIC_CANVAS_BOTTOM)return -1;
    return (int)fmax(66,fmin(TS_MOSAIC_CANVAS_BOTTOM-10,floor(y)-4));
}
static void mosaic_markers_render(TsFramebuffer *fb,const TsUiState *ui)
{
    uint32_t panel=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_PANEL,RGB(26,24,31));
    uint32_t ink=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_DATA2,RGB(129,206,200));
    uint32_t dim=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_DIM,RGB(133,127,141));
    uint32_t background=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_BACKGROUND,RGB(11,13,16));
    for(int i=0;i<TS_MOSAIC_MARKERS;++i) {
        int y=ts_ui_mosaic_marker_y(ui,i);if(y<0)continue;
        double time=ui->mosaic_marker_drag==i+1?ui->mosaic_marker_preview:ui->mosaic->markers[i].time;
        int line=66+(int)((time-ui->mosaic_scroll)*(ui->mosaic_scale>0?ui->mosaic_scale:24));
        for(int x=151;x<ts_ui_mosaic_canvas_right(ui);x+=7)rect(fb,x,line,3,1,mosaic_mix_color(ink,background,.75f));
        char label[4];snprintf(label,sizeof(label),"%02d",i+1);
        int active=ui->mosaic_marker_selected==i+1;
        rect(fb,113,y,38,10,panel);rect(fb,127,y,24,10,active?ink:panel);
        text(fb,132,y+2,label,active?background:ink,1);
    }
    rect(fb,0,358,640,22,panel);
    mosaic_button(fb,10,TS_MOSAIC_MARKER_Y,50,"VIEWS",ui->mosaic_views_open,1);
    mosaic_button(fb,64,TS_MOSAIC_MARKER_Y,44,"LOOP",ui->mosaic && ui->mosaic->marker_loop,1);
    for(int i=0;i<TS_MOSAIC_MARKERS;++i) {
        char label[4];snprintf(label,sizeof(label),"%02d",i+1);
        int x=TS_MOSAIC_MARKER_X+i*TS_MOSAIC_MARKER_STEP;
        int stored=ui->mosaic && ui->mosaic->markers[i].stored;
        int active=stored && ui->mosaic_marker_selected==i+1;
        mosaic_button(fb,x,TS_MOSAIC_MARKER_Y,30,label,active,1);
        if(stored)rect(fb,x+4,TS_MOSAIC_MARKER_Y+13,22,1,ink);
        else text(fb,x+9,TS_MOSAIC_MARKER_Y+4,label,dim,1);
    }
}
static void mosaic_marker_overlay(TsFramebuffer *fb,const TsUiState *ui)
{
    uint32_t panel=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_PANEL,RGB(31,28,38));
    uint32_t edge=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_EDGE,RGB(64,54,70));
    uint32_t ink=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_TEXT,RGB(224,214,233));
    uint32_t focus=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_DATA2,RGB(129,206,200));
    if(ui->mosaic_views_open) {
        rect(fb,200,160,240,90,edge);rect(fb,202,162,236,86,panel);
        text(fb,220,172,"SAVED CANVAS VIEWS",ink,1);
        text(fb,210,188,"CLICK RECALL / SHIFT SAVE",ink,1);
        for(int i=0;i<TS_MOSAIC_VIEWS;++i) {
            char label[4];snprintf(label,sizeof(label),"%d",i+1);
            mosaic_button(fb,220+i*40,204,36,label,ui->mosaic->views[i].stored,1);
        }
        text(fb,210,232,"MIDDLE CLEAR / ESC CLOSE",ink,1);
    }
    if(!ui->mosaic_marker_open)return;
    rect(fb,144,128,352,168,edge);rect(fb,146,130,348,164,panel);
    char title[40];snprintf(title,sizeof(title),"TIMELINE MARKER %02d",ui->mosaic_marker_open);text(fb,160,138,title,focus,1);
    text(fb,160,151,"NAME",ink,1);text(fb,160,190,"TIME (SECONDS)",ink,1);
    for(int i=0;i<2;++i) {
        int y=i?201:160,w=i?170:320;
        const char *value=i?ui->mosaic_marker_time:ui->mosaic_marker_name;
        rect(fb,160,y,w,20,ui->mosaic_marker_field==i?focus:edge);rect(fb,162,y+2,w-4,16,panel);
        char visible[33];snprintf(visible,sizeof(visible),"%.*s",i?26:31,value);text(fb,166,y+6,visible,ink,1);
        if(ui->mosaic_marker_field==i && ui->text_cursor_visible)rect(fb,166+(int)strlen(visible)*6,y+5,1,9,focus);
    }
    mosaic_button(fb,340,201,140,"AT PLAYHEAD",0,1);
    mosaic_button(fb,160,244,96,"APPLY",1,0);mosaic_button(fb,272,244,96,"DELETE",0,0);mosaic_button(fb,384,244,96,"CANCEL",0,0);
    text(fb,154,279,ui->mosaic_marker_status,ink,1);
}
