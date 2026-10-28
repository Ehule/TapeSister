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
static void mosaic_live_center_text(TsFramebuffer *fb,int x,int y,const char *label,uint32_t color)
{text(fb,x+83-(int)strlen(label)*3,y,label,color,1);}
static void mosaic_live_circle(TsFramebuffer *fb,int x,int y,int radius,double sx,double sy,uint32_t fill)
{
    int rows=(int)floor(radius*sy);
    for(int row=-rows;row<=rows;++row) {
        double at=row/sy;
        int half=(int)floor(sqrt(fmax(0,radius*radius-at*at))*sx);
        rect(fb,x-half,y+row,2*half+1,1,fill);
    }
}
static void mosaic_live_ring(TsFramebuffer *fb,int x,int y,double sx,double sy,double progress,uint32_t color,uint32_t dim)
{
    /* Cached UI geometry, independent of the audio callback. */
    static int ready;static short points[128][2];
    if(!ready) {
        for(int i=0;i<128;++i) {
            double a=i*(6.283185307179586/128)-1.5707963267948966;
            points[i][0]=(short)lrint(cos(a)*(TS_MOSAIC_LIVE_RADIUS-2));
            points[i][1]=(short)lrint(sin(a)*(TS_MOSAIC_LIVE_RADIUS-2));
        }
        ready=1;
    }
    for(int i=0;i<128;++i) {
        int a=(int)lrint((points[i][0]-2)*sx),b=(int)lrint((points[i][0]+3)*sx);
        int top=(int)lrint((points[i][1]-2)*sy),bottom=(int)lrint((points[i][1]+3)*sy);
        rect(fb,x+a,y+top,b-a,bottom-top,i<progress*128?color:dim);
    }
}
static void mosaic_live_transport_icon(TsFramebuffer *fb,int x,int y,int index,int active,uint32_t ink,uint32_t panel)
{
    uint32_t edge=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_EDGE,RGB(64,54,70));
    rect(fb,x,y,20,20,edge);rect(fb,x+2,y+2,16,16,active?palette_blend(panel,ink,15):panel);
    if(index==3) {rect(fb,x+5,y+5,3,10,ink);rect(fb,x+12,y+5,3,10,ink);return;}
    if(index==4) {rect(fb,x+5,y+5,10,10,ink);return;}
    int left=index<2;
    if(index==0 || index==6) {
        for(int step=0;step<2;++step)for(int i=0;i<5;++i) {
            int half=index==0?i:4-i;
            rect(fb,x+4+step*7+i,y+10-half,1,2*half+1,ink);
        }
        return;
    }
    for(int i=0;i<7;++i) {
        int px=x+6+i;
        int half=left?i:6-i;
        rect(fb,px,y+10-half,1,2*half+1,ink);
    }
}
static void mosaic_live_wave(TsFramebuffer *fb,int x,int y,const TsUiState *ui,const TsMosaicEvent *e,uint32_t color)
{
    rect(fb,x,y,150,18,ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_BACKGROUND,RGB(11,13,16)));
    const TsMosaicRecordPreview *p=&ui->mosaic_live_preview;
    for(int column=0;column<148;++column) {
        float peak=0;
        if(ui->mosaic_live_recording && p->frames && p->frames_per_peak) {
            size_t a=(size_t)((long double)p->frames*column/148)/p->frames_per_peak;
            size_t b=(size_t)((long double)p->frames*(column+1)/148)/p->frames_per_peak;
            if(b>=TS_MOSAIC_RECORD_PEAKS)b=TS_MOSAIC_RECORD_PEAKS-1;
            for(size_t at=a;at<=b && at<TS_MOSAIC_RECORD_PEAKS;++at)peak=fmaxf(peak,p->peaks[at]);
        } else if(!ui->mosaic_live_recording && e && e->source && !e->source->pattern && e->source->sample.frames) {
            size_t first=e->first+(size_t)((long double)(e->last-e->first)*column/148);
            size_t bin=(size_t)((long double)first*TS_MOSAIC_PEAKS/e->source->sample.frames);
            if(bin<TS_MOSAIC_PEAKS)peak=e->source->peaks[bin];
        }
        int half=(int)lrintf(fminf(1,peak)*7);
        rect(fb,x+1+column,y+9-half,1,2*half+1,color);
    }
}
/* Both renderers use the same UI snapshot and timing. A stopped selection
   must not hide another running loop, and capture setup is never play time. */
enum { MOSAIC_DISC_STOP, MOSAIC_DISC_RECORD, MOSAIC_DISC_PREPARE,
       MOSAIC_DISC_PLAY, MOSAIC_DISC_WAIT, MOSAIC_DISC_RELEASE };
typedef struct {
    const TsMosaicEvent *event;
    int state;
    uint32_t disc,ring;
    double phase,remaining;
    const char *label,*timer;
} MosaicLooperDisplay;
static const TsMosaicEvent *mosaic_looper_event(const TsUiState *ui)
{
    const TsMosaicEvent *selected=ts_mosaic_find(ui->mosaic,ui->mosaic_selected);
    const TsMosaicEvent *sequence=ui->mosaic_sequence_running?ts_mosaic_find(ui->mosaic,ui->mosaic_sequence_id):NULL;
    if(sequence)return sequence;
    const TsMosaicLive *l=selected && selected->live?mosaic_live_snapshot(ui,selected):NULL;
    if(l && l->id==selected->id && l->state && l->state!=TS_MOSAIC_LIVE_WAITING)return selected;
    const TsMosaicEvent *best=selected;int priority=0;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        const TsMosaicEvent *e=&ui->mosaic->events[i];const TsMosaicLive *l=&ui->mosaic_live[i];
        if(!e->id || !e->live || l->id!=e->id || !l->state)continue;
        int rank=l->state==TS_MOSAIC_LIVE_WAITING?1:3;
        if(e==selected)++rank;
        if(rank>priority){priority=rank;best=e;}
    }
    return best;
}
static MosaicLooperDisplay mosaic_looper_display(const TsUiState *ui)
{
    MosaicLooperDisplay d={.event=mosaic_looper_event(ui),.state=MOSAIC_DISC_STOP,
        .disc=RGB(78,88,105),.ring=RGB(161,177,199),.label="STOP",.timer="STOPPED"};
    const TsMosaicEvent *e=d.event;
    const TsMosaicLive *l=e && e->live?mosaic_live_snapshot(ui,e):NULL;
    int state=l && l->id==e->id?l->state:TS_MOSAIC_LIVE_STOPPED;
    if(ui->mosaic_live_recording) {
        d.state=MOSAIC_DISC_RECORD;d.disc=RGB(229,24,32);d.ring=RGB(255,139,119);d.label="RECORD";
        if(ui->mosaic_live_duration>0) {
            d.phase=ui->mosaic_live_seconds/ui->mosaic_live_duration;
            d.remaining=fmax(0,ui->mosaic_live_duration-ui->mosaic_live_seconds);d.timer="TAKE LEFT";
        } else {d.remaining=ui->mosaic_live_seconds;d.timer="REC / MANUAL";}
    } else if(ui->mosaic_live_finishing) {
        d.state=MOSAIC_DISC_PREPARE;d.disc=RGB(151,101,28);d.ring=RGB(255,200,106);d.label="PREP";d.timer="PREPARING";
    } else if(state==TS_MOSAIC_LIVE_WAITING) {
        d.state=MOSAIC_DISC_WAIT;d.disc=RGB(151,101,28);d.ring=RGB(255,200,106);d.label="WAIT";d.timer="WAITING BEAT";
    } else if(state) {
        d.state=state==TS_MOSAIC_LIVE_RELEASING?MOSAIC_DISC_RELEASE:MOSAIC_DISC_PLAY;
        d.disc=RGB(30,139,83);d.ring=RGB(129,221,162);d.label=d.state==MOSAIC_DISC_RELEASE?"END":"PLAY";
        double cycle=ts_mosaic_live_cycle(e),speed=1;
        if(e->live_tempo && ui->mosaic_live_master && e->live_beats>0 && ui->mosaic_live_quarter>0)
            speed=fmax(.03125,fmin(32,cycle/(e->live_beats*ui->mosaic_live_quarter)));
        if(ui->mosaic_sequence_running || e->live_finite) {
            double duration=ui->mosaic_sequence_running?ui->mosaic_sequence_duration:e->duration;
            d.phase=duration>0?l->elapsed/duration:0;
            d.remaining=fmax(0,(duration-l->elapsed)/speed);
            d.timer=ui->mosaic_sequence_running?"STEP LEFT":"LOOP LEFT";
        } else {
            d.phase=l->phase;
            double pitch=ts_tuning_note_pitch(&e->tuning,e->notes[0]-60);
            d.remaining=cycle*(1-d.phase)/fmax(.000001,speed*pitch);d.timer="CYCLE LEFT";
        }
        if(state==TS_MOSAIC_LIVE_RELEASING)d.timer="RELEASING";
    }
    d.phase=fmax(0,fmin(1,d.phase));return d;
}
/* Symbol coordinates in logical circle units, shared with native coverage. */
static int mosaic_looper_symbol(int state,double x,double y)
{
    if(state==MOSAIC_DISC_PLAY || state==MOSAIC_DISC_RELEASE)
        return x>=-8 && x<=12 && fabs(y+2)<=(12-x)*.6;
    if(state==MOSAIC_DISC_WAIT)
        return y>=-12 && y<=8 && ((x>=-9 && x<=-3) || (x>=3 && x<=9));
    return state==MOSAIC_DISC_STOP && x>=-9 && x<=9 && y>=-12 && y<=6;
}
static void mosaic_looper_disc_label(TsFramebuffer *fb,int x,int y,double sx,double sy,const MosaicLooperDisplay *d,const TsUiState *ui)
{
    if(d->state==MOSAIC_DISC_RECORD || d->state==MOSAIC_DISC_PREPARE) {
        mosaic_live_center_text(fb,x,y+78,d->state==MOSAIC_DISC_PREPARE?"...":d->label,RGB(255,255,255));
    } else {
        for(int row=(int)floor(-14*sy);row<=(int)ceil(11*sy);++row)
            for(int col=(int)floor(-12*sx);col<=(int)ceil(14*sx);++col)
                if(mosaic_looper_symbol(d->state,col/sx,row/sy))rect(fb,x+83+col,y+86+row,1,1,RGB(255,255,255));
    }
    char label[24];
    if(d->state==MOSAIC_DISC_RECORD) {
        /* The configured take length belongs only to the recording state. */
        if(ui->mosaic->live_capture.length)snprintf(label,sizeof(label),"%d %s",ui->mosaic->live_capture.length,ui->mosaic->live_capture.units?"BEATS":"SEC");
        else snprintf(label,sizeof(label),"MANUAL");
    } else snprintf(label,sizeof(label),"%s",d->label);
    if(sx>=.65 && sy>=.65)mosaic_live_center_text(fb,x,y+86+(int)lrint(12*sy),label,RGB(255,255,255));
}
static void mosaic_live_compact(TsFramebuffer *fb,const TsUiState *ui,
    const char *keyboard,uint32_t ink,uint32_t accent,uint32_t dim,uint32_t panel)
{
    const TsMosaic *m=ui->mosaic;const TsMosaicEvent *selected=ts_mosaic_find(ui->mosaic,ui->mosaic_selected);
    MosaicLooperDisplay display=mosaic_looper_display(ui);const TsMosaicEvent *e=display.event;
    const TsMosaicLive *l=e && e->live?mosaic_live_snapshot(ui,e):NULL;
    int state=l && l->id==e->id?l->state:TS_MOSAIC_LIVE_STOPPED;
    int capture=ui->mosaic_live_recording,finishing=ui->mosaic_live_finishing;
    int x,y;ts_ui_mosaic_live_origin(ui,&x,&y);
    rect(fb,x,y,TS_MOSAIC_LIVE_W,TS_MOSAIC_LIVE_H,accent);
    rect(fb,x+2,y+2,TS_MOSAIC_LIVE_W-4,TS_MOSAIC_LIVE_H-4,panel);
    char line[80];snprintf(line,sizeof(line),"LOOPER / %d ON",ui->mosaic_live_loops);text(fb,x+8,y+9,line,ink,1);
    mosaic_button(fb,x+144,y+4,14,"X",0,1);
    if(!m->live_capture.source)snprintf(line,sizeof(line),"EXT %s / MON %s",ui->config.record_input_channel==TS_INPUT_CHANNEL_STEREO?"L+R":ui->config.record_input_channel==TS_INPUT_CHANNEL_RIGHT?"R":"L",ui->monitor_enabled?"ON":"OFF");
    else snprintf(line,sizeof(line),"SRC: %s",m->live_capture.source==1?"DRY KEYS":"OUTPUT + LOOPS");
    text(fb,x+8,y+25,line,m->live_capture.source==2?accent:dim,1);
    double sx,sy;ts_ui_mosaic_live_circle_scale(ui,&sx,&sy);
    mosaic_live_ring(fb,x+83,y+86,sx,sy,display.phase,display.ring,ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_EDGE,RGB(64,54,70)));
    mosaic_live_circle(fb,x+83,y+86,31,sx,sy,display.disc);
    mosaic_looper_disc_label(fb,x,y,sx,sy,&display,ui);
    snprintf(line,sizeof(line),"%05.1fS %s",display.remaining,display.timer);mosaic_live_center_text(fb,x,y+135,line,ink);
    if((capture || finishing) && ui->mosaic_sequence_running)snprintf(line,sizeof(line),"%s / %s %d OF %d",capture?"REC":"PREP",ui->mosaic_sequence_finishing?"END":"CYCLE",ui->mosaic_sequence_index+1,ui->mosaic_sequence_count);
    else if(capture || finishing)snprintf(line,sizeof(line),"%s / %s",m->live_capture.continuous?"CONTINUOUS":"SINGLE",capture?"RECORDING":"FINISHING");
    else if(ui->mosaic_sequence_running)snprintf(line,sizeof(line),"%s / LOOP %d OF %d",ui->mosaic_sequence_finishing?"FINISHING":"CYCLING",ui->mosaic_sequence_index+1,ui->mosaic_sequence_count);
    else if(e)snprintf(line,sizeof(line),"CARD #%llu / %s",(unsigned long long)e->id,e->muted?"MUTED":e->solo?"SOLO":e->live?"LOOP EVENT":"TIMELINE");
    else snprintf(line,sizeof(line),"SELECT A CARD / REC ADDS ONE");
    char short_line[26];snprintf(short_line,sizeof(short_line),"%.25s",line);
    mosaic_live_center_text(fb,x,y+147,short_line,dim);
    mosaic_live_wave(fb,x+8,y+160,ui,e,capture?RGB(255,139,149):accent);
    for(int i=0;i<TS_MOSAIC_LIVE_TRANSPORT_COUNT;++i) {
        int active=i==0?ui->mosaic_sequence_running && !ui->mosaic_sequence_finishing:i==6?ui->mosaic_sequence_finishing:
            i==2?(ui->mosaic_live_play_all?ui->mosaic_live_loops>0:state!=0):i==3?capture:0;
        mosaic_live_transport_icon(fb,x+8+i*22,y+183,i,active,i==4?RGB(255,151,151):ink,panel);
    }
    snprintf(short_line,sizeof(short_line),"%.25s",ui->mosaic_live_hover[0]?ui->mosaic_live_hover:"CLICK DISC: RECORD / NEXT");
    mosaic_live_center_text(fb,x,y+207,short_line,accent);
    mosaic_button(fb,x+8,y+220,72,"",0,1);
    rect(fb,x+10+(ui->mosaic_live_play_all?35:0),y+222,33,12,palette_blend(panel,ink,15));
    text(fb,x+16,y+225,"ONE",ui->mosaic_live_play_all?dim:ink,1);
    text(fb,x+51,y+225,"ALL",ui->mosaic_live_play_all?ink:dim,1);
    mosaic_button(fb,x+86,y+220,72,selected && selected->source->pattern?"PATTERN":"LOOP EVENT",selected && selected->live,1);
    mosaic_button(fb,x+8,y+244,72,ui->mosaic_live_keys?"KEYS ON":"EDIT",ui->mosaic_live_keys,1);
    mosaic_button(fb,x+86,y+244,72,"ADVANCED",0,1);
    snprintf(line,sizeof(line),"%.24s",keyboard);mosaic_live_center_text(fb,x,y+266,line,dim);
    detail_opaque_panel(fb,x,y,TS_MOSAIC_LIVE_W,TS_MOSAIC_LIVE_H);
}
#include "ts_mosaic_sequence_ui.inc"
#include "ts_mosaic_looper_circle.inc"
static void mosaic_live_overlay(TsFramebuffer *fb,const TsUiState *ui,const TsInstrument *instrument)
{
    if(!ui->mosaic_live_open || !ui->mosaic || ts_ui_foreground_panel_open(ui))return;
    const TsMosaic *m=ui->mosaic;const TsMosaicEvent *e=ts_mosaic_find(ui->mosaic,ui->mosaic_selected);
    uint32_t ink=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_TEXT,RGB(224,214,233));
    uint32_t accent=RGB(245,184,91),dim=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_DIM,RGB(178,163,189));
    uint32_t panel=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_PANEL,RGB(31,28,38));
    char keyboard[48];snprintf(keyboard,sizeof(keyboard),"%s",ui->mosaic_live_keyboard);
    if(!keyboard[0])snprintf(keyboard,sizeof(keyboard),"TILE %02d / %s",instrument->selected_slot+1,ts_audition_source_name(ui->audition_source));
    if(!ui->mosaic_live_advanced){mosaic_live_compact(fb,ui,keyboard,ink,accent,dim,panel);return;}
    if(ui->mosaic_live_advanced==2){mosaic_sequence_setup(fb,ui,e,ink,accent,dim,panel);return;}
    rect(fb,120,78,510,272,accent);rect(fb,122,80,506,268,panel);
    char line[120];snprintf(line,sizeof(line),"LIVE LOOPS / %d ACTIVE / %s %.2FS",ui->mosaic_live_loops,
        ui->mosaic_live_recording?"RECORDING":ui->mosaic_live_finishing?"FINISHING":"IDLE",ui->mosaic_live_seconds);
    text(fb,132,88,line,ui->mosaic_live_recording?accent:ink,1);
    mosaic_button(fb,500,84,108,"SEQUENCE",0,1);
    const char *buttons[]={ui->mosaic_live_recording?"NEXT LAYER":"RECORD","STOP RECORDING","DROP PART","STOP LOOPS"};
    for(int i=0;i<4;++i)mosaic_button(fb,132+i*104,106,96,buttons[i],i==0 && ui->mosaic_live_recording,1);
    mosaic_button(fb,132,140,134,(const char *[]){"SRC: EXT INPUT","SRC: DRY KEYS","SRC: OUTPUT"}[m->live_capture.source],0,1);
    mosaic_button(fb,276,140,130,m->live_capture.continuous?"CONTINUOUS ON":"SINGLE TAKE",m->live_capture.continuous,1);
    if(m->live_capture.length)snprintf(line,sizeof(line),"LENGTH %d %s",m->live_capture.length,m->live_capture.units?"BEATS":"SEC");
    else snprintf(line,sizeof(line),"LENGTH MANUAL");
    mosaic_button(fb,416,140,192,line,0,1);
    mosaic_button(fb,132,166,150,m->live_capture.quantize?"LAUNCH NEXT BEAT":"LAUNCH NOW",m->live_capture.quantize,1);
    mosaic_button(fb,292,166,126,ui->monitor_enabled?"MONITOR ON":"MONITOR OFF",ui->monitor_enabled,1);
    mosaic_button(fb,424,166,96,"COPY WAV",0,1);
    mosaic_button(fb,526,166,82,"SIMPLE",0,1);
    snprintf(line,sizeof(line),m->live_capture.source==2?"OUTPUT CAPTURES PLAYING LOOPS TOO / FEEDBACK LAYERS":
        m->live_capture.source==1?"DRY KEYBOARD / EXCLUDES PLAYING MOSAIC LOOPS":
        "EXT: %s / MONITOR DOES NOT CHANGE THE RECORDED TAP",ts_input_channel_mode_name(ui->config.record_input_channel));
    text(fb,132,192,line,dim,1);
    text(fb,132,206,"LENGTH: CLICK / WHEEL 1 / RIGHT: SEC-BEATS / 60S CAP",dim,1);
    mosaic_button(fb,132,224,100,"LOOP EVENT",e && e->live,1);
    mosaic_button(fb,240,224,92,e && e->live_finite?"CARD ENDS":"UNTIL STOP",e && e->live_finite,1);
    mosaic_button(fb,340,224,90,"LAUNCH",0,1);mosaic_button(fb,438,224,90,"STOP",0,1);mosaic_button(fb,536,224,72,"RESTART",0,1);
    mosaic_button(fb,132,254,150,e && e->live_tempo?"TEMPO: MASTER TAPE":"TEMPO: FREE",e && e->live_tempo,1);
    mosaic_button(fb,292,254,150,ui->mosaic_sequence_selection_owned?"GROUP: SEQ OWNS":e && e->live_transport?"GROUP: FOLLOW":"GROUP: IGNORE",!ui->mosaic_sequence_selection_owned && e && e->live_transport,1);
    mosaic_button(fb,452,254,156,ui->mosaic_sequence_selection_owned?"SEEK: SEQ OWNS":e && e->live_position?"SEEK: RESTART":"SEEK: IGNORE",!ui->mosaic_sequence_selection_owned && e && e->live_position,1);
    if(e && e->live)snprintf(line,sizeof(line),"%s / SOURCE %.3FS / CARD %.2FS = %.2FX %s",mosaic_live_state_name(ui,e),ts_mosaic_live_cycle(e),e->duration,e->duration/fmax(.000001,ts_mosaic_live_cycle(e)),e->live_finite?"END":"INF");
    else snprintf(line,sizeof(line),"SELECT AN AUDIO CARD / LIVE LOOPS ARE GOLD WITH OWN PLAYHEADS");
    text(fb,132,286,line,ink,1);
    text(fb,132,299,"STOP RECORDING KEEPS THE TAKE / EARLIER LOOPS KEEP PLAYING",dim,1);
    mosaic_button(fb,132,310,144,ui->mosaic_live_keys?"QWERTY ON":"EDIT SHORTCUTS",ui->mosaic_live_keys,1);
    text(fb,286,315,keyboard,ink,1);
    snprintf(line,sizeof(line),"%.79s",ui->mosaic_live_status);text(fb,132,330,line,accent,1);
}
