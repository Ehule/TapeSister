#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>
static AudioState audio;
static TsUiState ui;
static TsInstrument bank;
static TsSamplePages pages;
static SisterWindow sister;
static void input(int x,int y,Uint8 button)
{
 SDL_Event e;SDL_zero(e);e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=character_filter_window.id;e.button.x=x*3/2;e.button.y=y*3/2;e.button.button=button;
 assert(character_filter_event(&e,0,&audio,&ui,&sister,&pages,&bank));
 e.type=SDL_MOUSEBUTTONUP;assert(character_filter_event(&e,0,&audio,&ui,&sister,&pages,&bank));
}
static void shot(const char *path)
{
 character_filter_window.painted=SDL_GetTicks()-40;character_filter_paint(&audio,&ui,&sister);
 SDL_Surface *s=SDL_CreateRGBSurfaceFrom(character_filter_window.fb.pixels,640,400,32,2560,0xff0000,0xff00,0xff,0);
 assert(s && !SDL_SaveBMP(s,path));SDL_FreeSurface(s);
}
int main(int argc,char **argv)
{
 SDL_SetMainReady();SDL_setenv("SDL_VIDEODRIVER","dummy",1);assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER));
 ts_ui_init(&ui);ts_instrument_init(&bank);char error[256];assert(ts_sample_pages_init(&pages,error,sizeof(error)));
 ts_sister_runtime_init(&audio.sister);audio.output_rate=48000;audio.output_device_channels=2;
 assert(ts_sister_runtime_enable(&audio.sister,48000,2,2,5,error,sizeof(error)));
 ts_sister_ui_model_init(&sister.model,&ui.config);sister.model.parameters=audio.sister.parameters;sister.model.fx_page=1;
 sister_fx_slot_select_type(&sister.model.parameters.fx.slot[2],TS_SISTER_FX_FILTER);
 ts_sister_runtime_set_parameters(&audio.sister,&sister.model.parameters);
 TsSisterUiHit edit=ts_sister_ui_hit_test_model(&sister.model,350,170);
 assert(edit.action==TS_SISTER_UI_ACTION_FILTER_EDIT && edit.index==2);
 sister_apply_action(0,&audio,&ui,&bank,&sister,NULL,NULL,edit,48000,2);
 assert(character_filter_window.visible && !character_filter_window.source);
 input(310,16,SDL_BUTTON_LEFT); /* Dual mono. */
 assert(sister.model.parameters.fx.slot[2].filter.value[TS_CF_ROUTE]==1);
 input(420,74,SDL_BUTTON_LEFT);assert(sister.model.parameters.fx.slot[2].filter.value[TS_CF_R_MODEL]==1);
 input(420,108,SDL_BUTTON_LEFT);assert(sister.model.parameters.fx.slot[2].filter.value[TS_CF_R_MODE]==0);
 input(170,148,SDL_BUTTON_LEFT);float res=sister.model.parameters.fx.slot[2].filter.value[TS_CF_L_RES];
 assert(res>=0 && res<1 && audio.sister.parameters.fx.slot[2].filter.value[TS_CF_L_RES]==res);
 SDL_SetModState(KMOD_SHIFT);input(170,148,SDL_BUTTON_LEFT);SDL_SetModState(KMOD_NONE);
 assert(sister.model.parameters.fx.slot[2].filter.locks&(1u<<TS_CF_L_RES));input(280,148,SDL_BUTTON_LEFT);
 assert(sister.model.parameters.fx.slot[2].filter.value[TS_CF_L_RES]==res);
 input(30,382,SDL_BUTTON_LEFT);assert(sister.model.parameters.fx.slot[2].filter.captured==1);
 TsSisterUiHit midi;int id=TS_CF_MIDI_BASE+2*32+TS_CF_L_CUT;char target[80];snprintf(target,sizeof(target),"sister.param.%d",id);
 assert(midi_sister_hit_from_target(target,.35f,&midi));sister_apply_action(0,&audio,&ui,&bank,&sister,NULL,NULL,midi,48000,2);
 assert(sister.model.parameters.fx.slot[2].filter.value[TS_CF_L_CUT]==.35f);
 midi_learn_set_active(&ui,&sister,1);input(65,148,SDL_BUTTON_LEFT);assert(!strcmp(ui.midi_learn_pending,target));midi_learn_set_active(&ui,&sister,0);
 sister.model.parameters.fx.slot[2].filter.locks=0;
 ts_character_filter_preset(&sister.model.parameters.fx.slot[2].filter,3);character_filter_publish(0,&audio,&ui,&sister);
 if(argc>1)shot(argv[1]);
 character_filter_hide();sister.model.fx_page=0;
 TsSisterUiHit blend=ts_sister_ui_hit_test_model(&sister.model,164,48);assert(blend.index==TS_MEMORY_PARAM_BLEND);
 sister_apply_action(0,&audio,&ui,&bank,&sister,NULL,NULL,blend,48000,2);assert(audio.sister.parameters.memory_blend>.4f && audio.sister.parameters.memory_blend<.6f);
 assert(midi_sister_hit_from_target("sister.param.3011",.5f,&midi));sister_apply_action(0,&audio,&ui,&bank,&sister,NULL,NULL,midi,48000,2);assert(audio.sister.parameters.memory_fade_seconds>1 && ui.config.sister_memory_fade_seconds==audio.sister.parameters.memory_fade_seconds);
 assert(ts_sister_ui_midi_target(blend,target,sizeof(target)) && !strcmp(target,"sister.param.3010"));
 TsMatrixControls controls=audio.sister.matrix.controls;controls.enabled=1;int slot=ts_matrix_effect_add(&controls,TS_MATRIX_FX_FILTER);assert(slot>=0);
 TsMatrixEffectPlan plan;assert(ts_matrix_effects_stage(&audio.sister.matrix_effects,&controls,48000,&plan));ts_matrix_effects_commit(&audio.sister.matrix_effects,&controls,48000,&plan);assert(ts_matrix_set(&audio.sister.matrix,&controls,48000));
 matrix_window.controls=controls;matrix_window.original_active=0;character_filter_matrix_open(controls.effect[slot].id);
 input(310,16,SDL_BUTTON_LEFT);input(420,74,SDL_BUTTON_LEFT);
 assert(audio.sister.matrix.controls.effect[slot].filter.value[TS_CF_R_MODEL]==1);
 snprintf(target,sizeof(target),"matrix.filter.%u.%d",controls.effect[slot].id,TS_CF_L_CUT);
 assert(ts_midi_target_is_continuous(target));assert(character_filter_matrix_midi(0,&audio,&ui,target,.27f));assert(audio.sister.matrix.controls.effect[slot].filter.value[TS_CF_L_CUT]==.27f);
 character_filter_close();matrix_history_clear();ts_sister_runtime_disable(&audio.sister);ts_sample_pages_free(&pages);ts_instrument_free(&bank);SDL_Quit();
 puts("Character filter native editor, MIDI, locks, states, Matrix and crossfade controls passed.");return 0;
}
