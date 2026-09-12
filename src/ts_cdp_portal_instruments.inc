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

/* Original TapeSister instruments composed from named CDP processes. These are
   editable recipes, not claims of new upstream CDP algorithms. */
static int portal_original_instrument(size_t index,TsPortalRecipe *r)
{
    static const char *names[]={"TAPE COMET","WIRE CHOIR","SYLLABLE RAIN","FORMANT LANTERN"};
    static const char *descriptions[]={
        "Tape acceleration sends a gesture into a tuned feedback tail. FLIGHT bends its path; RING PITCH and MEMORY shape the resonator. Mono or stereo.",
        "Transposed layers form a small choir; a low-pass softens their edges. INTERVAL and VOICES shape the harmony; COLOUR opens the top. Mono or stereo.",
        "Envelope segments repeat with shrinking tails, then excite a tuned delay. Try speech, singing or pulsed sounds. WINDOW sets the scale. Mono or stereo.",
        "Hold one spectrum, then rotate its formant envelope into a moving lantern of tone. MOMENT chooses the source colour; ORBIT animates it. Mono only."};
    static const char *ids[][3]={
        {"modify.speed.5","newdelay.newdelay","modify.loudness.1"},
        {"modify.stack","filter.lohi.1","modify.loudness.1"},
        {"envspeak.envspeak.5","newdelay.newdelay","modify.loudness.1"},
        {"spec.magnify","specfnu.specfnu.4","modify.loudness.1"}};
    if(index>=4 || !r)return 0;
    memset(r,0,sizeof(*r));snprintf(r->process_id,sizeof(r->process_id),"@chain");r->version=1;r->stage_count=3;
    snprintf(r->name,sizeof(r->name),"%s",names[index]);snprintf(r->description,sizeof(r->description),"%s",descriptions[index]);
    for(unsigned stage=0;stage<3;++stage) {
        TsPortalRecipe step;ts_portal_recipe_default(&step,ts_portal_process_find(ids[index][stage]));step.exposed=0;
#define MACRO(i,name,lo,hi,value) do {step.values[i]=value;step.exposed|=1u<<(i);if(!ts_portal_macro_set(&step,i,name,lo,hi,NULL,0))return 0;}while(0)
        if(stage==2) {MACRO(0,"OUTPUT",.1,1,.65);}
        else if(stage==0 && index==0) {MACRO(0,"FLIGHT",1.1,2.5,1.8);MACRO(1,"RISE TIME",.2,.8,.4);step.values[2]=0;}
        else if(stage==0 && index==1) {MACRO(0,"INTERVAL",3,12,7);MACRO(1,"VOICES",2,4,3);step.values[4]=.3;}
        else if(stage==0 && index==2) {MACRO(0,"WINDOW",20,80,30);MACRO(2,"DROPS",2,5,3);step.values[1]=5;}
        else if(stage==0 && index==3) {MACRO(0,"MOMENT",.02,.5,.08);MACRO(1,"GLOW TIME",.5,6,2);}
        else if(index==0 || index==2) {MACRO(0,"RING PITCH",36,72,index==0?48:55);MACRO(2,"MEMORY",.1,.75,.55);step.values[1]=.5;}
        else if(index==1) {MACRO(0,"REJECTION",-50,-20,-36);MACRO(2,"COLOUR",2500,8000,4000);}
        else {MACRO(0,"ORBIT",-1,1,.15);step.values[1]=.5;}
        ts_portal_step_set(&r->stages[stage],&step);
#undef MACRO
    }
    return 1;
}
/* Fixed, centered voicings use only ordinary saved chain data. The two stack
   intervals and compensating transpose deliberately stay out of Macros: a
   one-parameter macro cannot change all three while preserving the root. */
static int portal_supersaw_instrument(size_t index,TsPortalRecipe *r)
{
    static const char *names[]={"SUPERSAW TIGHT","SUPERSAW","WITCH SAW"};
    static const char *descriptions[]={
        "Nine close unison layers, centered on the source pitch. Try a sustained saw or FM tile. COLOUR and OUTPUT shape the result; Full edits the recipe.",
        "Nine detuned unison layers, centered on the source pitch. Try a sustained saw or FM tile. COLOUR and OUTPUT shape the result; Full edits the recipe.",
        "Nine widely detuned layers with a darker low-pass. Centered on the source pitch. Try saws or FM; use Mosaic pan and fades to build a larger ensemble."};
    static const double spread[]={.5,1,1.6};
    static const double colour[]={5000,3500,900};
    static const char *ids[]={"modify.stack","modify.stack","modify.speed.2",
                              "filter.lohi.1","modify.loudness.4"};
    if(index>=3 || !r)return 0;
    memset(r,0,sizeof(*r));snprintf(r->process_id,sizeof(r->process_id),"@chain");
    r->version=1;r->stage_count=5;
    snprintf(r->name,sizeof(r->name),"%s",names[index]);
    snprintf(r->description,sizeof(r->description),"%s",descriptions[index]);
    for(unsigned stage=0;stage<r->stage_count;++stage) {
        TsPortalRecipe step;ts_portal_recipe_default(&step,ts_portal_process_find(ids[stage]));
        step.exposed=0;
        if(stage<2) {
            step.values[0]=(stage==0?.07:.19)*spread[index];
            step.values[1]=3;step.values[2]=1;step.values[3]=0;
            step.values[4]=1;step.values[5]=1;
        } else if(stage==2) {
            /* Three layers start at 0, interval, 2*interval. Two stacks have
               midpoint interval A + interval B; subtract it in semitones. */
            step.values[0]=-.26*spread[index];
        } else if(stage==3) {
            step.values[0]=-40;step.values[1]=colour[index];step.values[2]=10000;
            step.values[3]=.01;step.values[4]=.5;
            step.exposed=1u<<1;
            if(!ts_portal_macro_set(&step,1,"COLOUR",400,6000,NULL,0))return 0;
        } else {
            step.values[0]=.7;step.exposed=1;
            if(!ts_portal_macro_set(&step,0,"OUTPUT",.1,.85,NULL,0))return 0;
        }
        ts_portal_step_set(&r->stages[stage],&step);
    }
    return 1;
}
size_t ts_portal_instrument_count(void){return 11;}
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
    if(index>=8)return portal_supersaw_instrument(index-8,r);
    if(index>=4)return portal_original_instrument(index-4,r);
    if(!r)return 0;
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
