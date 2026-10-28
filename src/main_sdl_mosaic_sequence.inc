/* UI/control thread only. Audio owns the frozen running order, not these edits. */
static void mosaic_sequence_parameter(SDL_AudioDeviceID output,TsUiState *ui,const char *command,int delta,int reset)
{
    if(output)SDL_LockAudioDevice(output);
    TsMosaic *m=ui->mosaic;TsMosaicEvent *e=ts_mosaic_find(m,ui->mosaic_selected);
    if(e && e->live) {
        double cycle=ts_mosaic_live_cycle(e);ts_mosaic_checkpoint(m);
        if(!strcmp(command,"step_length")) {
            double count=e->duration/fmax(.000001,cycle);
            if(reset)count=1;
            else if(delta)count=fmax(1,round(count)+delta);
            else {const double steps[]={1,2,4,8,16,32};int n=0;while(n<6 && steps[n]<=count+.00001)++n;count=steps[n%6];}
            e->duration=fmin(86400,fmax(.005,count*cycle));
        } else if(!strcmp(command,"length_mode"))e->live_finite^=1;
        else {
            double *value=!strcmp(command,"finish_fade")?&e->finish_fade:&e->fade_out;
            if(reset)*value=0;
            else if(delta)*value=fmax(0,*value+delta*.1);
            else {const double seconds[]={0,.1,.25,.5,1,2,4,8};int n=0;while(n<8 && seconds[n]<=*value+.00001)++n;*value=seconds[n%8];}
            *value=fmin(!strcmp(command,"finish_fade")?30:e->duration,*value);
        }
        ++m->revision;
        snprintf(ui->mosaic_live_status,sizeof(ui->mosaic_live_status),"CARD #%llu / %.3G BASE CYCLES / END FADE %.2FS / FINISH FADE %.2FS",
            (unsigned long long)e->id,e->duration/fmax(.000001,cycle),e->fade_out,e->finish_fade);
    }
    if(output)SDL_UnlockAudioDevice(output);
}
static void mosaic_sequence_edit(TsUiState *ui,const char *command)
{
    TsMosaic *m=ui->mosaic;TsMosaicEvent *e=ts_mosaic_find(m,ui->mosaic_selected);
    if(m->sequence.running){snprintf(ui->mosaic_live_status,sizeof(ui->mosaic_live_status),"STOP SEQUENCE TO CHANGE ITS ORDER / CARD MOVES AND MIX EDITS REMAIN LIVE");return;}
    if(!strcmp(command,"sequence_all")) {
        ts_mosaic_checkpoint(m);
        for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id && m->events[i].live)m->events[i].live_order=0;
    } else if(e && e->live) {
        if(!strcmp(command,"sequence_member")){ts_mosaic_checkpoint(m);e->live_order=e->live_order<0?0:-1;}
        else {
            int slots[TS_MOSAIC_EVENTS],count=ts_mosaic_sequence_order(m,slots),at=0;
            while(at<count && &m->events[slots[at]]!=e)++at;
            int next=at+(!strcmp(command,"sequence_up")?-1:1);
            if(at<count && next>=0 && next<count) {
                ts_mosaic_checkpoint(m);int swap=slots[at];slots[at]=slots[next];slots[next]=swap;
                for(int n=0;n<count;++n)m->events[slots[n]].live_order=n+1;
            }
        }
    }
    snprintf(ui->mosaic_live_status,sizeof(ui->mosaic_live_status),"SEQUENCE ORDER UPDATED / BACKGROUND CARDS KEEP THEIR OWN PLAYERS");
}
