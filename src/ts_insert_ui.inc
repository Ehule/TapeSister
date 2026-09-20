static const char *insert_help(int x,int y)
{
    if(y>=110 && y<134)return "DEVICE: CLICK / RIGHT-CLICK TO CHOOSE; APPLY PORTS; SAVE CFG TO REMEMBER";
    if(y>=150 && y<174)return x<320?
        "SEND: CHANNELS WITHIN THE SELECTED DEVICE; MASTER SPARES SKIPS MASTER 1/2":
        "RETURN: CHANNELS WITHIN SELECTED DEVICE; IN 5-6 DEVICE USES ITS LOCAL CH 1/2";
    if(y>=219 && y<260)return "LEVEL: -24 TO +12 DB; RIGHT-CLICK RESETS TO UNITY; CHANGES ARE SMOOTHED";
    if(y>=180 && y<198)return "APPLIED STREAM: CHANNELS / SAMPLE RATE / DEVICE BUFFER IN FRAMES; TILES CAN HAVE A DIFFERENT RATE";
    if(y>=308 && y<324)return "QUEUE: PRIMING TARGET, NOT ROUND-TRIP LATENCY; GAPS: UNDERRUNS; DROP: OVERFLOW FRAMES; S / R";
    if(y>=326 && y<350)return x>=420?
        "APPLY DEVICE AND CHANNEL CHOICES; FINISH RECORDING FIRST; MASTER / BACKEND STAY IN CFG":
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
        const char *device=side?ui->insert_return_device:ui->insert_send_device;
        snprintf(label,sizeof(label),"%.39s",*device?device:(side?"SHARED CFG INPUT":"MASTER DEVICE SPARES"));
        button(fb,x+14,110,256,label,0);
        text(fb,x+14,137,"DEVICE: CLICK TO CHOOSE",muted,1);
        int pair=side?ui->insert_return_choice:ui->insert_send_choice;
        if(!side && !pair)snprintf(label,sizeof(label),"SEND UNASSIGNED");
        else {
            int first=pair*2+1-(!side && ui->insert_send_device[0]?2:0);
            snprintf(label,sizeof(label),"DEVICE CH %d/%d",first,first+1);
        }
        button(fb,x+14,150,188,label,0);
        unsigned channels=side?ui->insert_inputs:ui->insert_outputs;
        if(ui->insert_rates[side])snprintf(label,sizeof(label),"%u CH / %u HZ / %u FR",channels,ui->insert_rates[side],ui->insert_buffers[side]);
        else snprintf(label,sizeof(label),"%u CHANNELS OPEN",channels);
        text(fb,x+14,184,label,muted,1);
        const char *applied=side?ui->config.insert_return_device:ui->config.insert_send_device;
        if(!applied[0])applied=side?ui->config.record_input_device:ui->config.audio_output_device;
        snprintf(label,sizeof(label),"APPLIED: %.33s",applied[0]?applied:"SYSTEM DEFAULT");
        text(fb,x+14,202,label,muted,1);
        float db=side?ui->config.insert.return_db:ui->config.insert.send_db;
        snprintf(label,sizeof(label),"%s %+.1F DB",side?"RETURN":"SEND",db);
        slider(fb,x+14,225,256,label,(db+24)/36,amber);
        int unity=x+14+(int)(256*24/36.0f);rect(fb,unity,252,1,5,teal);
    }
    int active=ts_router_active(&ui->config.router,TS_ROUTER_INSERT);
    int send=ui->config.insert.send_pair>0 &&
        (unsigned)(ui->config.insert.send_pair+(ui->config.insert_send_device[0]?0:1))*2<=ui->insert_outputs;
    int ret=(unsigned)(ui->config.insert.return_pair+1)*2<=ui->insert_inputs;
    const char *status=!active?"BYPASSED / INTERNAL PATH":!send?"SEND UNAVAILABLE / INSERT SILENT":
        !ret?"RETURN UNAVAILABLE / INSERT SILENT":ui->insert_return_peak<.0001f?"NO RETURN ACTIVITY / CHECK EXTERNAL PATH":"SEND / RETURN CONNECTED";
    text(fb,22,278,status,active?amber:teal,1);
    if(ui->insert_device_status[0])snprintf(label,sizeof(label),"%.98s",ui->insert_device_status);
    else snprintf(label,sizeof(label),"MASTER %u HZ / %u FR; DEVICE CH 1/2 = FIRST PAIR OF NAMED DEVICE",
        ui->insert_master_rate,ui->insert_master_buffer);
    text(fb,22,296,label,muted,1);
    snprintf(label,sizeof(label),"QUEUE S/R %.1F/%.1F MS   GAPS %u/%u   DROP %u/%u",
        ui->insert_queue_ms[0],ui->insert_queue_ms[1],ui->insert_gaps[0],ui->insert_gaps[1],ui->insert_drops[0],ui->insert_drops[1]);
    text(fb,22,311,label,muted,1);
    button(fb,22,326,92,"BYPASS",(ui->config.router.bypass_mask&(1u<<TS_ROUTER_INSERT))!=0);
    button(fb,128,326,70,"SOLO",ui->config.router.solo==TS_ROUTER_INSERT+1);
    button(fb,420,326,196,"APPLY PORTS",ui->insert_apply_pending);
    const char *help=ui->performance_tooltip[0]?ui->performance_tooltip:"PORT CHANGES: APPLY   LEVEL RESET: RIGHT-CLICK   ESC: ROUTER";
    snprintf(label,sizeof(label),"%.98s",help);text(fb,22,364,label,muted,1);
}
