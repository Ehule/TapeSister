/* Modeless native editor. Controls belong to the slot or stable Matrix ID;
   this window never owns DSP histories or an audio device. */
static struct {
 SDL_Window *window;SDL_Renderer *renderer;SDL_Texture *texture;
 TsFramebuffer fb;Uint32 id,painted;int visible,source,slot,drag,origin_x;
 uint32_t node;float origin;TsMosaic *context;uint64_t epoch;
 MatrixSnapshot *before;
} character_filter_window={.drag=-1};
static void character_filter_hide(void)
{
 character_filter_window.visible=0;character_filter_window.drag=-1;
 free(character_filter_window.before);character_filter_window.before=NULL;
 if(character_filter_window.window)SDL_HideWindow(character_filter_window.window);
}
static int character_filter_ensure(void)
{
 if(character_filter_window.window)return 1;
 character_filter_window.window=SDL_CreateWindow("TapeSister / Filter",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,960,600,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
 if(!character_filter_window.window)return 0;
 character_filter_window.renderer=SDL_CreateRenderer(character_filter_window.window,-1,SDL_RENDERER_SOFTWARE);
 if(!character_filter_window.renderer){SDL_DestroyWindow(character_filter_window.window);character_filter_window.window=NULL;return 0;}
 character_filter_window.texture=SDL_CreateTexture(character_filter_window.renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,640,400);
 if(!character_filter_window.texture){SDL_DestroyRenderer(character_filter_window.renderer);SDL_DestroyWindow(character_filter_window.window);memset(&character_filter_window,0,sizeof(character_filter_window));character_filter_window.drag=-1;return 0;}
 character_filter_window.id=SDL_GetWindowID(character_filter_window.window);return 1;
}
static void character_filter_open(SisterWindow *sister,int slot)
{
 if(slot<0 || slot>=sister->model.parameters.fx.slot_count || sister->model.parameters.fx.slot[slot].type!=TS_SISTER_FX_FILTER)return;
 if(!character_filter_ensure())return;
 character_filter_window.source=0;character_filter_window.slot=slot;character_filter_window.visible=1;character_filter_window.context=NULL;character_filter_window.drag=-1;
 SDL_ShowWindow(character_filter_window.window);SDL_RaiseWindow(character_filter_window.window);
}
static void character_filter_matrix_open(uint32_t id)
{
 if(!character_filter_ensure())return;
 character_filter_window.source=1;character_filter_window.node=id;character_filter_window.visible=1;character_filter_window.context=NULL;character_filter_window.drag=-1;
 SDL_ShowWindow(character_filter_window.window);SDL_RaiseWindow(character_filter_window.window);
}
static int character_filter_resolve(AudioState *audio,TsUiState *ui,SisterWindow *sister,
 TsCharacterFilterControls **c,float **gain,float **mix,int **enabled)
{
 if(!character_filter_window.visible)return 0;
 if(!character_filter_window.context){character_filter_window.context=ui->mosaic;character_filter_window.epoch=ui->mosaic?ui->mosaic->epoch:0;}
 else if(character_filter_window.context!=ui->mosaic || (ui->mosaic && character_filter_window.epoch!=ui->mosaic->epoch)){character_filter_hide();return 0;}
 if(character_filter_window.source){
  for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i){TsMatrixEffectControls *p=&matrix_window.controls.effect[i];
   if(p->id!=character_filter_window.node)continue;
   if(p->type!=TS_MATRIX_FX_FILTER)break;
   *c=&p->filter;*gain=&p->gain_db;*mix=&p->mix;*enabled=&p->enabled;return 1;
  }
 }else{
  int at=character_filter_window.slot;
  if(at>=0 && at<sister->model.parameters.fx.slot_count){TsSisterFxSlotControls *p=&sister->model.parameters.fx.slot[at];
   if(p->type==TS_SISTER_FX_FILTER){*c=&p->filter;*gain=&p->gain_db;*mix=&p->mix;*enabled=&p->enabled;return 1;}
  }
 }
 (void)audio;character_filter_hide();return 0;
}
static void character_filter_publish(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,SisterWindow *sister)
{
 if(character_filter_window.source)mx_commit_controls(device,audio,ui);
 else{
  if(device)SDL_LockAudioDevice(device);
  ts_sister_runtime_set_parameters(&audio->sister,&sister->model.parameters);
  ts_sister_runtime_mark_selected_preset_modified(&audio->sister);
  if(device)SDL_UnlockAudioDevice(device);
  sister->rendered_model_valid=0;
 }
 ui->project_modified=1;
}
static void character_filter_history_finish(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
 if(!character_filter_window.before)return;
 MatrixSnapshot *after=mx_snapshot(device,audio,pages,bank);
 if(after && !mx_equal(character_filter_window.before,after))mx_push(character_filter_window.before);
 else free(character_filter_window.before);
 free(after);character_filter_window.before=NULL;(void)ui;
}
static void character_filter_paint(AudioState *audio,TsUiState *ui,SisterWindow *sister)
{
 if(!character_filter_window.visible || SDL_GetTicks()-character_filter_window.painted<33)return;
 TsCharacterFilterControls *c;float *gain,*mix;int *enabled;
 if(!character_filter_resolve(audio,ui,sister,&c,&gain,&mix,&enabled))return;
 unsigned shared_locks=0;
 if(!character_filter_window.source){int slot=character_filter_window.slot;shared_locks=ts_sister_ui_parameter_locked(&sister->model,TS_SISTER_UI_SLOT_PARAMETER(slot,0)) | (ts_sister_ui_parameter_locked(&sister->model,TS_SISTER_UI_SLOT_PARAMETER(slot,4))?16:0);}
 ts_sister_filter_render(&character_filter_window.fb,c,*gain,*mix,*enabled,shared_locks,&ui->palette);
 SDL_UpdateTexture(character_filter_window.texture,NULL,character_filter_window.fb.pixels,640*sizeof(uint32_t));
 SDL_RenderClear(character_filter_window.renderer);SDL_RenderCopy(character_filter_window.renderer,character_filter_window.texture,NULL,NULL);SDL_RenderPresent(character_filter_window.renderer);
 character_filter_window.painted=SDL_GetTicks();
}
static int character_filter_event(const SDL_Event *e,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,SisterWindow *sister,TsSamplePages *pages,TsInstrument *bank)
{
 if(!character_filter_window.visible || event_window_id(e)!=character_filter_window.id)return 0;
 if(e->type==SDL_WINDOWEVENT && (e->window.event==SDL_WINDOWEVENT_CLOSE || e->window.event==SDL_WINDOWEVENT_FOCUS_LOST)){
  character_filter_history_finish(device,audio,ui,pages,bank);character_filter_window.drag=-1;
  if(e->window.event==SDL_WINDOWEVENT_CLOSE)character_filter_hide();return 1;
 }
 if(e->type==SDL_KEYDOWN){
  if(e->key.keysym.sym==SDLK_ESCAPE){character_filter_history_finish(device,audio,ui,pages,bank);character_filter_hide();return 1;}
  if(e->key.keysym.sym==SDLK_m && (e->key.keysym.mod&KMOD_CTRL) && (e->key.keysym.mod&KMOD_SHIFT)){
   midi_learn_set_active(ui,sister,!ui->midi_learn_active);return 1;}
  return 0;
 }
 if(e->type==SDL_MOUSEBUTTONUP){character_filter_history_finish(device,audio,ui,pages,bank);character_filter_window.drag=-1;return 1;}
 int down=e->type==SDL_MOUSEBUTTONDOWN,motion=e->type==SDL_MOUSEMOTION,wheel=e->type==SDL_MOUSEWHEEL;
 if(!down && !motion && !wheel)return 0;
 TsCharacterFilterControls *c;float *gain,*mix;int *enabled;
 if(!character_filter_resolve(audio,ui,sister,&c,&gain,&mix,&enabled))return 1;
 int rawx,rawy,x,y;if(down){rawx=e->button.x;rawy=e->button.y;}else if(motion){rawx=e->motion.x;rawy=e->motion.y;}else wheel_event_mouse(e,&rawx,&rawy);
 if(!sister_event_mouse(character_filter_window.window,rawx,rawy,&x,&y))return 1;
 float value;int field=ts_character_filter_hit(c,x,y,&value),left=down && e->button.button==SDL_BUTTON_LEFT;
 int id=field>=0 && field<TS_CF_COUNT?TS_CF_MIDI_BASE+character_filter_window.slot*32+field:-1;
 if(ui->midi_learn_active){if(left && field>=0){char target[80];
   if(character_filter_window.source)snprintf(target,sizeof(target),"matrix.filter.%u.%d",character_filter_window.node,field);
   else if(field<TS_CF_COUNT)snprintf(target,sizeof(target),"sister.param.%d",id);
   else{TsSisterUiHit h={TS_SISTER_UI_ACTION_PARAMETER,TS_SISTER_UI_SLOT_PARAMETER(character_filter_window.slot,field==100?4:0),0};ts_sister_ui_midi_target(h,target,sizeof(target));}
   midi_learn_click(ui,sister,target);}return 1;}
 if(motion && character_filter_window.drag<0)return 1;
 if(motion){if(!(e->motion.state&SDL_BUTTON_LMASK)){character_filter_history_finish(device,audio,ui,pages,bank);character_filter_window.drag=-1;return 1;}
  field=character_filter_window.drag;value=SDL_clamp(character_filter_window.origin+(x-character_filter_window.origin_x)/((SDL_GetModState()&KMOD_SHIFT)?2500.f:250.f),0,1);}
 if(field==-20){character_filter_history_finish(device,audio,ui,pages,bank);character_filter_hide();return 1;}
 if(field==-1)return 1;
 if(character_filter_window.source && !mx_editable())return 1;
 if(character_filter_window.source && !character_filter_window.before){character_filter_window.before=mx_snapshot(device,audio,pages,bank);if(!character_filter_window.before)return 1;}
 int changed=0,discrete=field==TS_CF_ROUTE || field==TS_CF_FOCUS || field==TS_CF_L_MODEL || field==TS_CF_R_MODEL || field==TS_CF_L_MODE || field==TS_CF_R_MODE;
 if(field>=0 && field<TS_CF_COUNT){
  if(left && (SDL_GetModState()&KMOD_SHIFT)){c->locks^=1u<<field;changed=1;}
  else if(!(c->locks&(1u<<field))){
   if(wheel)value=SDL_clamp(c->value[field]+wheel_event_delta(e,0)*((SDL_GetModState()&KMOD_SHIFT)?.002f:.02f),0,1);
   else if(down && e->button.button==SDL_BUTTON_RIGHT){TsCharacterFilterControls d;ts_character_filter_default(&d);value=d.value[field];}
   else if(!left && !motion)return 1;
   ts_character_filter_set(c,field,value);changed=1;
   if(left && !discrete){character_filter_window.drag=field;character_filter_window.origin=c->value[field];character_filter_window.origin_x=x;}
  }
 }else if(field==100 || field==101){
  if(!character_filter_window.source){int parameter=TS_SISTER_UI_SLOT_PARAMETER(character_filter_window.slot,field==100?4:0);
   if(left && (SDL_GetModState()&KMOD_SHIFT)){if(device)SDL_LockAudioDevice(device);sister_toggle_parameter_lock(audio,sister,parameter);if(device)SDL_UnlockAudioDevice(device);return 1;}
   if(ts_sister_ui_parameter_locked(&sister->model,parameter))return 1;
  }
  float old=field==100?*mix:(*gain+12)/24;
  if(wheel)value=SDL_clamp(old+wheel_event_delta(e,0)*.02f,0,1);
  else if(down && e->button.button==SDL_BUTTON_RIGHT)value=field==100?1:.5f;
  else if(!left && !motion)return 1;
  if(field==100)*mix=value;else *gain=value*24-12;changed=1;
  if(left){character_filter_window.drag=field;character_filter_window.origin=value;character_filter_window.origin_x=x;}
 }else if(left || (down && e->button.button==SDL_BUTTON_MIDDLE)){
  if(field==-10)*enabled=!*enabled;
  else if(field==-2 || field==-3)ts_character_filter_preset(c,(c->preset+(field==-2?TS_CF_PRESETS-1:1))%TS_CF_PRESETS);
  else if(field==-4)ts_character_filter_capture(c,c->selected?c->selected-1:0);
  else if(field<=-30 && field>-35){int at=-30-field;
   if(e->button.button==SDL_BUTTON_MIDDLE){c->captured&=~(1u<<at);if(c->selected==at+1)c->selected=0;}
   else if((SDL_GetModState()&KMOD_SHIFT) || !(c->captured&(1u<<at)))ts_character_filter_capture(c,at);
   else ts_character_filter_recall(c,at);
  }else return 1;changed=1;
 }
 if(changed)character_filter_publish(device,audio,ui,sister);
 if(character_filter_window.drag<0)character_filter_history_finish(device,audio,ui,pages,bank);
 return 1;
}
static int character_filter_matrix_midi(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,const char *target,float value)
{
 unsigned id;int field;char tail;
 if(sscanf(target,"matrix.filter.%u.%d%c",&id,&field,&tail)!=2)return 0;
 if(field<0 || (field>=TS_CF_COUNT && field!=100 && field!=101))return 0;
 matrix_window.controls=audio->sister.matrix.controls;
 for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i){TsMatrixEffectControls *c=&matrix_window.controls.effect[i];if(c->id!=id || c->type!=TS_MATRIX_FX_FILTER)continue;
  if(field==100)c->mix=SDL_clamp(value,0,1);else if(field==101)c->gain_db=SDL_clamp(value,0,1)*24-12;else ts_character_filter_set(&c->filter,field,value);
  mx_commit_controls(device,audio,ui);ui->project_modified=1;return 1;}
 return 1; /* Deleted stable IDs are never redirected to a reused port. */
}
static void character_filter_close(void)
{
 free(character_filter_window.before);if(character_filter_window.texture)SDL_DestroyTexture(character_filter_window.texture);
 if(character_filter_window.renderer)SDL_DestroyRenderer(character_filter_window.renderer);
 if(character_filter_window.window)SDL_DestroyWindow(character_filter_window.window);
 memset(&character_filter_window,0,sizeof(character_filter_window));character_filter_window.drag=-1;
}
