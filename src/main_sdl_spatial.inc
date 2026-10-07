/* A modeless native window. Rendering uses a copied view; no DSP work or file
   writes run under its short control lock. Closing never bypasses the effect. */
typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    TsFramebuffer *framebuffer;
    TsSpatialUi model;
    Uint32 id, presented, message_until;
    int visible, drag;
} SpatialWindow;
static SpatialWindow spatial_window;
static int spatial_wrap_index(int value,int count)
{ int r=value%count;return r<0?r+count:r; }

static void spatial_window_close(void)
{
    if(spatial_window.texture)SDL_DestroyTexture(spatial_window.texture);
    if(spatial_window.renderer)SDL_DestroyRenderer(spatial_window.renderer);
    if(spatial_window.window)SDL_DestroyWindow(spatial_window.window);
    free(spatial_window.framebuffer);memset(&spatial_window,0,sizeof(spatial_window));
}
static void spatial_capture(AudioState *audio,TsUiState *ui)
{
    /* Caller already owns the existing application audio-state lock. */
    ui->config.spatial=audio->sister.spatial.controls;
    if(!spatial_window.visible)return;
    spatial_window.model.controls=audio->sister.spatial.controls;
    spatial_window.model.view=audio->sister.spatial.view;
    spatial_window.model.view.channels=audio->output_device_channels;
    spatial_window.model.learn=ui->midi_learn_active;
    if(spatial_window.model.speaker>=ui->config.spatial.speakers)spatial_window.model.speaker=0;
}
static int spatial_show(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    if(!spatial_window.window){
        spatial_window.window=SDL_CreateWindow("TapeSister - Ambisonics",SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,960,600,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
        if(spatial_window.window){
            SDL_SetWindowMinimumSize(spatial_window.window,640,400);
            spatial_window.renderer=SDL_CreateRenderer(spatial_window.window,-1,SDL_RENDERER_ACCELERATED);
            if(!spatial_window.renderer)spatial_window.renderer=SDL_CreateRenderer(spatial_window.window,-1,SDL_RENDERER_SOFTWARE);
        }
        if(spatial_window.renderer)spatial_window.texture=SDL_CreateTexture(spatial_window.renderer,
            SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,640,400);
        spatial_window.framebuffer=calloc(1,sizeof(TsFramebuffer));
        if(!spatial_window.window||!spatial_window.renderer||!spatial_window.texture||!spatial_window.framebuffer){
            snprintf(ui->status,sizeof(ui->status),"AMBISONICS WINDOW FAILED: %.100s",SDL_GetError());
            spatial_window_close();return 0;
        }
        spatial_window.id=SDL_GetWindowID(spatial_window.window);
    }
    spatial_window.visible=1;
    if(device)SDL_LockAudioDevice(device);
    spatial_capture(audio,ui);
    if(device)SDL_UnlockAudioDevice(device);
    SDL_ShowWindow(spatial_window.window);SDL_RaiseWindow(spatial_window.window);return 1;
}
static void spatial_commit(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSpatialControls *c)
{
    if(device)SDL_LockAudioDevice(device);
    ts_spatial_set(&audio->sister.spatial,c);
    spatial_capture(audio,ui);
    if(device)SDL_UnlockAudioDevice(device);
}
static void spatial_window_update(const TsUiState *ui)
{
    if(!spatial_window.visible||SDL_GetTicks()-spatial_window.presented<33)return;
    if(SDL_GetTicks()>spatial_window.message_until)spatial_window.model.message[0]=0;
    if(ui->midi_learn_active)snprintf(spatial_window.model.message,sizeof(spatial_window.model.message),"%.103s",ui->status);
    ts_spatial_ui_render(spatial_window.framebuffer,&spatial_window.model,&ui->palette);
    SDL_UpdateTexture(spatial_window.texture,NULL,spatial_window.framebuffer->pixels,640*sizeof(uint32_t));
    SDL_RenderClear(spatial_window.renderer);SDL_RenderCopy(spatial_window.renderer,spatial_window.texture,NULL,NULL);
    SDL_RenderPresent(spatial_window.renderer);spatial_window.presented=SDL_GetTicks();
}
static int spatial_midi_index(const char *target)
{
    int i;char extra;return sscanf(target,"main.spatial.param.%d%c",&i,&extra)==1&&i>=0&&i<5?i:-1;
}
static int spatial_midi(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,const char *target,float v)
{
    int index=spatial_midi_index(target);
    if(index<0&&strcmp(target,"main.spatial.enable")&&strcmp(target,"main.spatial.trigger"))return 0;
    TsSpatialControls c;
    if(device)SDL_LockAudioDevice(device);c=audio->sister.spatial.controls;if(device)SDL_UnlockAudioDevice(device);
    if(index>=0)c.value[index]=v;
    else if(!strcmp(target,"main.spatial.enable"))c.enabled=!c.enabled;
    else ++c.rise_trigger;
    spatial_commit(device,audio,ui,&c);return 1;
}
static void spatial_hide(SDL_AudioDeviceID device,AudioState *audio)
{
    spatial_window.visible=spatial_window.drag=0;
    if(spatial_window.window)SDL_HideWindow(spatial_window.window);
    if(device)SDL_LockAudioDevice(device);ts_spatial_test(&audio->sister.spatial,-1);if(device)SDL_UnlockAudioDevice(device);
}
static int spatial_event(SDL_Event *e,SDL_Window *main,SDL_AudioDeviceID device,
                         AudioState *audio,TsUiState *ui,SisterWindow *sister)
{
    Uint32 id=event_window_id(e);
    if(e->type==SDL_KEYDOWN&&e->key.keysym.sym==SDLK_F9&&(e->key.keysym.mod&KMOD_CTRL)){
        if(!e->key.repeat){if(spatial_window.visible)spatial_hide(device,audio);else spatial_show(device,audio,ui);}return 1;
    }
    if(!spatial_window.id||id!=spatial_window.id)return 0;
    if(e->type==SDL_WINDOWEVENT){
        if(e->window.event==SDL_WINDOWEVENT_CLOSE)spatial_hide(device,audio);
        if(e->window.event==SDL_WINDOWEVENT_FOCUS_LOST){
            spatial_window.drag=0;
            if(device)SDL_LockAudioDevice(device);ts_spatial_test(&audio->sister.spatial,-1);if(device)SDL_UnlockAudioDevice(device);
        }
        return 1;
    }
    if(e->type==SDL_KEYDOWN||e->type==SDL_KEYUP){
        if(e->type==SDL_KEYDOWN&&e->key.keysym.sym==SDLK_ESCAPE){
            if(ui->midi_learn_active)midi_learn_escape(ui,sister,SDL_GetTicks());else spatial_hide(device,audio);return 1;
        }
        if(e->type==SDL_KEYDOWN&&e->key.keysym.sym==SDLK_m&&
           (e->key.keysym.mod&KMOD_CTRL)&&(e->key.keysym.mod&KMOD_SHIFT)){
            if(!e->key.repeat)midi_learn_set_active(ui,sister,!ui->midi_learn_active);return 1;
        }
        /* Keep the instrument's existing note/transport ownership, including
           key-up after a window focus change. */
        e->key.windowID=SDL_GetWindowID(main);return 0;
    }
    int x=-1,y=-1,wheel=0;
    if(e->type==SDL_MOUSEBUTTONDOWN||e->type==SDL_MOUSEBUTTONUP)logical_mouse(spatial_window.window,e->button.x,e->button.y,&x,&y);
    else if(e->type==SDL_MOUSEMOTION)logical_mouse(spatial_window.window,e->motion.x,e->motion.y,&x,&y);
    else if(e->type==SDL_MOUSEWHEEL){
#if SDL_VERSION_ATLEAST(2,26,0)
        logical_mouse(spatial_window.window,e->wheel.mouseX,e->wheel.mouseY,&x,&y);
#else
        int mx,my;SDL_GetMouseState(&mx,&my);logical_mouse(spatial_window.window,mx,my,&x,&y);
#endif
        wheel=e->wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-e->wheel.y:e->wheel.y;
    }else return 1;
    int down=e->type==SDL_MOUSEBUTTONDOWN;
    if(e->type==SDL_MOUSEBUTTONUP){spatial_window.drag=0;return 1;}
    int motion=e->type==SDL_MOUSEMOTION;
    if(motion&&(!(e->motion.state&SDL_BUTTON_LMASK)||!spatial_window.drag))return 1;
    int right=down&&e->button.button==SDL_BUTTON_RIGHT;
    int delta=wheel?wheel:right?-1:1;
    int fine=(SDL_GetModState()&KMOD_SHIFT)!=0;
    TsSpatialUi *m=&spatial_window.model;
    if(down&&y>=9&&y<31&&x>=586){spatial_hide(device,audio);return 1;}
    if(down&&y>=9&&y<31&&x>=450&&x<574){midi_learn_set_active(ui,sister,!ui->midi_learn_active);return 1;}
    if(down&&y>=40&&y<63&&x>=12&&x<312){m->page=(x-12)/102;spatial_window.drag=0;return 1;}
    if(ui->midi_learn_active){
        if(down){char target[64]="";int p=m->page==0?ts_spatial_ui_parameter(x,y):-1;
            if(p>=0&&p<5)snprintf(target,sizeof(target),"main.spatial.param.%d",p);
            else if(y>=9&&y<31&&x>=208&&x<304)snprintf(target,sizeof(target),"main.spatial.enable");
            else if(m->page==2&&y>=286&&y<309&&x>=122&&x<230)snprintf(target,sizeof(target),"main.spatial.trigger");
            midi_learn_click(ui,sister,target);
        }return 1;
    }
    TsSpatialControls c;
    if(device)SDL_LockAudioDevice(device);c=audio->sister.spatial.controls;if(device)SDL_UnlockAudioDevice(device);
    if((down||wheel)&&y>=9&&y<31){
        if(x>=208&&x<304)c.enabled=!c.enabled;
        else if(x>=312&&x<442)c.stereo_pair=spatial_wrap_index(c.stereo_pair+delta,c.speakers);
        else return 1;
        spatial_commit(device,audio,ui,&c);return 1;
    }
    if(m->page==0){
        if(down&&y>=245&&y<268){if(x>=14&&x<114)c.mono=!c.mono;
            else if(x>=122&&x<232)c.transform=spatial_wrap_index(c.transform+delta,4);}
        int p=ts_spatial_ui_parameter(x,y);
        if(motion&&spatial_window.drag>=10&&spatial_window.drag<15)p=spatial_window.drag-10;
        if(p>=0&&p<5){
            if(wheel)c.value[p]+=(fine?.001f:.01f)*wheel;
            else if(right)c.value[p]=p==0||p==2?.5f:p==1?.25f:p==4?1.f:0.f;
            else {c.value[p]=(x-(12+125*p))/112.f;spatial_window.drag=10+p;}
        }else if((x>=68&&x<=252&&y>=76&&y<=244)||spatial_window.drag==1){
            if(wheel)c.value[TS_SPATIAL_SPREAD]+=(fine?.001f:.01f)*wheel;
            else if(down||motion){
                float dx=160.f-x,dy=164.f-y;
                float a=atan2f(dx,dy)/(2.f*M_PI)+.5f;
                if(fine)c.value[TS_SPATIAL_ROTATE]=a;
                else {c.value[TS_SPATIAL_AIM]=a;c.value[TS_SPATIAL_AMOUNT]=sqrtf(dx*dx+dy*dy)/72.f;}
                spatial_window.drag=1;
            }
        }
    }else if(m->page==1){
        if(down&&x<300){float best=18*18;
            for(int i=0;i<c.speakers;++i){float a=c.azimuth[i]*M_PI/180;
                float dx=x-(160-80*sinf(a)),dy=y-(174-80*cosf(a)),d=dx*dx+dy*dy;
                if(d<best){best=d;m->speaker=i;}}
            return 1;
        }
        if((down||wheel)&&y>=80&&y<103){
            if(x>=326&&x<464)ts_spatial_ring(&c,c.speakers+(wheel?wheel:x<395?-1:1),c.point);
            else if(x>=474&&x<616){
                TsSpatialControls ring=c;ts_spatial_ring(&ring,c.speakers,!c.point);
                c.point=ring.point;memcpy(c.azimuth,ring.azimuth,sizeof(c.azimuth));
            }
            if(m->speaker>=c.speakers)m->speaker=0;
        }else if((down||wheel)&&x>=326&&x<616&&y>=116&&y<139)c.decoder=spatial_wrap_index(c.decoder+delta,3);
        else if((down||wheel)&&x>=326&&x<616&&y>=157&&y<180){
            m->speaker=spatial_wrap_index(m->speaker+(wheel?wheel:x<471?-1:1),c.speakers);return 1;
        }else if((down||wheel)&&x>=500&&x<616&&y>=198&&y<314){
            int row=(y-198)/29,sp=m->speaker;
            if(row==0)c.output[sp]+=delta;
            if(row==1)c.azimuth[sp]+=delta*(fine?.1f:1.f);
            if(row==2)c.trim_db[sp]+=delta*(fine?.1f:1.f);
            if(row==3)c.delay_ms[sp]+=delta*(fine?.01f:.1f);
        }else if(down&&y>=328&&y<351&&x>=326&&x<616){
            if(device)SDL_LockAudioDevice(device);
            ts_spatial_test(&audio->sister.spatial,x<466?m->speaker:-1);
            if(device)SDL_UnlockAudioDevice(device);return 1;
        }
    }else{
        int control=-1;
        if(y>=101&&y<=140)control=x<310?0:1;
        if(y>=209&&y<=248)control=x<310?2:3;
        if(motion&&spatial_window.drag>=20)control=spatial_window.drag-20;
        if(control>=0){float *f=control==0?&c.lfo_rate:control==1?&c.lfo_depth:control==2?&c.rise_length:&c.rise_depth;
            if(wheel)*f+=(fine?.001f:.01f)*wheel;
            else {*f=(x-(control%2?326:18))/(control%2?288.f:276.f);spatial_window.drag=20+control;}
        }else if(down&&x>=14&&x<628&&((y>=147&&y<170)||(y>=255&&y<278))){
            int p=(x-14)/124;if(p<5){if(y<200)c.lfo_targets^=1u<<p;else c.rise_targets^=1u<<p;}
        }else if(down&&y>=286&&y<309){if(x>=14&&x<114)c.rise_loop=!c.rise_loop;else if(x>=122&&x<230)++c.rise_trigger;}
        else if((down||wheel)&&y>=324&&y<347){
            if(x>=392&&x<614)c.morph_seconds*=powf(fine?1.01f:1.1f,(float)delta);
            else if(down&&x>=14&&x<382){int which=(x-14)/94,side=which/2;
                if(which%2){c.morph_target=side;++c.morph_trigger;}
                else {memcpy(c.state[side],c.value,sizeof(c.value));c.captured|=1u<<side;}
            }
        }
    }
    spatial_commit(device,audio,ui,&c);return 1;
}
