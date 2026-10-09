static void source_route_output_render(TsFramebuffer *fb,const TsSourceRouteUi *m,const TsPalette *palette)
{
    render_palette=palette;
    const TsSourceRoute *r=&m->route;
    uint32_t dim=RGB(155,148,163),edge=RGB(70,60,78);char label[112];
    int inherit=r->mode==TS_SOURCE_INHERIT,clean=r->mode==TS_SOURCE_PAIR||r->mode==TS_SOURCE_SPEAKER;
    rect(fb,0,0,640,400,RGB(24,21,29));frame(fb,6,6,628,388,RGB(30,26,34),edge);
    snprintf(label,sizeof(label),m->track>=0?"TRACK %02d ROUTING":"TILE %02d ROUTING",m->track>=0?m->track+1:m->slot+1);
    text(fb,18,18,label,PAL_NOTE,1);button(fb,586,9,40,"X",0);
    text(fb,18,48,m->name,PAL_TEXT,1);
    if(m->track>=0)button(fb,444,39,180,inherit?"USE TILE: ON":"USE TILE: OFF",inherit);
    else text(fb,444,48,"TILE DEFAULT",dim,1);
    button(fb,18,78,190,"MAIN ROUTE",r->mode==TS_SOURCE_MAIN);
    button(fb,222,78,190,"CLEAN STEREO PAIR",r->mode==TS_SOURCE_PAIR);
    button(fb,426,78,198,"CLEAN SINGLE SPEAKER",r->mode==TS_SOURCE_SPEAKER);
    frame(fb,18,112,346,192,RGB(23,20,28),edge);
    text(fb,30,124,inherit?"FOLLOW EACH SOUNDING TILE":clean?"DESTINATION":"EXISTING SHARED SIGNAL PATH",PAL_EFFECT,1);
    if(clean) {
        snprintf(label,sizeof(label),r->mode==TS_SOURCE_PAIR?"L: SPEAKER %02d":"SPEAKER %02d",r->speaker+1);
        button(fb,30,144,150,label,0);
        if(r->mode==TS_SOURCE_PAIR){snprintf(label,sizeof(label),"R: SPEAKER %02d",r->second+1);button(fb,198,144,150,label,0);}
        snprintf(label,sizeof(label),"HARDWARE OUT %d",m->array.output[r->speaker]+1);text(fb,30,174,label,dim,1);
        if(r->mode==TS_SOURCE_PAIR){snprintf(label,sizeof(label),"HARDWARE OUT %d",m->array.output[r->second]+1);text(fb,198,174,label,dim,1);}
        if(r->mode==TS_SOURCE_PAIR) {
            snprintf(label,sizeof(label),"PAN / BALANCE  %s%d",r->pan>0?"+":"",r->pan);text(fb,30,202,label,PAL_TEXT,1);
            spatial_bar(fb,32,224,312,(r->pan+100)/200.f,(r->pan+100)/200.f,PAL_MOUSE);
            text(fb,30,240,"LEFT",dim,1);text(fb,162,240,"CENTER",dim,1);text(fb,314,240,"RIGHT",dim,1);
            snprintf(label,sizeof(label),"WIDTH %d%%",r->width+100);text(fb,30,260,label,PAL_TEXT,1);
            spatial_bar(fb,32,285,312,(r->width+100)/200.f,(r->width+100)/200.f,PAL_EFFECT);
        } else {
            text(fb,30,214,"MONO SUM TO ONE SPEAKER",PAL_TEXT,1);
            text(fb,30,239,"STEREO INPUT: (L + R) / 2",dim,1);
            text(fb,30,264,"FIXED POSITION - NO FIELD MOTION",dim,1);
        }
    } else if(inherit) {
        text(fb,30,160,"EACH NOTE USES ITS TILE'S ROUTE.",PAL_TEXT,1);
        text(fb,30,185,"CHOOSE A ROUTE ABOVE TO OVERRIDE.",dim,1);
        text(fb,30,219,"THE TILE ITSELF IS UNCHANGED.",dim,1);
        text(fb,30,264,"APPLIES THROUGHOUT THE SONG.",dim,1);
    } else {
        text(fb,30,160,"SHARED ROUTER AND EFFECTS",PAL_TEXT,1);
        text(fb,30,185,"MASTER EQ / LIMITER / OUT",PAL_TEXT,1);
        text(fb,30,210,"CURRENT AMBISONICS OR STEREO PAIR",dim,1);
        text(fb,30,264,"EXISTING PROJECT SOUND IS PRESERVED.",dim,1);
    }
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
    unsigned wanted=clean?(1u<<r->speaker)|(r->mode==TS_SOURCE_PAIR?1u<<r->second:0):0;
    if(wanted&~m->available)text(fb,18,327,"DESTINATION UNAVAILABLE - THIS ROUTE FALLS BACK TO OUT 1/2",RGB(239,166,101),1);
    else text(fb,18,327,clean?"CLEAN BYPASSES ROUTER + EQ. GLOBAL OUT STILL CONTROLS LEVEL.":inherit?
        "TRACK OVERRIDES TILE ONLY WHEN YOU CHOOSE AN EXPLICIT ROUTE.":"MAIN USES THE CURRENT ROUTER AND OUTPUT SETTINGS.",PAL_TEXT,1);
    text(fb,18,345,clean?"PAIR: PAN/WIDTH APPLY AFTER TRACKER PANNING.  FILE OUT IS STEREO.":
        "ROUTES SAVE WITH THE PROJECT.  ESC CLOSES; PLAYBACK CONTINUES.",dim,1);
    button(fb,18,365,144,m->track>=0?"RESET: USE TILE":"RESET: MAIN",0);
    if(m->can_rename)button(fb,178,365,144,"RENAME TILE",0);
    button(fb,480,365,144,"CLOSE",0);
}

static const char *source_send_name(int bus)
{
    static const char *names[]={"PRISM","PEDALBOARD","FALLOUT"};
    return bus>=0 && bus<TS_SOURCE_SENDS?names[bus]:"CLEAN";
}
static void source_send_render(TsFramebuffer *fb,const TsSourceRouteUi *m)
{
    uint32_t dim=RGB(155,148,163),edge=RGB(70,60,78);char label[100];
    const TsSourceRoute *r=&m->route;
    int inherit=r->mode==TS_SOURCE_INHERIT,enabled=r->mix_enabled&&!inherit;
    rect(fb,0,0,640,400,RGB(24,21,29));frame(fb,6,6,628,388,RGB(30,26,34),edge);
    text(fb,18,48,m->name,PAL_TEXT,1);
    if(m->track>=0)button(fb,444,39,180,inherit?"USE TILE: ON":"USE TILE: OFF",inherit);
    else text(fb,444,48,"TILE DEFAULT",dim,1);
    button(fb,18,78,190,enabled?"CLEAN + SENDS: ON":"CLEAN + SENDS: OFF",enabled);
    text(fb,222,87,inherit?"ENABLE TO OVERRIDE THIS TRACK":enabled?"PARALLEL MIX / MAIN CHAIN BYPASSED":"ENABLE TO SET A PARALLEL MIX",dim,1);
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
            text(fb,111,y,state,dim,1);
        } else text(fb,111,y,"DIRECT",dim,1);
        spatial_bar(fb,226,y+5,320,value*.01f,value*.01f,color);
        snprintf(label,sizeof(label),"%3d%%",value);text(fb,572,y,label,color,1);
    }
    text(fb,18,321,inherit?"EACH NOTE FOLLOWS ITS TILE'S CLEAN AND SEND LEVELS.":
        "CLEAN 0% = EFFECTS ONLY.  SEND LEVELS ARE INDEPENDENT.",PAL_TEXT,1);
    text(fb,18,340,"SELECT RETURNS TO SWITCH A PROCESSOR FROM CHAIN TO SEND.",dim,1);
    button(fb,18,365,144,m->track>=0?"RESET: USE TILE":"RESET: MAIN",0);
    if(m->can_rename)button(fb,178,365,144,"RENAME TILE",0);
    button(fb,480,365,144,"CLOSE",0);
}
static void source_return_render(TsFramebuffer *fb,const TsSourceRouteUi *m,const TsPalette *palette)
{
    uint32_t bg=RGB(30,26,34),dim=RGB(155,148,163);char label[100];
    int bus=m->bus,stage=ts_router_send_stage(bus),send=(m->router.send_mask&(1u<<bus))!=0;
    TsSourceRouteUi placed=*m;placed.route=m->router.return_route[bus];placed.track=-1;placed.can_rename=0;
    source_route_output_render(fb,&placed,palette);
    rect(fb,12,10,616,96,bg);
    snprintf(label,sizeof(label),"%s / SHARED RETURN",source_send_name(bus));text(fb,18,48,label,PAL_TEXT,1);
    button(fb,444,39,180,send?"ROLE: SEND":"ROLE: CHAIN",send);
    for(int i=0;i<3;++i)button(fb,18+i*204,78,i==2?198:190,source_send_name(i),bus==i);
    button(fb,230,116,118,placed.route.mode==TS_SOURCE_PAIR?"STEREO PAIR":"ONE SPEAKER",0);
    rect(fb,18,317,606,44,bg);
    const char *status=!send?"IN SERIAL CHAIN; SOURCE SENDS ARE INACTIVE":
        !ts_router_active(&m->router,stage)?"RETURN BYPASSED BY ROUTER":
        !(m->enabled&(1u<<stage))?"PROCESSOR IS OFF; RETURN IS SILENT":"WET RETURN / NO EXTRA DRY COPY";
    unsigned wanted=(1u<<placed.route.speaker)|(placed.route.mode==TS_SOURCE_PAIR?1u<<placed.route.second:0);
    if(send && (wanted&~m->available))status="MISSING DESTINATION: FALLBACK TO OUT 1/2";
    text(fb,18,324,status,PAL_TEXT,1);
    text(fb,18,344,bus==TS_SEND_PEDALBOARD?"POST SLOTS; PRE/HEAD INSERTS STAY IN SISTER.":"PROCESSOR MIX CONTROLS THE WET AMOUNT.",dim,1);
    snprintf(label,sizeof(label),"RETURN LEVEL  %d%%",m->router.return_level[bus]);text(fb,402,324,label,PAL_EFFECT,1);
    spatial_bar(fb,402,347,200,m->router.return_level[bus]*.01f,m->router.return_level[bus]*.01f,PAL_EFFECT);
    button(fb,18,365,144,"RESET RETURN",0);
}
void ts_source_route_ui_render(TsFramebuffer *fb,const TsSourceRouteUi *m,const TsPalette *palette)
{
    render_palette=palette;
    if(m->page==2)source_return_render(fb,m,palette);
    else if(m->page==1)source_send_render(fb,m);
    else source_route_output_render(fb,m,palette);
    char title[48];
    if(m->track==-2)snprintf(title,sizeof(title),"SHARED EFFECT ROUTING");
    else snprintf(title,sizeof(title),m->track>=0?"TRACK %02d ROUTING":"TILE %02d ROUTING",m->track>=0?m->track+1:m->slot+1);
    text(fb,18,18,title,PAL_NOTE,1);
    if(m->track!=-2) {
        button(fb,250,9,98,"OUTPUT",m->page==0);
        button(fb,358,9,98,"SENDS",m->page==1);
    }
    button(fb,466,9,110,"RETURNS",m->page==2);
    button(fb,586,9,40,"X",0);
}
