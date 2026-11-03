#include "tapesister/character_filter.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
static float clamp(float x,float lo,float hi){return !isfinite(x)?lo:fmaxf(lo,fminf(hi,x));}
static float unit(float x){return clamp(x,0,1);}
/* Bounded rational soft clipping: unity slope, no transcendental per pole. */
static float soft(float x){x=clamp(x,-8,8);return x*(27+x*x)/(27+9*x*x);}
float ts_character_filter_cutoff(float x){return 20*powf(1000,unit(x));}
float ts_character_filter_cutoff_normalized(float hz){return unit(logf(fmaxf(20,hz)/20)/logf(1000));}
void ts_character_filter_default(TsCharacterFilterControls *c)
{
 memset(c,0,sizeof(*c));c->initialized=1;
 c->value[TS_CF_L_CUT]=c->value[TS_CF_R_CUT]=ts_character_filter_cutoff_normalized(18000);
 c->value[TS_CF_L_RES]=c->value[TS_CF_R_RES]=.1f;
 c->value[TS_CF_L_TRIM]=c->value[TS_CF_R_TRIM]=.5f;
 c->value[TS_CF_SPREAD_AMOUNT]=c->value[TS_CF_ENV_AMOUNT]=.5f;
 c->value[TS_CF_LFO_RATE]=.25f;c->value[TS_CF_LFO_PHASE]=1;
 c->value[TS_CF_ENV_ATTACK]=.4f;c->value[TS_CF_ENV_RELEASE]=.5f;
}
int ts_character_filter_valid(const TsCharacterFilterControls *c)
{
 if(!c || c->initialized!=1 || c->preset>=TS_CF_PRESETS || c->captured>31 || c->selected>5 || (c->locks>>TS_CF_COUNT))return 0;
 for(int i=0;i<TS_CF_COUNT;++i)if(!isfinite(c->value[i]) || c->value[i]<0 || c->value[i]>1)return 0;
 for(int k=0;k<5;++k)if(c->captured&(1u<<k))for(int i=0;i<TS_CF_COUNT;++i)
  if(!isfinite(c->state[k][i]) || c->state[k][i]<0 || c->state[k][i]>1)return 0;
 return 1;
}
void ts_character_filter_sanitize(TsCharacterFilterControls *c)
{
 if(!c->initialized){ts_character_filter_default(c);return;}
 for(int i=0;i<TS_CF_COUNT;++i)c->value[i]=unit(c->value[i]);
 c->locks&=(1u<<TS_CF_COUNT)-1;c->captured&=31;c->selected=c->selected<=5?c->selected:0;
 c->preset=c->preset<TS_CF_PRESETS?c->preset:0;
 for(int k=0;k<5;++k)for(int i=0;i<TS_CF_COUNT;++i)c->state[k][i]=unit(c->state[k][i]);
 c->initialized=1;
}
void ts_character_filter_set(TsCharacterFilterControls *c,int field,float x)
{
 if(field<0 || field>=TS_CF_COUNT || (c->locks&(1u<<field)))return;
 c->value[field]=unit(x);
}
static const char *names[TS_CF_PRESETS]={"OPEN TAPE","MS-20 BITE","POLIVOKS SNARL","SPLIT CURRENT","SLOW OPPOSITES","TOUCH FILTER","HOLLOW BAND"};
const char *ts_character_filter_preset_name(int i){return i>=0 && i<TS_CF_PRESETS?names[i]:"CUSTOM";}
void ts_character_filter_preset(TsCharacterFilterControls *c,int index)
{
 if(index<0 || index>=TS_CF_PRESETS)return;
 TsCharacterFilterControls p;ts_character_filter_default(&p);
 for(int ch=0;ch<2;++ch){int b=ch*8;
  if(index){p.value[TS_CF_L_CUT+b]=ts_character_filter_cutoff_normalized(index==5?600:1800);p.value[TS_CF_L_RES+b]=.68f;p.value[TS_CF_L_DRIVE+b]=.28f;}
  if(index==2 || index==6 || (index==3 && ch))p.value[TS_CF_L_MODEL+b]=1;
  if(index==2){p.value[TS_CF_L_RES+b]=.82f;p.value[TS_CF_L_DRIVE+b]=.4f;}
  if(index==6){p.value[TS_CF_L_MODE+b]=1;p.value[TS_CF_L_RES+b]=.5f;}
 }
 if(index==3){p.value[TS_CF_ROUTE]=1;p.value[TS_CF_R_CUT]=ts_character_filter_cutoff_normalized(1650);p.value[TS_CF_L_HP]=ts_character_filter_cutoff_normalized(110);p.value[TS_CF_L_HP_RES]=.32f;}
 if(index==4){p.value[TS_CF_ROUTE]=.5f;p.value[TS_CF_SPREAD_AMOUNT]=.65f;p.value[TS_CF_LFO_DEPTH]=.25f;p.value[TS_CF_LFO_RATE]=.18f;}
 if(index==5)p.value[TS_CF_ENV_AMOUNT]=.75f;
 for(int i=0;i<TS_CF_COUNT;++i)if(!(c->locks&(1u<<i)))c->value[i]=p.value[i];
 c->initialized=1;c->preset=(uint8_t)index;c->selected=0;
}
void ts_character_filter_capture(TsCharacterFilterControls *c,int i)
{if(i>=0 && i<5){memcpy(c->state[i],c->value,sizeof(c->value));c->captured|=1u<<i;c->selected=i+1;}}
int ts_character_filter_recall(TsCharacterFilterControls *c,int i)
{if(i<0 || i>=5 || !(c->captured&(1u<<i)))return 0;for(int k=0;k<TS_CF_COUNT;++k)if(!(c->locks&(1u<<k)))c->value[k]=c->state[i][k];c->selected=i+1;return 1;}
int ts_character_filter_write(FILE *f,const char *key,const TsCharacterFilterControls *c)
{
 if(!ts_character_filter_valid(c))return 0;
 if(fprintf(f,"%s=%u,%u,%u,%u",key,c->locks,c->preset,c->captured,c->selected)<0)return 0;
 for(int i=0;i<TS_CF_COUNT;++i)if(fprintf(f,",%.9g",c->value[i])<0)return 0;
 for(int k=0;k<5;++k)if(c->captured&(1u<<k))for(int i=0;i<TS_CF_COUNT;++i)if(fprintf(f,",%.9g",c->state[k][i])<0)return 0;
 return fputc('\n',f)!=EOF;
}
int ts_character_filter_read(TsCharacterFilterControls *c,const char *text)
{
 TsCharacterFilterControls p;ts_character_filter_default(&p);double head[4];char *end;
 for(int i=0;i<4;++i){errno=0;head[i]=strtod(text,&end);if(errno || end==text || *end!=',' || !isfinite(head[i]) || head[i]<0 || floor(head[i])!=head[i])return 0;text=end+1;}
 if(head[0]>((1u<<TS_CF_COUNT)-1) || head[1]>=TS_CF_PRESETS || head[2]>31 || head[3]>5)return 0;
 p.locks=(uint32_t)head[0];p.preset=(uint8_t)head[1];p.captured=(uint8_t)head[2];p.selected=(uint8_t)head[3];
 int n=TS_CF_COUNT;for(int k=0;k<5;++k)if(p.captured&(1u<<k))n+=TS_CF_COUNT;
 int at=0;
 for(int k=-1;k<5;++k){if(k>=0 && !(p.captured&(1u<<k)))continue;
  float *v=k<0?p.value:p.state[k];
  for(int i=0;i<TS_CF_COUNT;++i){errno=0;v[i]=strtof(text,&end);if(errno || end==text || !isfinite(v[i]) || v[i]<0 || v[i]>1)return 0;
   if(++at<n){if(*end!=',')return 0;text=end+1;}else{text=end;}}
 }
 while(isspace((unsigned char)*text))++text;if(*text)return 0;
 if(!ts_character_filter_valid(&p))return 0;*c=p;return 1;
}
void ts_character_filter_label(const TsCharacterFilterControls *c,int field,char *s,unsigned n)
{
 float v=c->value[field];int local=field>=TS_CF_R_MODEL && field<=TS_CF_R_TRIM?field-8:field;
 switch(local){
 case TS_CF_ROUTE:snprintf(s,n,"%s",(const char*[]){"LINKED","SPREAD","DUAL MONO"}[(int)lrintf(v*2)]);break;
 case TS_CF_L_MODEL:snprintf(s,n,"MODEL %s",v>=.5f?"POLIVOKS":"MS-20");break;
 case TS_CF_L_MODE:snprintf(s,n,"%s",v>=.5f?"BAND PASS":"LOW PASS");break;
 case TS_CF_L_CUT:case TS_CF_L_HP:snprintf(s,n,"%s %.0F HZ",local==TS_CF_L_HP?"HP CUT":"CUTOFF",ts_character_filter_cutoff(v));break;
 case TS_CF_L_RES:case TS_CF_L_HP_RES:snprintf(s,n,"%s %.0F%%",local==TS_CF_L_RES?"RES":"HP RES",100*v);break;
 case TS_CF_L_DRIVE:snprintf(s,n,"DRIVE +%.1F DB",24*v);break;
 case TS_CF_L_TRIM:snprintf(s,n,"TRIM %+.1F DB",24*v-12);break;
 case TS_CF_SPREAD_AMOUNT:snprintf(s,n,"SPREAD %+.2F OCT",4*v-2);break;
 case TS_CF_LFO_RATE:snprintf(s,n,"LFO %.3F HZ",.005f*powf(4000,v));break;
 case TS_CF_LFO_DEPTH:snprintf(s,n,"LFO DEPTH %.0F%%",100*v);break;
 case TS_CF_LFO_PHASE:snprintf(s,n,"L/R PHASE %.0F",180*v);break;
 case TS_CF_ENV_AMOUNT:snprintf(s,n,"ENV %+.0F%%",200*v-100);break;
 case TS_CF_ENV_ATTACK:snprintf(s,n,"ATTACK %.0F MS",1*powf(500,v));break;
 case TS_CF_ENV_RELEASE:snprintf(s,n,"RELEASE %.0F MS",10*powf(300,v));break;
 default:snprintf(s,n,"%s",v>=.5f?"EDIT RIGHT":"EDIT LEFT");break;
 }
}
void ts_character_filter_init(TsCharacterFilter *f,unsigned rate)
{memset(f,0,sizeof(*f));f->rate=rate?rate:48000;f->smoothing=1-expf(-1.f/(.02f*f->rate));f->gain=1;f->dc_radius=expf(-6.2831853f*3/f->rate);}
/* Trapezoidal two-state solver. Korg35 uses the unequal-section 7/3 damping
   law and signal-dependent positive feedback; Polivoks uses nonlinear,
   finite-slew programmable-opamp integrators. These are circuit-inspired
   behavioural models, not transistor-perfect emulations. */
static float pole(TsCharacterFilterPole *p,float u,float g,float resonance,int model,int band)
{
 float r=model?1.8f-2.12f*resonance/(1+.8f*fabsf(p->band)):
  2.3333333f-2.62f*resonance/(1+.45f*fabsf(p->band)+.08f*fabsf(u));
 float b=(p->band+g*(u-p->low))/(1+r*g+g*g),l=p->low+g*b;
 float nb=2*b-p->band,nl=2*l-p->low;
 if(model){nb=p->band+.8f*soft((nb-p->band)/.8f);nl=p->low+.8f*soft((nl-p->low)/.8f);}
 p->band=fabsf(nb)<1e-20f?0:clamp(nb,-4,4);p->low=fabsf(nl)<1e-20f?0:clamp(nl,-4,4);
 return band?b:l;
}
TsStereoFrame ts_character_filter_process(TsCharacterFilter *f,const TsCharacterFilterControls *c,TsStereoFrame input,float mix,float gain_db,float gate)
{
 if(!f->initialized){memcpy(f->smooth,c->value,sizeof(f->smooth));f->initialized=1;}
 for(int i=0;i<TS_CF_COUNT;++i)f->smooth[i]+=f->smoothing*(c->value[i]-f->smooth[i]);
 input.l=isfinite(input.l)?input.l:0;input.r=isfinite(input.r)?input.r:0;
 float peak=fmaxf(fabsf(input.l),fabsf(input.r));
 int route=(int)lrintf(c->value[TS_CF_ROUTE]*2);
 if(!(f->clock%16)){
  f->phase_step=.005f*powf(4000,f->smooth[TS_CF_LFO_RATE])/f->rate;
  f->gain_target=powf(10,clamp(gain_db,-12,12)/20);
  f->attack=1-expf(-1.f/(.001f*powf(500,f->smooth[TS_CF_ENV_ATTACK])*f->rate));
  f->release=1-expf(-1.f/(.01f*powf(300,f->smooth[TS_CF_ENV_RELEASE])*f->rate));
 }
 for(int ch=0;ch<2;++ch){float e=route==TS_CF_DUAL?fabsf(ch?input.r:input.l):peak;f->envelope[ch]+=(e>f->envelope[ch]?f->attack:f->release)*(e-f->envelope[ch]);}
 f->phase+=f->phase_step;
 if(f->phase>=1)f->phase-=1;
 float out[2],in[2]={isfinite(input.l)?input.l:0,isfinite(input.r)?input.r:0};
 for(int ch=0;ch<2;++ch){TsCharacterFilterChannel *s=&f->channel[ch];int b=route==TS_CF_DUAL?ch*8:0;
  float target=c->value[TS_CF_L_MODEL+b]>=.5f,mode=c->value[TS_CF_L_MODE+b]>=.5f;
  s->model_mix+=f->smoothing*(target-s->model_mix);s->mode_mix+=f->smoothing*(mode-s->mode_mix);
  if(!(f->clock%16)){
   float lfo=sinf(6.2831853f*(f->phase+(route==TS_CF_LINKED?0:ch*f->smooth[TS_CF_LFO_PHASE]*.5f)));
   float oct=4*f->smooth[TS_CF_LFO_DEPTH]*lfo+4*(2*f->smooth[TS_CF_ENV_AMOUNT]-1)*fminf(1,f->envelope[ch]*2);
   if(route==TS_CF_SPREAD)oct+=(ch?1:-1)*(4*f->smooth[TS_CF_SPREAD_AMOUNT]-2)*.5f;
   float cut=clamp(ts_character_filter_cutoff(f->smooth[TS_CF_L_CUT+b])*exp2f(oct),10,.42f*f->rate);
   float hp=clamp(ts_character_filter_cutoff(f->smooth[TS_CF_L_HP+b]),10,.42f*f->rate);
   s->target_lp=tanf(3.14159265f*cut/(2*f->rate));s->target_hp=tanf(3.14159265f*hp/(2*f->rate));
   s->target_drive=powf(10,24*f->smooth[TS_CF_L_DRIVE+b]/20);
   s->target_trim=powf(10,(24*f->smooth[TS_CF_L_TRIM+b]-12)/20);
   s->target_res=f->smooth[TS_CF_L_RES+b];s->target_hp_res=f->smooth[TS_CF_L_HP_RES+b];
   if(!f->clock){s->g_lp=s->target_lp;s->g_hp=s->target_hp;s->drive=s->target_drive;s->trim=s->target_trim;s->res=s->target_res;s->hp_res=s->target_hp_res;}
  }
  /* Smooth the effective channel coefficients as well as the controls: a
     routing change can select an already-settled, different right bank. */
#define FOLLOW(field,target) s->field+=f->smoothing*(s->target-s->field)
  FOLLOW(g_lp,target_lp);FOLLOW(g_hp,target_hp);FOLLOW(drive,target_drive);FOLLOW(trim,target_trim);FOLLOW(res,target_res);FOLLOW(hp_res,target_hp_res);
#undef FOLLOW
  s->g_poli=s->g_lp;
  float wet=0;
  /* Fixed 2x core; linear input interpolation plus averaging gives bounded,
     allocation-free antialiasing at a low cost for older performance rigs. */
  for(int k=0;k<2;++k){float x=(k?in[ch]:(s->previous+in[ch])*.5f)*s->drive;
   float u=soft(x),g=s->g_lp*(1-.06f*soft(u+s->ms_lp.low));
   float hp=u-pole(&s->ms_hp,u,s->g_hp,s->hp_res,0,0);
   float ms=pole(&s->ms_lp,soft(hp),g,s->res,0,0);
   float pl=pole(&s->polivoks,soft(x+.035f*x*x),s->g_poli,s->res,1,0);
   float pb=s->polivoks.band;
   float poli=pl+s->mode_mix*(pb-pl);
   wet+=.5f*(ms+s->model_mix*(poli-ms));
  }
  s->previous=in[ch];wet*=s->trim;
  float dc=wet-s->dc_x+f->dc_radius*s->dc_y;s->dc_x=wet;s->dc_y=fabsf(dc)<1e-20f?0:dc;
  out[ch]=clamp(dc,-8,8);
 }
 f->mix+=f->smoothing*(unit(mix)*unit(gate)-f->mix);
 f->gain+=f->smoothing*(f->gain_target-f->gain);
 if(fabsf(f->mix-unit(mix)*unit(gate))<1e-6f)f->mix=unit(mix)*unit(gate);
 if(fabsf(f->gain-f->gain_target)<1e-6f)f->gain=f->gain_target;
 ++f->clock;
 float makeup=1+(f->gain-1)*unit(gate);
 return (TsStereoFrame){(in[0]+f->mix*(out[0]-in[0]))*makeup,(in[1]+f->mix*(out[1]-in[1]))*makeup};
}
int ts_character_filter_hit(const TsCharacterFilterControls *c,int x,int y,float *value)
{
 *value=0;
 if(y>=8 && y<26){if(x>=16 && x<62)return -10;
  for(int i=0;i<3;++i)if(x>=82+i*100 && x<176+i*100){*value=i*.5f;return TS_CF_ROUTE;}}
 if(y>=46 && y<62){if(x<310){*value=0;return TS_CF_FOCUS;}if(x>=330){*value=1;return TS_CF_FOCUS;}}
 for(int ch=0;ch<2;++ch){int left=18+ch*312,b=ch*8;if(x<left || x>=left+292)continue;
  /* Skip this bank's edits without blocking the shared controls below it. */
  if(ch && c->value[TS_CF_ROUTE]<.75f)continue;
  if(y>=66 && y<84){*value=c->value[TS_CF_L_MODEL+b]>=.5f?0:1;return TS_CF_L_MODEL+b;}
  int right=x>=left+148,field=-1;
  if(y>=100 && y<118){
   if(c->value[TS_CF_L_MODEL+b]>=.5f){*value=right?1:0;return TS_CF_L_MODE+b;}
   field=(right?TS_CF_L_HP_RES:TS_CF_L_HP)+b;
  }
  if(y>=140 && y<158)field=(right?TS_CF_L_RES:TS_CF_L_CUT)+b;
  if(y>=180 && y<198)field=(right?TS_CF_L_TRIM:TS_CF_L_DRIVE)+b;
  if(field>=0){*value=unit((x-left-(right?148:0))/139.f);return field;}
 }
 if(y>=236 && y<254 && x>=18 && x<616){int col=(x-18)/204;if(col>2)col=2;*value=unit((x-18-col*204)/189.f);return (int[]){TS_CF_SPREAD_AMOUNT,TS_CF_LFO_RATE,TS_CF_LFO_DEPTH}[col];}
 if(y>=268 && y<286 && x>=18 && x<616){
  int field,left,w;if(x<210){field=TS_CF_LFO_PHASE;left=18;w=190;}else if(x<414){field=TS_CF_ENV_AMOUNT;left=222;w=190;}else if(x<518){field=TS_CF_ENV_ATTACK;left=426;w=88;}else{field=TS_CF_ENV_RELEASE;left=526;w=88;}
  *value=unit((x-left)/(float)(w-1));return field;
 }
 if(y>=304 && y<322 && x>=18 && x<616){int right=x>=324;*value=unit((x-(right?324:18))/291.f);return right?101:100;}
 if(y>=340 && y<362){if(x>=18 && x<50)return -2;if(x>=584 && x<616)return -3;}
 if(y>=374 && y<394){for(int i=0;i<5;++i)if(x>=18+i*44 && x<56+i*44)return -30-i;if(x>=260 && x<378)return -4;if(x>=560 && x<620)return -20;}
 return -1;
}
