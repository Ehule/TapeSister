static const TsMosaicLive *mosaic_live_snapshot(const TsUiState *ui,const TsMosaicEvent *e)
{
    const TsMosaicEvent *live=ts_mosaic_find(ui->mosaic,e->id);
    return live && live->live?&ui->mosaic_live[live-ui->mosaic->events]:NULL;
}
static const char *mosaic_live_state_name(const TsUiState *ui,const TsMosaicEvent *e)
{
    const TsMosaicLive *l=mosaic_live_snapshot(ui,e);
    return !l || l->id!=e->id || !l->state?"LIVE STOP":l->state==TS_MOSAIC_LIVE_WAITING?"LIVE WAIT":
        l->state==TS_MOSAIC_LIVE_RELEASING?"LIVE END":"LIVE PLAY";
}
static void mosaic_live_overlay(TsFramebuffer *fb,const TsUiState *ui)
{
    if(!ui->mosaic_live_open || !ui->mosaic || ts_ui_foreground_panel_open(ui))return;
    const TsMosaic *m=ui->mosaic;const TsMosaicEvent *e=ts_mosaic_find(ui->mosaic,ui->mosaic_selected);
    uint32_t ink=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_TEXT,RGB(224,214,233));
    uint32_t accent=RGB(245,184,91),dim=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_DIM,RGB(178,163,189));
    rect(fb,120,78,510,272,accent);rect(fb,122,80,506,268,ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_PANEL,RGB(31,28,38)));
    char line[120];snprintf(line,sizeof(line),"LIVE LOOPS / %d ACTIVE / %s %.2FS",ui->mosaic_live_loops,
        ui->mosaic_live_recording?"RECORDING":ui->mosaic_live_finishing?"FINISHING":"IDLE",ui->mosaic_live_seconds);
    text(fb,132,88,line,ui->mosaic_live_recording?accent:ink,1);
    const char *buttons[]={ui->mosaic_live_recording?"NEXT LAYER":"RECORD","STOP CAP","DROP PART","STOP LOOPS"};
    for(int i=0;i<4;++i)mosaic_button(fb,132+i*104,106,96,buttons[i],i==0 && ui->mosaic_live_recording,1);
    mosaic_button(fb,132,140,134,(const char *[]){"SRC: EXT INPUT","SRC: DRY KEYS","SRC: OUTPUT"}[m->live_capture.source],0,1);
    mosaic_button(fb,276,140,130,m->live_capture.continuous?"CONTINUOUS ON":"SINGLE TAKE",m->live_capture.continuous,1);
    if(m->live_capture.length)snprintf(line,sizeof(line),"LENGTH %d %s",m->live_capture.length,m->live_capture.units?"BEATS":"SEC");
    else snprintf(line,sizeof(line),"LENGTH MANUAL");
    mosaic_button(fb,416,140,192,line,0,1);
    mosaic_button(fb,132,166,150,m->live_capture.quantize?"LAUNCH NEXT BEAT":"LAUNCH NOW",m->live_capture.quantize,1);
    mosaic_button(fb,292,166,126,ui->monitor_enabled?"MONITOR ON":"MONITOR OFF",ui->monitor_enabled,1);
    mosaic_button(fb,424,166,96,"COPY WAV",0,1);
    mosaic_button(fb,526,166,82,"CLOSE",0,1);
    snprintf(line,sizeof(line),m->live_capture.source==2?"OUTPUT CAPTURES PLAYING LOOPS TOO / FEEDBACK LAYERS":
        m->live_capture.source==1?"DRY KEYBOARD / EXCLUDES PLAYING MOSAIC LOOPS":
        "EXT: %s / MONITOR DOES NOT CHANGE THE RECORDED TAP",ts_input_channel_mode_name(ui->config.record_input_channel));
    text(fb,132,192,line,dim,1);
    text(fb,132,206,"LENGTH: CLICK / WHEEL 1 / RIGHT: SEC-BEATS / 60S CAP",dim,1);
    mosaic_button(fb,132,224,100,e && e->live?"TO TIMELINE":"MAKE LIVE",e && e->live,1);
    mosaic_button(fb,240,224,92,e && e->live_finite?"CARD ENDS":"UNTIL STOP",e && e->live_finite,1);
    mosaic_button(fb,340,224,90,"LAUNCH",0,1);mosaic_button(fb,438,224,90,"STOP",0,1);mosaic_button(fb,536,224,72,"RESTART",0,1);
    mosaic_button(fb,132,254,150,e && e->live_tempo?"TEMPO: MASTER TAPE":"TEMPO: FREE",e && e->live_tempo,1);
    mosaic_button(fb,292,254,150,e && e->live_transport?"GROUP: FOLLOW":"GROUP: IGNORE",e && e->live_transport,1);
    mosaic_button(fb,452,254,156,e && e->live_position?"SEEK: RESTART":"SEEK: IGNORE",e && e->live_position,1);
    if(e && e->live)snprintf(line,sizeof(line),"%s / SOURCE %.3FS / CARD %.2FS = %.2FX %s",mosaic_live_state_name(ui,e),ts_mosaic_live_cycle(e),e->duration,e->duration/fmax(.000001,ts_mosaic_live_cycle(e)),e->live_finite?"END":"INF");
    else snprintf(line,sizeof(line),"SELECT AN AUDIO CARD / LIVE LOOPS ARE GOLD WITH OWN PLAYHEADS");
    text(fb,132,286,line,ink,1);
    text(fb,132,300,"FORWARD LOOPS / CARD ENDS: RESIZE STOP / INF: LENGTH IS A FOOTPRINT",dim,1);
    text(fb,132,312,"STOP CAP KEEPS PARTIAL / DROP PART SKIPS IT / EARLIER LOOPS STAY",dim,1);
    snprintf(line,sizeof(line),"%.79s",ui->mosaic_live_status);text(fb,132,330,line,accent,1);
}
