static void mosaic_array_overlay(TsFramebuffer *fb,const TsUiState *ui)
{
    if(!ui->mosaic_array_open)return;
    uint32_t panel=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_PANEL,RGB(32,28,38));
    uint32_t ink=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_TEXT,RGB(224,214,233));
    uint32_t dim=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_DIM,RGB(166,155,178));
    rect(fb,96,48,448,312,dim);rect(fb,98,50,444,308,panel);
    text(fb,110,62,"ARRAY SOURCES",ink,1);
    const char *modes[]={"COUNT + GRID","FILL RANGE","FIT COUNT"};int mode=ui->mosaic_array_mode;
    for(int i=0;i<3;++i)mosaic_button_height(fb,110+i*140,82,130,24,modes[i],mode==i);
    text(fb,110,128,"COUNT",mode==1?dim:ink,1);
    mosaic_button_height(fb,174,120,94,24,mode==1?"AUTO":ui->mosaic_array_count,mode!=1 && !ui->mosaic_array_field);
    text(fb,326,128,"GRID STEPS",mode==2?dim:ink,1);
    mosaic_button_height(fb,440,120,90,24,mode==2?"AUTO":ui->mosaic_array_steps,mode!=2 && ui->mosaic_array_field);
    if(mode!=2) {
        text(fb,110,152,"SPACING = GRID DIVISION X STEPS",dim,1);
        const char *units[]={"SECOND","BEAT","BAR (4 BEATS)"};
        for(int i=0;i<3;++i)mosaic_button_height(fb,110+i*140,166,130,24,units[i],ui->mosaic_array_grid.unit==i);
        static const int divisions[]={1,2,3,4,6,8,12,16,24,32,48,64};
        for(int i=0;i<12;++i){char label[12];snprintf(label,sizeof(label),"1/%d",divisions[i]);
            mosaic_button_height(fb,110+(i%6)*70,208+(i/6)*28,62,24,label,ui->mosaic_array_grid.division==divisions[i]);}
    } else {
        text(fb,110,174,"EVEN STARTS ACROSS THE SELECTED TIME RANGE",ink,1);
        text(fb,110,194,"EXACT COUNT / GRID DOES NOT RESTRICT SPACING",dim,1);
        text(fb,110,222,"LONG CARDS MAY OVERLAP IN TIME",dim,1);
    }
    char label[100];double quantity=strtod(ui->mosaic_array_count,NULL),steps=strtod(ui->mosaic_array_steps,NULL);
    double grid=ts_mosaic_grid_step(ui->mosaic_array_grid,ui->mosaic_grid_quarter);
    double first=mode==2?ui->mosaic_array_range.start:
        ceil((mode?ui->mosaic_array_range.start:ui->mosaic_array_time)/grid-1e-9)*grid;
    double spacing=mode==2?(quantity>0?(ui->mosaic_array_range.end-ui->mosaic_array_range.start)/quantity:0):grid*steps;
    double rows=mode==1?(spacing>0?fmax(0,ceil((ui->mosaic_array_range.end-first)/spacing-1e-9)):0):quantity;
    snprintf(label,sizeof(label),"%.0F COPIES X %d SOURCES = %.0F CARDS / %.5GS APART",rows,ui->mosaic_array_sources,rows*ui->mosaic_array_sources,spacing);
    text(fb,110,268,label,ink,1);
    if(mode)snprintf(label,sizeof(label),"RANGE %.4G - %.4G S / FULL CARD LENGTHS KEPT",ui->mosaic_array_range.start,ui->mosaic_array_range.end);
    else snprintf(label,sizeof(label),"START %.5G S AT CLICK / FULL CARD LENGTHS KEPT",first);
    text(fb,110,282,label,dim,1);
    if(ui->mosaic_array_status[0]) {
        snprintf(label,sizeof(label),"%.70s",ui->mosaic_array_status);text(fb,110,298,label,ink,1);
        if(strlen(ui->mosaic_array_status)>70){snprintf(label,sizeof(label),"%.70s",ui->mosaic_array_status+70);text(fb,110,310,label,ink,1);}
    } else text(fb,110,302,ui->mosaic->auto_space?"SPACE ON: SEPARATE SIDEWAYS / WHEEL EDITS NUMBERS":"SPACE OFF: OVERLAPS ALLOWED / WHEEL EDITS NUMBERS",dim,1);
    mosaic_button_height(fb,110,326,200,24,"CREATE ARRAY",1);mosaic_button_height(fb,330,326,200,24,"CANCEL",0);
}
