static void mosaic_grid_overlay(TsFramebuffer *fb,const TsUiState *ui)
{
    if(!ui->mosaic_grid_open)return;
    uint32_t panel=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_PANEL,RGB(32,28,38));
    uint32_t ink=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_TEXT,RGB(224,214,233));
    uint32_t dim=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_DIM,RGB(166,155,178));
    rect(fb,112,104,416,212,dim);rect(fb,114,106,412,208,panel);
    text(fb,136,120,"CARD GRID / HOLD ALT TO SNAP",ink,1);
    text(fb,136,138,"UNIT",dim,1);
    const char *units[]={"SECOND","BEAT","BAR (4 BEATS)"};
    for(int i=0;i<3;++i)mosaic_button_height(fb,136+i*124,150,112,24,units[i],ui->mosaic_grid_edit.unit==i);
    text(fb,136,186,"DIVISIONS PER UNIT",dim,1);
    static const int divisions[]={1,2,3,4,6,8,12,16,24,32,48,64};
    for(int i=0;i<12;++i){char label[12];snprintf(label,sizeof(label),"1/%d",divisions[i]);
        mosaic_button_height(fb,136+(i%6)*62,202+(i/6)*30,54,24,label,ui->mosaic_grid_edit.division==divisions[i]);}
    char label[80];snprintf(label,sizeof(label),"STEP %.4GS / %.1F BPM",ts_mosaic_grid_step(ui->mosaic_grid_edit,ui->mosaic_grid_quarter),
        ui->mosaic_grid_quarter>0?60/ui->mosaic_grid_quarter:120);
    text(fb,136,264,label,dim,1);
    mosaic_button_height(fb,136,278,170,24,"APPLY",1);mosaic_button_height(fb,330,278,170,24,"CANCEL",0);
}
static void mosaic_grid_lines(TsFramebuffer *fb,const TsUiState *ui,double scale)
{
    if(!ui->mosaic || !ui->mosaic->grid.enabled)return;
    double step=ts_mosaic_grid_step(ui->mosaic->grid,ui->mosaic_grid_quarter);
    /* Thin dense grids for display only; snapping retains the exact interval. */
    while(step*scale<8)step*=2;
    uint32_t ink=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_GRID,RGB(59,47,70));
    double end=ui->mosaic_scroll+TS_MOSAIC_CANVAS_HEIGHT/scale;
    for(double t=ceil(ui->mosaic_scroll/step)*step;t<end;t+=step) {
        int y=66+(int)lround((t-ui->mosaic_scroll)*scale);
        if(y>=66 && y<TS_MOSAIC_CANVAS_BOTTOM)rect(fb,151,y,ts_ui_mosaic_canvas_right(ui)-151,1,ink);
    }
}
