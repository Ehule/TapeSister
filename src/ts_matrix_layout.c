#include "tapesister/matrix_layout.h"
#include <inttypes.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

void ts_matrix_layout_default(TsMatrixLayout *l)
{
    memset(l,0,sizeof(*l));
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
    unsigned seen=0;
    for(int i=0;i<TS_MATRIX_DESTINATIONS;++i) {
        unsigned d=l->column[i];
        if(d>=TS_MATRIX_DESTINATIONS || (seen&(1u<<d)))return 0;
        seen|=1u<<d;
    }
    if(!l->row_count)return 1;
    LayoutKey *keys=malloc((size_t)l->row_count*sizeof(*keys));if(!keys)return 0;
    int valid=1;
    for(int i=0;i<l->row_count;++i) {
        unsigned kind=l->row_kind[i];uint64_t id=l->row_id[i];
        if(kind>3 || (kind==0 && id>=TS_TRACKER_LANES) || (kind==2 && id>=TS_MATRIX_ROWS) ||
           ((kind==1 || kind==3) && !id)){valid=0;break;}
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
           !layout_number(&value,3,&kind) || *value++!=',' ||
           !layout_number(&value,UINT64_MAX,&id) || *value)return -1;
        l->row_kind[i]=(unsigned char)kind;l->row_id[i]=id;++l->row_count;return 1;
    }
    return -1;
}
