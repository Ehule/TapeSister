void ts_source_route_ui_render(TsFramebuffer *fb,const TsSourceRouteUi *m,const TsPalette *palette)
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
