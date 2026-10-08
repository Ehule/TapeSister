/* XM exchange uses TapeHead's decoder/replayer and portable host tile storage. */
#include "ft2_module_loader.h"
#include "ft2_module_saver.h"
#include "ft2_sysreqs.h"
#include <stdarg.h>
extern bool loadXM(FILE *,uint32_t);
extern void tapeheadEmbeddedPendingLengths(const song_t *,uint16_t [32],int *);
static char xm_message[256];
void tapeheadEmbeddedXmError(const char *message){snprintf(xm_message,sizeof(xm_message),"%s",message);}
static void xm_loader_message(const char *format,...) {
    va_list args;va_start(args,format);vsnprintf(xm_message,sizeof(xm_message),format,args);va_end(args);
}
static UNICHAR *xm_path(const char *path) {
#ifdef _WIN32
    int n=MultiByteToWideChar(CP_UTF8,0,path,-1,NULL,0);
    UNICHAR *p=n?malloc(n*sizeof(*p)):NULL;
    if(p && !MultiByteToWideChar(CP_UTF8,0,path,-1,p,n)){free(p);p=NULL;}
    return p;
#else
    return strdup(path);
#endif
}
static void xm_clear_temporary(void) {
    for(int i=0;i<256;++i){free(patternTmp[i]);patternTmp[i]=NULL;patternNumRowsTmp[i]=64;}
    for(int a=1;a<=256;++a)if(instrTmp[a]) {
        for(int k=0;k<16;++k)freeSmpData(&instrTmp[a]->smp[k]);
        free(instrTmp[a]);instrTmp[a]=NULL;
    }
    memset(&songTmp,0,sizeof(songTmp));
    sampleLauncherBeginModuleLoad();fastTracksPOCBeginModuleLoad();
}
int ts_tapehead_xm_prepare(const char *path,TsSamplePages *staged,char *error,size_t size) {
    if(!embed.initialized || !path || !staged)return fail(error,size,"Tracker is not initialized");
    memset(staged,0,sizeof(*staged));xm_clear_temporary();xm_message[0]=0;
    void (*previous_message)(const char *,...)=loaderMsgBox;loaderMsgBox=xm_loader_message;
    UNICHAR *filename=xm_path(path);FILE *f=filename?UNICHAR_FOPEN(filename,"rb"):NULL;free(filename);
    if(!f){loaderMsgBox=previous_message;return fail(error,size,"Unable to open XM file");}
    fseek(f,0,SEEK_END);long length=ftell(f);rewind(f);
    xmHdr_t header={0};size_t header_read=fread(&header,1,sizeof(header),f);rewind(f);
    int ok=header_read==sizeof(header) && length>0 && (uint64_t)length<=UINT32_MAX && loadXM(f,(uint32_t)length);
    if(ferror(f))ok=0;
    fclose(f);loaderMsgBox=previous_message;
    if(!ok || xm_message[0]) {
        fail(error,size,xm_message[0]?xm_message:"Invalid or truncated XM file");goto bad;
    }
    if(!ts_sample_pages_init(staged,error,size))goto bad;
    staged->active_live=0;
    TsSisterTracker *t=&staged->tracker;
    t->channel_count=(songTmp.numChannels+1)&~1;t->bpm=songTmp.BPM;t->ticks_per_line=songTmp.speed;
    t->order_count=songTmp.songLength?songTmp.songLength:1;
    t->restart_order=songTmp.songLoopStart<t->order_count?songTmp.songLoopStart:0;
    uint16_t lengths[32];int control;
    tapeheadEmbeddedPendingLengths(&songTmp,lengths,&control);t->control_lane=control;
    for(int ch=0;ch<32;++ch)t->lanes[ch].length=lengths[ch];
    size_t tile_count=0;
    for(int a=1;a<=128;++a) {
        instr_t *ins=instrTmp[a];TsTrackerInstrument *v=&t->instruments[a];
        if(!ins && !songTmp.instrName[a][0])continue;
        v->present=1;snprintf(v->name,sizeof(v->name),"%.22s",songTmp.instrName[a]);
        if(!ins)continue;
        sanitizeInstrument(ins);
        for(int k=0;k<16;++k)sanitizeSample(&ins->smp[k]);
        instrument_metadata(v,ins);
        for(int k=0;k<ins->numSamples;++k) {
            sample_t *s=&ins->smp[k];sanitizeSample(s);
            if(!s->length || !s->dataPtr)continue;
            size_t page=tile_count/16;int slot=tile_count%16;
            if(page==staged->page_count && !ts_sample_pages_append(staged,error,size))goto bad;
            TsInstrument *bank=staged->pages[page];
            TsSample sample;ts_sample_init(&sample);
            sample.frames=s->length;sample.channels=1;sample.sample_rate=8363;
            double frequency=8363.0*exp2(s->relativeNote/12.0+s->finetune/1536.0);
            TsTuning tuning={60-s->relativeNote,s->finetune*100.0f/128.0f};
            if(tuning.root_note<0 || tuning.root_note>127) {
                tuning.root_note=CLAMP(tuning.root_note,0,127);
                double exact_rate=frequency*exp2((tuning.root_note-60)/12.0);
                sample.sample_rate=(uint32_t)round(exact_rate);
                tuning.fine_tune_cents=(float)(1200*log2(exact_rate/sample.sample_rate));
            }
            snprintf(sample.name,sizeof(sample.name),"%.22s",s->name[0]?s->name:v->name);
            if(!sample.name[0])snprintf(sample.name,sizeof(sample.name),"XM %02X SAMPLE %02X",a,k+1);
            sample.data=malloc(sample.frames*sizeof(float));
            if(!sample.data){fail(error,size,"Unable to allocate XM sample");goto bad;}
            for(size_t j=0;j<sample.frames;++j)sample.data[j]=(s->flags&SAMPLE_16BIT)?
                ((int16_t*)s->dataPtr)[j]/32768.0f:s->dataPtr[j]/128.0f;
            bank->selected_slot=slot;
            int imported=ts_instrument_import_sample(bank,&sample,(s->flags&3)!=0,
                s->loopStart,s->loopStart+s->loopLength,(s->flags&LOOP_PINGPONG)?TS_LOOP_PING_PONG:TS_LOOP_FORWARD,error,size);
            ts_sample_free(&sample);if(!imported)goto bad;
            bank->loop_crossfade_ms=bank->bank[slot].loop_crossfade_ms=0;
            bank->tuning=bank->audible_tuning=tuning;
            bank->bank[slot].tuning=bank->bank[slot].audible_tuning=tuning;
            v->tiles[k]=bank->bank[slot].tile_id;
            if(!t->aliases[a])t->aliases[a]=v->tiles[k];
            ++tile_count;
        }
    }
    /* Preserve empty patterns and all hidden channels; orders can refer to an
       empty pattern beyond the last physically stored pattern. */
    int count=MAX(1,header.numPatterns);
    for(int i=0;i<256;++i)if(patternTmp[i] || patternNumRowsTmp[i]!=64)count=i+1;
    for(int i=0;i<t->order_count;++i)count=MAX(count,songTmp.orders[i]+1);
    size_t bytes=TH_HEADER+32*TH_LANE_BYTES+256+count*TH_PATTERN_BYTES+TH_PREFS_BYTES;
    uint8_t *p=calloc(1,bytes);if(!p){fail(error,size,"Unable to allocate XM score");goto bad;}
    t->embedded_data=p;t->embedded_size=bytes;
    memcpy(p,"STH3",4);p[5]=t->channel_count;p[6]=tmpLinearPeriodsFlag;p+=8;
    put(&p,count,2);put(&p,t->bpm,2);put(&p,t->ticks_per_line,1);put(&p,64,1);
    put(&p,t->order_count,2);put(&p,t->restart_order,2);put(&p,songTmp.orders[0],1);put(&p,0,2);
    put(&p,4,1);put(&p,1,1);put(&p,1,1);put(&p,1,1);put(&p,1,1);put(&p,0,1);
    put(&p,0,1);put(&p,0,1);put(&p,tapeheadConfig.patternColorMode,1);
    memcpy(p,songTmp.name,20);p+=21;put(&p,control+1,1);
    for(int ch=0;ch<32;++ch) {
        put(&p,lengths[ch],2);put(&p,0,1);put(&p,7,1);put(&p,0,1);put(&p,0,1);put(&p,0,1);put(&p,256,2);put(&p,0,1);
    }
    memcpy(p,songTmp.orders,256);p+=256;
    for(int i=0;i<count;++i) {
        TsPatternId id;
        if(!ts_sister_tracker_add_pattern(t,patternNumRowsTmp[i],&id,error,size))goto bad;
        put(&p,i,1);put(&p,patternNumRowsTmp[i],2);put(&p,id,4);
        TsTrackerPattern *native=t->patterns[i];
        for(int row=0;row<256;++row)for(int ch=0;ch<32;++ch) {
            note_t c=patternTmp[i]?patternTmp[i][row*32+ch]:(note_t){0};
            if(c.note>97 || c.instr>128 || c.efx>35){fail(error,size,"Unsupported XM pattern command");goto bad;}
            put(&p,c.note,1);put(&p,c.instr,1);put(&p,c.vol,1);put(&p,c.efx,1);put(&p,c.efxData,1);put(&p,c.tuneType,1);put(&p,c.tuneData,1);
            TsTrackerCell *d=&native->cells[row][ch];
            if(c.note==97)d->note_kind=TS_TRACKER_NOTE_OFF;
            else if(c.note){d->note_kind=TS_TRACKER_NOTE_PITCH;d->note=c.note+11;}
            d->tile_id=c.instr?t->aliases[c.instr]:0;
            if(c.vol>=0x10 && c.vol<=0x50){d->has_volume=1;d->volume=c.vol-0x10;}
            if(c.tuneType){d->tune_command=c.tuneType==0x16?'M':'N';d->tune_value=c.tuneData;}
            if(c.efx || c.efxData){d->fx_command=c.efx<10?'0'+c.efx:'A'+c.efx-10;d->fx_value=c.efxData;}
        }
    }
    for(int i=0;i<t->order_count;++i)t->orders[i]=t->patterns[songTmp.orders[i]]->id;
    t->editor_pattern=t->orders[0];preferences_get(p);
    if(!ts_sister_tracker_validate(t,error,size))goto bad;
    xm_clear_temporary();return 1;
bad:
    xm_clear_temporary();ts_sample_pages_free(staged);return 0;
}
int ts_tapehead_xm_save(const char *path,char *error,size_t size) {
    if(!embed.initialized || !path || !path[0])return fail(error,size,"Open TrackSister before exporting XM");
    /* Standard XM samples are mono. Refuse conversions that would silently
       discard sound or tile-loop behavior. */
    for(int a=1;a<=128;++a)if(instr[a])for(int k=0;k<16;++k) {
        const sample_t *s=&instr[a]->smp[k];
        if(s->tileData && (s->tileChannels>1 || s->tileCrossfade ||
           (s->flags&3 && s->tileLoopMode!=TS_LOOP_FORWARD && s->tileLoopMode!=TS_LOOP_PING_PONG)))
            return fail(error,size,"XM requires mono samples and forward/ping-pong loops without crossfade. Keep a TSR or export WAV.");
    }
    char *temporary=malloc(strlen(path)+40);if(!temporary)return fail(error,size,"Unable to prepare XM export");
    snprintf(temporary,strlen(path)+40,"%s.%016llx.partial",path,(unsigned long long)SDL_GetPerformanceCounter());
    UNICHAR *tmp=xm_path(temporary),*dest=xm_path(path);
    /* Reserve a unique sibling without overwriting an unrelated partial file. */
    FILE *reserved=tmp && dest?UNICHAR_FOPEN(tmp,"wbx"):NULL;
    int created=reserved!=NULL;
    xm_message[0]=0;int ok=created && fclose(reserved)==0 && saveXM(tmp);
#ifdef _WIN32
    if(ok)ok=MoveFileExW(tmp,dest,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    if(ok)ok=rename(temporary,path)==0;
#endif
    if(!ok && created)UNICHAR_REMOVE(tmp);
    free(tmp);free(dest);free(temporary);
    if(!ok)return fail(error,size,xm_message[0]?xm_message:"Unable to save XM file");
    return 1;
}
