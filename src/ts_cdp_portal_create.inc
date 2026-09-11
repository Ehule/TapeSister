/* CREATE uses a deliberately bounded palette, not arbitrary registry limits.
   All random choices become ordinary, persistable Portal parameter values. */
static double portal_roll_value(uint32_t *state,double low,double high)
{
    *state^=*state<<13;*state^=*state>>17;*state^=*state<<5;
    return low+(high-low)*(*state/4294967295.0);
}
int ts_portal_create_recipe(uint32_t seed,const TsSample *source,
                            TsPortalRecipe *out,char *error,size_t size)
{
    static const char *ids[]={"modify.speed.2","modify.radical.5","modify.radical.4",
        "modify.stack","newdelay.newdelay","extend.baktobak","extend.zigzag.1",
        "extend.freeze.2","spec.magnify","specfold.specfold.3","specfnu.specfnu.4","blur.blur"};
    if(!source || !source->sample_rate || !source->data || !out)
        return fail(error,size,"CREATE + CDP NEEDS A SOURCE");
    double duration=(double)source->frames/source->sample_rate;
    uint32_t state=seed?seed:0x43524541u;
    unsigned start=seed%12;
    for(unsigned attempt=0;attempt<12;++attempt) {
        unsigned pick=(start+attempt)%12;
        TsPortalRecipe r;const TsPortalProcess *p=ts_portal_process_find(ids[pick]);
        if(!p)continue;
        ts_portal_recipe_default(&r,p);
        if(source->channels==2 && !ts_portal_stereo_supported(&r))continue;
#define ROLL(lo,hi) portal_roll_value(&state,lo,hi)
        switch(pick) {
        case 0:r.values[0]=round(ROLL(-12,12));break;
        case 1:r.values[0]=ROLL(20,500);break;
        case 2:r.values[0]=round(ROLL(6,14));r.values[1]=round(ROLL(1,5));break;
        case 3:r.values[0]=round(ROLL(-5,12));r.values[1]=round(ROLL(2,4));r.values[2]=ROLL(.4,.8);r.values[4]=.2;break;
        case 4:r.values[0]=round(ROLL(36,72));r.values[1]=ROLL(.2,.45);r.values[2]=ROLL(.2,.55);break;
        case 5:r.values[0]=ROLL(.2,.6)*duration;break;
        case 6:r.values[0]=duration*.1;r.values[1]=duration*.8;r.values[2]=duration*1.5;
               r.values[3]=.05;r.values[5]=fmin(.4,duration*.6);r.values[6]=1+state%32767;break;
        case 7:r.values[0]=round(ROLL(3,7));r.values[5]=duration*.15;r.values[6]=duration*.4;
               r.values[1]=fmin(.06,duration*.1);r.values[8]=1+state%32767;break;
        case 8:r.values[0]=ROLL(.1,.7)*duration;r.values[1]=ROLL(.5,3);break;
        case 9:r.values[2]=1+state%64;break;
        case 10:r.values[0]=ROLL(-.5,.5);break;
        case 11:r.values[0]=round(ROLL(2,12));break;
        }
#undef ROLL
        TsCdpCommand commands[TS_CDP_MAX_STAGES];size_t count;
        /* Split stereo recipes are validated against a mono channel here;
           the actual runner retains its independent-channel output checks. */
        TsSample check=*source;
        if(source->channels==2 && ts_portal_stereo_policy(&r)==TS_PORTAL_STEREO_SPLIT)
            check.channels=1;
        if(!ts_portal_recipe_validate(&r,error,size) ||
           !ts_portal_build_commands(&r,&check,commands,&count,error,size))continue;
        snprintf(r.name,sizeof(r.name),"ROLL %.34s",p->title);
        snprintf(r.description,sizeof(r.description),
            "CREATE seed %08X: fresh FM through %.34s. Save or pin these rolled settings to reuse the transformation.",seed,p->title);
        *out=r;return 1;
    }
    return fail(error,size,"SOURCE IS TOO SHORT OR LONG FOR THE CREATE + CDP PALETTE");
}
