/* Real editor/controller plus FT2 playback. No mock source or replayer. */
static void pattern_update_snapshot(const char *path,int width,int height)
{
    SDL_SetWindowSize(test_window,width,height);
    SDL_Renderer *renderer=SDL_CreateRenderer(test_window,-1,SDL_RENDERER_SOFTWARE);assert(renderer);
    SDL_Texture *texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,640,400);assert(texture);
    TsFramebuffer *fb=malloc(sizeof(*fb));assert(fb);TsUiWaveformDetail details[2]={0};SDL_Texture *textures[2]={0};
    native_waveform_prepare(renderer,details,1);ts_audio_ui_render(fb,test_ui,test_bank);
    SDL_UpdateTexture(texture,NULL,fb->pixels,640*4);SDL_RenderCopy(renderer,texture,NULL,NULL);native_waveform_copy(renderer,textures,details,fb);
    SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_ARGB8888);assert(surface);
    assert(!SDL_RenderReadPixels(renderer,NULL,surface->format->format,surface->pixels,surface->pitch));assert(!SDL_SaveBMP(surface,path));SDL_FreeSurface(surface);
    SDL_DestroyTexture(texture);for(int i=0;i<2;++i){if(textures[i])SDL_DestroyTexture(textures[i]);ts_ui_waveform_detail_free(&details[i]);}
    SDL_DestroyRenderer(renderer);ts_ui_waveform_detail_begin(NULL);free(fb);SDL_SetWindowSize(test_window,640,400);
}
static void mosaic_pattern_updates(void)
{
    SDL_SetWindowSize(test_window,640,400);
    audit_blank_song(8);
    note_t *raw=pattern[editor.editPattern];assert(raw);
    raw[0]=(note_t){.note=49,.instr=1,.vol=0x50};
    /* Hidden rows still need their aliases when a snapshot is saved. */
    test_pages->tracker.aliases[128]=ts_tile_id_new();raw[200*32]=(note_t){.note=49,.instr=128};export_score();
    TsMosaic *m=ts_mosaic_create();assert(m);m->gain=1;
    static MosaicController c;memset(&c,0,sizeof(c));c.pages=test_pages;
    test_ui->mosaic=test_audio->mosaic=m;test_ui->mosaic_open=1;test_ui->tracker_open=0;
    test_ui->mosaic_pattern_tab=1;test_ui->mosaic_source_page=0;test_ui->mosaic_source_selected=-1;
    test_ui->mosaic_scroll=test_ui->mosaic_xscroll=0;test_ui->mosaic_scale=150;test_ui->mosaic_hscale=1;
    mosaic_poll(0,test_ui,test_bank,&c);
    TsMosaicSource *v1=c.pattern_sources[0];assert(v1 && v1->pattern->lineage && v1->pattern->version==1);
    TsMosaicEvent *a=ts_mosaic_add(m,v1,0,20),*b=ts_mosaic_add(m,v1,.125,150),*once=ts_mosaic_add(m,v1,0,280);
    assert(a && b && once);a->duration=b->duration=once->duration=3;once->looping=0;
    a->pan=-.2f;a->fade_in=.03;a->fade_out=.08;a->gain=.7;a->notes[0]=65;
    mosaic_select_only(test_ui,a->id);mosaic_select_add(test_ui,b->id);
    /* Opening by card remembers that owner even in a group selection. */
    assert(mosaic_enter(0,test_audio,test_ui,test_bank,&c,a->id));assert(c.pattern_owner==a->id);
    raw[0].note=56;export_score();test_ui->tracker_open=0;test_ui->mosaic_open=1;
    mosaic_poll(0,test_ui,test_bank,&c);
    TsMosaicSource *v2=c.pattern_sources[0];assert(v2 && v2!=v1 && v2->pattern->lineage==v1->pattern->lineage);
    assert(test_ui->mosaic_pattern_choice && test_ui->mosaic_pattern_owner==a->id && test_ui->mosaic_pattern_all==3);
    ui_sync_dialog_workspaces(test_ui);assert(test_ui->mosaic_open && !test_ui->tracker_open);
    assert(v1->pattern->older && !v2->pattern->older);
    TsFramebuffer *fb=malloc(sizeof(*fb));assert(fb);ts_ui_render(fb,test_ui,test_bank);ts_ui_render_mosaic_choice(fb,test_ui);
    assert(ts_ui_write_ppm(fb,"mosaic-pattern-update-dialog.ppm"));free(fb);
    pattern_update_snapshot("mosaic-pattern-update-dialog-native.bmp",1280,800);
    int history=m->history_count;TsMosaicEvent before=*a;
    static PortalController pc;static SisterWindow sw;static TransformController tc;
    SDL_Event choose;SDL_zero(choose);choose.type=SDL_MOUSEBUTTONDOWN;choose.button.button=SDL_BUTTON_LEFT;
    choose.button.x=180;choose.button.y=243;choose.button.windowID=SDL_GetWindowID(test_window);
    assert(mosaic_event(&choose,test_window,0,test_audio,test_ui,test_bank,&c,&pc,&sw,&tc,48000));assert(a->source==v2 && b->source==v1 && once->source==v1);
    assert(m->history_count==history+1 && a->revision==before.revision);
    before.source=v2;assert(!memcmp(a,&before,sizeof(before)));
    assert(ts_mosaic_undo(m,0) && a->source==v1);assert(ts_mosaic_undo(m,1) && a->source==v2);
    /* Keep dismisses this version, and manual U can review it again. */
    choose.button.button=SDL_BUTTON_RIGHT;choose.button.x=30;choose.button.y=94;
    assert(mosaic_event(&choose,test_window,0,test_audio,test_ui,test_bank,&c,&pc,&sw,&tc,48000));assert(test_ui->mosaic_pattern_choice);
    choose.type=SDL_KEYDOWN;choose.key.keysym.sym=SDLK_ESCAPE;choose.key.repeat=0;
    assert(mosaic_event(&choose,test_window,0,test_audio,test_ui,test_bank,&c,&pc,&sw,&tc,48000));mosaic_poll(0,test_ui,test_bank,&c);assert(!test_ui->mosaic_pattern_choice);
    c.pattern_ready=0;mosaic_poll(0,test_ui,test_bank,&c);assert(!test_ui->mosaic_pattern_choice);
    /* Return without editing, cursor movement and unused instrument metadata
       do not create another generation. */
    assert(mosaic_enter(0,test_audio,test_ui,test_bank,&c,b->id));editor.row=2;
    test_pages->tracker.instruments[127].panning[0]=12;export_score();
    test_ui->tracker_open=0;test_ui->mosaic_open=1;mosaic_poll(0,test_ui,test_bank,&c);
    assert(c.pattern_sources[0]==v2 && !test_ui->mosaic_pattern_choice);
    /* Direct editor edits offer selected matching cards. */
    c.pattern_owner=0;raw[0].note=61;patternNumRows[editor.editPattern]=12;export_score();c.pattern_ready=0;mosaic_poll(0,test_ui,test_bank,&c);
    TsMosaicSource *v3=c.pattern_sources[0];assert(v3!=v2 && test_ui->mosaic_pattern_choice);
    assert(!test_ui->mosaic_pattern_owner && test_ui->mosaic_pattern_selected==2);
    mosaic_pattern_resolve(0,test_ui,&c,0);
    /* Live update: independent boundaries, stable phase until each repeat,
       one-shot remains on its old snapshot. New length applies at boundary. */
    assert(ts_mosaic_save(m,".",test_error,sizeof(test_error)));TsMosaic *reference=ts_mosaic_create();
    assert(ts_mosaic_load(reference,".",test_error,sizeof(test_error)));reference->gain=m->gain;
    static MosaicController reopened;memset(&reopened,0,sizeof(reopened));reopened.pages=test_pages;
    test_ui->mosaic=reference;mosaic_poll(0,test_ui,test_bank,&reopened);
    assert(!test_ui->mosaic_pattern_choice && reference->events[0].source->pattern->older);
    test_ui->mosaic=m;mosaic_sources_refresh(test_ui,test_bank,&c);mosaic_select_only(test_ui,a->id);mosaic_select_add(test_ui,b->id);
    mosaic_controller_free(&reopened);
    TsStereoFrame warmup[7200];pattern_render(reference,warmup,7200,0);pattern_render(m,warmup,7200,0);
    uint64_t generation=m->voices[0].generation;double old_origin=m->voices[0].pattern_origin;
    mosaic_pattern_notices(test_ui,test_bank,&c,1);assert(test_ui->mosaic_pattern_choice);
    history=m->history_count;mosaic_pattern_resolve(0,test_ui,&c,2);
    assert(a->source==v3 && b->source==v3 && once->source==v3 && m->history_count==history+1);
    assert(m->voices[0].pattern_source==v2 && m->voices[1].pattern_source==v1 && m->voices[2].pattern_source==v1);
    for(int i=0;i<100;++i){TsStereoFrame expected=ts_mosaic_read(reference,48000),actual=ts_mosaic_read(m,48000);
        assert(fabs(expected.l-actual.l)<1e-6 && fabs(expected.r-actual.r)<1e-6);}
    ts_mosaic_free(reference);
    assert(m->voices[0].generation==generation && m->voices[0].pattern_origin==old_origin);
    double boundary=m->voices[0].pattern_switch;
    while(m->time<boundary+.001)ts_mosaic_read(m,48000);
    assert(m->voices[0].pattern_source==v3 && fabs(m->voices[0].pattern_origin-boundary)<1e-9);
    assert(m->voices[1].pattern_source==v1 && m->voices[2].pattern_source==v1);
    while(m->time<boundary+.13)ts_mosaic_read(m,48000);
    assert(m->voices[1].pattern_source==v3 && m->voices[2].pattern_source==v1);
    mosaic_poll(0,test_ui,test_bank,&c);assert(!test_ui->mosaic_pattern_pending[0] && test_ui->mosaic_pattern_pending[2]);
    /* Persistence records the chosen version, not transient sounding state. */
    assert(ts_mosaic_save(m,".",test_error,sizeof(test_error)));TsMosaic *loaded=ts_mosaic_create();
    assert(ts_mosaic_load(loaded,".",test_error,sizeof(test_error)));
    assert(loaded->events[2].source->pattern->lineage==v3->pattern->lineage && loaded->events[2].source->pattern->version==v3->pattern->version);
    assert(loaded->events[0].source->pattern->acknowledged==v3->hash);
    assert(loaded->events[0].fade_in==a->fade_in && loaded->events[0].pan==a->pan);
    ts_mosaic_free(loaded);
    ts_mosaic_seek(m,.25);ts_mosaic_read(m,48000);assert(m->voices[2].pattern_source==v3);
    assert(ts_mosaic_undo(m,0) && a->source==v2 && b->source==v1);assert(ts_mosaic_undo(m,1) && a->source==v3);
    /* A source can change again while an earlier update is pending. */
    pattern_render(m,warmup,7200,0);ts_mosaic_update_pattern(m,a,v1);ts_mosaic_update_pattern(m,a,v2);
    assert(m->pattern_retired[0]==v3);ts_mosaic_collect(m);
    m->playing=0;ts_mosaic_pattern_retire(m);assert(!m->pattern_retired[0] && !m->voices[0].active);
    m->playing=1;ts_mosaic_read(m,48000);assert(m->voices[0].pattern_source==v2);
    /* Explicit version marker and sidebar badge in the actual renderer. */
    m->playing=0;ts_mosaic_pattern_retire(m);mosaic_poll(0,test_ui,test_bank,&c);snapshot("mosaic-pattern-update-cards.bmp");
    pattern_update_snapshot("mosaic-pattern-update-cards-fractional.bmp",1729,973);
    clock_t cpu=clock();
    for(int pass=0;pass<20;++pass){c.pattern_ready=0;mosaic_poll(0,test_ui,test_bank,&c);}
    fprintf(stderr,"Pattern review: %.3f ms CPU per unchanged workspace return (one source, three cards)\n",1000.0*(clock()-cpu)/CLOCKS_PER_SEC/20);
    cpu=clock();for(int pass=0;pass<10000;++pass)mosaic_poll(0,test_ui,test_bank,&c);
    fprintf(stderr,"Pattern review idle: %.4f ms CPU per poll\n",1000.0*(clock()-cpu)/CLOCKS_PER_SEC/10000);
    uint64_t lineage=test_pages->tracker.source_id;
    assert(ts_sister_tracker_save_file(&test_pages->tracker,"source-lineage.score",test_error,sizeof(test_error)));
    TsSisterTracker restored;ts_sister_tracker_init(&restored);
    assert(ts_sister_tracker_load_file(&restored,"source-lineage.score",test_error,sizeof(test_error)) && restored.source_id==lineage);ts_sister_tracker_free(&restored);
    /* Different module with identical numeric IDs cannot bind old snapshots. */
    test_pages->tracker.source_id=ts_tile_id_new();assert(!mosaic_pattern_editor(0,test_ui,&c,v1->pattern));test_pages->tracker.source_id=lineage;
    test_pages->tracker.source_id=0;v1->pattern->lineage=0;ts_pattern_link_legacy(m,&test_pages->tracker);
    assert(test_pages->tracker.source_id && v1->pattern->lineage==test_pages->tracker.source_id);
    mosaic_controller_free(&c);ts_mosaic_free(m);test_ui->mosaic=test_audio->mosaic=NULL;test_ui->mosaic_pattern_choice=0;
    fprintf(stderr,"Mosaic pattern update workflow passed\n");
}
