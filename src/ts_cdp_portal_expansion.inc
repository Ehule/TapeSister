static int portal_expansion_process(const TsPortalProcess *p)
{
    for(size_t i=0;i<sizeof(expansion_processes)/sizeof(expansion_processes[0]);++i)
        if(p==&expansion_processes[i])return 1;
    return 0;
}
static int portal_input_channels(const TsPortalRecipe *r,const TsSample *input)
{
    return input && (input->channels==1 || (input->channels==2 &&
           ts_portal_stereo_policy(r)==TS_PORTAL_STEREO_NATIVE));
}
static int portal_expansion_build(const TsPortalRecipe *r,const TsPortalProcess *p,
    const TsSample *input,TsCdpCommand *commands,size_t *count,char *error,size_t size)
{
    if(!input || !input->data || !input->sample_rate || input->frames<2 ||
       input->frames>TS_PORTAL_MAX_FRAMES || !portal_input_channels(r,input))
        return fail(error,size,"PROCESS NEEDS A SUPPORTED SOURCE WITHIN THE PORTAL LIMIT");
    const double *v=r->values;
    double duration=(double)input->frames/input->sample_rate,estimate=duration,peak=0;
    int spectral=p->family==TS_PORTAL_SPECTRAL;
    if(duration<.04 || (spectral && input->frames<2048))
        return fail(error,size,"SOURCE NEEDS 40 MS; SPECTRAL PROCESSES ALSO NEED 2048 FRAMES");
    for(size_t n=0;n<input->frames*input->channels;++n) {
        if(!isfinite(input->data[n]))return fail(error,size,"SOURCE CONTAINS NONFINITE AUDIO");
        peak=fmax(peak,fabs(input->data[n]));
    }
    if(!strcmp(p->id,"modify.speed.5")) {
        if(v[1]-v[2]<.01 || v[2]>=duration || v[1]>duration)
            return fail(error,size,"ACCELERATION START MUST PRECEDE GOAL; BOTH MUST FIT SOURCE");
        /* Accelerating playback exhausts input no later than unity playback.
           Deceleration ends at CDP's minimum increment (.002), even if the
           source cannot be exhausted. Bound that termination time. */
        estimate=v[0]>=1?duration+.1:v[2]+log(.002)*(v[1]-v[2])/log(v[0])+.1;
    } else if(!strcmp(p->id,"modify.stack")) {
        if(v[3]>=duration)return fail(error,size,"ATTACK POSITION MUST FIT SOURCE");
        estimate=(duration+v[3])*exp2(fabs(v[0])*(v[1]-1)/12);
    } else if(!strcmp(p->command,"zigzag")) {
        if(v[0]>=v[1] || v[1]>duration || v[2]<duration ||
           v[3]<=2*v[4]*.001 || v[3]>=v[5] || v[5]>v[1]-v[0])
            return fail(error,size,"ZIGZAG REGION, MIN/MAX SEGMENTS AND SPLICES MUST FIT; OUTPUT MUST REACH SOURCE LENGTH");
        estimate=v[2]+2*duration;
    } else if(!strcmp(p->executable,"extend") && !strcmp(p->command,"drunk")) {
        unsigned splice=p->mode==1?5:7;
        if(v[1]>=duration || v[1]-v[2]<0 || v[1]+v[2]>duration ||
           v[3]>v[2] || v[4]<=2*v[splice]*.001)
            return fail(error,size,"DRUNK TRAVEL MUST FIT AROUND START; CLOCK MUST EXCEED TWO SPLICES");
        if(p->mode==2 && (v[5]>v[6] || v[9]>v[10] || v[9]<=2*v[splice]*.001))
            return fail(error,size,"SOBER MINIMUMS MUST NOT EXCEED MAXIMUMS OR OVERLAP SPLICES");
        estimate=v[0]+2*duration+(p->mode==2?v[10]:0);
    } else if(!strcmp(p->command,"iterate")) {
        if(p->mode==1 && v[0]<duration)return fail(error,size,"ITERATE OUTPUT MUST REACH SOURCE LENGTH");
        double longest=duration*exp2(v[3]/12);
        estimate=p->mode==1?v[0]+longest:(v[0]+1)*(longest+v[1]*(1+v[2]));
    } else if(!strcmp(p->command,"freeze")) {
        if(p->mode==1 && v[0]<duration)return fail(error,size,"FREEZE OUTPUT MUST REACH SOURCE LENGTH");
        if(v[5]>=v[6] || v[6]>duration-.02 || v[5]<.02 ||
           v[6]-v[5]<.04 || v[1]>v[6]-v[5])
            return fail(error,size,"FREEZE REGION NEEDS 40 MS AND ROOM AT BOTH ENDS; DELAY MUST FIT REGION");
        double longest=(v[6]-v[5])*exp2(v[3]/12);
        estimate=p->mode==1?v[0]+duration+longest:duration+(v[0]+1)*(longest+v[1]*(1+v[2]));
    } else if(!strcmp(p->command,"baktobak")) {
        if(v[0]+2*v[1]*.001>=duration)
            return fail(error,size,"JOIN MUST LEAVE ROOM FOR TWO SPLICES BEFORE SOURCE END");
        estimate=2*duration;
    } else if(!strcmp(p->executable,"bounce")) {
        if(duration<.04 || v[5]>duration || v[5]>v[1])
            return fail(error,size,"MINIMUM BOUNCE LENGTH MUST FIT SOURCE AND FIRST GAP");
        estimate=duration+(v[0]+1)*v[1];
    } else if(!strcmp(p->executable,"dvdwind")) {
        if(v[1]*.001*4>duration)
            return fail(error,size,"FAST SCAN NEEDS AT LEAST FOUR CHUNKS OF SOURCE");
    } else if(!strcmp(p->command,"lohi")) {
        if(fmax(v[1],v[2])>=input->sample_rate*.45 || fmax(v[1],v[2])<1.25*fmin(v[1],v[2]))
            return fail(error,size,"FILTER EDGES NEED 25 PERCENT SEPARATION AND MUST STAY BELOW 90 PERCENT OF NYQUIST");
        estimate+=v[3];
    } else if(!strcmp(p->executable,"newdelay")) {
        double hz=440*exp2((v[0]-69)/12);
        if(hz>=input->sample_rate*.25)return fail(error,size,"TUNED DELAY NEEDS AT LEAST FOUR FRAMES PER PERIOD");
        estimate+=10;
    } else if(!strcmp(p->executable,"silend")) {
        estimate+=v[0];
    } else if(!strcmp(p->executable,"envspeak")) {
        if(duration<.2 || v[0]*.001*4>duration || v[1]*2>=v[0])
            return fail(error,size,"SYLLABLE SOURCE NEEDS FOUR WINDOWS; WINDOW MUST EXCEED TWO SPLICES");
        if(peak<.001)return fail(error,size,"SYLLABLE SOURCE IS TOO QUIET TO SEGMENT");
        estimate=duration*(p->mode==2?2:v[2]+1)*2;
    } else if(!strcmp(p->executable,"tremenv")) {
        if(duration<v[2]*.004)return fail(error,size,"TREMOLO SOURCE NEEDS FOUR ENVELOPE WINDOWS");
    } else if(!strcmp(p->executable,"clip")) {
        if(p->mode==1 && peak<=v[0]+.0001)
            return fail(error,size,"CLIP LEVEL MUST BE BELOW SOURCE PEAK");
    } else if(!strcmp(p->executable,"gate")) {
        if(peak<=pow(10,v[0]/20)+.0001)
            return fail(error,size,"GATE WOULD REMOVE THE WHOLE SOURCE; LOWER THRESHOLD");
    } else if(!strcmp(p->executable,"quirk")) {
        if(peak<.001)return fail(error,size,"QUIRK NEEDS AUDIBLE WAVECYCLES");
    } else if(!strcmp(p->id,"spec.magnify")) {
        if(v[0]>=duration-.025)return fail(error,size,"MAGNIFY POSITION MUST LEAVE AN ANALYSIS WINDOW BEFORE SOURCE END");
        estimate=v[1];
    } else if(!strcmp(p->id,"blur.drunk")) {
        if(v[1]+v[0]*256/input->sample_rate>=duration || v[2]<duration)
            return fail(error,size,"WALK START AND STEP RANGE MUST FIT SOURCE; OUTPUT MUST REACH SOURCE LENGTH");
        estimate=v[2]+duration;
    } else if(!strcmp(p->id,"blur.scatter")) {
        if(v[1]<input->sample_rate/1024.0 || v[0]>=floor(input->sample_rate*.5/v[1]))
            return fail(error,size,"SCATTER BLOCK WIDTH MUST FIT A BIN; KEEP COUNT MUST FIT AVAILABLE BLOCKS");
    } else if(!strcmp(p->executable,"caltrain")) {
        if(v[0]>duration || v[0]<256.0/input->sample_rate || v[1]>=input->sample_rate*.5)
            return fail(error,size,"BLUR TIME MUST FIT SOURCE AND ANALYSIS HOP; DIVIDE MUST BE BELOW NYQUIST");
    } else if(!strcmp(p->executable,"superaccu")) {
        estimate+=30; /* Bounded decay <= .2; includes ample room below CDP's tail cutoff. */
    } else if(!strcmp(p->executable,"spectstr")) {
        estimate=(duration+.05)*v[0]+.05;
    } else if(!strcmp(p->executable,"specfold")) {
        if(v[0]+v[1]>512 || (p->mode==1 && exp2(v[2])>v[1]/2))
            return fail(error,size,"SPECTRAL REGION MUST FIT 512 BINS AND HAVE ROOM FOR EVERY FOLD");
    }
    if(ceil(estimate*input->sample_rate)+4096>TS_PORTAL_MAX_FRAMES)
        return fail(error,size,"REQUESTED OUTPUT EXCEEDS PORTAL LIMIT; SHORTEN SOURCE OR REDUCE PROCESS DURATION");
    TsCdpCommand *c=&commands[spectral?1:0];
    snprintf(c->executable,sizeof(c->executable),"%s",p->executable);
    snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%s",p->command);
    if(p->mode)snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%u",p->mode);
    snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%s",spectral?"input.ana":"input.wav");
    snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%s",spectral?"effect.ana":"output.wav");
    for(unsigned i=0;i<p->parameter_count;++i) {
        const TsPortalParam *param=&p->parameters[i];
        /* Apply segmentation from the beginning; a skip count depends on peaks
           discovered by CDP and is intentionally outside this scalar subset. */
        if(i==2 && !strcmp(p->executable,"envspeak"))
            snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"0");
        if(param->type==TS_PORTAL_SWITCH) {
            if(v[i])snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%s",param->flag);
        } else snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%s%.9g",param->flag,v[i]);
    }
    if(p->mode==2 && !strcmp(p->executable,"envspeak"))
        snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"0");
    snprintf(c->expected_output,sizeof(c->expected_output),"%s",spectral?"effect.ana":"output.wav");
    c->expected_output_type=spectral?TS_CDP_IO_ANALYSIS:TS_CDP_IO_WAV;
    if(spectral) {
        TsCdpCommand *a=&commands[0],*s=&commands[2];
        snprintf(a->executable,sizeof(a->executable),"pvoc");
        const char *analysis[]={"anal","1","input.wav","input.ana","-c1024","-o3"};
        for(int i=0;i<6;++i)snprintf(a->arguments[a->argc++],TS_CDP_TEXT_MAX,"%s",analysis[i]);
        snprintf(a->expected_output,sizeof(a->expected_output),"input.ana");a->expected_output_type=TS_CDP_IO_ANALYSIS;
        snprintf(s->executable,sizeof(s->executable),"pvoc");
        const char *synth[]={"synth","effect.ana","output.wav"};
        for(int i=0;i<3;++i)snprintf(s->arguments[s->argc++],TS_CDP_TEXT_MAX,"%s",synth[i]);
        snprintf(s->expected_output,sizeof(s->expected_output),"output.wav");s->expected_output_type=TS_CDP_IO_WAV;
    }
    *count=spectral?3:1;return 1;
}
