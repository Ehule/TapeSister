/* Byte-level fixtures independent of TapeHead's writers. */
static void module_fixture_le16(uint8_t *p,unsigned v){p[0]=v;p[1]=v>>8;}
static void module_fixture_le32(uint8_t *p,unsigned v){module_fixture_le16(p,v);module_fixture_le16(p+2,v>>16);}
static void module_fixture_write(const char *path,const uint8_t *data,size_t n) {
    FILE *f=fopen(path,"wb");assert(f && fwrite(data,1,n,f)==n && !fclose(f));
}
static void mod_fixture(const char *path,unsigned channels,int stk,int flt) {
    unsigned head=stk?600:1084,order=stk?470:950;
    size_t n=head+64*channels*4+512;uint8_t *data=calloc(n,1);assert(data);
    memcpy(data,"MOD import fixture",18);memcpy(data+20,"Looped sample",13);
    data[42]=1;data[44]=stk?0:7;data[45]=48;data[47]=8;data[49]=120;
    data[order]=1;data[order+1]=stk?120:0;
    if(!stk) {
        char sig[5];snprintf(sig,sizeof(sig),"%02uCH",channels);
        memcpy(data+1080,flt?"FLT8":channels==4?"M.K.":sig,4);
    }
    unsigned note=head+(flt?1024+3*4:(channels-1)*4);
    data[note]=1;data[note+1]=0xac;data[note+2]=0x1c;data[note+3]=48;
    memset(data+head+64*channels*4,32,512);
    module_fixture_write(path,data,n);free(data);
}
static void fixture_bits(uint8_t *data,unsigned *bit,unsigned value,unsigned count) {
    for(unsigned i=0;i<count;++i,++*bit)if(value&(1u<<i))data[*bit/8]|=1u<<(*bit%8);
}
/* compression: 0 PCM; 1 fixed-width IT214; 2 variable-width IT215. */
static void it_fixture(const char *path,unsigned channels,int instruments,int bits,int compression) {
    uint8_t data[8192]={0};unsigned ns=instruments?2:1;
    memcpy(data,"IMPM",4);memcpy(data+4,"IT import fixture",17);
    module_fixture_le16(data+32,2);module_fixture_le16(data+34,instruments);
    module_fixture_le16(data+36,ns);module_fixture_le16(data+38,1);
    module_fixture_le16(data+40,0x215);module_fixture_le16(data+42,0x214);
    module_fixture_le16(data+44,8|(instruments?4:0));data[48]=128;data[49]=48;data[50]=6;data[51]=125;
    memset(data+64,32,64);memset(data+128,64,64);data[193]=255;
    unsigned offset=194+4*(instruments+ns+1),smp_table=194+4*instruments;
    if(instruments) {
        module_fixture_le32(data+194,offset);uint8_t *ins=data+offset;
        memcpy(ins,"IMPI",4);memcpy(ins+32,"Mapped IT instrument",20);ins[24]=128;
        module_fixture_le16(ins+20,32);
        for(unsigned note=0;note<120;++note){ins[64+note*2]=note;ins[65+note*2]=note<60?1:2;}
        ins[304]=1;ins[305]=2;ins[310]=64;ins[313]=0;module_fixture_le16(ins+314,20);
        offset+=554;
    }
    unsigned smp[2];
    for(unsigned k=0;k<ns;++k) {
        smp[k]=offset;module_fixture_le32(data+smp_table+4*k,offset);uint8_t *s=data+offset;
        memcpy(s,"IMPS",4);s[17]=64;s[18]=1|16|(bits==16?2:0)|(compression?8:0);s[19]=48;
        memcpy(s+20,"IT loop sample",14);s[46]=compression==2?5:1;
        module_fixture_le32(s+48,512);module_fixture_le32(s+52,16);module_fixture_le32(s+56,256);
        module_fixture_le32(s+60,8363);offset+=80;
    }
    module_fixture_le32(data+smp_table+4*ns,offset);uint8_t *p=data+offset;
    module_fixture_le16(p+2,2);unsigned n=8;
    p[n++]=128|channels;p[n++]=15;p[n++]=60;p[n++]=1;p[n++]=48;p[n++]=1;p[n++]=3;p[n++]=0;
    p[n++]=channels;p[n++]=72;p[n++]=1;p[n++]=48;p[n++]=20;p[n++]=140;p[n++]=0;
    module_fixture_le16(p,n-8);offset+=n;
    for(unsigned k=0;k<ns;++k) {
        module_fixture_le32(data+smp[k]+72,offset);
        if(compression) {
            unsigned bit=0;uint8_t *packed=data+offset+2;
            if(compression==2) {
                /* Switch down to width 1, emit a zero, switch up through a
                   low-width marker, then switch from width 7 to maximum. */
                fixture_bits(packed,&bit,1u<<bits,bits+1);
                fixture_bits(packed,&bit,0,1);fixture_bits(packed,&bit,1,1);
                fixture_bits(packed,&bit,5,bits==8?3:4); /* width 7 */
                unsigned border=(((1u<<bits)-1)>>(bits+1-7))-bits/2;
                fixture_bits(packed,&bit,border+bits,7); /* maximum width */
                for(unsigned i=1;i<512;++i)fixture_bits(packed,&bit,1,bits+1);
            } else {
                for(unsigned i=0;i<512;++i)fixture_bits(packed,&bit,i?0:(bits==16?8192:32),bits+1);
            }
            unsigned bytes=(bit+7)/8;module_fixture_le16(data+offset,bytes);offset+=2+bytes;
        } else if(bits==16) {
            for(unsigned i=0;i<512;++i)module_fixture_le16(data+offset+2*i,8192);offset+=1024;
        } else {memset(data+offset,32,512);offset+=512;}
    }
    module_fixture_write(path,data,offset);
}
static void module_patch_byte(const char *path,long offset,unsigned value) {
    FILE *f=fopen(path,"r+b");assert(f && !fseek(f,offset,SEEK_SET) && fputc(value,f)!=EOF && !fclose(f));
}
static void module_reject(const char *path,const char *message) {
    uint64_t hash=ts_sister_tracker_hash(&test_pages->tracker);size_t pages=test_pages->page_count;
    TsSamplePages staged={0};assert(!ts_tapehead_module_prepare(path,&staged,test_error,sizeof(test_error)));
    assert(strstr(test_error,message) && !staged.pages);
    assert(ts_sister_tracker_hash(&test_pages->tracker)==hash && test_pages->page_count==pages);
}
static void module_imports(void) {
    assert(ts_module_kind("MOD.Amiga tune")==TS_MODULE_MOD);
    assert(ts_module_kind("c:\\music\\UPPER.IT")==TS_MODULE_IT);
    assert(ts_module_kind("/music/a.XM")==TS_MODULE_XM && !ts_module_kind("/music.it/song.wav"));
    for(int mode=0;mode<4;++mode) {
        unsigned channels=mode==1?32:mode==2?8:4;
        mod_fixture("fixture.mod",channels,mode==3,mode==2);
        TsSamplePages staged={0};
        assert(ts_tapehead_module_prepare("fixture.mod",&staged,test_error,sizeof(test_error)));
        assert(staged.tracker.channel_count==channels && staged.tracker.bpm==125);
        assert(staged.tracker.instruments[1].sample_count==1 && staged.tracker.instruments[1].tiles[0]);
        /* SoundTracker plays a looped sample starting at its byte loop offset. */
        assert(staged.pages[0]->bank[0].sample.frames==(mode==3?504:512) && staged.pages[0]->bank[0].sample.data[0]==.25f);
        assert(staged.tracker.patterns[0]->cells[0][channels-1].note_kind==TS_TRACKER_NOTE_PITCH);
        assert(staged.tracker.patterns[0]->cells[0][channels-1].fx_command=='C');
        assert(staged.tracker.instruments[1].volume[0]==48 && staged.pages[0]->bank[0].has_loop);
        ts_sample_pages_free(&staged);
    }
    mod_fixture("fixture.mod",32,0,0);
    assert(load_instrument(0,test_audio,test_ui,test_bank,test_pages,NULL,0,"fixture.mod"));
    assert(song.numChannels==32 && instr[1]->smp[0].tileData && instr[1]->smp[0].finetune==112);
    startPlaying(PLAYMODE_PATT,0);double energy=0;
    for(unsigned b=0;b<8;++b){const float *p=ts_tapehead_render(256,48000);assert(p);for(unsigned i=0;i<512;++i)energy+=fabs(p[i]);}
    assert(energy>.1);ts_tapehead_stop();export_score();project_roundtrip();
    assert(save_xm_song(0,test_audio,test_ui,test_bank,test_pages,0,"converted.xm",test_error,sizeof(test_error)));
    TsSamplePages staged={0};assert(ts_tapehead_module_prepare("converted.xm",&staged,test_error,sizeof(test_error)));
    assert(staged.tracker.channel_count==32 && staged.pages[0]->bank[0].sample.frames==512);ts_sample_pages_free(&staged);
    mod_fixture("fixture.mod",33,0,0);module_reject("fixture.mod","32 tracks");
    mod_fixture("fixture.mod",4,0,0);module_patch_byte("fixture.mod",42,127);module_reject("fixture.mod","Truncated");
    for(int bits=8;bits<=16;bits+=8)for(int compression=0;compression<=2;++compression) {
        it_fixture("fixture.it",32,compression==0,bits,compression);
        if(!ts_tapehead_module_prepare("fixture.it",&staged,test_error,sizeof(test_error))){fprintf(stderr,"IT fixture: %s\n",test_error);assert(0);}
        assert(staged.tracker.channel_count==32 && staged.tracker.patterns[0]->rows==2);
        assert(staged.tracker.patterns[0]->cells[0][31].note_kind==TS_TRACKER_NOTE_PITCH);
        assert(staged.tracker.patterns[0]->cells[0][31].fx_command=='F' && staged.tracker.patterns[0]->cells[0][31].fx_value==3);
        const TsSample *s=&staged.pages[0]->bank[0].sample;assert(s->frames==512);
        if(compression==2) {
            /* 2.15 integrates the delta once more, yielding triangular numbers. */
            float unit=bits==8?1/128.0f:1/32768.0f;
            assert(s->data[0]==0 && s->data[1]==unit && s->data[2]==3*unit && s->data[3]==6*unit);
        } else assert(s->data[0]==.25f && s->data[511]==.25f);
        if(!compression)assert(staged.tracker.instruments[1].sample_count==2 && staged.tracker.instruments[1].note_map[60]==1 && staged.tracker.instruments[1].vol_flags==1);
        ts_sample_pages_free(&staged);
    }
    it_fixture("fixture.it",32,1,16,0);
    assert(load_instrument(0,test_audio,test_ui,test_bank,test_pages,NULL,0,"fixture.it"));
    assert(strstr(test_ui->status,"APPROXIMATE") && instr[1]->smp[1].tileData);
    export_score();project_roundtrip();
    assert(save_xm_song(0,test_audio,test_ui,test_bank,test_pages,0,"converted.xm",test_error,sizeof(test_error)));
    assert(ts_tapehead_module_prepare("converted.xm",&staged,test_error,sizeof(test_error)));
    assert(staged.tracker.instruments[1].sample_count==2 && staged.tracker.instruments[1].note_map[60]==1);ts_sample_pages_free(&staged);
    it_fixture("fixture.it",33,0,8,0);module_reject("fixture.it","32-track");
    it_fixture("fixture.it",4,0,8,0);module_patch_byte("fixture.it",194,255);module_patch_byte("fixture.it",195,255);module_reject("fixture.it","sample header");
    it_fixture("fixture.it",4,0,8,0);module_patch_byte("fixture.it",202+18,5);module_reject("fixture.it","Stereo");
    it_fixture("fixture.it",4,0,8,0);module_patch_byte("fixture.it",282,1);module_reject("fixture.it","packed IT pattern");
    it_fixture("fixture.it",4,0,8,0);module_patch_byte("fixture.it",285,2);module_reject("fixture.it","1-256 rows");
    it_fixture("fixture.it",4,0,8,1);
    /* Legal block framing, illegal width marker inside the packed sample. */
    module_patch_byte("fixture.it",307,255);module_patch_byte("fixture.it",308,255);module_reject("fixture.it","memory");
    it_fixture("fixture.it",4,0,8,1);module_patch_byte("fixture.it",305,255);module_patch_byte("fixture.it",306,255);module_reject("fixture.it","block length");
    /* A physically stored trailing blank pattern survives even if unsequenced. */
    xm_fixture("tail.xm",8,0);module_patch_byte("tail.xm",64,1);
    module_patch_byte("tail.xm",336+9+80+5,64);
    assert(ts_tapehead_module_prepare("tail.xm",&staged,test_error,sizeof(test_error)));
    assert(staged.tracker.pattern_count==2 && staged.tracker.patterns[1]->rows==64);
    ts_sample_pages_free(&staged);remove("tail.xm");
    remove("fixture.mod");remove("fixture.it");remove("converted.xm");
}
static void raw_module_imports(void) {
    const char *names[]={"raw.xm","raw.mod","raw.it","raw.wav"};
    xm_fixture(names[0],8,0);mod_fixture(names[1],4,0,0);it_fixture(names[2],4,0,8,0);
    assert(ts_sample_save_wav32f(&test_bank->current,names[3],test_error,sizeof(test_error)));
    BrowserAudition audition={0};ImportController controller;import_controller_init(&controller);
    PendingFileOperation pending={0};TsSample selection;ts_sample_init(&selection);
    for(unsigned n=0;n<4;++n) {
        test_ui->tracker_open=0;browser_open(test_ui,TS_BROWSER_LOAD_WAV);assert(!test_ui->browser.import_as_raw);
        snprintf(test_ui->browser.directory,sizeof(test_ui->browser.directory),".");
        test_ui->browser.entry_count=1;test_ui->browser.selected=0;
        memset(&test_ui->browser.entries[0],0,sizeof(test_ui->browser.entries[0]));
        snprintf(test_ui->browser.entries[0].name,sizeof(test_ui->browser.entries[0].name),"%s",names[n]);
        snprintf(test_ui->browser.filename,sizeof(test_ui->browser.filename),"%s",names[n]);
        browser_toggle_raw(0,test_audio,test_ui,&audition);assert(test_ui->browser.import_as_raw);
        if(!n)snapshot("module-raw-browser.bmp");
        uint64_t hash=ts_sister_tracker_hash(&test_pages->tracker);size_t pages=test_pages->page_count;
        uint64_t before=ts_sample_hash(&test_bank->current);
        browser_action(0,test_audio,test_ui,test_bank,&selection,test_pages,NULL,0,&pending,1);
        assert(pending.active && pending.force_raw);
        /* Queued choice owns the mode; later UI changes cannot reroute it. */
        test_ui->browser.import_as_raw=0;
        run_pending_file_operation(0,test_audio,test_ui,test_bank,&selection,&controller,test_pages,NULL,0,&pending);
        assert(controller.worker);
        for(unsigned wait=0;wait<5000 && controller.worker;++wait) {
            SDL_Delay(1);poll_import_worker(0,test_audio,test_ui,test_bank,&selection,&controller);
        }
        assert(!controller.worker && test_ui->import_preview_open && test_ui->import_preview_raw);
        assert(controller.decoded.kind==TS_AUDIO_IMPORT_RAW && ts_sister_tracker_hash(&test_pages->tracker)==hash && test_pages->page_count==pages);
        assert(ts_sample_hash(&test_bank->current)==before);
        FILE *f=fopen(names[n],"rb");assert(f && !fseek(f,0,SEEK_END));size_t bytes=ftell(f);fclose(f);
        assert(controller.decoded.sample.frames==bytes); /* Whole-file bytes, including headers. */
        if(!n) {
            snapshot("module-raw-preview.bmp");
            handle_import_action(0,test_audio,test_ui,test_bank,&selection,&controller,TS_UI_IMPORT_ACTION_RATE_NEXT,0,48000);
            assert(controller.decoded.sample.sample_rate==48000);
            apply_import_preview(0,test_audio,test_ui,test_bank,&selection,&controller,0);
            assert(test_bank->current.frames==bytes && ts_sister_tracker_hash(&test_pages->tracker)==hash);
        }
        close_import_preview(0,test_audio,test_ui,&controller);ts_browser_close(&test_ui->browser);
        remove(names[n]);
    }
    ts_sample_free(&selection);
    /* A new normal load session defaults back to score import. */
    browser_open(test_ui,TS_BROWSER_LOAD_WAV);assert(!test_ui->browser.import_as_raw);ts_browser_close(&test_ui->browser);
}
