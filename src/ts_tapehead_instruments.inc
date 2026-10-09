/* Portable instrument metadata lives in the host score, not in tile PCM. */
#include "ft2_inst_ed.h"
static int instrument_has_tile(const TsSisterTracker *t,int a,TsTileId id) {
    if(t->instruments[a].present) {
        for(int k=0;k<t->instruments[a].sample_count;++k)
            if(t->instruments[a].tiles[k]==id)return 1;
        return 0;
    }
    return t->aliases[a]==id;
}
#define INSTRUMENT_FIELDS(X) \
    X(vol_length,volEnvLength) X(pan_length,panEnvLength) \
    X(vol_sustain,volEnvSustain) X(vol_start,volEnvLoopStart) X(vol_end,volEnvLoopEnd) \
    X(pan_sustain,panEnvSustain) X(pan_start,panEnvLoopStart) X(pan_end,panEnvLoopEnd) \
    X(vol_flags,volEnvFlags) X(pan_flags,panEnvFlags) X(fadeout,fadeout) \
    X(vib_type,autoVibType) X(vib_sweep,autoVibSweep) X(vib_depth,autoVibDepth) X(vib_rate,autoVibRate)
static void instrument_metadata(TsTrackerInstrument *v,const instr_t *ins) {
    v->present=1;v->sample_count=ins->numSamples;
    memcpy(v->note_map,ins->note2SampleLUT,96);
    for(int k=0;k<12;++k)for(int j=0;j<2;++j) {
        v->vol_points[k][j]=ins->volEnvPoints[k][j];v->pan_points[k][j]=ins->panEnvPoints[k][j];
    }
#define COPY_FIELD(host,ft) v->host=ins->ft;
    INSTRUMENT_FIELDS(COPY_FIELD)
#undef COPY_FIELD
    for(int k=0;k<16;++k){v->volume[k]=ins->smp[k].volume;v->panning[k]=ins->smp[k].panning;v->relative_note[k]=ins->smp[k].relativeNote;v->finetune[k]=ins->smp[k].finetune;
        snprintf(v->sample_names[k],sizeof(v->sample_names[k]),"%.22s",ins->smp[k].name);}
}
static int restore_instruments(const TsSisterTracker *t) {
    instr_t *prepared[129]={0};
    for(int a=1;a<=128;++a)if(t->instruments[a].present) {
        prepared[a]=calloc(1,sizeof(*prepared[a]));
        if(!prepared[a]){for(int j=1;j<a;++j)free(prepared[j]);return 0;}
    }
    /* Voices are stopped by score_import. Clear all former sample maps and
       envelopes when switching projects, including now-unbound instruments. */
    stopVoices();stopAllScopes();
    for(int a=1;a<=128;++a) {
        freeInstr(a);memset(embed.tile_stamp[a],0,sizeof(embed.tile_stamp[a]));
        for(int k=0;k<16;++k){free(embed.tile_data[a][k]);embed.tile_data[a][k]=NULL;}
        song.instrName[a][0]=0;
        const TsTrackerInstrument *v=&t->instruments[a];
        if(!v->present)continue;
        instr_t *ins=instr[a]=prepared[a];ins->numSamples=v->sample_count;
        memcpy(ins->note2SampleLUT,v->note_map,96);
        for(int k=0;k<12;++k)for(int j=0;j<2;++j) {
            ins->volEnvPoints[k][j]=v->vol_points[k][j];ins->panEnvPoints[k][j]=v->pan_points[k][j];
        }
#define COPY_FIELD(host,ft) ins->ft=v->host;
        INSTRUMENT_FIELDS(COPY_FIELD)
#undef COPY_FIELD
        snprintf(song.instrName[a],sizeof(song.instrName[a]),"%s",v->name);
    }
    collect_tiles();
    return 1;
}
