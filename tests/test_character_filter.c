#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/character_filter.h"
#include "tapesister/config.h"
#include "tapesister/sister_post_fx.h"
#include "tapesister/sister_machine.h"
#include "tapesister/sister_project_state.h"
#include "tapesister/sister_preset.h"
#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
static float energy(TsCharacterFilterControls c,float hz)
{
 TsCharacterFilter f;ts_character_filter_init(&f,48000);double e=0;
 for(int i=0;i<24000;++i){float v=.1f*sinf(6.2831853f*hz*i/48000);TsStereoFrame o=ts_character_filter_process(&f,&c,(TsStereoFrame){v,v},1,0,1);if(i>12000)e+=o.l*o.l;}
 return (float)e;
}
static void dsp(void)
{
 const unsigned rates[]={1000,44100,48000,96000,192000};
 for(unsigned r=0;r<5;++r)for(int p=0;p<TS_CF_PRESETS;++p){
  TsCharacterFilter f;TsCharacterFilterControls c;ts_character_filter_init(&f,rates[r]);ts_character_filter_default(&c);ts_character_filter_preset(&c,p);
  for(unsigned i=0;i<rates[r]/4+1000;++i){float v=.5f*sinf(i*.071f);TsStereoFrame o=ts_character_filter_process(&f,&c,(TsStereoFrame){v,0},1,0,1);
   assert(isfinite(o.l) && isfinite(o.r) && fabsf(o.l)<8.1f && fabsf(o.r)<8.1f);assert(o.r==0);
   if(i==100){c.value[TS_CF_L_RES]=c.value[TS_CF_R_RES]=1;c.value[TS_CF_L_DRIVE]=c.value[TS_CF_R_DRIVE]=1;}
  }
  TsStereoFrame o=ts_character_filter_process(&f,&c,(TsStereoFrame){NAN,INFINITY},1,0,1);assert(isfinite(o.l) && isfinite(o.r));
 }
 TsCharacterFilter a,b;TsCharacterFilterControls c;ts_character_filter_default(&c);c.value[TS_CF_LFO_DEPTH]=.7f;
 ts_character_filter_init(&a,48000);ts_character_filter_init(&b,48000);
 for(int i=0;i<48000;++i){float v=.1f*sinf(i*.12f);TsStereoFrame o=ts_character_filter_process(&a,&c,(TsStereoFrame){v,v},1,0,1);assert(o.l==o.r);}
 ts_character_filter_preset(&c,3);TsCharacterFilterControls d=c;d.value[TS_CF_L_MODEL]=1;d.value[TS_CF_L_DRIVE]=1;d.value[TS_CF_ENV_AMOUNT]=c.value[TS_CF_ENV_AMOUNT]=.8f;
 ts_character_filter_init(&a,48000);ts_character_filter_init(&b,48000);double different=0;
 for(int i=0;i<24000;++i){TsStereoFrame v={.1f*sinf(i*.123f),.09f*cosf(i*.091f)};
  TsStereoFrame x=ts_character_filter_process(&a,&c,v,1,0,1),y=ts_character_filter_process(&b,&d,v,1,0,1);
  assert(x.r==y.r);different+=fabs(x.l-y.l);
 }assert(different>1);
 ts_character_filter_preset(&c,4);ts_character_filter_init(&a,48000);different=0;
 for(int i=0;i<24000;++i){float v=.1f*sinf(i*.13f);TsStereoFrame x=ts_character_filter_process(&a,&c,(TsStereoFrame){v,v},1,0,1);different+=fabs(x.l-x.r);}assert(different>1);
 ts_character_filter_default(&c);c.value[TS_CF_L_CUT]=ts_character_filter_cutoff_normalized(1000);
 float low=energy(c,100),high=energy(c,8000);assert(low>high*10);
 c.value[TS_CF_L_HP]=ts_character_filter_cutoff_normalized(3000);assert(energy(c,100)<low*.05f);
 ts_character_filter_preset(&c,6);float band_low=energy(c,80),band_mid=energy(c,1800);assert(band_mid>band_low*2);
 ts_character_filter_init(&a,48000);
 for(int i=0;i<48000;++i)ts_character_filter_process(&a,&c,(TsStereoFrame){.1f,.2f},1,0,1);
 for(int i=0;i<48000;++i)ts_character_filter_process(&a,&c,(TsStereoFrame){.1f,.2f},1,0,0);
 TsStereoFrame dry=ts_character_filter_process(&a,&c,(TsStereoFrame){.1f,.2f},1,0,0);assert(fabsf(dry.l-.1f)<1e-6f && fabsf(dry.r-.2f)<1e-6f);
}
static void persistence(void)
{
 TsCharacterFilterControls c,d;ts_character_filter_default(&c);ts_character_filter_preset(&c,3);c.locks=1u<<TS_CF_L_DRIVE;
 for(int i=0;i<5;++i){for(int f=0;f<TS_CF_COUNT;++f)c.value[f]=(float)((i*17+f*11)%101)/100;ts_character_filter_capture(&c,i);}
 FILE *f=tmpfile();assert(f && ts_character_filter_write(f,"Filter",&c));rewind(f);char line[4096];assert(fgets(line,sizeof(line),f));fclose(f);assert(ts_character_filter_read(&d,strchr(line,'=')+1));assert(!memcmp(&c,&d,sizeof(c)));
 assert(!ts_character_filter_read(&d,"0,0,0,0,nan"));
 float locked=c.value[TS_CF_L_DRIVE];ts_character_filter_preset(&c,2);assert(c.value[TS_CF_L_DRIVE]==locked);
 static TsSisterProjectState state,loaded;ts_sister_project_state_init(&state,48000);
 state.parameters.fx.slot[2].type=TS_SISTER_FX_FILTER;state.parameters.fx.slot[2].filter=c;
 state.parameters.memory_blend=.37f;state.parameters.memory_fade_seconds=12.5f;
 char error[256];int present;assert(ts_sister_project_state_save_file(&state,"character-filter-test.sister",error,sizeof(error)));
 assert(ts_sister_project_state_load_file(&loaded,"character-filter-test.sister",48000,&present,error,sizeof(error)));
 assert(!memcmp(&loaded.parameters.fx.slot[2].filter,&c,sizeof(c)));assert(loaded.parameters.memory_blend==.37f && loaded.parameters.memory_fade_seconds==12.5f);remove("character-filter-test.sister");
 static TsSisterPresetBank bank,loaded_bank;ts_sister_preset_bank_init(&bank,48000);
 assert(ts_sister_preset_save_new(&bank,"FILTER TEST",&state.parameters,48000,error,sizeof(error)));
 assert(ts_sister_preset_save(&bank,"character-filter-test.presets",error,sizeof(error)));
 assert(ts_sister_preset_load(&loaded_bank,"character-filter-test.presets",48000,error,sizeof(error)));
 assert(!memcmp(&loaded_bank.entries[loaded_bank.count-1].parameters.fx.slot[2].filter,&c,sizeof(c)));remove("character-filter-test.presets");
 static TsConfig config,loaded_config;ts_config_init(&config);
 int node=ts_matrix_effect_add(&config.matrix,TS_MATRIX_FX_FILTER);assert(node>=0);
 config.matrix.effect[node].filter=c;config.sister_memory_blend=.43f;config.sister_memory_fade_seconds=9.25f;
 assert(ts_config_save(&config,"character-filter-test.ini",error,sizeof(error)));
 assert(ts_config_load(&loaded_config,"character-filter-test.ini",error,sizeof(error)));
 assert(!memcmp(&loaded_config.matrix.effect[node].filter,&c,sizeof(c)));
 assert(loaded_config.sister_memory_blend==.43f && loaded_config.sister_memory_fade_seconds==9.25f);
 assert(TS_MATRIX_FX_COMPRESSOR==6 && !strcmp(ts_matrix_effect_name(6),"COMPRESSOR"));remove("character-filter-test.ini");
}
static void crossfade(void)
{
 TsSisterMachine m;assert(ts_sister_machine_init(&m,1000,2,5));TsSisterParameters p=m.parameters;
 TsStereoFrame *dark=m.dark.storage;float *tape=m.buffer.data;
 p.memory_mode=TS_SISTER_MEMORY_DARK;p.memory_fade_seconds=2;ts_sister_machine_set_parameters(&m,&p);
 assert(m.dark_blend.remaining==2000);
 for(int i=0;i<1000;++i){ts_sister_machine_process_frame(&m,(TsStereoFrame){.1f,-.1f},(TsStereoFrame){0,0});if(i%100==0)ts_sister_machine_set_parameters(&m,&p);}
 assert(fabsf(m.dark_blend.current-.5f)<.001f && m.dark_blend.remaining==1000);
 p.memory_fade_seconds=4;ts_sister_machine_set_parameters(&m,&p);assert(m.dark_blend.remaining==4000);
 p.memory_blend=.37f;ts_sister_machine_set_parameters(&m,&p);assert(m.dark_blend.remaining==50);
 for(int i=0;i<50;++i)ts_sister_machine_process_frame(&m,(TsStereoFrame){0,0},(TsStereoFrame){0,0});
 assert(m.dark_blend.current==.37f && m.dark.storage==dark && m.buffer.data==tape);
 p.memory_blend=-1;p.memory_mode=TS_SISTER_MEMORY_TAPE;p.memory_fade_seconds=.02f;ts_sister_machine_set_parameters(&m,&p);
 for(int i=0;i<20;++i)ts_sister_machine_process_frame(&m,(TsStereoFrame){0,0},(TsStereoFrame){0,0});assert(m.dark_blend.current==0);
 ts_sister_machine_free(&m);
}
int main(void){dsp();persistence();crossfade();puts("Character filters: stereo, nonlinear cores, modulation, presets, saves and timed/manual crossfades passed.");return 0;}
