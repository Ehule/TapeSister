/* One modeless EQ window. Stable node/child identities survive reordering;
   an editor never redirects an old curve onto a replacement processor. */
static struct {
    SDL_Window *window;SDL_Renderer *renderer;SDL_Texture *texture;TsFramebuffer fb;
    TsUiState *model;Uint32 id,return_id,painted;int visible,source;
    uint32_t node,child;TsMosaic *context;uint64_t epoch;
    MatrixSnapshot *before;
} eq_editor;
static void eq_editor_hide(void)
{
    eq_editor.visible=0;
    if(eq_editor.model)eq_editor.model->master_eq_drag=0;
    if(eq_editor.window)SDL_HideWindow(eq_editor.window);
    free(eq_editor.before);eq_editor.before=NULL;
}
static void eq_editor_destroy(void)
{
    eq_editor_hide();free(eq_editor.model);
    if(eq_editor.texture)SDL_DestroyTexture(eq_editor.texture);
    if(eq_editor.renderer)SDL_DestroyRenderer(eq_editor.renderer);
    if(eq_editor.window)SDL_DestroyWindow(eq_editor.window);
    memset(&eq_editor,0,sizeof(eq_editor));
}
static TsMasterEqControls *eq_editor_curve(AudioState *audio,TsUiState *ui)
{
    TsMosaic *context=ui->mosaic_gallery?ui->mosaic_gallery->root:ui->mosaic;
    if(eq_editor.context!=context || (context && eq_editor.epoch!=context->epoch))return NULL;
    if(eq_editor.source<0)return &audio->sister.master_eq.live;
    if(eq_editor.source==0) {
        TsSisterFxControls *fx=&audio->sister.parameters.fx;
        for(int i=0;i<fx->slot_count;++i)if(fx->slot[i].id==eq_editor.child && fx->slot[i].type==TS_SISTER_FX_EQ)return &fx->slot[i].eq;
    } else for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i) {
        TsMatrixEffectControls *c=&audio->sister.matrix.controls.effect[i];if(c->id!=eq_editor.node)continue;
        if(eq_editor.source==1 && c->type==TS_MATRIX_FX_EQ)return &c->eq;
        if(eq_editor.source==2 && c->type==TS_MATRIX_FX_PEDALBOARD)
            for(int j=0;j<c->board.count;++j)if(c->board.slot[j].id==eq_editor.child && c->board.slot[j].type==TS_SISTER_FX_EQ)return &c->board.slot[j].eq;
    }
    return NULL;
}
static void eq_editor_open(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,int source,uint32_t node,uint32_t child,Uint32 origin)
{
    if(!eq_editor.window) {
        eq_editor.window=SDL_CreateWindow("TapeSister / EQ",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,960,600,
            TS_WORKSPACE_WINDOW_FLAGS|SDL_WINDOW_ALWAYS_ON_TOP);
        if(!eq_editor.window)return;
        eq_editor.renderer=SDL_CreateRenderer(eq_editor.window,-1,SDL_RENDERER_SOFTWARE);
        if(eq_editor.renderer)eq_editor.texture=SDL_CreateTexture(eq_editor.renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,640,400);
        eq_editor.model=calloc(1,sizeof(*eq_editor.model));
        if(!eq_editor.texture || !eq_editor.model){eq_editor_destroy();return;}
        eq_editor.id=SDL_GetWindowID(eq_editor.window);
    }
    eq_editor_hide();eq_editor.source=source;eq_editor.node=node;eq_editor.child=child;eq_editor.return_id=origin;
    eq_editor.context=ui->mosaic_gallery?ui->mosaic_gallery->root:ui->mosaic;
    eq_editor.epoch=eq_editor.context?eq_editor.context->epoch:0;
    TsUiState *model=eq_editor.model;ts_ui_init(model);model->palette=ui->palette;model->config=ui->config;
    model->effect_eq_open=source>=0;model->master_eq_spectrum_off=source>=0;
    model->master_output=ui->master_output;
    master_eq_open(device,audio,model);
    if(device)SDL_LockAudioDevice(device);
    TsMasterEqControls *curve=eq_editor_curve(audio,ui);if(curve)model->config.master_eq=*curve;
    if(device)SDL_UnlockAudioDevice(device);
    if(!curve)return;
    if(source>=0)snprintf(model->status,sizeof(model->status),"FIVE BANDS / PRESETS / CTRL-CLICK BAND: SOLO / ESC: CLOSE");
    SDL_SetWindowTitle(eq_editor.window,source<0?"TapeSister / Master EQ":"TapeSister / Effect EQ");
    eq_editor.visible=1;eq_editor.painted=0;SDL_ShowWindow(eq_editor.window);
    if(SDL_GetWindowFlags(eq_editor.window)&SDL_WINDOW_MINIMIZED)SDL_RestoreWindow(eq_editor.window);
    SDL_RaiseWindow(eq_editor.window);
}
static void eq_editor_history_finish(SDL_AudioDeviceID device,AudioState *audio,TsSamplePages *pages,TsInstrument *bank)
{
    if(!eq_editor.before)return;
    MatrixSnapshot *after=mx_snapshot(device,audio,pages,bank);
    if(after && !mx_equal(eq_editor.before,after))mx_push(eq_editor.before);else free(eq_editor.before);
    free(after);eq_editor.before=NULL;
}
static int eq_editor_publish(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,SisterWindow *sister)
{
    TsMasterEqControls curve=eq_editor.model->config.master_eq;
    if(eq_editor.source==0) {
        TsSisterParameters next=audio->sister.parameters;
        for(int i=0;i<next.fx.slot_count;++i)if(next.fx.slot[i].id==eq_editor.child)next.fx.slot[i].eq=curve;
        if(device)SDL_LockAudioDevice(device);
        ts_sister_runtime_set_parameters(&audio->sister,&next);ts_sister_runtime_mark_selected_preset_modified(&audio->sister);
        if(device)SDL_UnlockAudioDevice(device);
    } else {
        matrix_window.controls=audio->sister.matrix.controls;
        for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i){TsMatrixEffectControls *c=&matrix_window.controls.effect[i];if(c->id!=eq_editor.node)continue;
            if(eq_editor.source==1)c->eq=curve;
            else for(int j=0;j<c->board.count;++j)if(c->board.slot[j].id==eq_editor.child)c->board.slot[j].eq=curve;}
        mx_commit_controls(device,audio,ui);
    }
    sister_board_sync(audio,ui,sister);sister->rendered_model_valid=0;ui->project_modified=1;matrix_window.dirty=1;return 1;
}
static int eq_editor_event(SDL_Event *e,SDL_Window *main,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,SisterWindow *sister,TsSamplePages *pages,TsInstrument *bank)
{
    if(!eq_editor.visible || event_window_id(e)!=eq_editor.id)return 0;
    if(!eq_editor_curve(audio,ui)){eq_editor_hide();return 1;}
    if((e->type==SDL_WINDOWEVENT && e->window.event==SDL_WINDOWEVENT_CLOSE) ||
       (e->type==SDL_KEYDOWN && e->key.keysym.sym==SDLK_ESCAPE)) {
        eq_editor_history_finish(device,audio,pages,bank);eq_editor_hide();editor_return_window(eq_editor.return_id);return 1;
    }
    if(ts_window_fullscreen_shortcut(e))return 1;
    /* Transport and held-note releases belong to the actual workspace, not
       the private EQ rendering model. */
    if(e->type==SDL_KEYUP || (e->type==SDL_KEYDOWN &&
       (e->key.keysym.sym==SDLK_SPACE || (note_for_key(e->key.keysym.sym)>=0 &&
        !(e->key.keysym.mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI))) ||
        (e->key.keysym.sym==SDLK_s && (e->key.keysym.mod&KMOD_CTRL))))) {
        if(main)e->key.windowID=SDL_GetWindowID(main);
        return 0;
    }
    if(e->type==SDL_WINDOWEVENT && e->window.event==SDL_WINDOWEVENT_FOCUS_LOST) {
        eq_editor_history_finish(device,audio,pages,bank);eq_editor.model->master_eq_drag=0;return 1;
    }
    if(e->type==SDL_MOUSEBUTTONDOWN || e->type==SDL_MOUSEWHEEL || e->type==SDL_MOUSEMOTION) {
        int rx,ry,x,y;
        if(e->type==SDL_MOUSEWHEEL)wheel_event_mouse(e,&rx,&ry);
        else if(e->type==SDL_MOUSEMOTION){rx=e->motion.x;ry=e->motion.y;}
        else {rx=e->button.x;ry=e->button.y;}
        logical_mouse(eq_editor.window,rx,ry,&x,&y);
        if(e->type==SDL_MOUSEBUTTONDOWN && (y<38 || y>=379))return 1;
        if(ui->midi_learn_active) {
            TsUiState *model=eq_editor.model;model->master_eq_presets_open=0;
            if(eq_editor.source<0 && e->type==SDL_MOUSEBUTTONDOWN) {
                int node=ts_ui_master_eq_node(model,x,y);
                if(y>=252 && y<276 && x>=20 && x<448 && (x-20)%88<76)node=(x-20)/88;
                if(node>=0)model->master_eq_band=node;
                else {char target[TS_MIDI_TARGET_ID_MAX]={0};
                    ts_ui_master_eq_midi_target(model,x,y,target,sizeof(target));midi_learn_click(ui,sister,target);
                    snprintf(model->status,sizeof(model->status),"%s",ui->status);}
            }
            return 1;
        }
    }
    mx_bind_mosaic(ui,pages);matrix_window.layout=&audio->sister.matrix_layout;
    if(e->type==SDL_KEYDOWN && (e->key.keysym.mod&KMOD_CTRL) && (e->key.keysym.sym==SDLK_z || e->key.keysym.sym==SDLK_y)) {
        eq_editor_history_finish(device,audio,pages,bank);
        mx_undo(device,audio,ui,pages,bank,e->key.keysym.sym==SDLK_y || (e->key.keysym.mod&KMOD_SHIFT));return 1;
    }
    int edit=e->type==SDL_MOUSEBUTTONDOWN || e->type==SDL_MOUSEWHEEL;
    if(edit && !eq_editor.before){mx_original(device,audio,pages,bank);eq_editor.before=mx_snapshot(device,audio,pages,bank);if(!eq_editor.before)return 1;}
    TsUiState *model=eq_editor.model;
    model->config.master_output_percent=ui->config.master_output_percent;
    model->master_output=ui->master_output;
    if(device)SDL_LockAudioDevice(device);
    TsMasterEqControls *live=eq_editor_curve(audio,ui);if(live)model->config.master_eq=*live;
    if(device)SDL_UnlockAudioDevice(device);
    TsMasterEqControls before=model->config.master_eq;
    effect_eq_handling=eq_editor.source>=0;
    master_eq_event(e,eq_editor.window,device,audio,model,sister);
    effect_eq_handling=0;
    if(eq_editor.source>=0 && memcmp(&before,&model->config.master_eq,sizeof(before)))eq_editor_publish(device,audio,ui,sister);
    else if(eq_editor.source<0){ui->config.master_eq=model->config.master_eq;ui->config.eq_morph=model->config.eq_morph;
        ui->config.master_output_percent=model->config.master_output_percent;ui->master_output=model->master_output;
        if(edit)ui->project_modified=1;}
    if(e->type==SDL_MOUSEBUTTONUP || (edit && !model->master_eq_drag))eq_editor_history_finish(device,audio,pages,bank);
    if(!model->master_eq_open){eq_editor_history_finish(device,audio,pages,bank);eq_editor_hide();editor_return_window(eq_editor.return_id);}
    return 1;
}
static void eq_editor_spectrum(AudioState *audio,TsUiState *ui,int visible,Uint32 now)
{
    if(eq_editor.visible && eq_editor.source<0)master_eq_spectrum_tick(audio,eq_editor.model,1,now);
    else master_eq_spectrum_tick(audio,ui,visible,now);
}
static void eq_editor_paint(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    if(!eq_editor.visible || !window_drawable(eq_editor.window))return;
    if(SDL_GetTicks()-eq_editor.painted<33)return;eq_editor.painted=SDL_GetTicks();
    TsUiState *model=eq_editor.model;model->palette=ui->palette;model->master_output=ui->master_output;
    if(device)SDL_LockAudioDevice(device);
    TsMasterEqControls *c=eq_editor_curve(audio,ui);
    if(c)model->config.master_eq=*c;
    if(eq_editor.source<0)master_eq_snapshot(audio,model);
    if(device)SDL_UnlockAudioDevice(device);
    if(!c){eq_editor_hide();return;}
    ts_ui_render_eq(&eq_editor.fb,model);
    SDL_UpdateTexture(eq_editor.texture,NULL,eq_editor.fb.pixels,640*sizeof(uint32_t));
    SDL_RenderClear(eq_editor.renderer);SDL_RenderCopy(eq_editor.renderer,eq_editor.texture,NULL,NULL);SDL_RenderPresent(eq_editor.renderer);
}
