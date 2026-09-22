static void insert_panel_open(TsUiState *ui)
{
    ts_audio_catalog_refresh();
    ui->insert_open=1;ui->insert_drag=0;ui->router_drag=0;
    ui->insert_send_choice=ui->config.insert.send_pair;
    ui->insert_return_choice=ui->config.insert.return_pair;
    snprintf(ui->insert_send_device,sizeof(ui->insert_send_device),"%s",ui->config.insert_send_device);
    snprintf(ui->insert_return_device,sizeof(ui->insert_return_device),"%s",ui->config.insert_return_device);
    ui->insert_offered_outputs=ts_audio_insert_choice_channels(ui,0);
}

static int insert_panel_mouse(const SDL_Event *e,int x,int y,
    SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    if(e->type==SDL_MOUSEBUTTONUP){ui->insert_drag=0;return ui->mouse_note<0;}
    if(e->type==SDL_MOUSEMOTION && !(e->motion.state&SDL_BUTTON_LMASK))ui->insert_drag=0;
    if(e->type==SDL_MOUSEBUTTONDOWN) {
        if(y<32){ui->insert_open=ui->router_open=0;return 0;}
        if(x>=546 && x<616 && y>=43 && y<67){ui->insert_open=0;return 1;}
        int side=x>=332,origin=36+side*310;
        int direction=e->button.button==SDL_BUTTON_RIGHT?-1:1;
        if(x>=origin && x<origin+256 && y>=110 && y<134) {
            ts_audio_insert_cycle_device(ui,side,direction);
        } else if(x>=origin && x<origin+188 && y>=150 && y<174) {
            unsigned channels=ts_audio_insert_choice_channels(ui,side);
            if(channels>TS_INSERT_MAX_CHANNELS)channels=0;
            int count=(int)channels/2;
            if(!side && ui->insert_send_device[0] && count)++count;
            int *choice=side?&ui->insert_return_choice:&ui->insert_send_choice;
            if(count>(side?0:1))*choice=(*choice+direction+count)%count;
            else snprintf(ui->status,sizeof(ui->status),"NO EXPOSED %s PAIRS; SELECT A DEVICE IN CFG",side?"INPUT":"SPARE OUTPUT");
        } else if(x>=origin && x<origin+256 && y>=219 && y<260) {
            if(e->button.button==SDL_BUTTON_RIGHT) {
                if(side)ui->config.insert.return_db=0;else ui->config.insert.send_db=0;
                if(device)SDL_LockAudioDevice(device);
                ts_sister_runtime_set_insert(&audio->sister,&ui->config.insert);
                if(device)SDL_UnlockAudioDevice(device);
            } else if(e->button.button==SDL_BUTTON_LEFT)ui->insert_drag=side+1;
        } else if(y>=326 && y<350) {
            if(x>=22 && x<114){router_manual(device,audio,ui,TS_ROUTER_INSERT,0);}
            else if(x>=128 && x<198){router_manual(device,audio,ui,TS_ROUTER_INSERT,1);}
            else if(x>=210 && x<306)ts_audio_catalog_refresh();
            else if(x>=420 && x<616)ui->insert_apply_pending=1;
        }
    }
    if(ui->insert_drag && (e->type==SDL_MOUSEMOTION || e->type==SDL_MOUSEBUTTONDOWN)) {
        int side=ui->insert_drag-1;
        float db=fmaxf(-24,fminf(12,(x-(36+side*310))/256.0f*36-24));
        if(side)ui->config.insert.return_db=db;else ui->config.insert.send_db=db;
        if(device)SDL_LockAudioDevice(device);
        ts_sister_runtime_set_insert(&audio->sister,&ui->config.insert);
        if(device)SDL_UnlockAudioDevice(device);
    }
    return y<382;
}
