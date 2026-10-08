/* Independent on-disk fixture, not produced by the exporter under test. */
static void xm_fixture(const char *path,unsigned channels,int amiga) {
    FILE *f=fopen(path,"wb");assert(f);
    xmHdr_t h={0};memcpy(h.ID,"Extended Module: ",17);memcpy(h.name,"XM boundary fixture",19);
    h.x1A=0x1a;h.version=0x104;h.headerSize=276;h.numOrders=2;h.numChannels=channels;
    h.numPatterns=2;h.numInstr=2;h.flags=!amiga;h.speed=1;h.BPM=125;h.orders[1]=1;
    assert(fwrite(&h,sizeof(h),1,f)==1);
    xmPatHdr_t p={0};p.headerSize=9;p.numRows=2;p.dataSize=2*channels*5;
    assert(fwrite(&p,sizeof(p),1,f)==1);
    for(unsigned row=0;row<2;++row)for(unsigned ch=0;ch<channels;++ch) {
        uint8_t note[5]={0};if(ch==channels-1){note[0]=row?61:49;note[1]=1;note[2]=0x50;}
        assert(fwrite(note,5,1,f)==1);
    }
    p.numRows=17;p.dataSize=0;assert(fwrite(&p,sizeof(p),1,f)==1);
    xmInsHdr_t ins={0};ins.instrSize=263;ins.sampleSize=40;ins.numSamples=3;
    memcpy(ins.name,"Mapped instrument",17);
    for(int n=0;n<96;++n)ins.note2SampleLUT[n]=n<60?1:2;
    ins.volEnvLength=3;ins.volEnvFlags=3;ins.volEnvSustain=1;ins.fadeout=1024;
    ins.volEnvPoints[0][1]=64;ins.volEnvPoints[1][0]=2;ins.volEnvPoints[1][1]=64;ins.volEnvPoints[2][0]=4;
    ins.panEnvLength=1;ins.panEnvPoints[0][1]=32;ins.panEnvFlags=1;ins.vibRate=3;ins.vibDepth=2;
    for(int k=0;k<3;++k) {
        ins.smp[k].volume=k==2?48:64;ins.smp[k].panning=128;
        if(k) {unsigned width=k==2?2:1;
            ins.smp[k].length=512*width;ins.smp[k].loopStart=16*width;ins.smp[k].loopLength=240*width;
            ins.smp[k].flags=k==1?1:(2|16);ins.smp[k].relativeNote=-2;ins.smp[k].finetune=-7;}
        memcpy(ins.smp[k].name,k==2?"Negative sample":"Positive sample",15);
    }
    assert(fwrite(&ins,263+3*40,1,f)==1);
    for(int j=0;j<512;++j)assert(fputc(j?0:32,f)!=EOF);
    for(int j=0;j<512;++j){assert(fputc(0,f)!=EOF);assert(fputc(j?0:192,f)!=EOF);}
    memset(&ins,0,sizeof(ins));ins.instrSize=29;memcpy(ins.name,"Empty instrument",16);
    assert(fwrite(&ins,29,1,f)==1);assert(!fclose(f));
}
static void xm_roundtrip(void) {
    ts_tapehead_stop();xm_fixture("fixture-32.xm",32,0);
    size_t old_pages=test_pages->page_count;
    uint64_t hash=ts_sister_tracker_hash(&test_pages->tracker);
    TsSamplePages staged;
    assert(ts_tapehead_module_prepare("fixture-32.xm",&staged,test_error,sizeof(test_error)));
    assert(ts_sister_tracker_hash(&test_pages->tracker)==hash);
    assert(staged.tracker.channel_count==32 && staged.tracker.pattern_count==2);
    assert(staged.tracker.patterns[1]->rows==17);
    assert(staged.tracker.patterns[0]->cells[0][31].note==60);
    assert(staged.tracker.instruments[1].tiles[0]==0 && staged.tracker.instruments[1].tiles[2]);
    assert(staged.tracker.instruments[1].note_map[60]==2 && staged.tracker.instruments[1].vol_flags==3);
    ts_sample_pages_free(&staged);
    assert(load_instrument(0,test_audio,test_ui,test_bank,test_pages,NULL,0,"fixture-32.xm"));
    assert(test_pages->page_count>old_pages && song.numChannels==32 && test_ui->tracker_open);
    assert(instr[1] && instr[1]->smp[1].tileData && instr[1]->smp[2].tileData);
    assert(instr[1]->volEnvFlags==3 && instr[1]->note2SampleLUT[60]==2);
    assert(pattern[0][31].note==49 && pattern[0][32+31].note==61);
    assert(patternNumRows[1]==17);
    assert(instr[1]->smp[1].relativeNote==-2 && instr[1]->smp[1].finetune==-7);
    assert(!strcmp(instr[1]->smp[0].name,"Positive sample"));
    /* The original editor scrolls to, edits, and retains the last channel. */
    for(int ch=0;ch<31;++ch)press(SDLK_TAB,SDL_SCANCODE_TAB,KMOD_NONE);
    assert(cursor.ch==31);press(SDLK_z,SDL_SCANCODE_Z,KMOD_NONE);
    assert(pattern[0][31].note);press(SDLK_z,SDL_SCANCODE_Z,KMOD_CTRL);
    assert(pattern[0][31].note==49);ts_tapehead_tick();snapshot("xm-32-tracks.bmp");
    click(390,146);click(149,26);assert(ts_tapehead_config_visible());
    click(130,140);assert(song.numChannels==30 && pattern[0][31].note==49);
    click(160,140);assert(song.numChannels==32);ts_tapehead_tick();snapshot("xm-track-settings.bmp");
    press(SDLK_ESCAPE,SDL_SCANCODE_ESCAPE,KMOD_NONE);
    /* Audible track 32, then its high-note sample. */
    startPlaying(PLAYMODE_PATT,0);double energy=0;
    for(int b=0;b<8;++b){const float *out=ts_tapehead_render(256,48000);assert(out);for(int n=0;n<512;++n)energy+=fabs(out[n]);}
    assert(energy>.1);ts_tapehead_stop();export_score();
    project_roundtrip();
    assert(ts_sister_tracker_save_file(&test_pages->tracker,"xm-score.tst",test_error,sizeof(test_error)));
    TsSisterTracker saved;ts_sister_tracker_init(&saved);
    assert(ts_sister_tracker_load_file(&saved,"xm-score.tst",test_error,sizeof(test_error)));
    assert(saved.channel_count==32 && saved.instruments[1].tiles[2]);
    ts_sister_tracker_free(&test_pages->tracker);test_pages->tracker=saved;
    assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));
    assert(instr[1]->smp[2].tileData && instr[1]->volEnvFlags==3 && song.numChannels==32);
    /* Saving does not alter empty pattern lengths, score or sounding samples. */
    hash=ts_sister_tracker_hash(&test_pages->tracker);
    if(!save_xm_song(0,test_audio,test_ui,test_bank,test_pages,0,"roundtrip-32.xm",test_error,sizeof(test_error))) {fprintf(stderr,"XM export: %s\n",test_error);assert(0);}
    assert(patternNumRows[1]==17 && hash==ts_sister_tracker_hash(&test_pages->tracker));
    instr[1]->smp[1].tileCrossfade=1;
    assert(!ts_tapehead_xm_save("roundtrip-32.xm",test_error,sizeof(test_error)) && strstr(test_error,"crossfade"));
    instr[1]->smp[1].tileCrossfade=0; /* Failed export must leave the previous file intact. */
    assert(ts_tapehead_module_prepare("roundtrip-32.xm",&staged,test_error,sizeof(test_error)));
    assert(staged.tracker.channel_count==32 && staged.tracker.patterns[0]->cells[1][31].note==72);
    assert(staged.tracker.instruments[1].sample_count==3 && staged.tracker.instruments[1].vol_flags==3);
    assert(staged.tracker.instruments[2].sample_count==0 && !staged.tracker.instruments[3].present);
    assert(!strncmp(staged.tracker.instruments[1].sample_names[0],"Positive sample",15)); /* XM pads names. */
    assert(staged.tracker.instruments[1].relative_note[1]==-2 && staged.tracker.instruments[1].finetune[1]==-7);
    assert(staged.tracker.instruments[1].note_map[60]==2 && staged.tracker.patterns[1]->rows==17);
    const TsInstrument *bank=staged.pages[0];assert(bank->bank[0].sample.data[0]==.25f);
    assert(bank->bank[1].sample.data[0]==-.5f && bank->bank[1].loop_mode==TS_LOOP_PING_PONG);
    ts_sample_pages_free(&staged);
    test_ui->tracker_open=0;browser_open(test_ui,TS_BROWSER_SAVE_XM);
    assert(!strcmp(ts_browser_mode_extension(test_ui->browser.mode),".xm"));
    snapshot("xm-save-browser.bmp");ts_browser_close(&test_ui->browser);test_ui->tracker_open=1;
    /* Other frequency table, normal 8-track scores, and explicit rejection. */
    xm_fixture("fixture-8.xm",8,1);assert(ts_tapehead_module_prepare("fixture-8.xm",&staged,test_error,sizeof(test_error)));
    assert(staged.tracker.channel_count==8 && staged.tracker.embedded_data[6]==0);ts_sample_pages_free(&staged);
    FILE *bad=fopen("fixture-8.xm","r+b");assert(bad);assert(fputc('?',bad)!=EOF);fclose(bad);
    assert(!ts_tapehead_module_prepare("fixture-8.xm",&staged,test_error,sizeof(test_error)));
    xm_fixture("fixture-8.xm",8,1);bad=fopen("fixture-8.xm","r+b");assert(bad);
    assert(!fseek(bad,336+7,SEEK_SET));assert(fputc(1,bad)!=EOF && fputc(0,bad)!=EOF);fclose(bad);
    assert(!ts_tapehead_module_prepare("fixture-8.xm",&staged,test_error,sizeof(test_error)));
    xm_fixture("fixture-8.xm",8,1);
    bad=fopen("fixture-8.xm","rb");assert(bad);
    FILE *short_sample=fopen("truncated-sample.xm","wb");assert(short_sample);
    /* Ends halfway through the second sample, beyond its valid header. */
    for(int j=0;j<336+9+80+9+263+120+512+512;++j){int c=fgetc(bad);assert(c!=EOF);assert(fputc(c,short_sample)!=EOF);}
    fclose(bad);fclose(short_sample);
    assert(!ts_tapehead_module_prepare("truncated-sample.xm",&staged,test_error,sizeof(test_error)));
    FILE *f=fopen("truncated.xm","wb");assert(f);fwrite("Extended Module: ",17,1,f);fclose(f);
    assert(!ts_tapehead_module_prepare("truncated.xm",&staged,test_error,sizeof(test_error)));
    assert(ts_sister_tracker_hash(&test_pages->tracker)==hash);
    xm_fixture("too-wide.xm",33,0);
    assert(!ts_tapehead_module_prepare("too-wide.xm",&staged,test_error,sizeof(test_error)));
    assert(strstr(test_error,"32 tracks") && ts_sister_tracker_hash(&test_pages->tracker)==hash);
    /* A new ordinary tile must never inherit imported envelopes or keymaps. */
    ts_sister_tracker_free(&test_pages->tracker);
    assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));
    assert(!instr[1]->volEnvFlags && !instr[1]->note2SampleLUT[60]);
    remove("fixture-32.xm");remove("roundtrip-32.xm");remove("fixture-8.xm");
    remove("truncated.xm");remove("truncated-sample.xm");remove("too-wide.xm");remove("xm-score.tst");
}
