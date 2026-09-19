static void router_activity(TsFramebuffer *fb,int x,int y,float peak)
{
    const float thresholds[]={.0001f,.003f,.02f,.1f,.5f};
    for(int i=0;i<5;++i)rect(fb,x+i*6,y,4,4,peak>thresholds[i]?PAL_WAVE_LEFT:RGB(38,43,47));
}
static const char *router_help(const TsUiState *ui,int x,int y)
{
    if(x>=574 && y>=42 && y<66)return "CLOSE ROUTER; AUDIO KEEPS PLAYING; F9 ALSO OPENS/CLOSES";
    if(y>=78 && y<108)return "FIXED SOURCE: CURRENT MIX; POWERED SISTER KEEPS ITS EXISTING INPUT SELECTION";
    if(y>=120 && y<304) {
        if(x>=512)return "SOLO ONE STAGE; CLICK AGAIN TO RESTORE EVERY PREVIOUS BYPASS SETTING";
        if(x>=414)return "BYPASS THIS STAGE; SETTINGS STAY INTACT; A PLAIN BYPASS CLICK EXITS SOLO";
        if(x>=344)return "SIGNAL AT THIS POINT; LIGHTS CAN GLOW WHILE A PROCESSOR IS BYPASSED OR OFF";
        int row=(y-120)/48;
        if(ui->config.router.order[row]==TS_ROUTER_PEDALBOARD)
            return "DRAG POST CHAIN AS ONE BLOCK; PRE/HEAD INSERTS STAY IN SISTER; BYPASS GATES ALL";
        return "DRAG UP/DOWN TO REORDER THE REAL AUDIO PATH; ESC CANCELS THE DRAG";
    }
    if(y>=318 && y<352)return "FIXED MASTER: EQ, LIMITER AND OUT FOLLOW ALL ROUTING; FILE OUT RECORDS THIS";
    return "";
}
static void router_render(TsFramebuffer *fb,const TsUiState *ui)
{
    if(!ui->router_open || ui->portal.open || ui->mosaic_edit_choice || ui->exit_confirm_open ||
       ui->project_overwrite_confirm_open || ui->file_busy || ui->browser.mode!=TS_BROWSER_CLOSED ||
       ui->config_open || ui->palette_open || ui->transform_open || ui->drone_open ||
       ui->import_preview_open || ui->load_selection_choice_open || ui->overdub_confirm_open ||
       ui->export_choice_open || ui->fm_bank_choice_open || ui->fm_full_choice_open ||
       ui->renaming_bank_slot>=0 || ui->renaming_recipe_slot>=0 || ui->exchange_dialog!=TS_UI_EXCHANGE_NONE)return;
    uint32_t teal=RGB(107,176,182),amber=RGB(247,196,91),muted=RGB(123,127,133);
    rect(fb,10,38,620,344,RGB(29,29,35));rect(fb,10,38,620,2,teal);
    text(fb,22,48,"GLOBAL ROUTER",amber,1);
    text(fb,250,48,ui->router_transition?"CHANGING ORDER":"SERIAL SIGNAL PATH",teal,1);
    button(fb,574,42,44,"X",0);
    rect(fb,154,78,336,28,RGB(17,21,24));text(fb,170,87,"SOURCE / INPUT",teal,1);
    router_activity(fb,422,89,ui->router_peaks[TS_ROUTER_COUNT*2]);
    text(fb,500,87,"FIXED",muted,1);
    const TsRouterControls *c=&ui->config.router;
    const char *detail[]={"LENSES / MORPH / MATRIX","TAPE / PRE + HEAD INSERTS",
        "GLOBAL TEXTURE / MASTER FX","STEREO POST / SLOT ORDER"};
    for(int row=0;row<TS_ROUTER_COUNT;++row) {
        int stage=c->order[row],y=120+row*48;
        int active=ts_router_active(c,stage),available=(ui->router_enabled&(1u<<stage))!=0;
        uint32_t color=active && available?teal:muted;
        rect(fb,319,y-14,2,14,ui->router_peaks[stage*2]>.0001f?teal:RGB(50,53,58));
        rect(fb,64,y,552,40,ui->router_drag==row+1?RGB(55,42,59):RGB(36,37,44));
        rect(fb,64,y,3,40,c->solo==stage+1?amber:color);
        for(int n=0;n<3;++n){rect(fb,77,y+13+n*5,3,2,color);rect(fb,83,y+13+n*5,3,2,color);}
        text(fb,99,y+8,ts_router_name(stage),color,1);
        text(fb,99,y+25,detail[stage],muted,1);
        const char *state=!active?(c->solo?"SKIP":"BYPASS"):!available?"OFF":c->solo==stage+1?"SOLO":"ON";
        text(fb,283,y+8,state,c->solo==stage+1?amber:muted,1);
        text(fb,351,y+8,"IN",muted,1);router_activity(fb,348,y+25,ui->router_peaks[stage*2]);
        button(fb,414,y+8,88,"BYPASS",(c->bypass_mask&(1u<<stage))!=0);
        button(fb,512,y+8,88,"SOLO",c->solo==stage+1);
        if(ui->router_drag && ui->router_drop==row) {
            rect(fb,62,y-3,556,2,amber);text(fb,20,y+15,">>",amber,1);
        }
    }
    rect(fb,319,304,2,14,ui->router_peaks[TS_ROUTER_COUNT*2+1]>.0001f?teal:RGB(50,53,58));
    rect(fb,100,318,470,34,RGB(17,21,24));
    text(fb,116,323,"MASTER / EQ > LIMITER > OUT",amber,1);
    text(fb,116,341,"SISTER DRY MONITOR JOINS HERE",muted,1);
    router_activity(fb,456,330,ui->router_peaks[TS_ROUTER_COUNT*2+1]);text(fb,518,328,"FIXED",muted,1);
    const char *help=ui->performance_tooltip[0]?ui->performance_tooltip:
        "DRAG: ORDER   BYPASS / SOLO: AUDITION   OFF: MODULE POWER   F9: CLOSE";
    char line[99];snprintf(line,sizeof(line),"%.98s",help);text(fb,22,364,line,ui->performance_tooltip[0]?amber:muted,1);
}
