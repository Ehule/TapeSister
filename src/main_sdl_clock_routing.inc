/* Clock controls describe tempo, future transport commands and explicit Home
   separately. Editing a route never starts/stops/locates a player. */
static void performance_clock_paint(const TsMasterClockControls *c,const TsMasterClock *runtime,
    const TsTapeHeadPerformance *tracker,unsigned group,unsigned arp_sync,Uint32 text,Uint32 dim,Uint32 accent,Uint32 edge)
{
    char line[128];
    perf_button(20,160,210,34,c->enabled?"MASTER CLOCK ON":"MASTER CLOCK OFF",PERF_BUTTON_MASTER,c->enabled,1);
    perf_button(252,160,40,34,"-",PERF_BUTTON_BPM_MINUS,0,0);
    snprintf(line,sizeof(line),"%u BPM",c->bpm);perf_text(312,170,line,c->source==TS_CLOCK_INTERNAL?text:dim,2);
    perf_button(440,160,40,34,"+",PERF_BUTTON_BPM_PLUS,0,0);
    perf_button(520,160,420,34,c->source==TS_CLOCK_INTERNAL?"SOURCE: INTERNAL":"SOURCE: TRACKSISTER",PERF_BUTTON_CLOCK_SOURCE,c->source==TS_CLOCK_TRACKER_SOURCE,0);
    double bpm=runtime->quarter_seconds>0?60/runtime->quarter_seconds:c->bpm;
    snprintf(line,sizeof(line),"%s / %.2f EFFECTIVE BPM / TRANSPORT %.2f BEATS / PHASE %.2f",
        !c->enabled?"OFF":runtime->running?"RUNNING":"PAUSED",bpm,runtime->beats,runtime->phase);perf_small(28,214,line,dim);
    perf_small(28,248,"DESTINATION",dim);perf_small(240,248,"TEMPO SOURCE",dim);perf_small(472,248,"START / PAUSE",dim);
    perf_small(620,248,"HOME",dim);perf_small(780,248,"PRESET",dim);
    const int channels[]={2,1,0};const char *names[]={"MOSAIC TIMELINE","TRACKSISTER","ARP NOTES"};
    unsigned homes=ts_master_clock_home_mask(c,group);
    for(int row=0;row<3;++row) {
        int y=274+row*60,ch=channels[row];unsigned bit=1u<<ch;
        perf_rect(20,y,920,54,edge);perf_small(28,y+18,names[row],text);
        if(ch==2)perf_small(240,y+18,"SECONDS / TAPE",text);
        else {
            if(ch==1)snprintf(line,sizeof(line),"%s",c->source==TS_CLOCK_TRACKER_SOURCE?"CLOCK SOURCE":c->tempo_mask&bit?"MASTER":"LOCAL SCORE");
            else if(c->tempo_mask&bit)snprintf(line,sizeof(line),"MASTER 1/%u",4u<<c->arp_division);
            else snprintf(line,sizeof(line),"%s",(const char *[]){"LOCAL","TRACKER 1/4","TRACKER 1/8","TRACKER 1/16"}[arp_sync%4]);
            perf_button(232,y+10,212,32,line,PERF_BUTTON_CLOCK_ROW+row*4,0,0);
        }
        perf_rect(474,y+20,12,12,group&bit?accent:dim);perf_small(496,y+20,group&bit?"FOLLOW":"IGNORE",text);
        perf_button(612,y+10,138,32,homes&bit?"FOLLOW":"IGNORE",PERF_BUTTON_CLOCK_ROW+row*4+1,homes&bit,0);
        perf_button(772,y+10,160,32,"INDEPENDENT",PERF_BUTTON_CLOCK_ROW+row*4+2,0,0);
    }
    if(c->source==TS_CLOCK_TRACKER_SOURCE)
        snprintf(line,sizeof(line),"TRACKER %s / CURRENT SCORE TEMPO %d BPM / CANNOT FOLLOW ITSELF",tracker->playing?"PLAYING":"STOPPED",tracker->score_bpm);
    else snprintf(line,sizeof(line),"INTERNAL RANGE 32-255 BPM / TEMPO CHANGES KEEP CURRENT TICK PHASE");
    perf_small(28,470,line,dim);
    perf_small(28,496,"SCHEDULED PATTERN CARDS: SAVED CLOCK + MOSAIC TAPE / CUES STAY LOCAL",dim);
    perf_small(28,520,"FUTURE LOOP CARDS: OWN TEMPO / TRANSPORT / POSITION ROUTES",accent);
}
static int performance_clock_event(SDL_Event *e,SDL_Window *window,SDL_AudioDeviceID device,AudioState *a,
    TsUiState *ui,TsSamplePages *pages,const TsInstrument *bank,int rate)
{
    if(performance_transport.page!=PERF_CLOCKS ||
       (e->type!=SDL_MOUSEBUTTONDOWN && e->type!=SDL_MOUSEWHEEL))return 0;
    int x,y;perf_pointer(e,&x,&y);
    TsMasterClockControls next=a->sister.clock_controls;
    unsigned group=a->sister.performance_transport,arp_sync=a->sister.performance_mixer.arp_sync;
    int source_prepare=0;
    if(y>=160 && y<194) {
        if(e->type==SDL_MOUSEWHEEL && x>=252 && x<480 && next.source==TS_CLOCK_INTERNAL) {
            int delta=e->wheel.y;if(e->wheel.direction==SDL_MOUSEWHEEL_FLIPPED)delta=-delta;
            int bpm=(int)next.bpm+delta;next.bpm=(unsigned)SDL_max(32,SDL_min(255,bpm));
        } else if(e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_LEFT) {
            if(x>=20 && x<230){next.enabled^=1;source_prepare=next.enabled && next.source==TS_CLOCK_TRACKER_SOURCE;}
            else if(((x>=252 && x<292) || (x>=440 && x<480)) && next.source==TS_CLOCK_INTERNAL) {
                int bpm=(int)next.bpm+(x>=440?1:-1);next.bpm=(unsigned)SDL_max(32,SDL_min(255,bpm));
            } else if(x>=520 && x<940) {
                next.source^=1;source_prepare=next.source==TS_CLOCK_TRACKER_SOURCE;
                if(source_prepare)next.tempo_mask&=~TS_CLOCK_TRACKER;
            } else return 0;
        } else return 0;
    } else if(e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_LEFT && y>=274 && y<454 && (y-274)%60<54) {
        int row=(y-274)/60,ch=(int[]){2,1,0}[row];unsigned bit=1u<<ch;
        if(x>=232 && x<444 && ch<2) {
            if(ch==1) {
                if(next.source==TS_CLOCK_TRACKER_SOURCE)return 1;
                next.tempo_mask^=bit;
            } else {
                unsigned choice=(next.tempo_mask&bit)?next.arp_division+1:arp_sync?arp_sync+3:0;
                choice=(choice+1)%7;
                if(choice>=1 && choice<=3){next.tempo_mask|=bit;next.arp_division=choice-1;}
                else {next.tempo_mask&=~bit;arp_sync=choice>=4?choice-3:0;source_prepare=arp_sync!=0;}
            }
        } else if(x>=466 && x<600)group^=bit;
        else if(x>=612 && x<750) {
            next.home_mask=ts_master_clock_home_mask(&next,group);next.position_custom=1;next.home_mask^=bit;
        } else if(x>=772 && x<932) {
            next.home_mask=ts_master_clock_home_mask(&next,group)&~bit;next.position_custom=1;
            group&=~bit;next.tempo_mask&=~bit;if(ch==0)arp_sync=0;
        } else return 0;
    } else return 0;
    if(source_prepare && !embedded_open(window,device,a,ui,pages,bank,rate))return 1;
    if(!ts_master_clock_valid(&next))return 1;
    if(device)SDL_LockAudioDevice(device);
    a->sister.clock_controls=next;a->sister.performance_transport=group;a->sister.performance_mixer.arp_sync=arp_sync;
    if(device)SDL_UnlockAudioDevice(device);
    snprintf(ui->status,sizeof(ui->status),"CLOCK ROUTE UPDATED / NO START, STOP OR SEEK / TEMPO TAKES EFFECT AT AUDIO BOUNDARY");
    performance_transport.painted=SDL_GetTicks()-100;return 1;
}
