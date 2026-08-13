#include "tapesister/pr13.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>

static float at_normalized(const TsSample *sample, double n)
{
    double p; size_t a,b; float f;
    if(n<=0.0)return sample->data[0];
    if(n>=1.0)return sample->data[sample->frames-1];
    p=n*(double)(sample->frames-1);a=(size_t)p;b=a+1<sample->frames?a+1:a;f=(float)(p-(double)a);
    return sample->data[a]+(sample->data[b]-sample->data[a])*f;
}

static void blend_stranger(TsBankSlot *candidate,const TsBankSlot *anchor,float mutation)
{
    float similarity=1.0f-mutation*0.95f;
    float peak=ts_sample_peak(&candidate->sample);
    float out_peak=0.0f;
    for(size_t i=0;i<candidate->sample.frames;++i){
        double n=candidate->sample.frames>1?(double)i/(double)(candidate->sample.frames-1):0.0;
        float x=candidate->sample.data[i]*(1.0f-similarity)+at_normalized(&anchor->sample,n)*similarity;
        candidate->sample.data[i]=x;if(fabsf(x)>out_peak)out_peak=fabsf(x);
    }
    if(peak>0.000001f&&out_peak>0.000001f){float g=peak/out_peak;for(size_t i=0;i<candidate->sample.frames;++i){float x=candidate->sample.data[i]*g;candidate->sample.data[i]=x>1.0f?1.0f:x<-1.0f?-1.0f:x;}}
}

static uint32_t next_seed(uint32_t x)
{
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x ? x : 0x54415045u;
}

int ts_pr13_generate_parent_in_slot(TsInstrument *instrument,int active_slot,int reseed,
                                    char *error,size_t error_size)
{
    TsInstrument temp;
    TsBankSlot *dst;
    TsGeneratorRecipe recipe;
    TsGeneratorKind kind;
    int locked;
    char clone_error[160];
    if(!instrument||active_slot<0||active_slot>=TS_BANK_SLOT_COUNT){snprintf(error,error_size,"Select a bank tile first");return 0;}
    dst=&instrument->bank[active_slot];
    locked=ts_pr13_slot_locked(instrument,active_slot);
    if(locked){snprintf(error,error_size,"Selected bank tile is locked");return 0;}
    recipe=dst->has_generator?dst->generator:instrument->generator;
    if(reseed){kind=recipe.kind;recipe.seed=next_seed(recipe.seed^0x9e3779b9u);}
    else{kind=(TsGeneratorKind)(((int)recipe.kind+1+(int)(next_seed(recipe.seed)&1u))%TS_GENERATOR_COUNT);recipe.seed=next_seed(recipe.seed^0x7f4a7c15u^instrument->family_sequence);}
    ts_instrument_init(&temp);
    temp.generator.seconds=recipe.seconds;
    temp.generator.frequency=recipe.frequency;
    if(!ts_instrument_generate(&temp,kind,recipe.seed,error,error_size)){ts_instrument_free(&temp);return 0;}
    ts_sample_free(&dst->sample);
    if(!ts_sample_clone(&dst->sample,&temp.bank[0].sample,clone_error,sizeof(clone_error))){ts_instrument_free(&temp);snprintf(error,error_size,"Could not replace selected tile");return 0;}
    dst->tuning=temp.bank[0].tuning; dst->audible_tuning=temp.bank[0].audible_tuning;
    dst->generator=temp.bank[0].generator; dst->has_generator=1;
    dst->capture_kind=TS_BANK_CAPTURE_ROOT; dst->relation=TS_FAMILY_ROOT;
    dst->parent_slot=-1; dst->lineage_seed=recipe.seed; dst->lineage_mutation=0.0f;
    dst->trajectory_step=0; dst->has_loop=0; dst->loop_first=dst->loop_last=0;
    dst->loop_crossfade_ms=8.0f; dst->loop_mode=TS_LOOP_FORWARD; dst->occupied=1;
    instrument->generator=dst->generator;
    instrument->family_anchor_slot=active_slot; instrument->family_last_slot=-1;
    ++instrument->family_sequence;
    ts_instrument_free(&temp);
    if(!ts_instrument_set_bank_as_current(instrument,active_slot,error,error_size))return 0;
    snprintf(error,error_size,reseed?"RESEEDED PARENT IN BANK %02d":"NEW PARENT IN BANK %02d",active_slot+1);
    return 1;
}

int ts_pr13_generate_family_candidate(TsInstrument *instrument, int active_slot,
                                      int reseed, int *created_slot,
                                      char *error, size_t error_size)
{
    int anchor=active_slot,old_last,old_path;
    float old_mutation,lineage_mutation;
    TsFamilyRelation old_relation;
    if(instrument==0)return 0;
    if(anchor<0||anchor>=TS_BANK_SLOT_COUNT||!instrument->bank[anchor].occupied)anchor=instrument->family_anchor_slot;
    old_last=instrument->family_last_slot;old_path=instrument->family_trajectory;
    old_mutation=instrument->family_mutation;old_relation=instrument->family_relation;
    if(reseed&&active_slot>0&&active_slot<TS_BANK_SLOT_COUNT&&instrument->bank[active_slot].occupied){
        TsBankSlot *selected=&instrument->bank[active_slot];
        if(selected->parent_slot>=0&&selected->parent_slot<TS_BANK_SLOT_COUNT)anchor=selected->parent_slot;
        instrument->family_relation=selected->relation;instrument->family_mutation=selected->lineage_mutation;
    }
    lineage_mutation=instrument->family_mutation;
    if(instrument->family_relation==TS_FAMILY_CHILD)instrument->family_mutation*=1.2941176f;
    else if(instrument->family_relation==TS_FAMILY_COUSIN)instrument->family_mutation*=0.9583333f;
    instrument->family_last_slot=-1;instrument->family_trajectory=0;
    {
        int ok=ts_instrument_generate_family_candidate(instrument,anchor,0,created_slot,error,error_size);
        if(ok&&created_slot!=0&&*created_slot>=0){
            TsBankSlot *candidate=&instrument->bank[*created_slot];
            TsBankSlot *parent=&instrument->bank[anchor];
            candidate->lineage_mutation=lineage_mutation;
            if(lineage_mutation<=0.0001f){
                char clone_error[80];
                ts_sample_clone(&candidate->sample,&parent->sample,clone_error,sizeof(clone_error));
                candidate->tuning=parent->tuning;candidate->audible_tuning=parent->audible_tuning;
                candidate->has_loop=parent->has_loop;candidate->loop_first=parent->loop_first;candidate->loop_last=parent->loop_last;
                candidate->loop_crossfade_ms=parent->loop_crossfade_ms;candidate->loop_mode=parent->loop_mode;
            }else if(candidate->relation==TS_FAMILY_STRANGER)blend_stranger(candidate,parent,lineage_mutation);
        }
        instrument->family_relation=old_relation;instrument->family_mutation=old_mutation;instrument->family_trajectory=old_path;
        if(!ok)instrument->family_last_slot=old_last;
        return ok;
    }
}
