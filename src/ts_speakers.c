#include "tapesister/speakers.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
void ts_speakers_default(TsSpeakerSetup *s)
{
    memset(s,0,sizeof(*s));s->count=16;
    for(int i=0;i<16;++i){s->output[i]=i;snprintf(s->name[i],TS_SPEAKER_NAME,"SPEAKER %d",i+1);}
}
int ts_speakers_valid(const TsSpeakerSetup *s)
{
    if(!s || s->count<1 || s->count>16)return 0;
    for(int i=0;i<16;++i) {
        if(s->output[i]<0 || s->output[i]>63 || !memchr(s->name[i],0,TS_SPEAKER_NAME))return 0;
        for(const char *p=s->name[i];*p;++p)if((unsigned char)*p<32 || (unsigned char)*p>126 || *p=='=')return 0;
        if(i<s->count)for(int j=0;j<i;++j)if(s->output[i]==s->output[j])return 0;
    }
    return 1;
}
int ts_speakers_channel(const TsSpeakerSetup *s,int i,unsigned channels)
{
    if(!s || i<0 || i>=s->count || i>=16)return -1;
    int ch=s->output[i];return ch>=0 && ch<64 && (unsigned)ch<channels?ch:-1;
}
int ts_speakers_write(FILE *f,const TsSpeakerSetup *s)
{
    if(!ts_speakers_valid(s) || fprintf(f,"Speakers.Count=%d\n",s->count)<0)return 0;
    for(int i=0;i<16;++i)if(fprintf(f,"Speakers.Output%d=%d\nSpeakers.Name%d=%s\n",i,s->output[i],i,s->name[i])<0)return 0;
    return 1;
}
int ts_speakers_read(TsSpeakerSetup *s,const char *key,const char *v)
{
    if(strncmp(key,"Speakers.",9))return 0;
    key+=9;int i=-1,n=0;
    if(sscanf(key,"Name%d%n",&i,&n)==1 && !key[n] && i>=0 && i<16) {
        if(strlen(v)>=TS_SPEAKER_NAME)return -1;
        for(const char *p=v;*p;++p)if((unsigned char)*p<32 || (unsigned char)*p>126 || *p=='=')return -1;
        snprintf(s->name[i],TS_SPEAKER_NAME,"%s",v);return 1;
    }
    int count=!strcmp(key,"Count");
    if(!count && !(sscanf(key,"Output%d%n",&i,&n)==1 && !key[n] && i>=0 && i<16))return -1;
    char *end;errno=0;long value=strtol(v,&end,10);
    if(errno || end==v || *end || value<(count?1:0) || value>(count?16:63))return -1;
    if(count)s->count=(int)value;else s->output[i]=(int)value;
    return 1;
}
