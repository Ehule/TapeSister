/* One modeless oscillator editor for Canvas, P1/Pn and MetaFX stages. */
static struct {
    SDL_Window *window;SDL_Renderer *renderer;SDL_Texture *texture;TsFramebuffer fb;
    Uint32 id,return_id,painted;int visible,source,stage,drag;uint32_t node,child;
    TsMosaic *context;uint64_t epoch;MatrixSnapshot *before;
} autopan_window={.drag=-1};
static void autopan_hide(void)
{autopan_window.visible=0;autopan_window.drag=-1;free(autopan_window.before);autopan_window.before=NULL;if(autopan_window.window)SDL_HideWindow(autopan_window.window);}
static void autopan_close(void)
{
    autopan_hide();if(autopan_window.texture)SDL_DestroyTexture(autopan_window.texture);
    if(autopan_window.renderer)SDL_DestroyRenderer(autopan_window.renderer);
    if(autopan_window.window)SDL_DestroyWindow(autopan_window.window);
    memset(&autopan_window,0,sizeof(autopan_window));autopan_window.drag=-1;
}
static void autopan_open(AudioState *audio,int source,uint32_t node,uint32_t child,int stage)
{
    if(!autopan_window.window) {
        autopan_window.window=SDL_CreateWindow("TapeSister / Auto Pan",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,960,600,TS_WORKSPACE_WINDOW_FLAGS);
        if(!autopan_window.window)return;
        autopan_window.renderer=SDL_CreateRenderer(autopan_window.window,-1,SDL_RENDERER_SOFTWARE);
        if(autopan_window.renderer)autopan_window.texture=SDL_CreateTexture(autopan_window.renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,640,400);
        if(!autopan_window.texture){autopan_close();return;}
        autopan_window.id=SDL_GetWindowID(autopan_window.window);
    }
    SDL_Window *origin=SDL_GetKeyboardFocus();if(origin && origin!=autopan_window.window)autopan_window.return_id=SDL_GetWindowID(origin);
    if(source)matrix_window.controls=audio->sister.matrix.controls;
    free(autopan_window.before);autopan_window.before=NULL;
    autopan_window.source=source;autopan_window.node=node;autopan_window.child=child;autopan_window.stage=stage;
    autopan_window.context=NULL;autopan_window.visible=1;autopan_window.drag=-1;
    SDL_ShowWindow(autopan_window.window);SDL_RaiseWindow(autopan_window.window);
}
static TsAutoPanSettings *autopan_resolve(TsUiState *ui,SisterWindow *sister,float **a,float **b,float **c)
{
    if(!autopan_window.visible)return NULL;
    TsMosaic *context=ui->mosaic_gallery?ui->mosaic_gallery->root:ui->mosaic;
    if(!autopan_window.context){autopan_window.context=context;autopan_window.epoch=context?context->epoch:0;}
    else if(context!=autopan_window.context || (context && context->epoch!=autopan_window.epoch)){autopan_hide();return NULL;}
    TsSisterFxSlotControls *slot=NULL;
    if(!autopan_window.source) {
        if(sister->board_id){autopan_hide();return NULL;}
        for(int i=0;i<sister->model.parameters.fx.slot_count;++i)if(sister->model.parameters.fx.slot[i].id==autopan_window.child)slot=&sister->model.parameters.fx.slot[i];
    } else for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i) {
        TsMatrixEffectControls *v=&matrix_window.controls.effect[i];if(v->id!=autopan_window.node)continue;
        if(autopan_window.source==1 && v->type==TS_MATRIX_FX_AUTOPAN){*a=&v->a;*b=&v->b;*c=&v->c;return &v->autopan;}
        if(autopan_window.source==2 && v->type==TS_MATRIX_FX_PEDALBOARD)
            for(int j=0;j<v->board.count;++j)if(v->board.slot[j].id==autopan_window.child)slot=&v->board.slot[j];
        if(autopan_window.source==3 && v->type==TS_MATRIX_FX_META && autopan_window.stage>=0 && autopan_window.stage<v->meta.count) {
            TsMetaStage *s=&v->meta.stage[autopan_window.stage];
            if(s->type==TS_SISTER_FX_AUTOPAN){*a=&s->value[0];*b=&s->value[1];*c=&s->value[2];return &s->autopan;}
        }
    }
    if(slot && slot->type==TS_SISTER_FX_AUTOPAN){*a=&slot->parameter_a;*b=&slot->parameter_b;*c=&slot->parameter_c;return &slot->autopan;}
    autopan_hide();return NULL;
}
static void autopan_rect(int x,int y,int w,int h,uint32_t color)
{for(int yy=SDL_max(0,y);yy<SDL_min(400,y+h);++yy)for(int xx=SDL_max(0,x);xx<SDL_min(640,x+w);++xx)autopan_window.fb.pixels[yy*640+xx]=color;}
static void autopan_text(int x,int y,const char *s,uint32_t ink,int scale)
{for(;*s && x<632;++s,x+=6*scale){const char *g=ts_ui_glyph(*s);for(int yy=0;yy<7;++yy)for(int xx=0;xx<5;++xx)if(g[yy*5+xx]=='1')autopan_rect(x+xx*scale,y+yy*scale,scale,scale,ink);}}
static void autopan_paint(TsUiState *ui,SisterWindow *sister)
{
    if(!autopan_window.visible || !window_drawable(autopan_window.window))return;
    if(SDL_GetTicks()-autopan_window.painted<33)return;autopan_window.painted=SDL_GetTicks();
    float *a,*b,*c;TsAutoPanSettings *s=autopan_resolve(ui,sister,&a,&b,&c);if(!s)return;
    uint32_t bg=ts_theme_color(&ui->palette,TS_THEME_PEDALBOARD,TS_COLOR_BACKGROUND,0xff17151bu);
    uint32_t ink=ts_theme_color(&ui->palette,TS_THEME_PEDALBOARD,TS_COLOR_TEXT,0xffe5dce9u);
    uint32_t accent=0xffc6a5d5u;
    autopan_rect(0,0,640,400,bg);autopan_text(20,16,"AUTO PAN",accent,2);
    autopan_text(20,40,"CLICK / DRAG / WHEEL: EDIT    RIGHT CLICK: DEFAULT",ink,1);
    for(int k=0;k<TS_AP_COUNT;++k) {
        float v=k==0?*a:k==1?*b:k==2?*c:s->value[k-3];char label[96];ts_autopan_label(label,sizeof(label),k,v);
        int y=66+k*32;autopan_text(20,y,label,ink,1);
        autopan_rect(20,y+14,600,3,0xff514656u);autopan_rect(20+(int)(v*596),y+11,4,9,accent);
    }
    autopan_text(20,365,"STEREO PHASE: DUAL TREMOLO     CENTER: BALANCE BIAS",ink,1);
    autopan_text(20,384,"TEMPO: MASTER CLOCK    1 MS EDGE SLEW    ESC: RETURN",ink,1);
    SDL_UpdateTexture(autopan_window.texture,NULL,autopan_window.fb.pixels,640*sizeof(uint32_t));
    SDL_RenderClear(autopan_window.renderer);SDL_RenderCopy(autopan_window.renderer,autopan_window.texture,NULL,NULL);SDL_RenderPresent(autopan_window.renderer);
}
static void autopan_history_finish(SDL_AudioDeviceID device,AudioState *audio,TsSamplePages *pages,TsInstrument *bank)
{
    if(!autopan_window.before)return;
    MatrixSnapshot *after=mx_snapshot(device,audio,pages,bank);
    if(after && !mx_equal(autopan_window.before,after))mx_push(autopan_window.before);else free(autopan_window.before);
    free(after);autopan_window.before=NULL;
}
static int autopan_event(const SDL_Event *e,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,SisterWindow *sister,TsSamplePages *pages,TsInstrument *bank)
{
    if(!autopan_window.visible || event_window_id(e)!=autopan_window.id)return 0;
    if((e->type==SDL_KEYDOWN && e->key.keysym.sym==SDLK_ESCAPE) || (e->type==SDL_WINDOWEVENT && e->window.event==SDL_WINDOWEVENT_CLOSE)) {
        autopan_history_finish(device,audio,pages,bank);autopan_hide();editor_return_window(autopan_window.return_id);return 1;
    }
    if(e->type==SDL_KEYDOWN && (e->key.keysym.mod&KMOD_CTRL) && (e->key.keysym.sym==SDLK_z || e->key.keysym.sym==SDLK_y)) {
        autopan_history_finish(device,audio,pages,bank);mx_undo(device,audio,ui,pages,bank,e->key.keysym.sym==SDLK_y || (e->key.keysym.mod&KMOD_SHIFT));
        if(!autopan_window.source){sister->model.parameters=audio->sister.parameters;sister->rendered_model_valid=0;}return 1;
    }
    if(e->type==SDL_MOUSEBUTTONUP || (e->type==SDL_WINDOWEVENT && e->window.event==SDL_WINDOWEVENT_FOCUS_LOST)) {
        autopan_history_finish(device,audio,pages,bank);autopan_window.drag=-1;return 1;
    }
    int down=e->type==SDL_MOUSEBUTTONDOWN,motion=e->type==SDL_MOUSEMOTION,wheel=e->type==SDL_MOUSEWHEEL;
    if(!down && !motion && !wheel)return 0;
    if(motion && autopan_window.drag<0)return 1;
    int rx,ry,x,y;if(down){rx=e->button.x;ry=e->button.y;}else if(motion){rx=e->motion.x;ry=e->motion.y;}else wheel_event_mouse(e,&rx,&ry);
    if(!sister_event_mouse(autopan_window.window,rx,ry,&x,&y))return 1;
    int k=motion?autopan_window.drag:(y>=60 && y<60+TS_AP_COUNT*32?(y-60)/32:-1);if(k<0 || k>=TS_AP_COUNT)return 1;
    float *a,*b,*c;TsAutoPanSettings *s=autopan_resolve(ui,sister,&a,&b,&c);if(!s)return 1;
    if(!autopan_window.before)autopan_window.before=mx_snapshot(device,audio,pages,bank);
    if(!autopan_window.before)return 1;
    float *target=k==0?a:k==1?b:k==2?c:&s->value[k-3];int steps=ts_autopan_steps(k);
    float value=SDL_clamp((x-20)/596.f,0,1);
    if(wheel)value=SDL_clamp(*target+wheel_event_delta(e,0)*(steps?1.f/steps:(SDL_GetModState()&KMOD_SHIFT)?.002f:.02f),0,1);
    else if(down && e->button.button==SDL_BUTTON_RIGHT){TsAutoPanSettings d;ts_autopan_settings_default(&d);value=k==0?ts_autopan_default_rate():k==1?1:k==2?.5f:d.value[k-3];}
    else if(down && e->button.button!=SDL_BUTTON_LEFT)return 1;
    if(steps)value=roundf(value*steps)/steps;
    *target=value;
    if(autopan_window.source)mx_commit_controls(device,audio,ui);
    else {if(device)SDL_LockAudioDevice(device);ts_sister_runtime_set_parameters(&audio->sister,&sister->model.parameters);ts_sister_runtime_mark_selected_preset_modified(&audio->sister);if(device)SDL_UnlockAudioDevice(device);sister->rendered_model_valid=0;}
    ui->project_modified=1;matrix_window.dirty=1;
    if(down && e->button.button==SDL_BUTTON_LEFT && !steps)autopan_window.drag=k;
    if(autopan_window.drag<0)autopan_history_finish(device,audio,pages,bank);
    return 1;
}
