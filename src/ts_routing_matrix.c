#include "tapesister/routing_matrix.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

const char *ts_matrix_node_name(int n)
{
    static const char *names[]={"MAIN MIX","SISTER","PRISM","PEDALBOARD","FALLOUT","MASTER EQ","LIMITER"};
    return n>=0 && n<TS_MATRIX_NODES?names[n]:"OUTPUT";
}
void ts_matrix_default(TsMatrixControls *c)
{
    memset(c,0,sizeof(*c));
    for(int i=0;i<TS_MATRIX_ROWS;++i)c->row[i].matrix_enabled=1;
    c->row[TS_MATRIX_MAIN].matrix_gain[TS_MATRIX_EQ]=1000;
    c->row[TS_MATRIX_EQ].matrix_gain[TS_MATRIX_LIMITER]=1000;
    c->row[TS_MATRIX_LIMITER].matrix_gain[TS_MATRIX_NODES]=1000;
    for(int i=TS_MATRIX_SISTER;i<=TS_MATRIX_FALLOUT;++i)c->row[i].matrix_gain[TS_MATRIX_MAIN]=1000;
    c->row[TS_MATRIX_FM].matrix_gain[TS_MATRIX_MAIN]=1000;
    c->row[TS_MATRIX_LINK].matrix_gain[TS_MATRIX_MAIN]=1000;
    snprintf(c->output_name[0],TS_MATRIX_NAME,"MAIN SPEAKERS");
}
static int order_graph(const TsMatrixControls *c,int *order)
{
    unsigned done=0;
    for(int pos=0;pos<TS_MATRIX_NODES;++pos) {
        int found=-1;
        for(int n=0;n<TS_MATRIX_NODES;++n)if(!(done&(1u<<n))) {
            int incoming=0;
            for(int s=0;s<TS_MATRIX_NODES;++s)if(!(done&(1u<<s)) &&
                c->row[s].matrix_gain[n] && !(c->delayed[s]&(1u<<n)))incoming=1;
            if(!incoming){found=n;break;}
        }
        if(found<0)return 0;
        if(order)order[pos]=found;
        done|=1u<<found;
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
    if(!c || c->enabled<0 || c->enabled>1 || c->feedback<0 || c->feedback>1)return 0;
    for(int s=0;s<TS_MATRIX_ROWS;++s) {
        if(!ts_source_route_valid(&c->row[s],0) || !c->row[s].matrix_enabled)return 0;
        if(s<TS_MATRIX_NODES) {
            if(c->delayed[s]>>TS_MATRIX_NODES || (!c->feedback && c->delayed[s]))return 0;
            for(int d=0;d<TS_MATRIX_NODES;++d)if((c->delayed[s]&(1u<<d)) &&
                (c->row[s].matrix_gain[d]>500 || !c->row[s].matrix_gain[d]))return 0;
        }
    }
    for(int i=0;i<TS_MATRIX_INPUTS;++i)if(!name_valid(c->input_name[i]))return 0;
    for(int i=0;i<TS_MATRIX_OUTPUTS;++i)if(!name_valid(c->output_name[i]))return 0;
    return order_graph(c,NULL);
}
int ts_matrix_connect(TsMatrixControls *c,int s,int d,int gain)
{
    if(!c || s<0 || s>=TS_MATRIX_ROWS || d<0 || d>=TS_MATRIX_DESTINATIONS || gain<0 || gain>2000)return -1;
    TsMatrixControls next=*c;next.row[s].matrix_gain[d]=gain;
    if(s<TS_MATRIX_NODES) {
        if(!gain)next.delayed[s]&=~(1u<<d);
        if(!order_graph(&next,NULL)) {
            if(!next.feedback)return 0;
            next.delayed[s]|=1u<<d;
        }
        if(next.delayed[s]&(1u<<d))next.row[s].matrix_gain[d]=gain>500?500:gain;
    }
    if(!ts_matrix_valid(&next))return -1;
    *c=next;return 1;
}
void ts_matrix_cut_feedback(TsMatrixControls *c)
{
    for(int s=0;s<TS_MATRIX_NODES;++s) {
        for(int d=0;d<TS_MATRIX_NODES;++d)if(c->delayed[s]&(1u<<d))c->row[s].matrix_gain[d]=0;
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
    /* Route compilation and validation happen on the control thread. */
    int changed=!m->ready || m->controls.enabled!=c->enabled ||
        memcmp(m->controls.delayed,c->delayed,sizeof(c->delayed));
    m->controls=*c;order_graph(c,m->order);
    for(int i=0;i<TS_MATRIX_ROWS;++i) {
        TsSourceRoute route=c->row[i];
        if(i<TS_MATRIX_NODES)for(int d=0;d<TS_MATRIX_NODES;++d)
            if(c->delayed[i]&(1u<<d))route.matrix_gain[d]=0;
        ts_source_route_set(&m->voice[i],route,rate);
    }
    if(changed)memset(m->history,0,sizeof(m->history));
    m->decay=rate?expf(-1.f/(rate*.15f)):0;m->ready=1;return 1;
}
static float peak(TsStereoFrame f){return fmaxf(fabsf(f.l),fabsf(f.r));}
void ts_matrix_process(TsRoutingMatrix *m,TsSourceRouteMix *sums,
    const TsStereoFrame live[TS_MATRIX_ROWS-TS_MATRIX_NODES],TsMatrixProcess fn,void *context)
{
    for(int s=TS_MATRIX_NODES;s<TS_MATRIX_ROWS;++s) {
        TsStereoFrame input=ts_stereo_frame_sanitize(live[s-TS_MATRIX_NODES]);
        m->output_peak[s]=fmaxf(peak(input),m->output_peak[s]*m->decay);
        ts_source_route_frame(&m->voice[s],input,sums);
    }
    for(int s=0;s<TS_MATRIX_NODES;++s)if(m->controls.delayed[s]) {
        TsStereoFrame old=m->history[s][m->position];
        const TsSourceRoute *r=&m->controls.row[s];
        float l=r->pan>0?1-r->pan*.01f:1,rh=r->pan<0?1+r->pan*.01f:1;
        for(int d=0;d<TS_MATRIX_NODES;++d)if(m->controls.delayed[s]&(1u<<d)) {
            float g=r->matrix_gain[d]*.001f;
            sums->graph[d].l+=old.l*g*l;sums->graph[d].r+=old.r*g*rh;
        }
    }
    for(int i=0;i<TS_MATRIX_NODES;++i) {
        int n=m->order[i];TsStereoFrame input=ts_stereo_frame_sanitize(sums->graph[n]);
        m->input_peak[n]=fmaxf(peak(input),m->input_peak[n]*m->decay);
        TsStereoFrame output=ts_stereo_frame_sanitize(fn(context,n,input));
        m->output_peak[n]=fmaxf(peak(output),m->output_peak[n]*m->decay);
        /* A bounded, explicit delay is the only graph feedback path. */
        m->history[n][m->position]=(TsStereoFrame){tanhf(output.l),tanhf(output.r)};
        ts_source_route_frame(&m->voice[n],output,sums);
    }
    for(int p=0;p<TS_MATRIX_OUTPUTS;++p)m->output[p]=ts_stereo_frame_sanitize(sums->graph[TS_MATRIX_NODES+p]);
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
    if(fprintf(f,"Matrix.Enabled=%d\nMatrix.Feedback=%d\n",c->enabled,c->feedback)<0)return 0;
    for(int s=0;s<TS_MATRIX_ROWS;++s) {
        if(fprintf(f,"Matrix.Row%d=%d,%d,%u",s,c->row[s].pan,c->row[s].width,s<TS_MATRIX_NODES?c->delayed[s]:0)<0)return 0;
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
    if(errno || end==*v || x<-100 || x>65535)return 0;
    *v=end;*n=(int)x;return 1;
}
int ts_matrix_read(TsMatrixControls *c,const char *key,const char *v)
{
    if(strncmp(key,"Matrix.",7))return 0;
    key+=7;int n=-1,used=0;TsMatrixControls next=*c;
    if(!strcmp(key,"Enabled") || !strcmp(key,"Feedback")) {
        if(!integer(&v,&n) || *v || n<0 || n>1)return -1;
        if(!strcmp(key,"Enabled"))next.enabled=n;else next.feedback=n;
    } else if(sscanf(key,"Row%d%n",&n,&used)==1 && !key[used] && n>=0 && n<TS_MATRIX_ROWS) {
        int pan,width,delay;
        if(!integer(&v,&pan) || pan>100 || *v++!=',' || !integer(&v,&width) || width>100 || *v++!=',' || !integer(&v,&delay) || delay<0 || delay>>TS_MATRIX_NODES)return -1;
        next.row[n].pan=pan;next.row[n].width=width;if(n<TS_MATRIX_NODES)next.delayed[n]=(unsigned)delay;else if(delay)return -1;
        for(int d=0;d<TS_MATRIX_DESTINATIONS;++d) {
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
