/* F11 live monitor is an explicit extra feed to MAIN MIX. Existing Matrix
   input routes, raw recording and external Insert returns remain independent. */
enum { PERF_INPUT_ROWS=6, PERF_INPUT_Y=86, PERF_INPUT_STEP=30 };
static int performance_input_available(unsigned channel)
{
    return channel<performance_transport.input_channels &&
        ts_audio_range_contains(performance_transport.input_range,channel,performance_transport.input_channels);
}
static void performance_inputs_sync(SDL_AudioDeviceID device,SDL_AudioDeviceID *capture,
    AudioState *audio,ExternalInputState *input,TsUiState *ui)
{
    performance_transport.inputs=&input->performance;
    /* These project controls are written by the UI only; publishing them must
       not add an audio-device lock to every idle controller refresh. */
    (void)device;
    int selected=ts_performance_inputs_set(&input->performance,&audio->sister.performance_mixer);
    int wanted=selected || performance_transport.visible;
    if(wanted!=external_input_requested(input,TS_INPUT_CONSUMER_PERFORMANCE)) {
        external_input_request(input,TS_INPUT_CONSUMER_PERFORMANCE,wanted);
        if(!sync_external_input_consumers(capture,audio,input,&ui->config,
            performance_transport.input_error,sizeof(performance_transport.input_error)))
            snprintf(ui->status,sizeof(ui->status),"LIVE INPUTS: %.130s",performance_transport.input_error);
    }
    performance_transport.input_channels=atomic_load(&input->performance.channels);
    performance_transport.input_range=input->range;
    snprintf(performance_transport.input_label,sizeof(performance_transport.input_label),"%.127s",input->device_label);
    int pages=(performance_transport.input_channels+PERF_INPUT_ROWS-1)/PERF_INPUT_ROWS;
    if(performance_transport.input_page>=pages)performance_transport.input_page=pages?pages-1:0;
    int tuning=performance_transport.visible && performance_transport.tuner_on &&
        window_drawable(performance_transport.window) && performance_input_available(performance_transport.tuner_channel);
    ts_performance_inputs_tuner(&input->performance,tuning,performance_transport.tuner_channel);
}
static void performance_input_gain(SDL_AudioDeviceID device,AudioState *audio,unsigned channel,float gain)
{
    if(channel>=TS_PERFORMANCE_INPUTS || !performance_input_available(channel))return;
    if(device)SDL_LockAudioDevice(device);
    audio->sister.performance_mixer.input_gain[channel]=fmaxf(0,fminf(2,gain));
    if(device)SDL_UnlockAudioDevice(device);
    ts_performance_inputs_set(audio->performance_inputs,&audio->sister.performance_mixer);
    performance_transport.painted=SDL_GetTicks()-100;
}
static void performance_inputs_paint(const TsPerformanceMixer *m)
{
    Uint32 text=ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_TEXT,0xffdbe8e3u);
    Uint32 dim=ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_DIM,0xff94aaa7u);
    Uint32 accent=ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_DATA4,0xff8addb4u);
    Uint32 edge=ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_EDGE,0xff202b30u);
    Uint32 grid=ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_GRID,0xff47605du);
    perf_rect(600,16,1,516,grid);perf_text(620,20,"LIVE INPUTS",accent,2);
    char line[96];snprintf(line,sizeof(line),"%.51s",performance_transport.input_label[0]?performance_transport.input_label:"INPUT DEVICE FROM AUDIO CONFIG");
    perf_text(620,45,line,dim,1);
    perf_text(620,64,"CHECK TO MAIN MIX",dim,1);perf_text(798,64,"INPUT TRIM",dim,1);
    unsigned start=(unsigned)performance_transport.input_page*PERF_INPUT_ROWS;
    for(unsigned row=0;row<PERF_INPUT_ROWS;++row) {
        unsigned channel=start+row;int y=PERF_INPUT_Y+(int)row*PERF_INPUT_STEP;
        if(channel>=performance_transport.input_channels)break;
        int available=performance_input_available(channel);
        perf_rect(620,y,320,25,edge);
        perf_rect(628,y+7,11,11,available && m->input_enabled[channel]?accent:grid);
        snprintf(line,sizeof(line),"IN %u",channel+1);perf_text(649,y+9,line,available?text:dim,1);
        if(!available){perf_text(723,y+9,"OUTSIDE INPUT RANGE",dim,1);continue;}
        int position=724+(int)lrintf(m->input_gain[channel]*66);
        perf_rect(724,y+11,132,4,grid);perf_rect(724,y+11,position-724,4,accent);
        perf_rect(789,y+7,2,12,dim);perf_rect(position-2,y+6,5,14,text);
        if(m->input_gain[channel]>0)snprintf(line,sizeof(line),"%+.1f DB",20*log10f(m->input_gain[channel]));
        else snprintf(line,sizeof(line),"SILENT");
        perf_text(876,y+9,line,text,1);
    }
    if(!performance_transport.input_channels)
        perf_text(628,112,"NO INPUT DEVICE / CHECK AUDIO CONFIG",dim,1);
    unsigned count=performance_transport.input_channels,finish=start+PERF_INPUT_ROWS;
    if(finish>count)finish=count;
    snprintf(line,sizeof(line),"%u-%u OF %u INPUTS",count?start+1:0,finish,count);
    perf_text(620,278,line,dim,1);
    perf_button(852,268,40,25,"<",PERF_BUTTON_INPUT_PREV,0,0);
    perf_button(900,268,40,25,">",PERF_BUTTON_INPUT_NEXT,0,0);
    perf_text(620,304,"PAIR CHECKED: L/R / SINGLE: MONO",dim,1);
    perf_rect(620,324,320,1,grid);
    perf_button(620,338,138,29,performance_transport.tuner_on?"TUNER: ON":"TUNER: OFF",PERF_BUTTON_TUNER,performance_transport.tuner_on,1);
    perf_button(774,338,32,29,"<",PERF_BUTTON_TUNE_PREV,0,0);
    snprintf(line,sizeof(line),"IN %u",performance_transport.tuner_channel+1);perf_text(817,349,line,text,1);
    perf_button(908,338,32,29,">",PERF_BUTTON_TUNE_NEXT,0,0);
    Uint32 now=SDL_GetTicks();TsInputTuner *t=&performance_transport.tuner;
    if(performance_transport.tuner_on && performance_transport.inputs && now-performance_transport.tuner_tick>=100) {
        if(ts_performance_inputs_analyze(performance_transport.inputs,t))performance_transport.tuner_signal=now;
        if(now-performance_transport.tuner_signal>350)t->reading.valid=0;
        performance_transport.tuner_tick=now;
    }
    TsTunerReading pitch=t->reading;
    int valid=performance_transport.tuner_on && performance_input_available(performance_transport.tuner_channel) && pitch.valid;
    const char *notes[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    if(valid)snprintf(line,sizeof(line),"%s%d",notes[pitch.note%12],pitch.note/12-1);
    else snprintf(line,sizeof(line),"--");
    perf_text(636,388,line,valid?accent:dim,5);
    if(valid)snprintf(line,sizeof(line),"%+.1f CENTS",pitch.cents);else snprintf(line,sizeof(line),"-- CENTS");
    perf_text(790,390,line,valid?text:dim,1);
    if(valid)snprintf(line,sizeof(line),"%.2f HZ",pitch.frequency);else snprintf(line,sizeof(line),"A4 = 440 HZ");
    perf_text(790,411,line,dim,1);
    perf_rect(640,457,280,3,grid);perf_rect(775,447,10,22,edge);
    for(int c=-50;c<=50;c+=10)perf_rect(780+c*28/10,450,1,c?12:19,dim);
    if(valid)perf_rect(777+(int)lrintf(pitch.cents*2.8f),445,6,24,fabsf(pitch.cents)<=3?accent:text);
    perf_text(640,479,"-50 FLAT",dim,1);perf_text(777,479,"0",accent,1);perf_text(867,479,"SHARP +50",dim,1);
    perf_text(620,505,performance_transport.tuner_on?"HOLD ONE TERRA NOTE / A4 = 440 HZ":"OFF - NO PITCH ANALYSIS",dim,1);
    perf_text(620,525,"TUNER READS BEFORE TRIM / F9 ROUTES SEPARATE",dim,1);
}
static int performance_inputs_event(SDL_Event *e,SDL_AudioDeviceID device,AudioState *audio)
{
    if(e->type==SDL_MOUSEMOTION && performance_transport.input_drag) {
        performance_input_gain(device,audio,(unsigned)performance_transport.input_drag-1,(e->motion.x-724)/66.f);return 1;
    }
    int x,y;
    if(e->type==SDL_MOUSEWHEEL)wheel_event_mouse(e,&x,&y);
    else if(e->type==SDL_MOUSEBUTTONDOWN){x=e->button.x;y=e->button.y;}
    else return 0;
    if(x<620 || x>=940)return 0;
    if(y>=PERF_INPUT_Y && y<PERF_INPUT_Y+PERF_INPUT_ROWS*PERF_INPUT_STEP && (y-PERF_INPUT_Y)%PERF_INPUT_STEP<25) {
        unsigned channel=(unsigned)performance_transport.input_page*PERF_INPUT_ROWS+(y-PERF_INPUT_Y)/PERF_INPUT_STEP;
        if(!audio->performance_inputs || !performance_input_available(channel))return 1;
        if(e->type==SDL_MOUSEWHEEL) {
            if(x>=718) {
                float delta=e->wheel.preciseY?e->wheel.preciseY:e->wheel.y;
                if(e->wheel.direction==SDL_MOUSEWHEEL_FLIPPED)delta=-delta;
                performance_input_gain(device,audio,channel,audio->sister.performance_mixer.input_gain[channel]+delta*.02f);
            }
        } else if(x<710 && e->button.button==SDL_BUTTON_LEFT) {
            if(device)SDL_LockAudioDevice(device);
            audio->sister.performance_mixer.input_enabled[channel]^=1;
            if(device)SDL_UnlockAudioDevice(device);
            ts_performance_inputs_set(audio->performance_inputs,&audio->sister.performance_mixer);
        } else if(x>=718) {
            if(e->button.button==SDL_BUTTON_RIGHT || (e->button.button==SDL_BUTTON_LEFT && e->button.clicks>=2))performance_input_gain(device,audio,channel,1);
            else if(e->button.button==SDL_BUTTON_LEFT) {
                performance_transport.input_drag=(int)channel+1;SDL_CaptureMouse(SDL_TRUE);
                performance_input_gain(device,audio,channel,(x-724)/66.f);
            }
        }
        performance_transport.painted=SDL_GetTicks()-100;return 1;
    }
    if(e->type!=SDL_MOUSEBUTTONDOWN || e->button.button!=SDL_BUTTON_LEFT)return 1;
    if(y>=268 && y<293 && x>=852) {
        int pages=(performance_transport.input_channels+PERF_INPUT_ROWS-1)/PERF_INPUT_ROWS;
        int next=performance_transport.input_page+(x>=900?1:-1);
        if(next>=0 && next<pages)performance_transport.input_page=next;
        performance_feedback(x>=900?PERF_BUTTON_INPUT_NEXT:PERF_BUTTON_INPUT_PREV,1);
    } else if(y>=338 && y<367) {
        if(x<758) {
            performance_transport.tuner_on^=1;
            memset(&performance_transport.tuner,0,sizeof(performance_transport.tuner));
            performance_feedback(PERF_BUTTON_TUNER,1);
        } else if(x<806 || x>=908) {
            int direction=x>=908?1:-1;
            unsigned count=performance_transport.input_channels;
            for(unsigned i=0;i<count;++i) {
                performance_transport.tuner_channel=(performance_transport.tuner_channel+count+direction)%count;
                if(performance_input_available(performance_transport.tuner_channel))break;
            }
            memset(&performance_transport.tuner,0,sizeof(performance_transport.tuner));
            performance_feedback(direction>0?PERF_BUTTON_TUNE_NEXT:PERF_BUTTON_TUNE_PREV,1);
        }
        ts_performance_inputs_tuner(audio->performance_inputs,performance_transport.tuner_on &&
            performance_input_available(performance_transport.tuner_channel),performance_transport.tuner_channel);
    }
    performance_transport.painted=SDL_GetTicks()-100;return 1;
}
