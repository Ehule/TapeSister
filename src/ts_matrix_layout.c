#include "tapesister/matrix_layout.h"
#include <inttypes.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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
int ts_matrix_layout_valid(const TsMatrixLayout *l)
{
    if(!l || l->row_count<0 || l->row_count>TS_MATRIX_LAYOUT_ROWS)return 0;
    unsigned char seen[TS_MATRIX_DESTINATIONS]={0};
    for(int i=0;i<TS_MATRIX_DESTINATIONS;++i) {
        unsigned d=l->column[i];
        if(d>=TS_MATRIX_DESTINATIONS || seen[d])return 0;
        seen[d]=1;
    }
    if(l->canvas_count<0 || l->canvas_count>TS_FX_CANVAS_POSITIONS || l->canvas_view<0 || l->canvas_view>1 || l->canvas_straight<0 || l->canvas_straight>1 || l->canvas_snap<0 || l->canvas_snap>1 ||
       l->canvas_split<TS_FX_CANVAS_SPLIT_MIN || l->canvas_split>TS_FX_CANVAS_SPLIT_MAX ||
       !isfinite(l->canvas_x) || !isfinite(l->canvas_y) ||
       fabsf(l->canvas_x)>1000000 || fabsf(l->canvas_y)>1000000 ||
       !isfinite(l->canvas_zoom) || l->canvas_zoom<.001f || l->canvas_zoom>2)return 0;
    for(int i=0;i<l->canvas_count;++i) {
        const TsFxCanvasPosition *p=&l->canvas[i];
        if(p->kind<0 || p->kind>5 || !isfinite(p->x) || !isfinite(p->y) ||
           !isfinite(p->scale) || (p->scale!=0 && (p->scale<.25f || p->scale>4.f)) ||
           fabsf(p->x)>1000000 || fabsf(p->y)>1000000 ||
           (p->kind==0 && p->id>=TS_TRACKER_LANES) ||
           (p->kind==2 && p->id>=TS_MATRIX_ROWS) ||
           (p->kind==5 && p->id>=TS_MATRIX_DESTINATIONS) ||
           ((p->kind==1 || p->kind==3 || p->kind==4) && !p->id))return 0;
        for(int j=0;j<i;++j)if(l->canvas[j].kind==p->kind && l->canvas[j].id==p->id)return 0;
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
    if(l->canvas_view && fprintf(f,"FxCanvas.View=%.9g,%.9g,%.9g\n",l->canvas_x,l->canvas_y,l->canvas_zoom)<0)return 0;
    if(fprintf(f,"FxCanvas.Straight=%d\n",l->canvas_straight)<0)return 0;
    if(fprintf(f,"FxCanvas.Snap=%d\n",l->canvas_snap)<0)return 0;
    if(fprintf(f,"FxCanvas.Split=%d\n",l->canvas_split)<0)return 0;
    for(int i=0;i<l->canvas_count;++i) {
        const TsFxCanvasPosition *p=&l->canvas[i];
        if(fprintf(f,"FxCanvas.Node%d=%d,%" PRIu64 ",%.9g,%.9g",i,p->kind,p->id,p->x,p->y)<0)return 0;
        if(p->scale!=0 && fprintf(f,",%.9g",p->scale)<0)return 0;
        if(fputc('\n',f)==EOF)return 0;
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
    if(!strcmp(key,"FxCanvas.Straight")) {
        if(!strcmp(value,"0"))l->canvas_straight=0;else if(!strcmp(value,"1"))l->canvas_straight=1;else return -1;
        return 1;
    }
    if(!strcmp(key,"FxCanvas.Snap")) {
        if(!strcmp(value,"0"))l->canvas_snap=0;else if(!strcmp(value,"1"))l->canvas_snap=1;else return -1;
        return 1;
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
