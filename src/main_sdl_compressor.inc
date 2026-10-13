/* Compact, modeless F9 panel. Included after matrix drawing helpers. */
static int mx_editable(void);
static void mx_compressor_paint(void)
{
    MatrixWindow *w=&matrix_window;char label[100];
    mx_text(32,703,"COMPRESSOR",MX_GREEN,2);
    mx_button(190,696,126,w->compressor.enabled?"ACTIVE":"BYPASS",w->compressor.enabled);
    mx_button(330,696,210,w->compressor.external?"SC: EXTERNAL":"SC: INTERNAL",w->compressor.external);
    snprintf(label,sizeof(label),"GAIN REDUCTION %.1f DB",w->controls.enabled?w->compressor_gr:0);
    mx_text(570,703,label,MX_YELLOW,2);
    mx_rect(914,700,310,14,MX_BG);
    mx_rect(914,700,(int)(310*fminf(1,fmaxf(0,w->controls.enabled?w->compressor_gr/24:0))),14,MX_AMBER);
    const char *names[]={"THRESHOLD","RATIO","ATTACK","RELEASE","OUTPUT"};
    const float values[]={w->compressor.threshold_db,w->compressor.ratio,w->compressor.attack_ms,w->compressor.release_ms,w->compressor.output_db};
    for(int i=0;i<5;++i) {
        int x=32+i*242;
        snprintf(label,sizeof(label),i==1?"%s %.1f:1":i==2 || i==3?"%s %.1f MS":"%s %+.1f DB",names[i],values[i]);
        mx_text(x,735,label,MX_TEXT,1);
        mx_rect(x,758,220,8,MX_BG);
        int marker=(int)(216*ts_compressor_normalized(&w->compressor,i));
        mx_rect(x,758,marker+4,8,0xff416359u);mx_rect(x+marker,754,4,16,MX_GREEN);
    }
    mx_text(32,779,w->controls.enabled?
        "COMP IN: SOUND TO COMPRESS | COMP SC: TRIGGER ONLY | ROUTE COMPRESSOR ROW TO MAIN / FX / OUT":
        "COMPRESSOR NEEDS MATRIX MODE. USE MATRIX ABOVE TO IMPORT THE CURRENT ROUTING.",MX_DIM,1);
}
static void mx_compressor_event(const SDL_Event *e,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,int x,int y,int wheel)
{
    MatrixWindow *w=&matrix_window;
    if(!mx_editable())return;
    TsCompressorControls c=w->compressor;int change=0;
    int click=e->type==SDL_MOUSEBUTTONDOWN,left=click && e->button.button==SDL_BUTTON_LEFT;
    if(left && y>=696 && y<724) {
        if(x>=190 && x<316){c.enabled=!c.enabled;change=1;}
        if(x>=330 && x<540){c.external=!c.external;change=1;}
    }
    int control=x>=32 && x<1220?(x-32)/242:-1;
    if(control>=0 && control<5 && x<32+control*242+220 && y>=730 && y<777) {
        if(wheel) {
            float step=(SDL_GetModState()&KMOD_SHIFT)?.002f:.02f;
            ts_compressor_set_normalized(&c,control,ts_compressor_normalized(&c,control)+wheel*step);change=1;
        } else if(left)w->drag=10+control;
        else if(click && e->button.button==SDL_BUTTON_RIGHT) {
            TsCompressorControls defaults;ts_compressor_default(&defaults);
            ts_compressor_set_normalized(&c,control,ts_compressor_normalized(&defaults,control));change=1;
        }
    }
    if(w->drag>=10 && (left || e->type==SDL_MOUSEMOTION)) {
        control=w->drag-10;ts_compressor_set_normalized(&c,control,(x-32-control*242)/216.f);change=1;
    }
    if(change) {
        if(device)SDL_LockAudioDevice(device);
        ts_compressor_set(&audio->sister.compressor,&c);
        w->compressor=ui->config.compressor=audio->sister.compressor.controls;
        if(device)SDL_UnlockAudioDevice(device);
        w->dirty=1;
    }
}
