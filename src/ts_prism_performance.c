#include "tapesister/prism.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

_Static_assert(sizeof(float)==4 && sizeof(int)==4,"Prism state uses 32-bit values");
_Static_assert(offsetof(TsPrismControls,a)==sizeof(TsPrismPatch),"Prism patch prefix must match");

#include "ts_prism_factory.inc"

TsPrismPatch *ts_prism_state(TsPrismControls *c,int state)
{
    if(!c || state<0 || state>=TS_PRISM_STATES)return NULL;
    return state==0?&c->a:state==1?&c->b:&c->extra[state-2];
}
const TsPrismPatch *ts_prism_state_const(const TsPrismControls *c,int state)
{
    if(!c || state<0 || state>=TS_PRISM_STATES)return NULL;
    return state==0?&c->a:state==1?&c->b:&c->extra[state-2];
}
int ts_prism_pair_ready(const TsPrismControls *c)
{
    if(c && c->active_pair_valid)return 1;
    if(!c || !ts_prism_state_const(c,c->endpoint[0]) || !ts_prism_state_const(c,c->endpoint[1]))return 0;
    unsigned mask=(1u<<c->endpoint[0])|(1u<<c->endpoint[1]);
    return c->endpoint[0]!=c->endpoint[1] && (c->captured&mask)==mask;
}
const TsPrismPatch *ts_prism_endpoint_patch(const TsPrismControls *c,int side)
{
    if(!c || side<0 || side>1)return NULL;
    return c->active_pair_valid?&c->active_pair[side]:ts_prism_state_const(c,c->endpoint[side]);
}
void ts_prism_assign_endpoint(TsPrismControls *c,int side,int state)
{
    const TsPrismPatch *patch=ts_prism_state_const(c,state);
    if(!patch || side<0 || side>1)return;
    c->endpoint[side]=state;
    if(c->active_pair_valid)c->active_pair[side]=*patch;
}
char ts_prism_state_letter(int state)
{
    return state>=0 && state<TS_PRISM_STATES?(char)('A'+state):'~';
}
void ts_prism_copy_bank(TsPrismControls *to,const TsPrismControls *from)
{
    to->a=from->a;to->b=from->b;memcpy(to->extra,from->extra,sizeof(to->extra));
    to->captured=from->captured;memcpy(to->endpoint,from->endpoint,sizeof(to->endpoint));
}

void ts_prism_capture(TsPrismControls *c,int endpoint)
{
    TsPrismPatch *patch=ts_prism_state(c,endpoint);if(!patch)return;
    memcpy(patch,c,sizeof(TsPrismPatch));
    c->captured|=1<<endpoint;
    c->active_pair_valid=0;
}
void ts_prism_recall(TsPrismControls *c,int endpoint)
{
    const TsPrismPatch *patch=ts_prism_state_const(c,endpoint);
    if(!patch || !(c->captured&(1<<endpoint)))return;
    memcpy(c,patch,sizeof(TsPrismPatch));
    c->active_pair_valid=0;
    c->morph_enabled=0;c->morph_trigger=0;c->morph=(float)(endpoint==c->endpoint[1]);
}
void ts_prism_sequence_toggle(TsPrismControls *c,int lens)
{
    if(!c || lens<0 || lens>=TS_PRISM_LENSES)return;
    for(int i=0;i<c->seq_count;++i)if(c->sequence[i]==lens) {
        memmove(c->sequence+i,c->sequence+i+1,(size_t)(c->seq_count-i-1)*sizeof(int));
        c->sequence[--c->seq_count]=0;return;
    }
    if(c->seq_count<TS_PRISM_LENSES)c->sequence[c->seq_count++]=lens;
}
float ts_prism_snap_pitch(float cents,int snap)
{
    float step=snap==1 ? 100 : snap==2 ? 50 : snap==3 ? 1200.f/31 : 0;
    return step>0 ? roundf(cents/step)*step : cents;
}
const char *ts_prism_snap_name(int snap)
{
    static const char *const names[]={"FREE","SEMITONE","50 CENT","31-TET"};
    return names[snap>=0 && snap<4 ? snap : 0];
}
static float roll(uint32_t *seed)
{
    *seed=*seed*1664525u+1013904223u;
    return (*seed>>8)*(1.f/16777216.f);
}
static float change(float value,float range,uint32_t *seed)
{
    return value+(roll(seed)*2-1)*range;
}
void ts_prism_generate(TsPrismControls *c,uint32_t seed,int vary)
{
    if(!c)return;
    TsPrismControls old=*c,n;
    ts_prism_controls_default(&n);
    if(vary)n=old;
    n.enabled=1;n.morph_enabled=0;n.morph_trigger=0;
    n.locks=old.locks;
    if(!(old.locks&TS_PRISM_LOCK_COUNT))
        n.lenses=vary ? old.lenses+(int)(roll(&seed)*7)-3 : 2+(int)(roll(&seed)*23);
    if(!(old.locks&TS_PRISM_LOCK_PITCH)) {
        if(!vary || roll(&seed)<.12f)n.mode=(int)(roll(&seed)*TS_PRISM_MODE_COUNT);
        n.spread=vary ? change(old.spread,.25f,&seed) : roll(&seed)*2;
        n.focus=vary ? change(old.focus,.15f,&seed) : roll(&seed)*.8f;
        if(!vary)n.group_octave=(int)(roll(&seed)*3)-1;
        for(int i=1;i<TS_PRISM_LENSES;++i) {
            n.pitch_offset[i]=vary ? change(old.pitch_offset[i],30,&seed) : (roll(&seed)<.25f ? (roll(&seed)*2-1)*600 : 0);
            if(n.snap)n.pitch_offset[i]=ts_prism_snap_pitch(n.pitch_offset[i],n.snap);
        }
    } else {
        n.mode=old.mode;n.spread=old.spread;n.focus=old.focus;n.group_octave=old.group_octave;n.snap=old.snap;
        memcpy(n.pitch_offset,old.pitch_offset,sizeof(n.pitch_offset));
        memcpy(n.octave_offset,old.octave_offset,sizeof(n.octave_offset));
    }
    if(old.locks&TS_PRISM_LOCK_COUNT)n.lenses=old.lenses;
    if(!(old.locks&TS_PRISM_LOCK_MOTION)) {
        n.drift=vary ? change(old.drift,.25f,&seed) : roll(&seed)*2;
        n.drift_rate=vary ? old.drift_rate*exp2f((roll(&seed)*2-1)*.6f) : .005f*powf(8000,roll(&seed));
        n.stereo=vary ? change(old.stereo,.15f,&seed) : roll(&seed);
        for(int i=1;i<TS_PRISM_LENSES;++i)n.pan_offset[i]=vary ? change(old.pan_offset[i],.12f,&seed) : (roll(&seed)*2-1)*.4f;
    } else {
        n.drift=old.drift;n.drift_rate=old.drift_rate;n.stereo=old.stereo;
        memcpy(n.pan_offset,old.pan_offset,sizeof(n.pan_offset));
    }
    if(!(old.locks&TS_PRISM_LOCK_GLASS)) {
        if(!vary || roll(&seed)<.2f)n.input_shape=(int)(roll(&seed)*TS_PRISM_SHAPE_COUNT);
        if(!vary || roll(&seed)<.2f)n.output_shape=(int)(roll(&seed)*TS_PRISM_SHAPE_COUNT);
        n.color=vary ? change(old.color,.15f,&seed) : roll(&seed);
    } else { n.input_shape=old.input_shape;n.output_shape=old.output_shape;n.color=old.color; }
    if(!(old.locks&TS_PRISM_LOCK_MIX)) {
        n.body=vary ? change(old.body,.2f,&seed) : .3f+roll(&seed)*1.4f;
        n.mix=vary ? change(old.mix,.12f,&seed) : .5f+roll(&seed)*.5f;
        /* NEW starts with unity trims, no mute/solo and a usable output level. */
    } else {
        n.body=old.body;n.mix=old.mix;n.output_db=old.output_db;n.dry_level=old.dry_level;
        memcpy(n.trim_db,old.trim_db,sizeof(n.trim_db));n.mute_mask=old.mute_mask;n.solo_mask=old.solo_mask;
    }
    if(old.locks&TS_PRISM_LOCK_SEQUENCE) {
        n.seq_enabled=old.seq_enabled;n.seq_count=old.seq_count;n.seq_rate=old.seq_rate;
        memcpy(n.sequence,old.sequence,sizeof(n.sequence));n.seq_reset=old.seq_reset;
    }
    /* Captured endpoints are performance memories, never overwritten by dice. */
    ts_prism_copy_bank(&n,&old);n.morph=old.morph;n.morph_seconds=old.morph_seconds;
    n.matrix=old.matrix;n.matrix_run=0;n.active_pair_valid=0;
    ts_prism_controls_sanitize(&n);*c=n;
}

/* Explicit typed text fields, independent of C padding or host byte order. */
typedef struct { const char *name; size_t offset; int count, integer; } Field;
#define F(m) {#m,offsetof(TsPrismPatch,m),1,0}
#define I(m) {#m,offsetof(TsPrismPatch,m),1,1}
#define A(m,t) {#m,offsetof(TsPrismPatch,m),TS_PRISM_LENSES,t}
static const Field patch_fields[]={I(enabled),I(mode),I(lenses),F(spread),F(drift),F(focus),F(stereo),F(body),F(mix),F(output_db),A(pitch_offset,0),A(pan_offset,0),A(trim_db,0),F(dry_level),I(mute_mask),I(solo_mask),I(input_shape),I(output_shape),F(color),F(drift_rate),I(group_octave),A(octave_offset,1),I(snap)};
#undef F
#undef I
#undef A
#define F(m) {#m,offsetof(TsPrismControls,m),1,0}
#define I(m) {#m,offsetof(TsPrismControls,m),1,1}
#define U(m) {#m,offsetof(TsPrismControls,m),1,2}
static const Field performance_fields[]={I(captured),I(morph_enabled),F(morph),F(morph_seconds),I(morph_target),U(morph_trigger),I(seq_enabled),I(seq_count),{"sequence",offsetof(TsPrismControls,sequence),TS_PRISM_LENSES,1},F(seq_rate),U(seq_reset),U(locks)};
#undef F
#undef I
#undef U
static const Field matrix_fields[]={
    {"cell",offsetof(TsPrismControls,matrix)+offsetof(TsPrismMatrixControls,step),TS_PRISM_MATRIX_STEPS,1},
    {"length",offsetof(TsPrismControls,matrix)+offsetof(TsPrismMatrixControls,length),1,1},
    {"loop",offsetof(TsPrismControls,matrix)+offsetof(TsPrismMatrixControls,loop),1,1},
    {"seconds",offsetof(TsPrismControls,matrix)+offsetof(TsPrismMatrixControls,step_seconds),1,0},
    {"parked",offsetof(TsPrismControls,active_pair_valid),1,1}};
static void write_fields(FILE *f,const char *version,const char *prefix,const void *data,const Field *fields,size_t count)
{
    for(size_t i=0;i<count;++i)for(int j=0;j<fields[i].count;++j) {
        const unsigned char *ptr=(const unsigned char *)data+fields[i].offset+j*4;
        fprintf(f,"%s.%s.%s.%d=",version,prefix,fields[i].name,j);
        if(fields[i].integer==1) { int x;memcpy(&x,ptr,4);fprintf(f,"%d\n",x); }
        else if(fields[i].integer==2) { uint32_t x;memcpy(&x,ptr,4);fprintf(f,"%u\n",x); }
        else { float x;memcpy(&x,ptr,4);fprintf(f,"%.9g\n",(double)x); }
    }
}
void ts_prism_write(FILE *f,const TsPrismControls *c)
{
    write_fields(f,"P2","main",c,patch_fields,sizeof(patch_fields)/sizeof(*patch_fields));
    ts_prism_write_extensions(f,c);
}
void ts_prism_write_extensions(FILE *f,const TsPrismControls *c)
{
    size_t first=0,count=sizeof(patch_fields)/sizeof(*patch_fields);
    while(first<count && strcmp(patch_fields[first].name,"drift_rate"))++first;
    write_fields(f,"P2","main",c,patch_fields+first,count-first);
    write_fields(f,"P2","A",&c->a,patch_fields,sizeof(patch_fields)/sizeof(*patch_fields));
    write_fields(f,"P2","B",&c->b,patch_fields,sizeof(patch_fields)/sizeof(*patch_fields));
    write_fields(f,"P2","perf",c,performance_fields,sizeof(performance_fields)/sizeof(*performance_fields));
    for(int state=2;state<TS_PRISM_STATES;++state) {
        char group[2]={(char)('A'+state),0};
        write_fields(f,"P3",group,ts_prism_state_const(c,state),patch_fields,count);
    }
    fprintf(f,"P3.bank.endpoint.0=%d\nP3.bank.endpoint.1=%d\n",c->endpoint[0],c->endpoint[1]);
    write_fields(f,"P4","matrix",c,matrix_fields,sizeof(matrix_fields)/sizeof(*matrix_fields));
    write_fields(f,"P4","left",&c->active_pair[0],patch_fields,count);
    write_fields(f,"P4","right",&c->active_pair[1],patch_fields,count);
}
int ts_prism_read_field(TsPrismControls *c,const char *key,const char *value)
{
    if(strncmp(key,"P2.",3) && strncmp(key,"P3.",3) && strncmp(key,"P4.",3))return 0;
    const Field *fields=patch_fields;size_t count=sizeof(patch_fields)/sizeof(*fields);
    char group[8],name[48],tail;int index;
    if(sscanf(key+3,"%7[^.].%47[^.].%d%c",group,name,&index,&tail)!=3)return -1;
    void *data=c;
    if(strlen(group)==1 && group[0]>='A' && group[0]<'A'+TS_PRISM_STATES)
        data=ts_prism_state(c,group[0]-'A');
    else if(!strcmp(group,"left"))data=&c->active_pair[0];
    else if(!strcmp(group,"right"))data=&c->active_pair[1];
    else if(!strcmp(group,"matrix")) {fields=matrix_fields;count=sizeof(matrix_fields)/sizeof(*matrix_fields);}
    else if(!strcmp(group,"bank")) {
        static const Field bank_fields[]={{"endpoint",offsetof(TsPrismControls,endpoint),2,1}};
        fields=bank_fields;count=1;
    }
    else if(!strcmp(group,"perf")) { fields=performance_fields;count=sizeof(performance_fields)/sizeof(*fields); }
    else if(strcmp(group,"main"))return -1;
    for(size_t i=0;i<count;++i)if(!strcmp(name,fields[i].name)) {
        if(index<0 || index>=fields[i].count)return -1;
        unsigned char *ptr=(unsigned char *)data+fields[i].offset+index*4;
        char *end;errno=0;double x=strtod(value,&end);
        while(*end==' ' || *end=='\r' || *end=='\n' || *end=='\t')++end;
        if(end==value || *end || errno || !isfinite(x))return -1;
        if(fields[i].integer) {
            if(x!=floor(x) || x<(fields[i].integer==2 ? 0 : INT_MIN) || x>(fields[i].integer==2 ? UINT32_MAX : INT_MAX))return -1;
            if(fields[i].integer==2) { uint32_t v=(uint32_t)x;memcpy(ptr,&v,4); }
            else { int v=(int)x;memcpy(ptr,&v,4); }
        } else { float v=(float)x;if(!isfinite(v))return -1;memcpy(ptr,&v,4); }
        return 1;
    }
    return -1;
}
int ts_prism_bank_save(const TsPrismBank *bank,const char *path,char *error,size_t size)
{
    char temp[4096];FILE *f=NULL;int ok=0;
    if(bank && bank->count<=TS_PRISM_PRESETS && snprintf(temp,sizeof(temp),"%s.tmp",path)>0 && strlen(path)+4<sizeof(temp))f=fopen(temp,"wb");
    if(f) {
        fputs("TapeSister Prism Presets 3\n",f);
        for(unsigned i=0;i<bank->count;++i) {
            fprintf(f,"Preset=%u\nName=%.31s\n",i,bank->entries[i].name);
            ts_prism_write(f,&bank->entries[i].controls);
        }
        ok=!ferror(f);if(fclose(f))ok=0;
        if(ok) {
#ifdef _WIN32
            /* Windows rename cannot replace; use the same atomic replacement as other banks. */
            extern int ts_prism_replace_file(const char *,const char *);
            ok=ts_prism_replace_file(temp,path);
#else
            ok=rename(temp,path)==0;
#endif
        }
        if(!ok)remove(temp);
    }
    if(error && size)snprintf(error,size,"%s",ok?"":"Could not save Prism presets");
    return ok;
}
#ifdef _WIN32
#include <windows.h>
int ts_prism_replace_file(const char *from,const char *to)
{ return MoveFileExA(from,to,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0; }
#endif
int ts_prism_bank_load(TsPrismBank *bank,const char *path,char *error,size_t size)
{
    FILE *f=fopen(path,"rb");
    if(!f) { if(errno==ENOENT)return 1; if(error && size)snprintf(error,size,"Could not open Prism presets");return 0; }
    TsPrismBank *next=calloc(1,sizeof(*next));char line[256];int ok=next && fgets(line,sizeof(line),f) &&
        (!strcmp(line,"TapeSister Prism Presets 1\n") || !strcmp(line,"TapeSister Prism Presets 2\n") || !strcmp(line,"TapeSister Prism Presets 3\n"));
    int slot=-1;
    while(ok && fgets(line,sizeof(line),f)) {
        char *equals=strchr(line,'=');if(!equals){ok=0;break;}*equals++=0;
        equals[strcspn(equals,"\r\n")]=0;
        if(!strcmp(line,"Preset")) {
            char *end;long n=strtol(equals,&end,10);
            if(*end || n!=(long)next->count || n>=TS_PRISM_PRESETS){ok=0;break;}
            slot=(int)n;++next->count;ts_prism_controls_default(&next->entries[slot].controls);
        } else if(slot<0)ok=0;
        else if(!strcmp(line,"Name"))snprintf(next->entries[slot].name,32,"%.31s",equals);
        else ok=ts_prism_read_field(&next->entries[slot].controls,line,equals)>0;
    }
    if(ferror(f))ok=0;
    fclose(f);
    if(ok) { for(unsigned i=0;i<next->count;++i)ts_prism_controls_sanitize(&next->entries[i].controls);*bank=*next; }
    free(next);
    if(error && size)snprintf(error,size,"%s",ok?"":"Malformed Prism preset bank; bank unchanged");
    return ok;
}
