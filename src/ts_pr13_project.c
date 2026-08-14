#include "tapesister/pr13.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint16_t ts_pr13_lock_mask(const TsInstrument *instrument);
void ts_pr13_restore_lock_mask(TsInstrument *instrument, uint16_t mask);

#define P13_PROCESS_WORDS 24
#define P13_ENV_TRAILER_SIZE (4 + 2 + TS_BANK_SLOT_COUNT * (1 + P13_PROCESS_WORDS) * 4 + 4)

static int put_u32(FILE *file, uint32_t value)
{
    unsigned char bytes[4] = {
        (unsigned char)(value & 255u),
        (unsigned char)((value >> 8) & 255u),
        (unsigned char)((value >> 16) & 255u),
        (unsigned char)((value >> 24) & 255u)
    };
    return fwrite(bytes, 1, 4, file) == 4;
}

static int get_u32(FILE *file, uint32_t *value)
{
    unsigned char bytes[4];
    if (fread(bytes, 1, 4, file) != 4) return 0;
    *value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
             ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    return 1;
}

static int put_float13(FILE *file, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return put_u32(file, bits);
}

static int get_float13(FILE *file, float *value)
{
    uint32_t bits;
    if (!get_u32(file, &bits)) return 0;
    memcpy(value, &bits, sizeof(bits));
    return 1;
}

static int write_process(FILE *file, const TsProcessRecipe *p)
{
    return put_u32(file,p->seed) && put_float13(file,p->body) &&
        put_float13(file,p->edge) && put_float13(file,p->drift) &&
        put_u32(file,(uint32_t)p->noise_enabled) && put_float13(file,p->noise_amount) &&
        put_u32(file,(uint32_t)p->noise_color) && put_u32(file,(uint32_t)p->delay_enabled) &&
        put_float13(file,p->delay_seconds) && put_float13(file,p->delay_feedback) &&
        put_float13(file,p->delay_damping) && put_float13(file,p->delay_mix) &&
        put_u32(file,(uint32_t)p->reverb_enabled) && put_float13(file,p->reverb_decay) &&
        put_float13(file,p->reverb_damping) && put_float13(file,p->reverb_mix) &&
        put_u32(file,(uint32_t)p->filter_enabled) && put_u32(file,(uint32_t)p->filter_mode) &&
        put_float13(file,p->filter_cutoff_hz) && put_float13(file,p->filter_resonance) &&
        put_u32(file,(uint32_t)p->shaper_enabled) && put_u32(file,(uint32_t)p->shaper_mode) &&
        put_float13(file,p->shaper_drive) && put_float13(file,p->shaper_mix);
}

static int read_process(FILE *file, TsProcessRecipe *p)
{
    uint32_t value;
    if(!get_u32(file,&p->seed)||!get_float13(file,&p->body)||!get_float13(file,&p->edge)||
       !get_float13(file,&p->drift)||!get_u32(file,&value))return 0;p->noise_enabled=(int)value;
    if(!get_float13(file,&p->noise_amount)||!get_u32(file,&value))return 0;p->noise_color=(TsNoiseColor)value;
    if(!get_u32(file,&value))return 0;p->delay_enabled=(int)value;
    if(!get_float13(file,&p->delay_seconds)||!get_float13(file,&p->delay_feedback)||
       !get_float13(file,&p->delay_damping)||!get_float13(file,&p->delay_mix)||!get_u32(file,&value))return 0;p->reverb_enabled=(int)value;
    if(!get_float13(file,&p->reverb_decay)||!get_float13(file,&p->reverb_damping)||
       !get_float13(file,&p->reverb_mix)||!get_u32(file,&value))return 0;p->filter_enabled=(int)value;
    if(!get_u32(file,&value))return 0;p->filter_mode=(TsFilterMode)value;
    if(!get_float13(file,&p->filter_cutoff_hz)||!get_float13(file,&p->filter_resonance)||!get_u32(file,&value))return 0;p->shaper_enabled=(int)value;
    if(!get_u32(file,&value))return 0;p->shaper_mode=(TsShaperMode)value;
    return get_float13(file,&p->shaper_drive)&&get_float13(file,&p->shaper_mix);
}

int ts_pr13_save_project(const TsInstrument *instrument, const char *path,
                         char *error, size_t error_size)
{
    FILE *file;
    uint16_t mask;
    unsigned char header[6];
    if (!ts_instrument_save_recipe(instrument, path, error, error_size)) return 0;
    file = fopen(path, "ab");
    if (file == NULL) return 0;
    mask = ts_pr13_lock_mask(instrument);
    header[0]='P';header[1]='1';header[2]='3';header[3]='E';
    header[4]=(unsigned char)(mask&255u);header[5]=(unsigned char)(mask>>8);
    if(fwrite(header,1,sizeof(header),file)!=sizeof(header))goto failed;
    for(int i=0;i<TS_BANK_SLOT_COUNT;++i){
        TsProcessRecipe neutral;
        const TsProcessRecipe *process=&instrument->bank[i].process;
        if(!instrument->bank[i].has_process){ts_pr13_neutral_process(&neutral);process=&neutral;}
        if(!put_u32(file,(uint32_t)instrument->bank[i].has_process)||!write_process(file,process))goto failed;
    }
    if(fwrite("E13P",1,4,file)!=4)goto failed;
    if(fclose(file)!=0)return 0;
    if(error!=NULL&&error_size>0)error[0]='\0';
    return 1;
failed:
    fclose(file);
    return 0;
}

static int copy_without_trailer(const char *path, long bytes,
                                char *temporary, size_t temporary_size)
{
    FILE *source;
    FILE *destination;
    unsigned char buffer[4096];
    long remaining = bytes;
    snprintf(temporary, temporary_size, "%s.pr13tmp", path);
    source = fopen(path, "rb");
    if (source == NULL) return 0;
    destination = fopen(temporary, "wb");
    if (destination == NULL) { fclose(source); return 0; }
    while (remaining > 0) {
        size_t want = remaining > (long)sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
        size_t got = fread(buffer, 1, want, source);
        if (got != want || fwrite(buffer, 1, got, destination) != got) {
            fclose(source); fclose(destination); remove(temporary); return 0;
        }
        remaining -= (long)got;
    }
    fclose(source);
    if (fclose(destination) != 0) { remove(temporary); return 0; }
    return 1;
}

int ts_pr13_load_project(TsInstrument *instrument, const char *path,
                         char *error, size_t error_size)
{
    FILE *file;
    unsigned char marker[6];
    uint16_t mask=0;
    int has_old=0,has_env=0;
    long total_size=0,trailer_bytes=0;
    char temporary[1024];
    const char *load_path=path;
    TsProcessRecipe processes[TS_BANK_SLOT_COUNT];
    int has_process[TS_BANK_SLOT_COUNT]={0};

    file=fopen(path,"rb");
    if(file!=NULL&&fseek(file,0,SEEK_END)==0){
        total_size=ftell(file);
        if(total_size>=(long)P13_ENV_TRAILER_SIZE&&
           fseek(file,total_size-(long)P13_ENV_TRAILER_SIZE,SEEK_SET)==0&&
           fread(marker,1,6,file)==6&&marker[0]=='P'&&marker[1]=='1'&&marker[2]=='3'&&marker[3]=='E'){
            unsigned char footer[4];uint32_t present;int ok=1;
            mask=(uint16_t)marker[4]|(uint16_t)((uint16_t)marker[5]<<8);
            for(int i=0;i<TS_BANK_SLOT_COUNT&&ok;++i){ok=get_u32(file,&present)&&read_process(file,&processes[i]);has_process[i]=present?1:0;}
            if(ok&&fread(footer,1,4,file)==4&&memcmp(footer,"E13P",4)==0){has_env=1;trailer_bytes=P13_ENV_TRAILER_SIZE;}
        }
        if(!has_env&&total_size>=6&&fseek(file,-6,SEEK_END)==0&&fread(marker,1,6,file)==6&&
           marker[0]=='P'&&marker[1]=='1'&&marker[2]=='3'&&marker[3]=='L'){
            mask=(uint16_t)marker[4]|(uint16_t)((uint16_t)marker[5]<<8);has_old=1;trailer_bytes=6;
        }
    }
    if(file!=NULL)fclose(file);

    if(trailer_bytes>0){
        if(!copy_without_trailer(path,total_size-trailer_bytes,temporary,sizeof(temporary))){
            if(error!=NULL&&error_size>0)snprintf(error,error_size,"Could not prepare PR13 project for loading");
            return 0;
        }
        load_path=temporary;
    }
    if(!ts_instrument_load_recipe(instrument,load_path,error,error_size)){
        if(trailer_bytes>0)remove(temporary);return 0;
    }
    if(trailer_bytes>0)remove(temporary);
    ts_pr13_restore_lock_mask(instrument,(has_env||has_old)?mask:0);

    if(has_env){
        for(int i=0;i<TS_BANK_SLOT_COUNT;++i){
            if(instrument->bank[i].occupied&&has_process[i]){instrument->bank[i].process=processes[i];instrument->bank[i].has_process=1;}
        }
    }else{
        if(!has_old){instrument->process.body=0.0f;instrument->process.edge=0.0f;instrument->process.drift=0.5f;}
        for(int i=0;i<TS_BANK_SLOT_COUNT;++i)if(instrument->bank[i].occupied){
            int parent=instrument->bank[i].parent_slot;
            if(parent>=0&&parent<TS_BANK_SLOT_COUNT&&instrument->bank[parent].occupied&&instrument->bank[parent].has_process)
                instrument->bank[i].process=instrument->bank[parent].process;
            else instrument->bank[i].process=instrument->process;
            instrument->bank[i].has_process=1;
        }
    }
    return ts_pr13_rerender(instrument,-1,error,error_size);
}
