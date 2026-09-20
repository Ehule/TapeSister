static const char *insert_help(int x,int y)
{
    if(y>=150 && y<174)return x<320?
        "SEND: SPARE PAIRS ON THE CFG OUTPUT DEVICE; MASTER ALWAYS USES 1/2":
        "RETURN: RESERVED INPUT PAIR; EXCLUDED FROM ORDINARY EXT MONITORING";
    if(y>=219 && y<260)return "LEVEL: -24 TO +12 DB; RIGHT-CLICK RESETS TO UNITY; CHANGES ARE SMOOTHED";
    if(y>=326 && y<350)return x>=420?
        "APPLY PORT CHOICES; FINISH RECORDING FIRST; DEVICES AND BACKEND ARE SELECTED IN CFG":
        "ROUTER BYPASS / SOLO; ACTIVE INSERT IS 100% RETURN; MISSING RETURN STAYS SILENT";
    return "EXTERNAL INSERT: CONNECT SEND TO YOUR PROCESSOR AND ITS OUTPUT TO RETURN; NO DRY MIX";
}

static void insert_render(TsFramebuffer *fb,const TsUiState *ui)
{
    uint32_t amber=RGB(247,196,91),teal=RGB(107,176,182),muted=RGB(123,127,133);
    char label[100];
    rect(fb,10,38,620,344,RGB(29,29,35));rect(fb,10,38,620,2,teal);
    text(fb,22,48,"EXTERNAL INSERT",amber,1);
    text(fb,264,48,"SERIAL / 100% RETURN",teal,1);
    button(fb,546,43,70,"BACK",0);
    for(int side=0;side<2;++side) {
        int x=22+side*310;
        rect(fb,x,83,286,180,RGB(36,37,44));rect(fb,x,83,3,180,teal);
        text(fb,x+14,94,side?"RETURN / INPUT":"SEND / OUTPUT",teal,1);
        router_activity(fb,x+222,96,side?ui->insert_return_peak:ui->insert_send_peak);
        const char *device=side?ui->config.record_input_device:ui->config.audio_output_device;
        snprintf(label,sizeof(label),"%.42s",*device?device:"SYSTEM DEFAULT");
        text(fb,x+14,116,label,muted,1);
        text(fb,x+14,133,"DEVICE / BACKEND: GLOBAL CFG",muted,1);
        int pair=side?ui->insert_return_choice:ui->insert_send_choice;
        if(!side && !pair)snprintf(label,sizeof(label),"SEND UNASSIGNED");
        else snprintf(label,sizeof(label),"%s %d/%d",side?"INPUT":"OUTPUT",pair*2+1,pair*2+2);
        button(fb,x+14,150,188,label,0);
        unsigned channels=side?ui->insert_inputs:ui->insert_outputs;
        snprintf(label,sizeof(label),"%u CHANNELS OPEN",channels);text(fb,x+14,184,label,muted,1);
        text(fb,x+14,202,side?"PAIR RESERVED FROM EXT":"MASTER FIXED ON OUTPUT 1/2",muted,1);
        float db=side?ui->config.insert.return_db:ui->config.insert.send_db;
        snprintf(label,sizeof(label),"%s %+.1F DB",side?"RETURN":"SEND",db);
        slider(fb,x+14,225,256,label,(db+24)/36,amber);
        int unity=x+14+(int)(256*24/36.0f);rect(fb,unity,252,1,5,teal);
    }
    int active=ts_router_active(&ui->config.router,TS_ROUTER_INSERT);
    int send=ui->config.insert.send_pair>0 && (unsigned)(ui->config.insert.send_pair+1)*2<=ui->insert_outputs;
    int ret=(unsigned)(ui->config.insert.return_pair+1)*2<=ui->insert_inputs;
    const char *status=!active?"BYPASSED / INTERNAL PATH":!send?"SEND UNAVAILABLE / INSERT SILENT":
        !ret?"RETURN UNAVAILABLE / INSERT SILENT":ui->insert_return_peak<.0001f?"NO RETURN ACTIVITY / CHECK EXTERNAL PATH":"SEND / RETURN CONNECTED";
    text(fb,22,278,status,active?amber:teal,1);
    text(fb,22,296,"RETURN BUFFER: 4 INPUT BLOCKS + DEVICE / EXTERNAL LATENCY",muted,1);
    text(fb,22,311,"NO AUTOMATIC DRY FALLBACK; USE BYPASS TO AUDITION",muted,1);
    button(fb,22,326,92,"BYPASS",(ui->config.router.bypass_mask&(1u<<TS_ROUTER_INSERT))!=0);
    button(fb,128,326,70,"SOLO",ui->config.router.solo==TS_ROUTER_INSERT+1);
    button(fb,420,326,196,"APPLY PORTS",ui->insert_apply_pending);
    const char *help=ui->performance_tooltip[0]?ui->performance_tooltip:"PORT CHANGES: APPLY   LEVEL RESET: RIGHT-CLICK   ESC: ROUTER";
    snprintf(label,sizeof(label),"%.98s",help);text(fb,22,364,label,muted,1);
}
