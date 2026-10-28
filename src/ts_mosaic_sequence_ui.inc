static void mosaic_sequence_setup(TsFramebuffer *fb,const TsUiState *ui,const TsMosaicEvent *selected,
    uint32_t ink,uint32_t accent,uint32_t dim,uint32_t panel)
{
    rect(fb,120,78,510,272,accent);rect(fb,122,80,506,268,panel);
    int slots[TS_MOSAIC_EVENTS],count=ts_ui_mosaic_sequence_list(ui,slots),at=0,members=0;
    for(int n=0;n<count;++n)members+=ui->mosaic->events[slots[n]].live_order>=0;
    char line[120];snprintf(line,sizeof(line),"SEQUENCER / %s / %d OF %d",ui->mosaic_sequence_running?
        (ui->mosaic_sequence_finishing?"FINISHING":"CYCLING"):"STOPPED",ui->mosaic_sequence_running?ui->mosaic_sequence_index+1:0,ui->mosaic_sequence_running?ui->mosaic_sequence_count:members);
    text(fb,132,88,line,ink,1);mosaic_button(fb,500,84,108,"CAPTURE",0,1);
    mosaic_button(fb,132,106,92,"ALL IN",0,1);
    mosaic_button(fb,232,106,160,selected && selected->live_order<0?"ADD TO SEQUENCE":"MAKE BACKGROUND",0,1);
    mosaic_button(fb,402,106,88,"UP",0,1);mosaic_button(fb,500,106,108,"DOWN",0,1);
    while(at<count && ui->mosaic->events[slots[at]].id!=ui->mosaic_selected)++at;
    int first=at<count?(at/6)*6:0;
    for(int row=0;row<6 && first+row<count;++row) {
        int n=first+row;const TsMosaicEvent *e=&ui->mosaic->events[slots[n]];int y=128+row*18;
        if(e==selected)rect(fb,130,y,480,17,ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_EDGE,RGB(64,54,70)));
        char order[16];if(e->live_order<0)snprintf(order,sizeof(order),"BG");else snprintf(order,sizeof(order),"%02d",n+1);
        snprintf(line,sizeof(line),"%s  #%llu  %.27s  X%.3G%s",order,(unsigned long long)e->id,e->name,
            e->duration/fmax(.000001,ts_mosaic_live_cycle(e)),e->id==ui->mosaic_sequence_id?"  PLAYING":"");
        text(fb,136,y+5,line,e==selected?ink:e->live_order<0?dim:accent,1);
    }
    if(!count)text(fb,136,146,"ENABLE LOOP EVENT ON AN AUDIO CARD FIRST",dim,1);
    double cycle=selected?ts_mosaic_live_cycle(selected):0;
    snprintf(line,sizeof(line),"STEP X%.3G",selected?selected->duration/fmax(.000001,cycle):0);mosaic_button(fb,132,240,150,line,0,1);
    mosaic_button(fb,292,240,150,selected && selected->live_finite?"CARD ENDS":"UNTIL STOP",selected && selected->live_finite,1);
    snprintf(line,sizeof(line),"END FADE %.2FS",selected?selected->fade_out:0);mosaic_button(fb,452,240,156,line,0,1);
    snprintf(line,sizeof(line),"FINISH FADE %.2FS",selected?selected->finish_fade:0);mosaic_button(fb,132,268,150,line,0,1);
    mosaic_button(fb,292,268,150,ui->mosaic_sequence_selection_owned?"USE FINISH PASS":"FINISH LOOP",0,1);
    text(fb,452,273,"WHEEL / RIGHT RESET",dim,1);
    text(fb,132,294,ui->mosaic_sequence_running?"CURRENT STEP LENGTH HELD / STOP SEQUENCE TO REORDER":
        "STEP = BASE-PITCH CYCLES / BG LOOPS PLAY INDEPENDENTLY",dim,1);
    mosaic_button(fb,132,310,92,"CYCLE",ui->mosaic_sequence_running && !ui->mosaic_sequence_finishing,1);
    mosaic_button(fb,232,310,110,"FINISH PASS",ui->mosaic_sequence_finishing,1);
    mosaic_button(fb,352,310,150,"STOP SEQUENCE",0,1);mosaic_button(fb,526,310,82,"SIMPLE",0,1);
    snprintf(line,sizeof(line),"%.79s",ui->mosaic_live_status);text(fb,132,332,line,accent,1);
}
