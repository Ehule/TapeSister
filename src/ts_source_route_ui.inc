static void source_route_output_render(TsFramebuffer *fb,const TsSourceRouteUi *m,const TsPalette *palette)
{
    render_palette=palette;
    const TsSourceRoute *r=&m->route;
    uint32_t dim=RGB(155,148,163),edge=RGB(70,60,78);char label[112];
    int inherit=r->mode==TS_SOURCE_INHERIT,clean=r->mode==TS_SOURCE_PAIR||r->mode==TS_SOURCE_SPEAKER;
    int master=r->mode==TS_SOURCE_MAIN || (clean && r->mix_enabled && m->router.master_mix);
    rect(fb,0,0,640,400,RGB(24,21,29));frame(fb,6,6,628,388,RGB(30,26,34),edge);
    snprintf(label,sizeof(label),m->track>=0?"TRACK %02d ROUTING":"TILE %02d ROUTING",m->track>=0?m->track+1:m->slot+1);
    text(fb,18,18,label,PAL_NOTE,1);button(fb,586,9,40,"X",0);
    text(fb,18,48,m->name,PAL_TEXT,1);
    if(m->track>=0)button(fb,444,39,180,inherit?"ROUTING: TILE":"ROUTING: TRACK",inherit);
    else if(m->can_rename)button(fb,444,39,180,"RENAME TILE",0);
    else text(fb,444,48,"TILE ROUTING",dim,1);
    button(fb,18,78,190,"MAIN MIX",master);
    button(fb,222,78,190,m->advanced?"DIRECT: STEREO PAIR":"ADVANCED OUTPUTS...",clean&&!master&&r->mode==TS_SOURCE_PAIR);
    if(m->advanced)button(fb,426,78,198,"DIRECT: ONE SPEAKER",clean&&!master&&r->mode==TS_SOURCE_SPEAKER);
    frame(fb,18,112,346,192,RGB(23,20,28),edge);
    text(fb,30,124,inherit?"USING TILE ROUTING":master?"TO MAIN MIX":"DIRECT SPEAKER OUTPUT",PAL_EFFECT,1);
    if(clean || r->mode==TS_SOURCE_MAIN) {
        if(master)text(fb,30,153,r->mode==TS_SOURCE_SPEAKER?"MONO TO MAIN MIX":"STEREO TO MAIN MIX",PAL_TEXT,1);
        else {
            snprintf(label,sizeof(label),r->mode==TS_SOURCE_PAIR?"L: SPEAKER %02d":"SPEAKER %02d",r->speaker+1);
            button(fb,30,144,150,label,0);
            if(r->mode==TS_SOURCE_PAIR){snprintf(label,sizeof(label),"R: SPEAKER %02d",r->second+1);button(fb,198,144,150,label,0);}
        }
        if(master)text(fb,30,174,r->mix_enabled?"CLEAN SOUND + SHARED FX RETURNS":"SHARED FX CHAIN > MAIN MIX",dim,1);
        else {
            snprintf(label,sizeof(label),"HARDWARE OUT %d",m->array.output[r->speaker]+1);text(fb,30,174,label,dim,1);
            if(r->mode==TS_SOURCE_PAIR){snprintf(label,sizeof(label),"HARDWARE OUT %d",m->array.output[r->second]+1);text(fb,198,174,label,dim,1);}
        }
        if(r->mode!=TS_SOURCE_SPEAKER) {
            snprintf(label,sizeof(label),"PAN / BALANCE  %s%d",r->pan>0?"+":"",r->pan);text(fb,30,202,label,PAL_TEXT,1);
            spatial_bar(fb,32,224,312,(r->pan+100)/200.f,(r->pan+100)/200.f,PAL_MOUSE);
            text(fb,30,240,"LEFT",dim,1);text(fb,162,240,"CENTER",dim,1);text(fb,314,240,"RIGHT",dim,1);
            snprintf(label,sizeof(label),"WIDTH %d%%",r->width+100);text(fb,30,260,label,PAL_TEXT,1);
            spatial_bar(fb,32,285,312,(r->width+100)/200.f,(r->width+100)/200.f,PAL_EFFECT);
        } else {
            text(fb,30,214,master?"MONO SUM TO MASTER":"MONO SUM TO ONE SPEAKER",PAL_TEXT,1);
            text(fb,30,239,"STEREO INPUT: (L + R) / 2",dim,1);
            text(fb,30,264,"FIXED POSITION - NO FIELD MOTION",dim,1);
        }
    } else if(inherit) {
        text(fb,30,160,"EACH NOTE USES ITS TILE'S ROUTE.",PAL_TEXT,1);
        text(fb,30,185,"CLICK ROUTING: TILE TO USE TRACK.",dim,1);
        text(fb,30,219,"PAN, OUTPUT AND SENDS COME FROM TILE.",dim,1);
        text(fb,30,264,"MAIN MIX CREATES A TRACK OVERRIDE.",dim,1);
    } else {
        text(fb,30,160,"SHARED ROUTER AND EFFECTS",PAL_TEXT,1);
        text(fb,30,185,"MASTER EQ / LIMITER / OUT",PAL_TEXT,1);
        text(fb,30,210,"CURRENT AMBISONICS OR STEREO PAIR",dim,1);
        text(fb,30,264,"EXISTING PROJECT SOUND IS PRESERVED.",dim,1);
    }
    if(master) {
        text(fb,394,139,"MAIN MIX PATH",PAL_EFFECT,1);
        text(fb,394,171,m->router.master_mix?"MASTER INSERT":"INSERT IN FX CHAIN",PAL_TEXT,1);
        text(fb,394,195,ts_router_active(&m->router,TS_ROUTER_INSERT)?"EXTERNAL LOOP ACTIVE":"INSERT BYPASSED",dim,1);
        text(fb,394,227,"EQ > LIMITER > OUT",PAL_TEXT,1);
        text(fb,394,261,"STEREO / OUTPUT FIELD",dim,1);
    } else if(inherit) {
        text(fb,394,151,"NOTE > TILE ROUTE",PAL_EFFECT,1);
        text(fb,394,185,"PAN / OUTPUT / SENDS",PAL_TEXT,1);
        text(fb,394,219,"SET ON EACH TILE",dim,1);
    } else {
    spatial_grid(fb,494,204);
    for(int i=0;i<m->array.speakers;++i) {
        float a=m->array.azimuth[i]*3.14159265358979323846f/180.f;
        int x=494-(int)(80*sinf(a)),y=204-(int)(80*cosf(a));
        int selected=clean&&(i==r->speaker||(r->mode==TS_SOURCE_PAIR&&i==r->second));
        uint32_t color=selected?PAL_MOUSE:dim;
        if(!(m->available&(1u<<i)))color=RGB(193,127,87);
        frame(fb,x-10,y-8,20,16,RGB(23,20,28),color);
        snprintf(label,sizeof(label),"%d",i+1);text(fb,x-(i<9?3:6),y-3,label,color,1);
        snprintf(label,sizeof(label),"OUT %d",m->array.output[i]+1);text(fb,x-(int)strlen(label)*3,y+13,label,dim,1);
    }
    text(fb,402,306,"SPEAKER ARRAY MAPPING",dim,1);
    }
    unsigned wanted=clean?(1u<<r->speaker)|(r->mode==TS_SOURCE_PAIR?1u<<r->second:0):0;
    if(!inherit && !master && (wanted&~m->available))text(fb,18,327,"DESTINATION UNAVAILABLE - THIS ROUTE FALLS BACK TO OUT 1/2",RGB(239,166,101),1);
    else text(fb,18,327,master?"MAIN MIX INCLUDES EQ + LIMITER. INSERT CAN BE ENABLED OR BYPASSED.":clean?"DIRECT OUTPUT BYPASSES MASTER INSERT + EQ + MASTER LIMITER.":inherit?
        "TRACK OVERRIDES TILE ONLY WHEN YOU CHOOSE AN EXPLICIT ROUTE.":"MAIN USES THE CURRENT ROUTER AND OUTPUT SETTINGS.",PAL_TEXT,1);
    text(fb,18,345,inherit?"TILE ROUTES MAY DIFFER FROM NOTE TO NOTE. CLICK MAIN MIX TO OVERRIDE.":m->track<0?
        "TRACK ROUTES OVERRIDE TILE SETTINGS. USE ROUTING: TILE TO INHERIT.":
        "PAN/WIDTH APPLY LIVE. RIGHT-CLICK TO RESET; WHEEL TO ADJUST.",dim,1);
    button(fb,18,365,144,"RESET: MAIN MIX",0);
    button(fb,178,365,286,"MAIN MIX / MASTER INSERT...",0);
    button(fb,480,365,144,"CLOSE",0);
}

static const char *source_send_name(int bus)
{
    static const char *names[]={"PRISM","PEDALBOARD","FALLOUT"};
    return bus>=0 && bus<TS_SOURCE_SENDS?names[bus]:"CLEAN SOUND";
}
static void source_send_render(TsFramebuffer *fb,const TsSourceRouteUi *m)
{
    uint32_t dim=RGB(155,148,163),edge=RGB(70,60,78);char label[100];
    const TsSourceRoute *r=&m->route;
    int inherit=r->mode==TS_SOURCE_INHERIT,enabled=r->mix_enabled&&!inherit;
    rect(fb,0,0,640,400,RGB(24,21,29));frame(fb,6,6,628,388,RGB(30,26,34),edge);
    text(fb,18,48,m->name,PAL_TEXT,1);
    if(m->track>=0)button(fb,444,39,180,inherit?"ROUTING: TILE":"ROUTING: TRACK",inherit);
    else if(m->can_rename)button(fb,444,39,180,"RENAME TILE",0);
    else text(fb,444,48,"TILE ROUTING",dim,1);
    button(fb,18,78,190,enabled?"PATH: EFFECT SENDS":"PATH: SHARED CHAIN",enabled);
    text(fb,222,87,inherit?"CLICK TO SET TRACK SENDS":enabled?"CLEAN + SEPARATE SHARED EFFECTS":"CLICK TO SET INDIVIDUAL FX AMOUNTS",dim,1);
    text(fb,30,119,"SENDS TAP BEFORE CLEAN LEVEL, PAN AND WIDTH",PAL_EFFECT,1);
    frame(fb,18,136,606,167,RGB(23,20,28),edge);
    for(int row=0;row<4;++row) {
        int y=147+row*39,bus=row-1,value=row?r->send_level[bus]:r->clean_level;
        uint32_t color=enabled?row?PAL_EFFECT:PAL_MOUSE:dim;
        text(fb,30,y,source_send_name(bus),color,1);
        if(row) {
            int stage=ts_router_send_stage(bus);
            const char *state=!(m->router.send_mask&(1u<<bus))?"CHAIN":
                !ts_router_active(&m->router,stage)?"BYPASS":!(m->enabled&(1u<<stage))?"OFF":"SEND";
            button(fb,111,y-7,102,!(m->router.send_mask&(1u<<bus))?"IN CHAIN >":state,0);
        } else text(fb,124,y,m->router.master_mix?"MAIN MIX":"DIRECT",dim,1);
        spatial_bar(fb,226,y+5,320,value*.01f,value*.01f,color);
        snprintf(label,sizeof(label),"%3d%%",value);text(fb,572,y,label,color,1);
    }
    text(fb,18,321,inherit?"EACH NOTE FOLLOWS ITS TILE'S CLEAN AND SEND LEVELS.":m->track<0?
        "TRACK SENDS OVERRIDE THESE UNLESS ROUTING: TILE IS SELECTED.":
        "CLEAN 0% = EFFECTS ONLY.  SEND LEVELS ARE INDEPENDENT.",PAL_TEXT,1);
    text(fb,18,340,"IN CHAIN > OPENS FX SETUP. ONE SHARED INSTANCE OF EACH EFFECT.",dim,1);
    button(fb,18,365,144,"RESET: MAIN MIX",0);
    button(fb,178,365,286,"MAIN MIX / MASTER INSERT...",0);
    button(fb,480,365,144,"CLOSE",0);
}
static void source_return_render(TsFramebuffer *fb,const TsSourceRouteUi *m,const TsPalette *palette)
{
    uint32_t bg=RGB(30,26,34),dim=RGB(155,148,163);char label[100];
    int bus=m->bus,stage=ts_router_send_stage(bus),send=(m->router.send_mask&(1u<<bus))!=0;
    TsSourceRouteUi placed=*m;placed.route=m->router.return_route[bus];placed.track=-1;placed.can_rename=0;
    placed.route.mix_enabled=1;
    source_route_output_render(fb,&placed,palette);
    rect(fb,12,10,616,96,bg);
    snprintf(label,sizeof(label),"%s / SHARED RETURN",source_send_name(bus));text(fb,18,48,label,PAL_TEXT,1);
    button(fb,444,39,180,send?"USE: SHARED SEND":"USE: SHARED CHAIN",send);
    for(int i=0;i<3;++i)button(fb,18+i*204,78,i==2?198:190,source_send_name(i),bus==i);
    button(fb,230,116,118,placed.route.mode==TS_SOURCE_PAIR?"STEREO PAIR":m->router.master_mix?"MONO SUM":"ONE SPEAKER",0);
    rect(fb,18,317,606,44,bg);
    const char *status=!send?"IN CHAIN: PER-SOURCE SENDS INACTIVE":
        !ts_router_active(&m->router,stage)?"RETURN BYPASSED BY ROUTER":
        !(m->enabled&(1u<<stage))?"PROCESSOR OFF: RETURN SILENT":"EFFECT RETURN ONLY / NO DRY COPY";
    unsigned wanted=(1u<<placed.route.speaker)|(placed.route.mode==TS_SOURCE_PAIR?1u<<placed.route.second:0);
    if(send && !m->router.master_mix && (wanted&~m->available))status="MISSING DESTINATION: FALLBACK TO OUT 1/2";
    text(fb,18,324,status,PAL_TEXT,1);
    text(fb,18,344,bus==TS_SEND_PEDALBOARD?"POST SLOTS; PRE/HEAD INSERTS STAY IN SISTER.":"USE SETTING AFFECTS ALL TILES / TRACKS.",dim,1);
    snprintf(label,sizeof(label),"RETURN LEVEL  %d%%",m->router.return_level[bus]);text(fb,402,324,label,PAL_EFFECT,1);
    spatial_bar(fb,402,347,200,m->router.return_level[bus]*.01f,m->router.return_level[bus]*.01f,PAL_EFFECT);
    button(fb,18,365,144,"RESET RETURN",0);
    button(fb,178,365,286,"MAIN MIX / MASTER INSERT...",0);
}
static void source_main_mix_render(TsFramebuffer *fb,const TsSourceRouteUi *m)
{
    uint32_t dim=RGB(155,148,163),edge=RGB(70,60,78);
    rect(fb,0,0,640,400,RGB(24,21,29));frame(fb,6,6,628,388,RGB(30,26,34),edge);
    text(fb,18,48,"GLOBAL: CLEAN SOUND AND SHARED FX RETURNS",PAL_TEXT,1);
    button(fb,18,78,350,"MAIN MIX: CLEAN + FX RETURNS",m->router.master_mix);
    button(fb,384,78,240,"ADVANCED: DIRECT SPEAKERS",!m->router.master_mix);
    const char *names[]={"CLEAN SOUND + FX RETURNS","MASTER INSERT","MASTER EQ","LIMITER","MAIN OUTPUT"};
    const char *detail[]={"COLLECTED INTO ONE STEREO MIX",
        ts_router_active(&m->router,TS_ROUTER_INSERT)?"ON: EXTERNAL RETURN REPLACES MIX":"BYPASSED: MIX CONTINUES INTERNALLY",
        "TONE SHAPING FOR THE COMPLETE MIX","FINAL LEVEL PROTECTION","OUT LEVEL > STEREO / SPEAKER FIELD"};
    for(int i=0;i<5;++i) {
        int y=116+i*37;
        frame(fb,18,y,606,31,RGB(23,20,28),edge);
        text(fb,30,y+12,names[i],i==1?PAL_MOUSE:PAL_EFFECT,1);
        text(fb,238,y+12,detail[i],dim,1);
    }
    if(!m->router.master_mix) {
        rect(fb,22,120,598,176,RGB(23,20,28));
        text(fb,30,135,"ADVANCED DIRECT MIX",PAL_MOUSE,1);
        text(fb,30,167,"CLEAN SOUND + FX RETURNS GO TO THEIR ASSIGNED SPEAKERS.",PAL_TEXT,1);
        text(fb,30,199,"THEY BYPASS MASTER INSERT, EQ AND MASTER LIMITER.",PAL_TEXT,1);
        text(fb,30,231,"SHARED CHAIN STILL USES EQ / LIMITER; INSERT STAYS IN CHAIN.",dim,1);
        text(fb,30,267,"SELECT MAIN MIX ABOVE TO INCLUDE FINAL PROCESSING.",dim,1);
    }
    text(fb,18,318,"EXPLICIT DIRECT TILE / TRACK OUTPUTS KEEP THEIR OWN DESTINATIONS.",dim,1);
    text(fb,18,342,"MIX CHANGES APPLY LIVE. SAVE THE PROJECT TO KEEP THEM.",PAL_TEXT,1);
    button(fb,18,365,200,"SET UP MASTER INSERT...",0);
    button(fb,234,365,230,"BACK TO ROUTING",0);
    button(fb,480,365,144,"CLOSE",0);
}
void ts_source_route_ui_render(TsFramebuffer *fb,const TsSourceRouteUi *m,const TsPalette *palette)
{
    render_palette=palette;
    if(m->page==3)source_main_mix_render(fb,m);
    else if(m->page==2)source_return_render(fb,m,palette);
    else if(m->page==1)source_send_render(fb,m);
    else source_route_output_render(fb,m,palette);
    char title[48];
    if(m->page==3)snprintf(title,sizeof(title),"MAIN MIX / MASTER");
    else if(m->track==-2)snprintf(title,sizeof(title),"SHARED EFFECT ROUTING");
    else snprintf(title,sizeof(title),m->track>=0?"TRACK %02d ROUTING":"TILE %02d ROUTING",m->track>=0?m->track+1:m->slot+1);
    text(fb,18,18,title,PAL_NOTE,1);
    if(m->track!=-2) {
        button(fb,250,9,98,"PAN / OUT",m->page==0);
        button(fb,358,9,98,"SENDS",m->page==1);
    }
    button(fb,466,9,110,"FX RETURNS",m->page==2);
    button(fb,586,9,40,"X",0);
}
