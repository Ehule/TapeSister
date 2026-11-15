/* Runs independently of audio graph order. Native processors already slew
   their parameters at audio rate; this layer only publishes control targets. */
static double modulation_phase(const TsMatrixModulation *m,unsigned lane)
{
    double p=m->state.phase[lane]+((m->running&(1u<<lane))?m->step[lane]*m->elapsed:0);
    return m->authored.modulation.lane[lane].mode==TS_MOD_LOOP?p-floor(p):fmin(1,p);
}
void ts_matrix_modulation_begin(TsMatrixEffects *f)
{
    if(!f || f->mod_remaining || (!f->mod_mask && !f->mod_targets))return;
    unsigned previously=0,touched=0;
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(f->current[i] && f->current[i]->mod && f->current[i]->mod->owned) {
        previously|=1u<<i;f->current[i]->mod->modulated=f->current[i]->mod->authored;f->current[i]->mod->owned=0;
    }
    if(!f->mod_mask && !previously){f->mod_targets=0;return;}
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(f->mod_mask&(1u<<i)) {
        TsMatrixEffectNode *source=f->current[i];if(!source)continue;
        const TsModControls *c=&source->mod->authored.modulation;
        /* Integer elapsed frames make scalar and arbitrary block partitioning
           reach exactly the same phase at every control tick. */
        for(unsigned lane=0;lane<c->count;++lane)source->mod->state.phase[lane]=modulation_phase(source->mod,lane);
        source->mod->elapsed=0;
        source->mod->running=0;
        if(source->mod->state.retrigger!=c->retrigger) {
            memset(source->mod->state.phase,0,sizeof(source->mod->state.phase));source->mod->state.retrigger=c->retrigger;
        }
        if(!source->mod->authored.enabled)continue;
        float values[TS_MOD_LANES];unsigned evaluated=0;
        for(unsigned m=0;m<c->map_count;++m) {
            const TsModMap *map=&c->map[m];
            if(!map->enabled || !c->lane[map->lane].enabled)continue;
            for(int j=0;j<TS_MATRIX_EFFECT_CAPACITY;++j) {
                TsMatrixEffectNode *dest=f->current[j];
                if(!dest || !dest->mod || dest->id!=map->target || (f->suspended&(1u<<j)) || !dest->mod->authored.enabled ||
                   ts_mod_target_type(&dest->mod->authored,map->child)!=map->type ||
                   !ts_mod_field_available(&dest->mod->authored,map->child,map->field))continue;
                if(map->child) {
                    int enabled=0;
                    for(int k=0;k<dest->mod->authored.board.count;++k)if(dest->mod->authored.board.slot[k].id==map->child)
                        enabled=dest->mod->authored.board.slot[k].enabled;
                    if(!enabled)continue;
                }
                if(!(evaluated&(1u<<map->lane))) {
                    values[map->lane]=ts_mod_value(&c->lane[map->lane],source->mod->state.phase[map->lane]);evaluated|=1u<<map->lane;
                }
                if(!((previously|touched)&(1u<<j)))dest->mod->modulated=dest->mod->authored;
                float value=map->low+(map->high-map->low)*values[map->lane];
                if(ts_mod_apply(&dest->mod->modulated,map,value)){touched|=1u<<j;dest->mod->owned=1;source->mod->running|=1u<<map->lane;}
                break;
            }
        }
        double quarter=isfinite(f->mod_quarter) && f->mod_quarter>0?f->mod_quarter:.5;
        for(unsigned lane=0;lane<c->count;++lane) {
            const TsModLane *l=&c->lane[lane];
            source->mod->step[lane]=1/((l->timing==TS_MOD_TEMPO?l->beats*quarter:l->seconds)*f->rate);
        }
    }
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if((previously|touched)&(1u<<i)) {
        TsMatrixEffectNode *n=f->current[i];const TsMatrixEffectControls *c=&n->mod->modulated;
        if(n->board) {
            ts_pedalboard_modulate(n->board,&c->board);
            n->mix_target=c->mix;n->gain_target=powf(10,c->gain_db*.05f);
        } else node_set(n,c,0);
    }
    f->mod_targets=touched;
    f->mod_remaining=f->mod_mask?(f->rate/150?f->rate/150:1):0;
}
void ts_matrix_modulation_end(TsMatrixEffects *f,unsigned frames)
{
    if(!f || !f->mod_mask)return;
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(f->mod_mask&(1u<<i)) {
        TsMatrixEffectNode *n=f->current[i];if(!n)continue;
        n->mod->elapsed+=frames;
    }
    f->mod_remaining=frames>=f->mod_remaining?0:f->mod_remaining-frames;
}
double ts_matrix_modulation_phase(const TsMatrixEffects *f,uint32_t id,unsigned lane)
{
    if(!f || lane>=TS_MOD_LANES)return 0;
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(f->current[i] && f->current[i]->id==id && f->current[i]->type==TS_MATRIX_FX_MODULATOR)return modulation_phase(f->current[i]->mod,lane);
    return 0;
}
