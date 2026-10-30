static void mosaic_gallery_overlay(TsFramebuffer *fb,const TsUiState *ui)
{
    if(!ui->mosaic_gallery_open)return;
    uint32_t panel=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_PANEL,RGB(31,28,38));
    uint32_t edge=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_EDGE,RGB(64,54,70));
    uint32_t ink=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_TEXT,RGB(224,214,233));
    uint32_t accent=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_DATA2,RGB(129,206,200));
    uint32_t dim=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_DIM,RGB(160,149,172));
    for(int i=0;i<TS_UI_WIDTH*TS_UI_HEIGHT;++i)fb->pixels[i]=mosaic_mix_color(fb->pixels[i],panel,.7f);
    rect(fb,114,72,512,286,edge);rect(fb,116,74,508,282,panel);
    char line[128];snprintf(line,sizeof(line),"GALLERY / %02d CANVASES",ui->mosaic_gallery_count);text(fb,128,85,line,ink,1);
    if(ui->mosaic_gallery_fading)snprintf(line,sizeof(line),"CROSSFADING %.0f%%",ui->mosaic_gallery_progress*100);
    else snprintf(line,sizeof(line),"MANUAL / ONE ACTIVE CANVAS");
    text(fb,410,85,line,accent,1);
    for(int i=0;i<ui->mosaic_gallery_count;++i) {
        int x=126+(i/8)*244,y=105+(i%8)*20;
        int selected=i==ui->mosaic_gallery_selected,active=i==ui->mosaic_gallery_active;
        rect(fb,x,y,236,18,selected?edge:panel);
        snprintf(line,sizeof(line),"%02d %.30s%s",i+1,ui->mosaic_gallery_names[i],active?" *":"");
        text(fb,x+4,y+5,line,active?accent:ink,1);
    }
    int selected=ui->mosaic_gallery_selected;
    if(selected<0 || selected>=ui->mosaic_gallery_count)selected=0;
    const char *modes[]={"START","RESUME","AT MARKER"};
    int entry=ui->mosaic_gallery_entry[selected];if(entry<0 || entry>2)entry=0;
    snprintf(line,sizeof(line),"ENTRY: %s",modes[entry]);mosaic_button(fb,128,273,147,line,entry!=0,0);
    snprintf(line,sizeof(line),"MARKER %02d %s",ui->mosaic_gallery_marker[selected]+1,ui->mosaic_gallery_marker_valid[selected]?"SET":"EMPTY");
    mosaic_button(fb,282,273,143,line,entry==2,0);
    snprintf(line,sizeof(line),"-  FADE %.2fS  +",ui->mosaic_gallery_fade);mosaic_button(fb,438,273,160,line,ui->mosaic_gallery_fading,0);
    if(ui->mosaic_gallery_rename) {
        rect(fb,128,300,470,16,edge);snprintf(line,sizeof(line),"NAME: %.47s%s",ui->mosaic_gallery_name,ui->text_cursor_visible?"_":"");text(fb,132,304,line,accent,1);
    } else text(fb,128,304,"UP/DOWN SELECT / ENTER SWITCH / WHEEL MARKER OR FADE",dim,1);
    mosaic_button(fb,128,319,72,"NEW",0,0);mosaic_button(fb,212,319,76,"RENAME",ui->mosaic_gallery_rename,0);
    mosaic_button(fb,300,319,76,ui->mosaic_gallery_delete?"CONFIRM":"DELETE",ui->mosaic_gallery_delete,0);
    mosaic_button(fb,388,319,94,selected==ui->mosaic_gallery_active?"ACTIVE":"ENTER",selected==ui->mosaic_gallery_active,0);
    mosaic_button(fb,494,319,104,"CLOSE",0,0);
    snprintf(line,sizeof(line),"%.78s",ui->mosaic_gallery_rename?"CTRL+A REPLACE / ENTER APPLY / ESC CANCEL":ui->mosaic_gallery_status);
    text(fb,128,348,line,dim,1);
}
