/* Names/ranges are presentation metadata: cache equality deliberately remains
   based on process, bypass and rendered values. */
void ts_portal_control_bounds(const TsPortalRecipe *r,unsigned i,int macros,double *lo,double *hi)
{
    const TsPortalProcess *p=ts_portal_process_find(r->process_id);
    *lo=p->parameters[i].minimum;*hi=p->parameters[i].maximum;
    if(macros && r->macros[i].configured){*lo=r->macros[i].minimum;*hi=r->macros[i].maximum;}
}
int ts_portal_macro_set(TsPortalRecipe *r,unsigned i,const char *name,double lo,double hi,char *error,size_t size)
{
    const TsPortalProcess *p=ts_portal_process_find(r->process_id);
    if(!p || i>=p->parameter_count || !name || strlen(name)>=sizeof(r->macros[i].name))return fail(error,size,"INVALID MACRO");
    if(!isfinite(lo) || !isfinite(hi) || lo<p->parameters[i].minimum || hi>p->parameters[i].maximum)return fail(error,size,"MACRO RANGE OUTSIDE CDP LIMITS");
    if(ts_portal_factory_find(p->id)) {lo=ts_portal_parameter_quantize(p,i,lo);hi=ts_portal_parameter_quantize(p,i,hi);}
    TsPortalRecipe next=*r;TsPortalMacro *m=&next.macros[i];
    memset(m,0,sizeof(*m));m->configured=1;m->minimum=lo;m->maximum=hi;snprintf(m->name,sizeof(m->name),"%s",name);
    if(!ts_portal_recipe_validate(&next,error,size))return 0;
    *r=next;return 1;
}
int ts_portal_control_at(const TsPortalUi *ui,int row,TsPortalControl *out)
{
    if(row<0)return 0;
    unsigned stages=ui->chain_active && ui->macro_view?ui->chain.stage_count:1;
    for(unsigned n=0;n<stages;++n) {
        int stage=ui->chain_active?(ui->macro_view?(int)n:ui->chain_stage):-1;
        int selected=stage<0 || stage==ui->chain_stage;
        const char *id=selected?ui->recipe.process_id:ui->chain.stages[stage].process_id;
        const TsPortalProcess *p=ts_portal_process_find(id);if(!p)continue;
        unsigned exposed=selected?ui->recipe.exposed:ui->chain.stages[stage].exposed;
        for(unsigned i=0;i<p->parameter_count;++i) {
            if(ui->macro_view && !(exposed&(1u<<i)))continue;
            if(row--!=0)continue;
            out->stage=stage;out->index=(int)i;out->process=p;out->exposed=(exposed&(1u<<i))!=0;
            out->value=selected?ui->recipe.values[i]:ui->chain.stages[stage].values[i];
            out->macro=selected?&ui->recipe.macros[i]:&ui->chain.stages[stage].macros[i];return 1;
        }
    }
    return 0;
}
int ts_portal_control_count(const TsPortalUi *ui)
{TsPortalControl c;int n=0;while(ts_portal_control_at(ui,n,&c))++n;return n;}

size_t ts_portal_instrument_count(void){return 4;}
int ts_portal_instrument_recipe(size_t index,TsPortalRecipe *r)
{
    static const char *names[]={"DUST HALO","DARK GLASS","BROKEN SIGNAL","SLOW BLOOM"};
    static const char *descriptions[]={
        "Grain Scramble scatters fragments; Spectral Blur softens their edges; Gain sets the output. Start with at least one second of mono audio.",
        "Tape Transpose lowers pitch and lengthens the sound; Spectral Blur smooths attacks; Gain sets output. Try voices or resonant hits.",
        "Bit Reduction roughens detail; Ring Modulate adds moving sidebands; Gain sets output. Try a steady tone or a drum loop.",
        "Granular Time slows the source without tape transposition; Spectral Blur softens grain edges; Gain sets output. Try a phrase or field recording."};
    static const char *ids[][3]={
        {"modify.brassage.4","blur.blur","modify.loudness.1"},
        {"modify.speed.2","blur.blur","modify.loudness.1"},
        {"modify.radical.4","modify.radical.5","modify.loudness.1"},
        {"modify.brassage.2","blur.blur","modify.loudness.1"}};
    if(index>=4 || !r)return 0;
    memset(r,0,sizeof(*r));snprintf(r->process_id,sizeof(r->process_id),"@chain");r->version=1;r->stage_count=3;
    snprintf(r->name,sizeof(r->name),"%s",names[index]);snprintf(r->description,sizeof(r->description),"%s",descriptions[index]);
    for(int stage=0;stage<3;++stage) {
        const TsPortalProcess *p=ts_portal_process_find(ids[index][stage]);if(!p)return 0;
        TsPortalRecipe step;ts_portal_recipe_default(&step,p);step.exposed=1;
        const char *name=stage==2?"OUTPUT":stage==1?(index==2?"METAL":"HAZE"):
                         index==0?"FRAGMENTS":index==1?"PITCH":index==2?"RESOLUTION":"TIME SPEED";
        double lo=stage==2?.1:stage==1?(index==2?5:1):index==0?25:index==1?-24:index==2?4:.25;
        double hi=stage==2?1:stage==1?(index==2?600:32):index==0?120:index==1?0:index==2?16:1;
        step.values[0]=stage==2?.7:stage==1?(index==2?65:8):index==0?50:index==1?-12:index==2?8:.5;
        if(!ts_portal_macro_set(&step,0,name,lo,hi,NULL,0))return 0;
        if(index==0 && stage==0) {
            step.values[1]=120;step.exposed|=2;
            if(!ts_portal_macro_set(&step,1,"MEMORY",0,500,NULL,0))return 0;
        }
        ts_portal_step_set(&r->stages[stage],&step);
    }
    return 1;
}
