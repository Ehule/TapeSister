#include "tapesister/compressor.h"
#include <math.h>
#include <string.h>

void ts_compressor_default(TsCompressorControls *c)
{ *c=(TsCompressorControls){0,0,-24,4,10,250,0}; }
int ts_compressor_valid(const TsCompressorControls *c)
{
    return c && (c->enabled==0 || c->enabled==1) && (c->external==0 || c->external==1) &&
        isfinite(c->threshold_db) && c->threshold_db>=-60 && c->threshold_db<=0 &&
        isfinite(c->ratio) && c->ratio>=1 && c->ratio<=20 &&
        isfinite(c->attack_ms) && c->attack_ms>=.1f && c->attack_ms<=200 &&
        isfinite(c->release_ms) && c->release_ms>=20 && c->release_ms<=3000 &&
        isfinite(c->output_db) && c->output_db>=-24 && c->output_db<=24;
}
void ts_compressor_set(TsCompressor *c,const TsCompressorControls *controls)
{
    if(!c || !ts_compressor_valid(controls))return;
    c->controls=*controls;
    c->target_attack=exp(-1000.0/(c->rate*controls->attack_ms));
    c->target_release=exp(-1000.0/(c->rate*controls->release_ms));
    c->target_output=pow(10,controls->output_db*.05);
}
void ts_compressor_prepare(TsCompressor *c,unsigned rate)
{
    c->rate=rate?rate:48000;c->smooth=1-exp(-1.0/(.005*c->rate));
    ts_compressor_set(c,&c->controls);
    c->attack=c->target_attack;c->release=c->target_release;
}
void ts_compressor_init(TsCompressor *c)
{
    memset(c,0,sizeof(*c));ts_compressor_default(&c->controls);
    c->gain=c->output=1;c->threshold=c->controls.threshold_db;c->slope=1-1/c->controls.ratio;
    ts_compressor_prepare(c,48000);
}
static double follow(double value,double target,double step)
{ double d=target-value;return fabs(d)<1e-9?target:value+d*step; }
static float peak(TsStereoFrame x){return fmaxf(fabsf(x.l),fabsf(x.r));}
TsStereoFrame ts_compressor_process(TsCompressor *c,TsStereoFrame input,TsStereoFrame key)
{
    input=ts_stereo_frame_sanitize(input);key=ts_stereo_frame_sanitize(key);
    c->wet=follow(c->wet,(float)c->controls.enabled,c->smooth);
    c->external=follow(c->external,(float)c->controls.external,c->smooth);
    c->threshold=follow(c->threshold,c->controls.threshold_db,c->smooth);
    c->slope=follow(c->slope,1-1/c->controls.ratio,c->smooth);
    c->attack=follow(c->attack,c->target_attack,c->smooth);
    c->release=follow(c->release,c->target_release,c->smooth);
    c->output=follow(c->output,c->target_output,c->smooth);
    float level=peak(input)*(1-c->external)+peak(key)*c->external;
    c->key_peak=level;
    if(c->wet==0) {c->gain=1;c->reduction_db=0;return input;}
    float over=20*log10f(fmaxf(level,1e-12f))-c->threshold;
    float reduction=over<=-3?0:over<3?c->slope*(over+3)*(over+3)/12:c->slope*over;
    float target=powf(10,-reduction*.05f);
    c->gain=follow(c->gain,target,1-(target<c->gain?c->attack:c->release));
    float applied=1+c->wet*(c->gain-1);
    c->reduction_db=-20*log10f(fmaxf(applied,1e-12f));
    float gain=1+c->wet*(c->gain*c->output-1);
    return ts_stereo_frame_sanitize((TsStereoFrame){input.l*gain,input.r*gain});
}
float ts_compressor_normalized(const TsCompressorControls *c,int control)
{
    switch(control) {
    case 0:return (c->threshold_db+60)/60;
    case 1:return logf(c->ratio)/logf(20);
    case 2:return logf(c->attack_ms/.1f)/logf(2000);
    case 3:return logf(c->release_ms/20)/logf(150);
    case 4:return (c->output_db+24)/48;
    default:return 0;
    }
}
void ts_compressor_set_normalized(TsCompressorControls *c,int control,float value)
{
    if(!isfinite(value))return;
    value=fmaxf(0,fminf(1,value));
    switch(control) {
    case 0:c->threshold_db=-60+60*value;break;
    case 1:c->ratio=powf(20,value);break;
    case 2:c->attack_ms=fminf(200,.1f*powf(2000,value));break;
    case 3:c->release_ms=fminf(3000,20*powf(150,value));break;
    case 4:c->output_db=-24+48*value;break;
    }
}
int ts_compressor_write(FILE *f,const TsCompressorControls *c)
{
    return ts_compressor_valid(c) && fprintf(f,"Compressor.Controls=%d,%d,%.9g,%.9g,%.9g,%.9g,%.9g\n",
        c->enabled,c->external,c->threshold_db,c->ratio,c->attack_ms,c->release_ms,c->output_db)>0;
}
int ts_compressor_read(TsCompressorControls *c,const char *key,const char *value)
{
    if(strncmp(key,"Compressor.",11))return 0;
    TsCompressorControls next={0};char tail;
    if(strcmp(key,"Compressor.Controls") || sscanf(value,"%d,%d,%f,%f,%f,%f,%f %c",
        &next.enabled,&next.external,&next.threshold_db,&next.ratio,&next.attack_ms,&next.release_ms,&next.output_db,&tail)!=7 ||
        !ts_compressor_valid(&next))return -1;
    *c=next;return 1;
}
