/* Native fixed-pixel master page: same primitives and palette as the instrument. */
static int eq_node_x(const TsUiState *ui,int i)
{ return 44+(int)lrintf(552*ts_master_eq_normalized(&ui->config.master_eq.band[i],0,ui->master_eq_rate)); }
static int eq_node_y(const TsUiState *ui,int i)
{ const TsEqBand *b=&ui->config.master_eq.band[i];return 155-(int)lrintf((ts_master_eq_has_gain(b->type)?b->gain_db:0)*4.5f); }
int ts_ui_master_eq_node(const TsUiState *ui,int x,int y)
{
    int best=-1,distance=145;
    for(int pass=0;pass<5;++pass) {
        int i=(ui->master_eq_band+pass)%5;
        int dx=x-eq_node_x(ui,i),dy=y-eq_node_y(ui,i),d=dx*dx+dy*dy;
        if(d<distance){distance=d;best=i;}
    }
    return best;
}
int ts_ui_master_eq_control(int x,int y)
{
    if(y<300 || y>=328)return -1;
    for(int i=0;i<3;++i)if(x>=20+i*204 && x<20+i*204+188)return i;
    return -1;
}
int ts_ui_master_eq_midi_target(const TsUiState *ui,int x,int y,char *target,size_t size)
{
    if(!ui->master_eq_open)return 0;
    int c=ts_ui_master_eq_control(x,y);
    if(c>=0 && (c!=1 || ts_master_eq_has_gain(ui->config.master_eq.band[ui->master_eq_band].type)))
        snprintf(target,size,"main.eq.band.%d.%d",ui->master_eq_band+1,c);
    else if(x>=354 && x<446 && y>=44 && y<68)snprintf(target,size,"main.eq.bypass");
    else if(x>=20 && x<136 && y>=337 && y<361)snprintf(target,size,"sister.limiter");
    else if(x>=414 && x<620 && y>=337 && y<361)snprintf(target,size,"main.master_output");
    else return 0;
    return 1;
}
static void master_eq_render(TsFramebuffer *fb,const TsUiState *ui)
{
    if(!ui->master_eq_open || ui->mosaic_edit_choice || ui->portal.open ||
       ui->exit_confirm_open || ui->project_overwrite_confirm_open || ui->overdub_confirm_open ||
       ui->file_busy || ui->transform_open || ui->drone_open || ui->import_preview_open ||
       ui->load_selection_choice_open || ui->palette_open || ui->config_open ||
       ui->renaming_bank_slot>=0 || ui->renaming_recipe_slot>=0 || ui->export_choice_open ||
       ui->exchange_dialog!=TS_UI_EXCHANGE_NONE || ui->browser.mode!=TS_BROWSER_CLOSED ||
       ui->fm_bank_choice_open || ui->fm_full_choice_open)return;
    const TsMasterEqControls *c=&ui->config.master_eq;
    const TsEqBand *b=&c->band[ui->master_eq_band];
    unsigned rate=ui->master_eq_rate?ui->master_eq_rate:48000;
    uint32_t amber=RGB(247,196,91),muted=RGB(123,127,133),teal=RGB(107,176,182);
    char label[96];
    frame(fb,10,38,620,341,RGB(29,27,32),PAL_BUTTON);
    text(fb,20,48,"MASTER / ROOM EQ",amber,1);
    if(c->solo_band)snprintf(label,sizeof(label),"SOLO %02d / CTRL-CLICK TO RESTORE",c->solo_band);
    else snprintf(label,sizeof(label),"BUTTONS: BYPASS / CTRL-CLICK: SOLO");
    text(fb,20,62,label,c->solo_band?amber:muted,1);
    button(fb,354,44,92,c->enabled?"EQ ON":"EQ BYPASS",c->enabled);
    button(fb,452,44,116,ui->master_eq_reset_pending?"CONFIRM RESET":"RESET EQ",ui->master_eq_reset_pending);
    button(fb,574,44,46,"X",0);
    frame(fb,43,78,555,156,RGB(10,12,15),RGB(61,59,66));
    const int hz[]={20,50,100,200,500,1000,2000,5000,10000,20000};
    for(int i=0;i<10;++i) {
        if(hz[i]>ts_master_eq_max_hz(rate))continue;
        int x=44+(int)(552*log(hz[i]/20.)/log(ts_master_eq_max_hz(rate)/20.));
        rect(fb,x,79,1,154,RGB(32,34,39));
        if(i==0 || i==2 || i==4 || i==5 || i==7 || i==9) {
            snprintf(label,sizeof(label),hz[i]>=1000?"%dK":"%d",hz[i]>=1000?hz[i]/1000:hz[i]);
            text(fb,x-6,237,label,muted,1);
        }
    }
    for(int db=-12;db<=12;db+=6) {
        int y=155-(int)(db*4.5);
        rect(fb,44,y,552,1,db==0?RGB(81,76,70):RGB(38,37,42));
        snprintf(label,sizeof(label),"%+d",db);text(fb,18,y-3,label,muted,1);
    }
    TsMasterEqControls curve=*c;curve.enabled=1;
    int previous=155;
    for(int x=0;x<552;++x) {
        double f=20*pow(ts_master_eq_max_hz(rate)/20.,x/551.);
        double db=ts_master_eq_response_db(&curve,rate,f);
        int y=155-(int)lrint(fmax(-17,fmin(17,db))*4.5);
        if(x)ui_line(fb,43+x,previous,44+x,y,c->enabled?amber:RGB(95,91,83));
        previous=y;
    }
    if(!c->enabled)rect(fb,44,155,552,1,amber);
    for(int i=0;i<5;++i) {
        int x=eq_node_x(ui,i),y=eq_node_y(ui,i),selected=i==ui->master_eq_band;
        uint32_t color=!ts_master_eq_band_active(c,i)?muted:selected?amber:teal;
        frame(fb,x-8,y-8,17,17,selected?RGB(70,57,35):RGB(26,32,37),color);
        snprintf(label,sizeof(label),"%d",i+1);text(fb,x-2,y-3,label,color,1);
        int solo=c->solo_band==i+1;
        snprintf(label,sizeof(label),"%02d %s",i+1,solo?"SOLO":
            !c->band[i].enabled?"BYPASS":c->solo_band?"SKIP":"ON");
        button(fb,20+i*122,252,112,label,ts_master_eq_band_active(c,i));
        if(solo)rect(fb,20+i*122,274,112,2,amber);
    }
    snprintf(label,sizeof(label),"BAND %d",ui->master_eq_band+1);text(fb,20,284,label,teal,1);
    button(fb,90,278,236,ts_master_eq_type_name(b->type),0);
    text(fb,340,286,b->type==TS_EQ_HIGH_PASS || b->type==TS_EQ_LOW_PASS?"12 DB/OCT  /  Q: RESONANCE":
         b->type==TS_EQ_NOTCH?"Q: NOTCH WIDTH / GAIN FIXED":"Q: WIDTH / WHEEL NODE TO ADJUST",muted,1);
    snprintf(label,sizeof(label),"FREQ %.1F HZ",fminf(b->frequency,ts_master_eq_max_hz(rate)));
    slider(fb,20,303,188,label,ts_master_eq_normalized(b,0,rate),teal);
    if(ts_master_eq_has_gain(b->type))snprintf(label,sizeof(label),"GAIN %+.1F DB",b->gain_db);
    else snprintf(label,sizeof(label),"GAIN --");
    slider(fb,224,303,188,label,ts_master_eq_normalized(b,1,rate),ts_master_eq_has_gain(b->type)?amber:muted);
    snprintf(label,sizeof(label),"Q %.2F",b->q);
    slider(fb,428,303,188,label,ts_master_eq_normalized(b,2,rate),teal);
    rect(fb,20,331,600,1,PAL_BUTTON);
    button(fb,20,338,116,ui->master_output.limiter_enabled?"LIMITER ON":"LIMITER OFF",ui->master_output.limiter_enabled);
    snprintf(label,sizeof(label),"CEIL %+.1F  GR %.1F DB",ui->master_output.limiter_ceiling_db,ui->master_output.limiter_gain_reduction_db);
    text(fb,146,347,label,ui->master_output.limiter_gain_reduction_db>.1?amber:muted,1);
    snprintf(label,sizeof(label),"OUT %.0F%%",ui->master_output.gain*100);
    slider(fb,414,338,202,label,ui->master_output.gain,amber);
    text(fb,20,366,"MASTER > EQ > LIMITER > OUT",muted,1);
    text(fb,310,366,"DRAG FREQ/GAIN  WHEEL Q  SHIFT: FINE",muted,1);
    if(ui->midi_learn_active) {
        char target[TS_MIDI_TARGET_ID_MAX];
        for(int i=0;i<3;++i) {
            if(i==1 && !ts_master_eq_has_gain(b->type))continue;
            snprintf(target,sizeof(target),"main.eq.band.%d.%d",ui->master_eq_band+1,i);
            midi_learn_tint(fb,20+i*204,300,188,28,midi_learn_target_state(&ui->config.midi_map,ui->midi_learn_pending,target));
        }
        midi_learn_tint(fb,354,44,92,24,midi_learn_target_state(&ui->config.midi_map,ui->midi_learn_pending,"main.eq.bypass"));
        midi_learn_tint(fb,20,337,116,24,midi_learn_target_state(&ui->config.midi_map,ui->midi_learn_pending,"sister.limiter"));
        midi_learn_tint(fb,414,337,206,24,midi_learn_target_state(&ui->config.midi_map,ui->midi_learn_pending,"main.master_output"));
    }
}
