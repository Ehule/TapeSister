#include "tapesister/matrix_layout.h"
#include <inttypes.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "tapesister/movement_name.h"
#include "ts_movement_rename.inc"

TsFxCanvasGroup *ts_matrix_layout_group(TsMatrixLayout *l,uint32_t id)
{if(id && l)for(int i=0;i<l->group_count;++i)if(l->groups[i].id==id)return &l->groups[i];return NULL;}
static int group_valid(const TsFxCanvasGroup *p)
{
    if(!p->id || (p->color>>24)!=255 || !isfinite(p->x) || !isfinite(p->y) || fabsf(p->x)>1000000 || fabsf(p->y)>1000000 ||
       !isfinite(p->w) || !isfinite(p->h) || p->w<32 || p->h<32 || p->w>2000000 || p->h>2000000 ||
       !isfinite(p->opacity) || p->opacity<0 || p->opacity>1 || !memchr(p->name,0,sizeof(p->name)))return 0;
    for(const unsigned char *s=(const unsigned char*)p->name;*s;++s)if(*s<32 || *s>126)return 0;return 1;
}
static int group_exists(const TsMatrixLayout *l,uint32_t id)
{if(!id)return 1;for(int i=0;i<l->group_count;++i)if(l->groups[i].id==id)return 1;return 0;}
void ts_matrix_layout_default(TsMatrixLayout *l)
{
    memset(l,0,sizeof(*l));
    l->canvas_zoom=1;l->canvas_snap=1;l->canvas_split=TS_FX_CANVAS_SPLIT_DEFAULT;
    for(int i=0;i<TS_MATRIX_DESTINATIONS;++i)l->column[i]=(unsigned char)i;
}
typedef struct { uint64_t id;unsigned kind; } LayoutKey;
static int key_compare(const void *a,const void *b)
{
    const LayoutKey *x=a,*y=b;
    if(x->kind!=y->kind)return x->kind<y->kind?-1:1;
    return x->id<y->id?-1:x->id>y->id;
}
static int cable_valid(const TsFxCableStyle *p)
{
    int effect=ts_matrix_effect_index(p->destination)>=0 ||
        (p->destination>=TS_MATRIX_EFFECT_SC_BASE && p->destination<TS_MATRIX_INSTANCE_DESTINATIONS);
    return p->kind>=0 && p->kind<=4 && (!p->canvas || p->kind==3) &&
        (p->kind!=0 || p->source<TS_TRACKER_LANES) && (p->kind!=2 || p->source<TS_MATRIX_ROWS) &&
        ((p->kind!=1 && p->kind!=3 && p->kind!=4) || p->source) &&
        p->destination>=0 && p->destination<TS_FX_DESTINATIONS &&
        (!!p->target==effect) && (p->color>>24)==255;
}
static int label_valid(const TsFxCanvasLabel *p)
{
    if(!isfinite(p->x) || !isfinite(p->y) || fabsf(p->x)>1000000 || fabsf(p->y)>1000000 ||
       !isfinite(p->scale) || p->scale<.25f || p->scale>4 || !memchr(p->text,0,sizeof(p->text)))return 0;
    for(const unsigned char *s=(const unsigned char *)p->text;*s;++s)if(*s!='\n' && (*s<32 || *s>126))return 0;
    return 1;
}
int ts_matrix_layout_valid(const TsMatrixLayout *l)
{
    if(!l || l->row_count<0 || l->row_count>TS_MATRIX_LAYOUT_ROWS)return 0;
    if(l->group_count<0 || l->group_count>TS_FX_GROUP_LIMIT)return 0;
    for(int i=0;i<l->group_count;++i){if(!group_valid(&l->groups[i]))return 0;for(int j=0;j<i;++j)if(l->groups[j].id==l->groups[i].id)return 0;}
    if(l->cable_count<0 || l->cable_count>TS_FX_CABLE_STYLES)return 0;
    for(int i=0;i<l->cable_count;++i)if(!cable_valid(&l->cables[i]))return 0;
    if(l->label_count<0 || l->label_count>TS_FX_LABEL_LIMIT)return 0;
    for(int i=0;i<l->label_count;++i)if(!label_valid(&l->labels[i]) || !group_exists(l,l->labels[i].group))return 0;
    if(l->heading_count<0 || l->heading_count>TS_FX_HEADING_LIMIT)return 0;
    for(int i=0;i<l->heading_count;++i) {
        if(!label_valid(&l->headings[i].label) || !group_exists(l,l->headings[i].label.group))return 0;
        for(int j=0;j<i;++j)if(l->headings[j].canvas==l->headings[i].canvas)return 0;
    }
    unsigned char seen[TS_MATRIX_DESTINATIONS]={0};
    for(int i=0;i<TS_MATRIX_DESTINATIONS;++i) {
        unsigned d=l->column[i];
        if(d>=TS_MATRIX_DESTINATIONS || seen[d])return 0;
        seen[d]=1;
    }
    if(l->canvas_count<0 || l->canvas_count>TS_FX_CANVAS_POSITIONS || l->canvas_view<0 || l->canvas_view>1 || l->canvas_straight<0 || l->canvas_straight>1 || l->canvas_snap<0 || l->canvas_snap>1 || l->canvas_flow<0 || l->canvas_flow>1 ||
       (l->canvas_flow_color && (l->canvas_flow_color>>24)!=255) ||
       l->canvas_split<TS_FX_CANVAS_SPLIT_MIN || l->canvas_split>TS_FX_CANVAS_SPLIT_MAX ||
       !isfinite(l->canvas_x) || !isfinite(l->canvas_y) ||
       fabsf(l->canvas_x)>1000000 || fabsf(l->canvas_y)>1000000 ||
       !isfinite(l->canvas_zoom) || l->canvas_zoom<.001f || l->canvas_zoom>2)return 0;
    for(int i=0;i<l->canvas_count;++i) {
        const TsFxCanvasPosition *p=&l->canvas[i];
        if(!group_exists(l,p->group) || !memchr(p->name,0,sizeof(p->name)) || (p->color && (p->color>>24)!=255))return 0;
        for(const unsigned char *t=(const unsigned char *)p->name;*t;++t)if(*t<32 || *t>126)return 0;
        if(p->kind<0 || p->kind>5 || !isfinite(p->x) || !isfinite(p->y) ||
           !isfinite(p->scale) || (p->scale!=0 && (p->scale<.25f || p->scale>4.f)) ||
           fabsf(p->x)>1000000 || fabsf(p->y)>1000000 ||
           (p->canvas && p->kind!=3) ||
           (p->kind==0 && p->id>=TS_TRACKER_LANES) ||
           (p->kind==2 && p->id>=TS_MATRIX_ROWS) ||
           (p->kind==5 && p->id>=TS_MATRIX_DESTINATIONS) ||
           ((p->kind==1 || p->kind==3 || p->kind==4) && !p->id))return 0;
        for(int j=0;j<i;++j)if(l->canvas[j].kind==p->kind && l->canvas[j].id==p->id && l->canvas[j].canvas==p->canvas)return 0;
    }
    if(!l->row_count)return 1;
    LayoutKey *keys=malloc((size_t)l->row_count*sizeof(*keys));if(!keys)return 0;
    int valid=1;
    for(int i=0;i<l->row_count;++i) {
        unsigned kind=l->row_kind[i];uint64_t id=l->row_id[i];
        if(kind>4 || (kind==0 && id>=TS_TRACKER_LANES) || (kind==2 && id>=TS_MATRIX_ROWS) ||
           ((kind==1 || kind==3 || kind==4) && !id)){valid=0;break;}
        keys[i]=(LayoutKey){id,kind};
    }
    if(valid) {
        qsort(keys,(size_t)l->row_count,sizeof(*keys),key_compare);
        for(int i=1;i<l->row_count;++i)if(!key_compare(keys+i-1,keys+i)){valid=0;break;}
    }
    free(keys);return valid;
}
int ts_matrix_layout_write(FILE *f,const TsMatrixLayout *l)
{
    if(!ts_matrix_layout_valid(l))return 0;
    for(int i=0;i<l->group_count;++i){const TsFxCanvasGroup *p=&l->groups[i];
        if(fprintf(f,"FxCanvas.Group%d=%u,%u,%.9g,%.9g,%.9g,%.9g,%.9g,",i,p->id,p->color,p->x,p->y,p->w,p->h,p->opacity)<0)return 0;
        for(const unsigned char *s=(const unsigned char*)p->name;*s;++s)if(fprintf(f,"%02x",*s)<0)return 0;
        if(fputc('\n',f)==EOF)return 0;}

    for(int i=0;i<l->cable_count;++i) {
        const TsFxCableStyle *p=&l->cables[i];
        if(fprintf(f,"FxCanvas.Cable%d=%d,%" PRIu64 ",%" PRIu64 ",%d,%u,%u\n",i,p->kind,p->source,p->canvas,p->destination,p->target,p->color)<0)return 0;
    }
    if(l->canvas_view && fprintf(f,"FxCanvas.View=%.9g,%.9g,%.9g\n",l->canvas_x,l->canvas_y,l->canvas_zoom)<0)return 0;
    if(fprintf(f,"FxCanvas.Straight=%d\n",l->canvas_straight)<0)return 0;
    if(fprintf(f,"FxCanvas.Snap=%d\n",l->canvas_snap)<0)return 0;
    if(l->canvas_flow && fprintf(f,"FxCanvas.Flow=1\n")<0)return 0;
    if(l->canvas_flow_color && fprintf(f,"FxCanvas.FlowColor=%" PRIu32 "\n",l->canvas_flow_color)<0)return 0;
    if(fprintf(f,"FxCanvas.Split=%d\n",l->canvas_split)<0)return 0;
    for(int i=0;i<l->label_count;++i) {
        const TsFxCanvasLabel *p=&l->labels[i];
        if(fprintf(f,"FxCanvas.Label%d=%.9g,%.9g,%.9g,",i,p->x,p->y,p->scale)<0)return 0;
        for(const unsigned char *s=(const unsigned char *)p->text;*s;++s)if(fprintf(f,"%02x",*s)<0)return 0;
        if(fputc('\n',f)==EOF)return 0;
        if(p->group && fprintf(f,"FxCanvas.MemberLabel%d=%u\n",i,p->group)<0)return 0;
    }
    for(int i=0;i<l->heading_count;++i) {
        const TsFxCanvasHeading *h=&l->headings[i];const TsFxCanvasLabel *p=&h->label;
        if(fprintf(f,"FxCanvas.Heading%d=%" PRIu64 ",%.9g,%.9g,%.9g,",i,h->canvas,p->x,p->y,p->scale)<0)return 0;
        for(const unsigned char *s=(const unsigned char *)p->text;*s;++s)if(fprintf(f,"%02x",*s)<0)return 0;
        if(fputc('\n',f)==EOF)return 0;
        if(p->group && fprintf(f,"FxCanvas.MemberHeading%d=%u\n",i,p->group)<0)return 0;
    }
    for(int i=0;i<l->canvas_count;++i) {
        const TsFxCanvasPosition *p=&l->canvas[i];
        if(fprintf(f,"FxCanvas.Node%d=%d,%" PRIu64 ",%.9g,%.9g",i,p->kind,p->id,p->x,p->y)<0)return 0;
        if(p->scale!=0 && fprintf(f,",%.9g",p->scale)<0)return 0;
        if(fputc('\n',f)==EOF)return 0;
        if(p->color || p->name[0]) {
            if(fprintf(f,"FxCanvas.Style%d=%u,",i,p->color)<0)return 0;
            for(const unsigned char *t=(const unsigned char *)p->name;*t;++t)if(fprintf(f,"%02x",*t)<0)return 0;
            if(fputc('\n',f)==EOF)return 0;
        }
        if(p->group && fprintf(f,"FxCanvas.MemberNode%d=%u\n",i,p->group)<0)return 0;
        if(p->canvas && fprintf(f,"FxCanvas.Scene%d=%" PRIu64 "\n",i,p->canvas)<0)return 0;
    }
    for(int i=0;i<TS_MATRIX_DESTINATIONS;++i)
        if(fprintf(f,"MatrixLayout.Column%d=%u\n",i,(unsigned)l->column[i])<0)return 0;
    for(int i=0;i<l->row_count;++i)
        if(fprintf(f,"MatrixLayout.Row%d=%u,%" PRIu64 "\n",i,(unsigned)l->row_kind[i],l->row_id[i])<0)return 0;
    return 1;
}
static int layout_number(const char **cursor,uint64_t maximum,uint64_t *number)
{
    const char *s=*cursor;while(isspace((unsigned char)*s))++s;
    if(!isdigit((unsigned char)*s))return 0;
    char *end;errno=0;unsigned long long n=strtoull(s,&end,10);
    if(errno || n>maximum)return 0;
    while(isspace((unsigned char)*end))++end;
    *number=(uint64_t)n;*cursor=end;return 1;
}
int ts_matrix_layout_read(TsMatrixLayout *l,const char *key,const char *value)
{
    uint64_t i,d,kind,id;const char *s;
    if(!strncmp(key,"FxCanvas.Group",14)){
        s=key+14;TsFxCanvasGroup p={0};uint64_t color;int end=0;
        if(!layout_number(&s,TS_FX_GROUP_LIMIT-1,&i) || *s || i!=(uint64_t)l->group_count ||
           !layout_number(&value,UINT32_MAX,&id) || !id || *value++!=',' || !layout_number(&value,UINT32_MAX,&color) || *value++!=',' ||
           sscanf(value,"%f,%f,%f,%f,%f,%n",&p.x,&p.y,&p.w,&p.h,&p.opacity,&end)!=5 || !end)return -1;
        p.id=(uint32_t)id;p.color=(uint32_t)color;s=value+end;size_t len=strlen(s);if(len%2 || len/2>=sizeof(p.name))return -1;
        for(size_t j=0;j<len;j+=2){if(!isxdigit((unsigned char)s[j]) || !isxdigit((unsigned char)s[j+1]))return -1;
            char byte[3]={s[j],s[j+1],0};unsigned v=(unsigned)strtoul(byte,NULL,16);if(v<32 || v>126)return -1;p.name[j/2]=(char)v;}
        if(!group_valid(&p) || ts_matrix_layout_group(l,p.id))return -1;l->groups[l->group_count++]=p;return 1;
    }
    if(!strncmp(key,"FxCanvas.Member",15)){
        const char *kind=key+15;int type=!strncmp(kind,"Node",4)?0:!strncmp(kind,"Label",5)?1:!strncmp(kind,"Heading",7)?2:-1;
        if(type<0)return -1;s=kind+(type==0?4:type==1?5:7);unsigned count=type==0?l->canvas_count:type==1?l->label_count:l->heading_count;
        if(!layout_number(&s,UINT32_MAX,&i) || *s || i>=count || !layout_number(&value,UINT32_MAX,&id) || *value || !id || !group_exists(l,(uint32_t)id))return -1;
        uint32_t *group=type==0?&l->canvas[i].group:type==1?&l->labels[i].group:&l->headings[i].label.group;
        if(*group)return -1;*group=(uint32_t)id;return 1;
    }
    if(!strncmp(key,"FxCanvas.Cable",14)) {
        s=key+14;uint64_t v[6];
        if(!layout_number(&s,TS_FX_CABLE_STYLES-1,&i) || *s || i!=(uint64_t)l->cable_count)return -1;
        for(int j=0;j<6;++j)if(!layout_number(&value,j==1 || j==2?UINT64_MAX:UINT32_MAX,&v[j]) || (j<5?*value++!=',':*value!=0))return -1;
        if(v[0]>4 || v[3]>=TS_FX_DESTINATIONS)return -1;
        TsFxCableStyle p={v[1],v[2],(int)v[0],(int)v[3],(uint32_t)v[4],(uint32_t)v[5]};
        if(!cable_valid(&p))return -1;
        l->cables[l->cable_count++]=p;return 1;
    }
    int heading=!strncmp(key,"FxCanvas.Heading",16);
    if(heading || !strncmp(key,"FxCanvas.Label",14)) {
        s=key+(heading?16:14);TsFxCanvasLabel p={0};int end=0;uint64_t canvas=0;
        if(!layout_number(&s,heading?TS_FX_HEADING_LIMIT-1:TS_FX_LABEL_LIMIT-1,&i) || *s ||
           i!=(uint64_t)(heading?l->heading_count:l->label_count))return -1;
        if(heading) {
            if(!layout_number(&value,UINT64_MAX,&canvas) || *value++!=',')return -1;
            for(int j=0;j<l->heading_count;++j)if(l->headings[j].canvas==canvas)return -1;
        }
        if(sscanf(value,"%f,%f,%f,%n",&p.x,&p.y,&p.scale,&end)!=3 || !end ||
           !isfinite(p.x) || !isfinite(p.y) || fabsf(p.x)>1000000 || fabsf(p.y)>1000000 ||
           !isfinite(p.scale) || p.scale<.25f || p.scale>4)return -1;
        s=value+end;size_t len=strlen(s);if(len%2 || len/2>=sizeof(p.text))return -1;
        for(size_t j=0;j<len;j+=2) {
            if(!isxdigit((unsigned char)s[j]) || !isxdigit((unsigned char)s[j+1]))return -1;
            char byte[3]={s[j],s[j+1],0};unsigned v=(unsigned)strtoul(byte,NULL,16);
            if(v!='\n' && (v<32 || v>126))return -1;p.text[j/2]=(char)v;
        }
        if(heading) {
            TsFxCanvasHeading *h=&l->headings[l->heading_count++];
            h->canvas=canvas;h->label=p;
        }
        else l->labels[l->label_count++]=p;
        return 1;
    }
    if(!strcmp(key,"FxCanvas.Straight")) {
        if(!strcmp(value,"0"))l->canvas_straight=0;else if(!strcmp(value,"1"))l->canvas_straight=1;else return -1;
        return 1;
    }
    if(!strcmp(key,"FxCanvas.Snap")) {
        if(!strcmp(value,"0"))l->canvas_snap=0;else if(!strcmp(value,"1"))l->canvas_snap=1;else return -1;
        return 1;
    }
    if(!strcmp(key,"FxCanvas.Flow")) {
        if(strcmp(value,"0") && strcmp(value,"1"))return -1;
        l->canvas_flow=value[0]=='1';return 1;
    }
    if(!strcmp(key,"FxCanvas.FlowColor")) {
        if(!layout_number(&value,UINT32_MAX,&i) || *value || (i && (i>>24)!=255))return -1;
        l->canvas_flow_color=(uint32_t)i;return 1;
    }
    if(!strcmp(key,"FxCanvas.Split")) {
        if(!layout_number(&value,TS_FX_CANVAS_SPLIT_MAX,&i) || *value || i<TS_FX_CANVAS_SPLIT_MIN)return -1;
        l->canvas_split=(int)i;return 1;
    }
    if(!strcmp(key,"FxCanvas.View")) {
        float x,y,z;char extra;
        if(sscanf(value,"%f,%f,%f%c",&x,&y,&z,&extra)!=3 || !isfinite(x) || !isfinite(y) ||
           fabsf(x)>1000000 || fabsf(y)>1000000 || !isfinite(z) || z<.001f || z>2)return -1;
        l->canvas_x=x;l->canvas_y=y;l->canvas_zoom=z;l->canvas_view=1;return 1;
    }
    if(!strncmp(key,"FxCanvas.Style",14)) {
        s=key+14;uint64_t color;
        if(!layout_number(&s,TS_FX_CANVAS_POSITIONS-1,&i) || *s || i>=(uint64_t)l->canvas_count ||
           !layout_number(&value,UINT32_MAX,&color) || *value++!=',' || (color && color>>24!=255))return -1;
        size_t len=strlen(value);if(len%2 || len/2>=sizeof(l->canvas[i].name))return -1;
        char name[sizeof(l->canvas[i].name)]={0};
        for(size_t j=0;j<len;j+=2) {
            if(!isxdigit((unsigned char)value[j]) || !isxdigit((unsigned char)value[j+1]))return -1;
            char byte[3]={value[j],value[j+1],0};unsigned v=(unsigned)strtoul(byte,NULL,16);
            if(v<32 || v>126)return -1;name[j/2]=(char)v;
        }
        l->canvas[i].color=(uint32_t)color;memcpy(l->canvas[i].name,name,sizeof(name));return 1;
    }
    if(!strncmp(key,"FxCanvas.Scene",14)) {
        s=key+14;
        if(!layout_number(&s,TS_FX_CANVAS_POSITIONS-1,&i) || *s || i>=(uint64_t)l->canvas_count ||
           l->canvas[i].kind!=3 || l->canvas[i].canvas || !layout_number(&value,UINT64_MAX,&id) || !id || *value)return -1;
        l->canvas[i].canvas=id;return 1;
    }
    if(!strncmp(key,"FxCanvas.Node",13)) {
        s=key+13;float x,y,scale=0;char extra;int end=0;
        if(!layout_number(&s,TS_FX_CANVAS_POSITIONS-1,&i) || *s || i!=(uint64_t)l->canvas_count ||
           !layout_number(&value,5,&kind) || *value++!=',' || !layout_number(&value,UINT64_MAX,&id) ||
           *value++!=',' || sscanf(value,"%f,%f%n",&x,&y,&end)!=2 || !isfinite(x) || !isfinite(y) ||
           fabsf(x)>1000000 || fabsf(y)>1000000)return -1;
        if(value[end] && (sscanf(value+end,",%f%c",&scale,&extra)!=1 || !isfinite(scale) || scale<.25f || scale>4.f))return -1;
        l->canvas[l->canvas_count++]=(TsFxCanvasPosition){id,(int)kind,x,y,scale};return 1;
    }
    if(!strncmp(key,"MatrixLayout.Column",19)) {
        s=key+19;
        if(!layout_number(&s,TS_MATRIX_DESTINATIONS-1,&i) || *s ||
           !layout_number(&value,TS_MATRIX_DESTINATIONS-1,&d) || *value)return -1;
        l->column[i]=(unsigned char)d;return 1;
    }
    if(!strncmp(key,"MatrixLayout.Row",16)) {
        s=key+16;
        /* Consecutive entries reject holes, duplicate indices and oversized files. */
        if(!layout_number(&s,TS_MATRIX_LAYOUT_ROWS-1,&i) || *s || i!=(uint64_t)l->row_count ||
           !layout_number(&value,4,&kind) || *value++!=',' ||
           !layout_number(&value,UINT64_MAX,&id) || *value)return -1;
        l->row_kind[i]=(unsigned char)kind;l->row_id[i]=id;++l->row_count;return 1;
    }
    return -1;
}
