/* Native Sister controls and audio-owned meters; no UI-driven modulation. */
static void sister_dark_memories(TsFramebuffer *fb, const TsSisterUiModel *m)
{
    text(fb,162,65,"DARK MATTER / FOUR DRIFTING MEMORIES",PAL_TUNING,1);
    for(int i=0;i<4;++i) {
        int top=84+i*20;
        uint32_t color=i&1?PAL_WAVE_RIGHT:PAL_WAVE_LEFT;
        char label[32];float seconds=m->engine.dark_seconds[i];
        if(seconds<=0) { static const float share[4]={.17f,.23f,.27f,.33f};seconds=m->parameters.buffer_seconds*share[i]; }
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
    const char *names[3][4]={{"FEEDBACK","BLUR","DRIFT","DRIVE"},
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
    tracker_tiny(fb,324,285,"H1=M1  H2=M2  H3=M3+M4",PAL_MOUSE);
    tracker_tiny(fb,324,295,"HOLD FREEZES MEMORY WRITES",PAL_MOUSE);
    const int ids2[4]={TS_SISTER_UI_PARAM_INPUT_GAIN,TS_SISTER_UI_PARAM_MONITOR_DRY,
        TS_SISTER_UI_PARAM_MONITOR_WET,TS_SISTER_UI_PARAM_MIX_OUTPUT};
    const char *labels[4]={"INPUT","DRY","WET","OUT"};
    float v[4]={m->parameters.input_gain/2,m->parameters.monitor_dry,m->parameters.monitor_wet,m->parameters.mix_output_gain/4};
    for(int i=0;i<4;++i)sister_percent_parameter_state(fb,10+i*155,308,148,labels[i],v[i],i==0?200:i==3?400:100,
        PAL_VOLUME,ts_sister_ui_parameter_locked(m,ids2[i]));
}
