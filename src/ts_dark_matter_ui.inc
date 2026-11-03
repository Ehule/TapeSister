/* Native Sister controls and audio-owned meters; no UI-driven modulation. */
static void sister_dark_memories(TsFramebuffer *fb, const TsSisterUiModel *m)
{
    const TsDarkMatterControls *d=&m->parameters.dark;
    sister_choice_parameter_state(fb,10,62,303,"ALGORITHM",ts_dark_matter_algorithm_name(d->algorithm),1,PAL_TUNING,
        ts_sister_ui_parameter_locked(m,TS_DARK_PARAM_ALGORITHM));
    char size_label[24];
    snprintf(size_label,sizeof(size_label),"%s",d->algorithm==TS_DARK_LEGACY?"BUFFER":ts_dark_matter_size_name(d->size));
    if(d->algorithm==TS_DARK_GRANULAR) {
        static const int ms[3]={60,180,600};
        snprintf(size_label,sizeof(size_label),"%s %dMS",ts_dark_matter_size_name(d->size),ms[d->size]);
    }
    if(d->algorithm==TS_DARK_TWO_DELAYS || d->algorithm==TS_DARK_FOUR_DELAYS) {
        static const float durations[2][3]={{2.5f,9.5f,22.f},{2.5f,8.5f,11.5f}};
        snprintf(size_label,sizeof(size_label),"%s %.1FS",ts_dark_matter_size_name(d->size),durations[d->algorithm-TS_DARK_TWO_DELAYS][d->size]);
    }
    sister_choice_parameter_state(fb,322,62,148,"SIZE",size_label,
        d->algorithm!=TS_DARK_LEGACY,PAL_TUNING,ts_sister_ui_parameter_locked(m,TS_DARK_PARAM_SIZE));
    tracker_tiny(fb,480,66,"CLICK / WHEEL",PAL_MOUSE);
    int count=d->algorithm==TS_DARK_TWO_DELAYS?2:d->algorithm==TS_DARK_GRANULAR?(d->size==2?3:2):4;
    for(int i=0;i<4;++i) {
        int top=84+i*20;
        uint32_t color=i&1?PAL_WAVE_RIGHT:PAL_WAVE_LEFT;
        char label[32];float seconds=m->engine.dark_seconds[i];
        if(i>=count){text(fb,18,top+3,"INACTIVE",PAL_MOUSE,1);continue;}
        if(seconds<=0 && d->algorithm==TS_DARK_LEGACY) { static const float share[4]={.17f,.23f,.27f,.33f};seconds=m->parameters.buffer_seconds*share[i]; }
        snprintf(label,sizeof(label),"M%d %5.2FS",i+1,seconds);
        text(fb,18,top+3,label,color,1);
        rect(fb,112,top,448,15,ts_theme_color(active_palette(),render_group,TS_COLOR_PANEL,RGB(20,20,24)));
        int level=(int)(sister_clamp(m->engine.dark_peak[i])*444);
        rect(fb,114,top+3,level,9,sister_dim_color(color));
        int cursor=114+(int)(sister_clamp(m->engine.dark_position[i])*441);
        rect(fb,cursor,top+1,2,13,color);
        snprintf(label,sizeof(label),"%.2F",m->engine.dark_peak[i]);
        text(fb,578,top+3,label,PAL_MOUSE,1);
    }
}
static void sister_dark_controls(TsFramebuffer *fb, const TsSisterUiModel *m)
{
    const TsDarkMatterControls *d=&m->parameters.dark;
    int hall=d->algorithm==TS_DARK_GIANT_REVERB,grain=d->algorithm==TS_DARK_GRANULAR;
    const char *names[3][4]={{"FEEDBACK",hall?"BLUR N/A":grain?"SCATTER":"BLUR",hall?"DRIFT N/A":grain?"GRAIN MOTION":"DRIFT","DRIVE"},
        {"REPLACE","DAMP","REVERSE","WIDTH"},{"FILTER","CUTOFF","Q","GAIN"}};
    const int ids[3][4]={{TS_DARK_PARAM_FEEDBACK,TS_DARK_PARAM_BLUR,TS_DARK_PARAM_DRIFT,TS_DARK_PARAM_DRIVE},
        {TS_DARK_PARAM_REPLACE,TS_DARK_PARAM_DAMP,TS_DARK_PARAM_REVERSE,TS_SISTER_UI_PARAM_WIDTH},
        {TS_SISTER_UI_PARAM_FILTER_TYPE,TS_SISTER_UI_PARAM_FILTER_CUTOFF,TS_SISTER_UI_PARAM_FILTER_Q,TS_SISTER_UI_PARAM_FILTER_GAIN}};
    float values[3][4]={{d->feedback/1.05f,d->blur,d->drift,d->drive},
        {d->replace,d->damp,(float)d->reverse,m->parameters.width},
        {0,log10f(m->parameters.filter_cutoff_hz/20)/3,(m->parameters.filter_q-.1f)/19.9f,(m->parameters.filter_gain_db+24)/48}};
    for(int row=0;row<3;++row)for(int col=0;col<4;++col) {
        int left=10+col*155,top=202+row*28;
        int lock=ts_sister_ui_parameter_locked(m,ids[row][col]);
        if(row==2 && col==0)
            sister_choice_parameter_state(fb,left,top,148,names[row][col],
                ts_sister_filter_type_name(m->parameters.filter_type),m->parameters.filter_type!=TS_SISTER_FILTER_BYPASS,PAL_INSTRUMENT,lock);
        else if(row==1 && col==2)
            sister_choice_parameter_state(fb,left,top,148,names[row][col],d->reverse?"ON":"OFF",d->reverse,PAL_TUNING,lock);
        else sister_percent_parameter_state(fb,left,top,148,names[row][col],values[row][col],
            row==0 && col==0?105:100,row==2?PAL_INSTRUMENT:PAL_TUNING,lock);
    }
    sister_parameter_state(fb,10,286,148,"DUCK",m->parameters.duck_enabled?m->parameters.duck_sensitivity:0,
        PAL_VOLUME,ts_sister_ui_parameter_locked(m,TS_SISTER_UI_PARAM_DUCK));
    sister_choice_parameter_state(fb,165,286,148,"DECOR",m->parameters.decorrelation_enabled?"ON":"OFF",
        m->parameters.decorrelation_enabled,PAL_WAVE_RIGHT,ts_sister_ui_parameter_locked(m,TS_SISTER_UI_PARAM_DECORRELATE));
    tracker_tiny(fb,324,285,hall?"DIFFUSE FEEDBACK HALL":grain?"H1=LEFT H2=RIGHT H3=CENTER":"H1=M1  H2=M2  H3=M3+M4",PAL_MOUSE);
    tracker_tiny(fb,324,295,"HOLD FREEZES MEMORY WRITES",PAL_MOUSE);
    const int ids2[4]={TS_SISTER_UI_PARAM_INPUT_GAIN,TS_SISTER_UI_PARAM_MONITOR_DRY,
        TS_SISTER_UI_PARAM_MONITOR_WET,TS_SISTER_UI_PARAM_MIX_OUTPUT};
    const char *labels[4]={"INPUT","DRY","WET","OUT"};
    float v[4]={m->parameters.input_gain/2,m->parameters.monitor_dry,m->parameters.monitor_wet,m->parameters.mix_output_gain/4};
    for(int i=0;i<4;++i)sister_percent_parameter_state(fb,10+i*155,308,148,labels[i],v[i],i==0?200:i==3?400:100,
        PAL_VOLUME,ts_sister_ui_parameter_locked(m,ids2[i]));
}
