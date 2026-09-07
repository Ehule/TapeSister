/* Factory instruments keep their existing command mappings. This metadata
   view is built once from that registry, then shared read-only by UI/workers. */
static TsPortalProcess portal_factories[TS_CDP_FACTORY_RECIPE_COUNT];
static char portal_factory_ids[TS_CDP_FACTORY_RECIPE_COUNT][64];
static char portal_factory_descriptions[TS_CDP_FACTORY_RECIPE_COUNT][320];
static atomic_flag portal_factory_lock=ATOMIC_FLAG_INIT;
static atomic_int portal_factory_ready;
static unsigned portal_factory_seed_max(const TsCdpRecipe *f)
{return !strcmp(f->id,"stutter") || !strcmp(f->id,"motor") || !strcmp(f->id,"wave_scramble")?256u:32767u;}
static uint64_t portal_factory_effective_seed(const TsCdpRecipe *f,uint64_t seed)
{return seed?((seed-1)%portal_factory_seed_max(f))+1:1;}
const TsCdpRecipe *ts_portal_factory_find(const char *id)
{return id && !strncmp(id,"factory.",8)?ts_cdp_recipe_find(id+8):NULL;}
const TsPortalProcess *ts_portal_factory_process_at(size_t index)
{
    if(index>=ts_cdp_factory_recipe_count())return NULL;
    if(!atomic_load_explicit(&portal_factory_ready,memory_order_acquire)) {
        while(atomic_flag_test_and_set_explicit(&portal_factory_lock,memory_order_acquire)){}
        if(!atomic_load_explicit(&portal_factory_ready,memory_order_relaxed)) {
            for(size_t i=0;i<ts_cdp_factory_recipe_count();++i) {
                const TsCdpRecipe *f=ts_cdp_factory_recipe_at(i);TsPortalProcess *p=&portal_factories[i];
                snprintf(portal_factory_ids[i],64,"factory.%s",f->id);
                snprintf(portal_factory_descriptions[i],320,"%s. %s",f->description,
                    !strcmp(f->id,"brassage")?"The factory renderer folds its stereo cloud to mono. Spread still affects the grain texture.":
                    f->seed_supported?"Curated factory controls. Seed repeats the random choices; adjust it for a new take.":
                    (!f->deterministic || !strcmp(f->id,"granulate"))?"Curated factory controls. Each render can create a different take, even with unchanged settings.":
                    "Curated factory controls. Save a personal pin or use this instrument as a chain stage.");
                p->id=portal_factory_ids[i];p->title=f->display_name;p->description=portal_factory_descriptions[i];
                p->command="factory";p->executable=f->stages[0].executable;p->family=TS_PORTAL_FACTORY;
                p->version=f->recipe_version;p->changes_duration=f->duration_may_change;p->parameter_count=f->control_count;
                for(unsigned n=0;n<f->control_count;++n) {
                    const TsCdpControlSpec *c=&f->controls[n];
                    p->parameters[n]=(TsPortalParam){c->id,c->label,c->unit,"",
                        c->type==TS_CDP_CONTROL_ENUMERATED?TS_PORTAL_ENUMERATED:
                        c->type==TS_CDP_CONTROL_STEPPED?TS_PORTAL_INTEGER:TS_PORTAL_REAL,
                        c->minimum,c->maximum,c->default_value};
                }
                if(f->mix_policy==TS_CDP_MIX_EXACT_FRAMES)
                    p->parameters[p->parameter_count++]=(TsPortalParam){"mix","DRY / WET","MIX REQUIRES MATCHING DRY AND WET LENGTHS","",TS_PORTAL_REAL,0,1,1};
                if(!strcmp(f->id,"filter_bank"))
                    p->parameters[p->parameter_count++]=(TsPortalParam){"tuning","ROOT HZ","ROOT FREQUENCY FOR THE HARMONIC FILTER BANK","",TS_PORTAL_REAL,20,20000,261.625565};
                if(f->seed_supported)
                    p->parameters[p->parameter_count++]=(TsPortalParam){"seed","SEED","SAME SEED REPEATS THE NATIVE RANDOM CHOICES","",TS_PORTAL_INTEGER,1,portal_factory_seed_max(f),1};
            }
            atomic_store_explicit(&portal_factory_ready,1,memory_order_release);
        }
        atomic_flag_clear_explicit(&portal_factory_lock,memory_order_release);
    }
    return &portal_factories[index];
}
void ts_portal_factory_recipe(const TsCdpRecipe *f,const TsCdpRecipeValues *v,TsPortalRecipe *r)
{
    memset(r,0,sizeof(*r));if(!f || !v)return;
    int index=ts_cdp_recipe_index_for_id(f->id);if(index<0)return;
    const TsPortalProcess *p=ts_portal_factory_process_at((size_t)index);
    snprintf(r->process_id,sizeof(r->process_id),"%s",p->id);snprintf(r->name,sizeof(r->name),"%s",p->title);
    r->version=p->version;r->exposed=(1u<<p->parameter_count)-1;
    for(unsigned n=0;n<f->control_count;++n)r->values[n]=v->controls[n];
    for(unsigned n=f->control_count;n<p->parameter_count;++n) {
        const char *id=p->parameters[n].id;
        r->values[n]=!strcmp(id,"mix")?v->mix:!strcmp(id,"tuning")?v->tuning_hz:(double)portal_factory_effective_seed(f,v->seed);
    }
    /* Two exact 32-bit halves retain legacy 64-bit seeds without introducing
       rounding through doubles. Slot 13 preserves the legacy tuning context.
       These are private recipe data, never additional user-facing controls. */
    r->values[13]=v->tuning_hz;r->values[14]=(uint32_t)v->seed;r->values[15]=(uint32_t)(v->seed>>32);
}
void ts_portal_factory_values(const TsPortalRecipe *r,TsCdpRecipeValues *v)
{
    const TsCdpRecipe *f=ts_portal_factory_find(r->process_id);
    memset(v,0,sizeof(*v));if(!f)return;
    const TsPortalProcess *p=ts_portal_factory_process_at((size_t)ts_cdp_recipe_index_for_id(f->id));
    ts_cdp_recipe_values_default(f,v);
    for(unsigned n=0;n<f->control_count;++n)v->controls[n]=(float)r->values[n];
    v->tuning_hz=(float)r->values[13];v->seed=(uint64_t)(uint32_t)r->values[14]|((uint64_t)(uint32_t)r->values[15]<<32);
    for(unsigned n=f->control_count;n<p->parameter_count;++n) {
        const char *id=p->parameters[n].id;
        if(!strcmp(id,"mix"))v->mix=(float)r->values[n];
        else if(!strcmp(id,"tuning"))v->tuning_hz=(float)r->values[n];
        else if((uint64_t)r->values[n]!=portal_factory_effective_seed(f,v->seed))v->seed=(uint64_t)r->values[n];
    }
}
/* UI controls use the factory's quantization rather than inventing a new map. */
double ts_portal_parameter_quantize(const TsPortalProcess *p,unsigned n,double value)
{
    const TsCdpRecipe *f=ts_portal_factory_find(p->id);
    if(f && n<f->control_count)return ts_cdp_control_quantize(&f->controls[n],(float)value);
    const TsPortalParam *s=&p->parameters[n];
    if(s->type!=TS_PORTAL_REAL)value=floor(value+.5);
    if(s->type==TS_PORTAL_ODD_INTEGER)value=2*floor(value/2)+1;
    return fmax(s->minimum,fmin(s->maximum,value));
}
double ts_portal_parameter_nudge(const TsPortalProcess *p,unsigned n,double value,int direction)
{
    const TsCdpRecipe *f=ts_portal_factory_find(p->id);
    if(f && n<f->control_count) {
        const TsCdpControlSpec *c=&f->controls[n];
        if(c->type==TS_CDP_CONTROL_ENUMERATED) {
            size_t at=0;while(at+1<c->valid_value_count && c->valid_values[at]<value)++at;
            int next=(int)at+direction;if(next<0)next=0;if(next>=(int)c->valid_value_count)next=(int)c->valid_value_count-1;
            return c->valid_values[next];
        }
        return ts_portal_parameter_quantize(p,n,value+direction*(c->step>0?c->step:.001));
    }
    const TsPortalParam *s=&p->parameters[n];
    return ts_portal_parameter_quantize(p,n,value+direction*(s->type==TS_PORTAL_REAL?.001:s->type==TS_PORTAL_ODD_INTEGER?2:1));
}
void ts_portal_parameter_format(const TsPortalProcess *p,unsigned n,double value,char *text,size_t size)
{
    const TsCdpRecipe *f=ts_portal_factory_find(p->id);
    if(f && n<f->control_count) {
        const TsCdpControlSpec *c=&f->controls[n];
        if(c->type==TS_CDP_CONTROL_ENUMERATED && c->value_names) {
            for(size_t i=0;i<c->valid_value_count;++i)if(value==c->valid_values[i]) {snprintf(text,size,"%s",c->value_names[i]);return;}
        }
    }
    snprintf(text,size,p->parameters[n].type==TS_PORTAL_SWITCH?(value?"ON":"OFF"):"%.4g",value);
}
