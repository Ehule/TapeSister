/* Versioned optional UI state lives in the staged, validated Sister sidecar.
   Older projects omit it. No device preference or running transport is saved. */
static int project_view_valid(const TsProjectView *v)
{
    if(!v->present)return 1;
    if(v->present!=1 || v->workspace<TS_PROJECT_VIEW_CANVAS || v->workspace>TS_PROJECT_VIEW_TRACKER ||
       (unsigned)v->keyboard>1 || (unsigned)v->ingredients>1 || (unsigned)v->recipes>1 ||
       v->parent_first>v->parent_last || !isfinite(v->scroll) || v->scroll<0 || v->scroll>1000000 ||
       !isfinite(v->xscroll) || v->xscroll<0 || v->xscroll>TS_MOSAIC_LAYOUT_LIMIT ||
       !isfinite(v->scale) || v->scale<.000001 || v->scale>1200 ||
       !isfinite(v->hscale) || v->hscale<TS_MOSAIC_MIN_HSCALE || v->hscale>4 ||
       !isfinite(v->time) || v->time<0 || v->time>1000000 ||
       (unsigned)v->follow>1 || (unsigned)v->pattern_tab>1 || v->source_page<0 || v->source_page>TS_MOSAIC_EVENTS ||
       v->selection_count>TS_MOSAIC_EVENTS || v->tracker_scroll<0 || v->tracker_scroll>65535 ||
       (unsigned)v->tracker_expanded>1 || v->tracker_field<0 || v->tracker_field>64)return 0;
    int primary=v->selected==0;
    for(unsigned i=0;i<v->selection_count;++i) {
        if(!v->selection[i])return 0;
        if(v->selection[i]==v->selected)primary=1;
        for(unsigned j=0;j<i;++j)if(v->selection[i]==v->selection[j])return 0;
    }
    return primary;
}
static int project_view_write(FILE *f,const TsProjectView *v)
{
    if(!v->present)return 1;
    if(!project_view_valid(v) || fprintf(f,
        "ProjectView.Version=1\nProjectView.Workspace=%d %d %d %d\n"
        "ProjectView.Canvas=%zu %zu\n"
        "ProjectView.Mosaic=%.17g %.17g %.17g %.17g %.17g %d %d %d %" PRIu64 "\n"
        "ProjectView.Tracker=%d %d %d\n",
        v->workspace,v->keyboard,v->ingredients,v->recipes,v->parent_first,v->parent_last,
        v->scroll,v->xscroll,v->scale,v->hscale,v->time,v->follow,v->pattern_tab,v->source_page,v->selected,
        v->tracker_scroll,v->tracker_expanded,v->tracker_field)<0)return 0;
    for(unsigned i=0;i<v->selection_count;++i)
        if(fprintf(f,"ProjectView.Card=%" PRIu64 "\n",v->selection[i])<0)return 0;
    return 1;
}
static int project_view_read(TsProjectView *v,unsigned *fields,const char *key,const char *value)
{
    unsigned bit=0;int n=0,ok=0;
    if(!strcmp(key,"ProjectView.Version")) {
        int version=0;bit=1;ok=sscanf(value,"%d %n",&version,&n)==1 && version==1;v->present=1;
    } else if(!strcmp(key,"ProjectView.Workspace")) {
        bit=2;ok=sscanf(value,"%d %d %d %d %n",&v->workspace,&v->keyboard,&v->ingredients,&v->recipes,&n)==4;
    } else if(!strcmp(key,"ProjectView.Canvas")) {
        bit=4;ok=sscanf(value,"%zu %zu %n",&v->parent_first,&v->parent_last,&n)==2;
    } else if(!strcmp(key,"ProjectView.Mosaic")) {
        bit=8;ok=sscanf(value,"%lf %lf %lf %lf %lf %d %d %d %" SCNu64 " %n",
            &v->scroll,&v->xscroll,&v->scale,&v->hscale,&v->time,&v->follow,&v->pattern_tab,&v->source_page,&v->selected,&n)==9;
    } else if(!strcmp(key,"ProjectView.Tracker")) {
        bit=16;ok=sscanf(value,"%d %d %d %n",&v->tracker_scroll,&v->tracker_expanded,&v->tracker_field,&n)==3;
    } else if(!strcmp(key,"ProjectView.Card") && v->selection_count<TS_MOSAIC_EVENTS) {
        *fields|=32;ok=sscanf(value,"%" SCNu64 " %n",&v->selection[v->selection_count++],&n)==1;
    }
    if(!ok || !n || value[n] || (bit && (*fields&bit)))return 0;
    *fields|=bit;return 1;
}
