#include "tapesister/routing_matrix.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

int ts_matrix_is_processor(int n)
{ return (n>=0 && n<TS_MATRIX_NODES) || n==TS_MATRIX_COMPRESSOR || (n>=TS_MATRIX_EFFECT_BASE && n<TS_MATRIX_ROWS); }
int ts_matrix_is_output(int n)
{ return n>=TS_MATRIX_NODES && n<TS_MATRIX_NODES+TS_MATRIX_OUTPUTS; }
int ts_matrix_processor(int index)
{ return index<TS_MATRIX_NODES?index:index==7?TS_MATRIX_COMPRESSOR:TS_MATRIX_EFFECT_BASE+index-8; }
static int dependency(int destination)
{ return destination==TS_MATRIX_SIDECHAIN?TS_MATRIX_COMPRESSOR:destination>=TS_MATRIX_EFFECT_SC_BASE && destination<TS_MATRIX_INSTANCE_DESTINATIONS?TS_MATRIX_EFFECT_BASE+destination-TS_MATRIX_EFFECT_SC_BASE:destination; }

int ts_matrix_effect_index(int n)
{return n>=TS_MATRIX_EFFECT_BASE && n<TS_MATRIX_ROWS?n-TS_MATRIX_EFFECT_BASE:-1;}
int ts_matrix_sidechain_for(int n)
{return n==TS_MATRIX_COMPRESSOR?TS_MATRIX_SIDECHAIN:ts_matrix_effect_index(n)>=0?TS_MATRIX_EFFECT_SC_BASE+n-TS_MATRIX_EFFECT_BASE:-1;}
int ts_matrix_destination_active(const TsMatrixControls *c,int d)
{
    if(d<0 || d>=TS_MATRIX_DESTINATIONS)return 0;
    if(d<TS_MATRIX_SHARED_DESTINATIONS || d>=TS_MATRIX_AMBISONICS)return 1;
    int i=d<TS_MATRIX_EFFECT_SC_BASE?d-TS_MATRIX_EFFECT_BASE:d-TS_MATRIX_EFFECT_SC_BASE;
    return c->effect[i].id && (d<TS_MATRIX_EFFECT_SC_BASE || c->effect[i].type==TS_MATRIX_FX_COMPRESSOR);
}
int ts_matrix_effect_add(TsMatrixControls *c,int type)
{
    if(type<=0 || type>=TS_MATRIX_FX_TYPES || c->next_effect_id==UINT32_MAX)return -1;
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(!c->effect[i].id) {
        ts_matrix_effect_default(&c->effect[i],type,c->next_effect_id++);return i;
    }
    return -1;
}
void ts_matrix_effect_remove(TsMatrixControls *c,int i)
{
    if(i<0 || i>=TS_MATRIX_EFFECT_CAPACITY)return;
    int n=TS_MATRIX_EFFECT_BASE+i,sc=TS_MATRIX_EFFECT_SC_BASE+i;
    memset(&c->effect[i],0,sizeof(c->effect[i]));
    c->row[n]=(TsSourceRoute){.matrix_enabled=1};c->delayed[n]=0;
    for(int s=0;s<TS_MATRIX_ROWS;++s) {
        c->row[s].matrix_gain[n]=c->row[s].matrix_gain[sc]=0;
        c->delayed[s]&=~((UINT64_C(1)<<n)|(UINT64_C(1)<<sc));
    }
}
const char *ts_matrix_node_name(int n)
{
    static const char *names[]={"MAIN MIX","SISTER","PRISM","PEDALBOARD","FALLOUT","MASTER EQ","LIMITER"};
    if(n==TS_MATRIX_COMPRESSOR)return "COMPRESSOR";
    if(n==TS_MATRIX_SIDECHAIN)return "COMP SIDECHAIN";
    return n>=0 && n<TS_MATRIX_NODES?names[n]:"OUTPUT";
}
void ts_matrix_default(TsMatrixControls *c)
{
    memset(c,0,sizeof(*c));c->next_effect_id=1;
    for(int i=0;i<TS_MATRIX_ROWS;++i)c->row[i].matrix_enabled=1;
    c->row[TS_MATRIX_MAIN].matrix_gain[TS_MATRIX_EQ]=1000;
    c->row[TS_MATRIX_EQ].matrix_gain[TS_MATRIX_LIMITER]=1000;
    c->row[TS_MATRIX_LIMITER].matrix_gain[TS_MATRIX_NODES]=1000;
    for(int i=TS_MATRIX_SISTER;i<=TS_MATRIX_FALLOUT;++i)c->row[i].matrix_gain[TS_MATRIX_MAIN]=1000;
    c->row[TS_MATRIX_COMPRESSOR].matrix_gain[TS_MATRIX_MAIN]=1000;
    c->row[TS_MATRIX_FM].matrix_gain[TS_MATRIX_MAIN]=1000;
    c->row[TS_MATRIX_LINK].matrix_gain[TS_MATRIX_MAIN]=1000;
    c->row[TS_MATRIX_MOSAIC].matrix_gain[TS_MATRIX_MAIN]=1000;
    c->row[TS_MATRIX_TILES].matrix_gain[TS_MATRIX_MAIN]=1000;
    c->row[TS_MATRIX_TRACKER].matrix_gain[TS_MATRIX_MAIN]=1000;
    snprintf(c->output_name[0],TS_MATRIX_NAME,"MAIN SPEAKERS");
}
static int order_graph(const TsMatrixControls *c,int *order)
{
    /* Compile dependencies once per edit. Re-scanning every source/destination
       for every candidate made a slider edit expensive inside audio exclusion.
       Bits also deduplicate a processor's ordinary and sidechain dependencies. */
    uint64_t incoming[TS_MATRIX_ROWS]={0};
    for(int k=0;k<TS_MATRIX_PROCESSORS;++k) {
        int s=ts_matrix_processor(k);
        for(int d=0;d<TS_MATRIX_DESTINATIONS;++d) {
            int n=dependency(d);
            if(ts_matrix_is_processor(n) && c->row[s].matrix_gain[d] &&
               !(c->delayed[s]&ts_matrix_feedback_bit(d)))
                incoming[n]|=UINT64_C(1)<<s;
        }
    }
    uint64_t done=0;
    for(int pos=0;pos<TS_MATRIX_PROCESSORS;++pos) {
        int found=-1;
        for(int k=0;k<TS_MATRIX_PROCESSORS;++k) {
            int n=ts_matrix_processor(k);if(done&(UINT64_C(1)<<n))continue;
            if(!(incoming[n]&~done)){found=n;break;}
        }
        if(found<0)return 0;
        if(order)order[pos]=found;
        done|=UINT64_C(1)<<found;
    }
    return 1;
}

static int name_valid(const char *s)
{
    for(int i=0;i<TS_MATRIX_NAME;++i) {
        if(!s[i])return 1;
        if((unsigned char)s[i]<32 || (unsigned char)s[i]>126 || s[i]=='=')return 0;
    }
    return 0;
}
int ts_matrix_valid(const TsMatrixControls *c)
{
    if(!c || c->enabled<0 || c->enabled>1 || c->feedback<0 || c->feedback>1 || c->board_insert<0 || c->board_insert>1)return 0;
    const unsigned serial=(1u<<TS_MATRIX_MAIN)|(1u<<TS_MATRIX_PRISM)|(1u<<TS_MATRIX_BOARD)|(1u<<TS_MATRIX_FALLOUT);
    if(c->serial_mask&~serial)return 0;
    if(!c->next_effect_id)return 0;
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i) {
        if(!ts_matrix_effect_valid(&c->effect[i]) || c->effect[i].id>=c->next_effect_id)return 0;
        for(int j=0;j<i;++j)if(c->effect[i].id && c->effect[i].id==c->effect[j].id)return 0;
    }
    for(int s=0;s<TS_MATRIX_ROWS;++s) {
        if(!ts_source_route_valid(&c->row[s],0) || !c->row[s].matrix_enabled)return 0;
        if(ts_matrix_is_processor(s)) {
            if(c->delayed[s]>>TS_MATRIX_INSTANCE_DESTINATIONS || (!c->feedback && c->delayed[s]))return 0;
            for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)if((c->delayed[s]&ts_matrix_feedback_bit(d)) &&
                (!ts_matrix_is_processor(dependency(d)) || c->row[s].matrix_gain[d]>500 || !c->row[s].matrix_gain[d]))return 0;
        } else if(c->delayed[s])return 0;
    }
    for(int i=0;i<TS_MATRIX_INPUTS;++i)if(!name_valid(c->input_name[i]))return 0;
    for(int i=0;i<TS_MATRIX_OUTPUTS;++i)if(!name_valid(c->output_name[i]))return 0;
    return order_graph(c,NULL);
}
int ts_matrix_connect(TsMatrixControls *c,int s,int d,int gain)
{
    if(!c || s<0 || s>=TS_MATRIX_ROWS || d<0 || d>=TS_MATRIX_DESTINATIONS || gain<0 || gain>2000)return -1;
    TsMatrixControls next=*c;next.row[s].matrix_gain[d]=gain;
    if(ts_matrix_is_processor(s)) {
        if(!gain)next.delayed[s]&=~ts_matrix_feedback_bit(d);
        if(!order_graph(&next,NULL)) {
            if(!next.feedback)return 0;
            next.delayed[s]|=ts_matrix_feedback_bit(d);
        }
        if(next.delayed[s]&ts_matrix_feedback_bit(d))next.row[s].matrix_gain[d]=gain>500?500:gain;
    }
    if(!ts_matrix_valid(&next))return -1;
    *c=next;return 1;
}
void ts_matrix_cut_feedback(TsMatrixControls *c)
{
    for(int s=0;s<TS_MATRIX_ROWS;++s) {
        for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)if(c->delayed[s]&ts_matrix_feedback_bit(d))c->row[s].matrix_gain[d]=0;
        c->delayed[s]=0;
    }
    c->feedback=0;
}
void ts_matrix_init(TsRoutingMatrix *m)
{
    memset(m,0,sizeof(*m));ts_matrix_default(&m->controls);
    ts_matrix_set(m,&m->controls,48000);
}
int ts_matrix_set(TsRoutingMatrix *m,const TsMatrixControls *c,unsigned rate)
{
    if(!m || !ts_matrix_valid(c))return 0;
    int activating=!m->controls.enabled && c->enabled;
    /* Route compilation and validation happen on the control thread. */
    int changed=!m->ready || m->controls.enabled!=c->enabled ||
        memcmp(m->controls.delayed,c->delayed,sizeof(c->delayed));
    if(!m->ready || activating)m->board_insert_current=(float)c->board_insert;
    m->mode_follow=rate?1-expf(-1.f/(rate*.012f)):1;
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(c->effect[i].id && c->effect[i].id!=m->controls.effect[i].id) {
        int n=TS_MATRIX_EFFECT_BASE+i;memset(m->history[n],0,sizeof(m->history[n]));
        m->input_peak[n]=m->output_peak[n]=m->input_peak[TS_MATRIX_EFFECT_SC_BASE+i]=0;
    }
    m->controls=*c;order_graph(c,m->order);
    for(int i=0;i<TS_MATRIX_ROWS;++i) {
        TsSourceRoute route=c->row[i];
        if(ts_matrix_is_processor(i))for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)
            if(c->delayed[i]&ts_matrix_feedback_bit(d))route.matrix_gain[d]=0;
        /* The dormant graph must start at its imported gains, not crossfade
           from unrelated default routes. Subsequent edits still slew. */
        if(activating)m->voice[i].ready=0;
        ts_source_route_set(&m->voice[i],route,rate);
    }
    if(changed)memset(m->history,0,sizeof(m->history));
    m->decay=rate?expf(-1.f/(rate*.15f)):0;m->ready=1;return 1;
}
static float peak(TsStereoFrame f){return fmaxf(fabsf(f.l),fabsf(f.r));}
void ts_matrix_process(TsRoutingMatrix *m,TsSourceRouteMix *sums,
    const TsStereoFrame live[TS_MATRIX_ROWS-TS_MATRIX_NODES],TsMatrixProcess fn,void *context)
{
    for(int s=TS_MATRIX_NODES;s<TS_MATRIX_ROWS;++s)if(!ts_matrix_is_processor(s)) {
        TsStereoFrame input=ts_stereo_frame_sanitize(live[s-TS_MATRIX_NODES]);
        m->output_peak[s]=fmaxf(peak(input),m->output_peak[s]*m->decay);
        ts_source_route_frame(&m->voice[s],input,sums);
    }
    for(int s=0;s<TS_MATRIX_ROWS;++s)if(m->controls.delayed[s]) {
        TsStereoFrame old=m->history[s][m->position];
        const TsSourceRoute *r=&m->controls.row[s];
        float l=r->pan>0?1-r->pan*.01f:1,rh=r->pan<0?1+r->pan*.01f:1;
        for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)if(m->controls.delayed[s]&ts_matrix_feedback_bit(d)) {
            float g=r->matrix_gain[d]*.001f;
            sums->graph[d].l+=old.l*g*l;sums->graph[d].r+=old.r*g*rh;
            float activity=fmaxf(fabsf(old.l*g*l),fabsf(old.r*g*rh));
            if(activity>m->voice[s].activity_send)m->voice[s].activity_send=activity;
        }
    }
    for(int i=0;i<TS_MATRIX_PROCESSORS;++i) {
        int n=m->order[i];
        if(n>=TS_MATRIX_EFFECT_BASE && !m->controls.effect[n-TS_MATRIX_EFFECT_BASE].id && !m->voice[n].remaining)continue;
        TsStereoFrame input=ts_stereo_frame_sanitize(sums->graph[n]);
        m->input_peak[n]=fmaxf(peak(input),m->input_peak[n]*m->decay);
        int sc=ts_matrix_sidechain_for(n);
        if(sc>=0) {
            m->sidechain=ts_stereo_frame_sanitize(sums->graph[sc]);
            m->input_peak[sc]=fmaxf(peak(m->sidechain),m->input_peak[sc]*m->decay);
        }
        TsStereoFrame output=ts_stereo_frame_sanitize(fn(context,n,input));
        m->output_peak[n]=fmaxf(peak(output),m->output_peak[n]*m->decay);
        /* Only feedback edges consume this history. Enabling/changing delayed
           edges clears history in ts_matrix_set, so dormant rows need no tanh
           or stores. Forward audio and effect tails still run every sample. */
        if(m->controls.delayed[n])
            m->history[n][m->position]=(TsStereoFrame){tanhf(output.l),tanhf(output.r)};
        ts_source_route_frame(&m->voice[n],output,sums);
    }
    for(int p=0;p<TS_MATRIX_OUTPUTS;++p)m->output[p]=ts_stereo_frame_sanitize(sums->graph[TS_MATRIX_NODES+p]);
    m->spatial_input=ts_stereo_frame_sanitize(sums->graph[TS_MATRIX_AMBISONICS]);
    m->input_peak[TS_MATRIX_AMBISONICS]=fmaxf(peak(m->spatial_input),m->input_peak[TS_MATRIX_AMBISONICS]*m->decay);
    for(int i=0;i<TS_SOURCE_SPEAKERS;++i) {
        TsStereoFrame f=ts_stereo_frame_sanitize(sums->graph[TS_MATRIX_SPEAKER_BASE+i]);
        m->speaker_output[i]=.5f*(f.l+f.r);
        m->input_peak[TS_MATRIX_SPEAKER_BASE+i]=fmaxf(fabsf(m->speaker_output[i]),m->input_peak[TS_MATRIX_SPEAKER_BASE+i]*m->decay);
    }
    m->position=(m->position+1)%TS_MATRIX_DELAY;
}
TsSourceRoute ts_matrix_source_route(TsSourceRoute old)
{
    if(old.matrix_enabled)return old;
    TsSourceRoute r={.matrix_enabled=1,.pan=old.pan,.width=old.width};
    if(old.mode==TS_SOURCE_MAIN || old.mode==TS_SOURCE_INHERIT)r.matrix_gain[TS_MATRIX_MAIN]=1000;
    else {
        int d=TS_MATRIX_NODES+old.speaker/2;
        r.matrix_gain[d]=old.mix_enabled?old.clean_level*10:1000;
        if(old.mode==TS_SOURCE_SPEAKER) {r.width=-100;r.pan=(old.speaker&1)?100:-100;}
    }
    if(old.mix_enabled) {
        r.matrix_gain[TS_MATRIX_PRISM]=old.send_level[TS_SEND_PRISM]*10;
        r.matrix_gain[TS_MATRIX_BOARD]=old.send_level[TS_SEND_PEDALBOARD]*10;
        r.matrix_gain[TS_MATRIX_FALLOUT]=old.send_level[TS_SEND_FALLOUT]*10;
    }
    return r;
}
int ts_matrix_write(FILE *f,const TsMatrixControls *c)
{
    if(!ts_matrix_valid(c))return 0;
    if(fprintf(f,"Matrix.Enabled=%d\nMatrix.Feedback=%d\nMatrix.SerialMask=%u\nMatrix.BoardInsert=%d\n",c->enabled,c->feedback,c->serial_mask,c->board_insert)<0)return 0;
    if(fprintf(f,"Matrix.NextEffectId=%" PRIu32 "\n",c->next_effect_id)<0)return 0;
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(c->effect[i].id) {
        const TsMatrixEffectControls *v=&c->effect[i];const TsCompressorControls *k=&v->compressor;
        if(fprintf(f,"Matrix.Effect%d=%" PRIu32 ",%d,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%d,%d,%.9g,%.9g,%.9g,%.9g,%.9g\n",
            i,v->id,v->type,v->enabled,v->a,v->b,v->c,v->mix,v->gain_db,v->transition,
            k->enabled,k->external,k->threshold_db,k->ratio,k->attack_ms,k->release_ms,k->output_db)<0)return 0;
    }
    for(int s=0;s<TS_MATRIX_ROWS;++s) {
        if(fprintf(f,"Matrix.Row%d=%d,%d,%" PRIu64,s,c->row[s].pan,c->row[s].width,ts_matrix_is_processor(s)?c->delayed[s]:UINT64_C(0))<0)return 0;
        for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)if(fprintf(f,",%d",c->row[s].matrix_gain[d])<0)return 0;
        if(fputc('\n',f)==EOF)return 0;
    }
    for(int p=0;p<TS_MATRIX_INPUTS;++p)if(fprintf(f,"Matrix.In%d=%s\n",p,c->input_name[p])<0)return 0;
    for(int p=0;p<TS_MATRIX_OUTPUTS;++p)if(fprintf(f,"Matrix.Out%d=%s\n",p,c->output_name[p])<0)return 0;
    return 1;
}
static int integer(const char **v,int *n)
{
    char *end;errno=0;long x=strtol(*v,&end,10);
    if(errno || end==*v || x<-100 || x>131071)return 0;
    *v=end;*n=(int)x;return 1;
}
int ts_matrix_read(TsMatrixControls *c,const char *key,const char *v)
{
    if(strncmp(key,"Matrix.",7))return 0;
    key+=7;int n=-1,used=0;TsMatrixControls next=*c;
    if(!strcmp(key,"NextEffectId")) {
        char *end;errno=0;unsigned long long id=strtoull(v,&end,10);
        if(*v<'0' || *v>'9' || errno || end==v || *end || !id || id>UINT32_MAX)return -1;
        next.next_effect_id=(uint32_t)id;
    } else if(sscanf(key,"Effect%d%n",&n,&used)==1 && !key[used] && n>=0 && n<TS_MATRIX_EFFECT_CAPACITY) {
        /* Parse in double precision first so out-of-range integer conversions
           cannot wrap on malformed project files. */
        double x[16];const char *p=v;
        for(int i=0;i<16;++i) {
            char *end;errno=0;x[i]=strtod(p,&end);
            if(errno || end==p || !isfinite(x[i]) || (i<15?*end!=',':*end!=0))return -1;
            p=end+(i<15);
        }
        if(x[0]<1 || x[0]>=UINT32_MAX || floor(x[0])!=x[0])return -1;
        const int ints[]={1,2,9,10};
        for(int i=0;i<4;++i)if(x[ints[i]]<0 || x[ints[i]]>TS_MATRIX_FX_TYPES || floor(x[ints[i]])!=x[ints[i]])return -1;
        TsMatrixEffectControls e={0};e.id=(uint32_t)x[0];e.type=(int)x[1];e.enabled=(int)x[2];
        e.a=x[3];e.b=x[4];e.c=x[5];e.mix=x[6];e.gain_db=x[7];e.transition=x[8];
        e.compressor=(TsCompressorControls){(int)x[9],(int)x[10],x[11],x[12],x[13],x[14],x[15]};
        if(!ts_matrix_effect_valid(&e))return -1;
        next.effect[n]=e;
    } else if(!strcmp(key,"SerialMask")) {
        const unsigned serial=(1u<<TS_MATRIX_MAIN)|(1u<<TS_MATRIX_PRISM)|(1u<<TS_MATRIX_BOARD)|(1u<<TS_MATRIX_FALLOUT);
        if(!integer(&v,&n) || *v || n<0 || ((unsigned)n&~serial))return -1;
        next.serial_mask=(unsigned)n;
    } else if(!strcmp(key,"BoardInsert")) {
        if(!integer(&v,&n) || *v || n<0 || n>1)return -1;
        next.board_insert=n;
    } else if(!strcmp(key,"Enabled") || !strcmp(key,"Feedback")) {
        if(!integer(&v,&n) || *v || n<0 || n>1)return -1;
        if(!strcmp(key,"Enabled"))next.enabled=n;else next.feedback=n;
    } else if(sscanf(key,"Row%d%n",&n,&used)==1 && !key[used] && n>=0 && n<TS_MATRIX_ROWS) {
        int pan,width;
        if(!integer(&v,&pan) || pan>100 || *v++!=',' || !integer(&v,&width) || width>100 || *v++!=',')return -1;
        if(*v<'0' || *v>'9')return -1;
        char *end;errno=0;uint64_t delay=strtoull(v,&end,10);
        if(errno || end==v || delay>>TS_MATRIX_INSTANCE_DESTINATIONS)return -1;v=end;
        next.row[n].pan=pan;next.row[n].width=width;if(ts_matrix_is_processor(n))next.delayed[n]=delay;else if(delay)return -1;
        memset(next.row[n].matrix_gain,0,sizeof(next.row[n].matrix_gain));
        for(int d=0;d<TS_MATRIX_DESTINATIONS;++d) {
            if(!*v && (d==TS_MATRIX_LEGACY_DESTINATIONS || d==TS_MATRIX_SHARED_DESTINATIONS || d==TS_MATRIX_INSTANCE_DESTINATIONS))break; /* Old 15-port sessions. */
            if(*v++!=',' || !integer(&v,&next.row[n].matrix_gain[d]) || next.row[n].matrix_gain[d]<0 || next.row[n].matrix_gain[d]>2000)return -1;
        }
        if(*v)return -1;
    } else {
        int input=sscanf(key,"In%d%n",&n,&used)==1 && !key[used];
        if(!input && !(sscanf(key,"Out%d%n",&n,&used)==1 && !key[used]))return -1;
        if(n<0 || n>=(input?TS_MATRIX_INPUTS:TS_MATRIX_OUTPUTS) || strlen(v)>=TS_MATRIX_NAME || !name_valid(v))return -1;
        snprintf(input?next.input_name[n]:next.output_name[n],TS_MATRIX_NAME,"%s",v);
    }
    /* Validate the complete graph after loading: row order may temporarily
       form a cycle while replacing the default graph. */
    *c=next;return 1;
}
