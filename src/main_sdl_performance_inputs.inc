/* F11 live monitor is an explicit extra feed to MAIN MIX. Existing Matrix
   input routes, raw recording and external Insert returns remain independent. */
enum { PERF_INPUT_ROWS=6, PERF_INPUT_Y=180, PERF_INPUT_STEP=40 };
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
    int wanted=selected || (performance_transport.visible && performance_transport.page==PERF_INPUTS);
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
    int tuning=performance_transport.visible && performance_transport.page==PERF_INPUTS && performance_transport.tuner_on &&
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
    perf_small(20,150,"LIVE INPUTS TO MAIN MIX / INPUT TRIM",accent);
    unsigned start=(unsigned)performance_transport.input_page*PERF_INPUT_ROWS;
    char line[96];
    for(unsigned row=0;row<PERF_INPUT_ROWS;++row) {
        unsigned channel=start+row;int y=PERF_INPUT_Y+(int)row*PERF_INPUT_STEP;
        if(channel>=performance_transport.input_channels)break;
        int available=performance_input_available(channel);
        perf_rect(20,y,480,34,edge);perf_rect(28,y+11,12,12,available && m->input_enabled[channel]?accent:dim);
        snprintf(line,sizeof(line),"IN %u",channel+1);perf_text(50,y+10,line,available?text:dim,2);
        if(!available){perf_small(150,y+12,"OUTSIDE INPUT RANGE",dim);continue;}
        int x=150+(int)lrintf(m->input_gain[channel]*100);
        perf_rect(150,y+16,200,4,dim);perf_rect(150,y+16,x-150,4,accent);
        perf_rect(249,y+10,2,15,dim);perf_rect(x-2,y+9,5,17,text);
        if(m->input_gain[channel]>0)snprintf(line,sizeof(line),"%+.1f DB",20*log10f(m->input_gain[channel]));else snprintf(line,sizeof(line),"SILENT");
        perf_small(380,y+12,line,text);
    }
    if(!performance_transport.input_channels)perf_small(28,220,"NO INPUT DEVICE / CHECK AUDIO CONFIG",dim);
    unsigned count=performance_transport.input_channels,finish=SDL_min(start+PERF_INPUT_ROWS,count);
    snprintf(line,sizeof(line),"%u-%u OF %u INPUTS",count?start+1:0,finish,count);perf_small(20,450,line,dim);
    perf_button(412,440,40,30,"<",PERF_BUTTON_INPUT_PREV,0,0);perf_button(460,440,40,30,">",PERF_BUTTON_INPUT_NEXT,0,0);
    snprintf(line,sizeof(line),"%.51s",performance_transport.input_label[0]?performance_transport.input_label:"INPUT DEVICE FROM AUDIO CONFIG");perf_small(20,486,line,dim);
    perf_small(20,512,"PAIR CHECKED: L/R / SINGLE: MONO",dim);
    perf_button(540,180,202,34,performance_transport.tuner_on?"TUNER ON":"TUNER OFF",PERF_BUTTON_TUNER,performance_transport.tuner_on,1);
    perf_button(770,180,32,34,"<",PERF_BUTTON_TUNE_PREV,0,0);perf_button(908,180,32,34,">",PERF_BUTTON_TUNE_NEXT,0,0);
    snprintf(line,sizeof(line),"IN %u",performance_transport.tuner_channel+1);perf_small(814,192,line,text);
    Uint32 now=SDL_GetTicks();TsInputTuner *t=&performance_transport.tuner;
    if(performance_transport.tuner_on && performance_transport.inputs && now-performance_transport.tuner_tick>=100) {
        if(ts_performance_inputs_analyze(performance_transport.inputs,t))performance_transport.tuner_signal=now;
        if(now-performance_transport.tuner_signal>350)t->reading.valid=0;
        performance_transport.tuner_tick=now;
    }
    TsTunerReading pitch=t->reading;
    int valid=performance_transport.tuner_on && performance_input_available(performance_transport.tuner_channel) && pitch.valid;
    const char *notes[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    if(valid)snprintf(line,sizeof(line),"%s%d",notes[pitch.note%12],pitch.note/12-1);else snprintf(line,sizeof(line),"--");
    perf_text(550,250,line,valid?accent:dim,8);
    if(valid)snprintf(line,sizeof(line),"%+.1f CENTS",pitch.cents);else snprintf(line,sizeof(line),"-- CENTS");perf_small(770,260,line,text);
    if(valid)snprintf(line,sizeof(line),"%.2f HZ",pitch.frequency);else snprintf(line,sizeof(line),"A4 = 440 HZ");perf_small(770,292,line,dim);
    perf_rect(560,355,360,4,dim);perf_rect(738,345,4,24,accent);
    for(int c=-50;c<=50;c+=10)perf_rect(740+c*36/10,350,1,c?12:24,dim);
    if(valid)perf_rect(737+(int)lrintf(pitch.cents*3.6f),342,6,30,accent);
    perf_small(560,390,"-50 FLAT",dim);perf_small(736,390,"0",accent);perf_small(830,390,"SHARP +50",dim);
    perf_small(540,446,"TUNER READS BEFORE INPUT TRIM",dim);
    perf_small(540,480,"F9 ROUTES REMAIN SEPARATE",dim);
    perf_small(540,512,"HIDDEN PAGE SUSPENDS PITCH ANALYSIS",dim);
}
static int performance_inputs_event(SDL_Event *e,SDL_AudioDeviceID device,AudioState *audio)
{
    if(performance_transport.page!=PERF_INPUTS)return 0;
    int x,y;
    if(e->type!=SDL_MOUSEMOTION && e->type!=SDL_MOUSEWHEEL && e->type!=SDL_MOUSEBUTTONDOWN)return 0;
    perf_pointer(e,&x,&y);
    if(e->type==SDL_MOUSEMOTION) {
        if(!performance_transport.input_drag)return 0;
        performance_input_gain(device,audio,(unsigned)performance_transport.input_drag-1,(x-150)/100.f);return 1;
    }
    if(x>=20 && x<500 && y>=PERF_INPUT_Y && y<PERF_INPUT_Y+PERF_INPUT_ROWS*PERF_INPUT_STEP && (y-PERF_INPUT_Y)%PERF_INPUT_STEP<34) {
        unsigned channel=(unsigned)performance_transport.input_page*PERF_INPUT_ROWS+(y-PERF_INPUT_Y)/PERF_INPUT_STEP;
        if(!audio->performance_inputs || !performance_input_available(channel))return 1;
        if(e->type==SDL_MOUSEWHEEL && x>=140) {
            float delta=e->wheel.preciseY?e->wheel.preciseY:e->wheel.y;if(e->wheel.direction==SDL_MOUSEWHEEL_FLIPPED)delta=-delta;
            performance_input_gain(device,audio,channel,audio->sister.performance_mixer.input_gain[channel]+delta*.02f);
        } else if(e->type==SDL_MOUSEBUTTONDOWN) {
            if(x<130 && e->button.button==SDL_BUTTON_LEFT) {
                if(device)SDL_LockAudioDevice(device);audio->sister.performance_mixer.input_enabled[channel]^=1;if(device)SDL_UnlockAudioDevice(device);
                ts_performance_inputs_set(audio->performance_inputs,&audio->sister.performance_mixer);
            } else if(x>=140) {
                if(e->button.button==SDL_BUTTON_RIGHT || (e->button.button==SDL_BUTTON_LEFT && e->button.clicks>=2))performance_input_gain(device,audio,channel,1);
                else if(e->button.button==SDL_BUTTON_LEFT){performance_transport.input_drag=(int)channel+1;SDL_CaptureMouse(SDL_TRUE);performance_input_gain(device,audio,channel,(x-150)/100.f);}
            }
        }
        performance_transport.painted=SDL_GetTicks()-100;return 1;
    }
    if(e->type!=SDL_MOUSEBUTTONDOWN || e->button.button!=SDL_BUTTON_LEFT)return 0;
    if(y>=440 && y<470 && x>=412 && x<500) {
        int page=performance_transport.input_page+(x>=460?1:-1),count=(performance_transport.input_channels+PERF_INPUT_ROWS-1)/PERF_INPUT_ROWS;
        if(page>=0 && page<count)performance_transport.input_page=page;
        performance_feedback(x>=460?PERF_BUTTON_INPUT_NEXT:PERF_BUTTON_INPUT_PREV,1);
    } else if(y>=180 && y<214 && x>=540 && x<940) {
        if(x<742){performance_transport.tuner_on^=1;memset(&performance_transport.tuner,0,sizeof(performance_transport.tuner));performance_feedback(PERF_BUTTON_TUNER,1);}
        else if(x<802 || x>=908) {
            int direction=x>=908?1:-1;unsigned count=performance_transport.input_channels;
            for(unsigned i=0;i<count;++i){performance_transport.tuner_channel=(performance_transport.tuner_channel+count+direction)%count;if(performance_input_available(performance_transport.tuner_channel))break;}
            memset(&performance_transport.tuner,0,sizeof(performance_transport.tuner));performance_feedback(direction>0?PERF_BUTTON_TUNE_NEXT:PERF_BUTTON_TUNE_PREV,1);
        }
        ts_performance_inputs_tuner(audio->performance_inputs,performance_transport.tuner_on && performance_input_available(performance_transport.tuner_channel),performance_transport.tuner_channel);
    } else return 0;
    performance_transport.painted=SDL_GetTicks()-100;return 1;
}
