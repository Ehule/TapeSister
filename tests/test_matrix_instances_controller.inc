static void matrix_instances_controller(const char *directory)
{
    MatrixWindow *w=&matrix_window;double energy[4];ts_tapehead_stop();ts_note_bank_clear(&a.notes);
    mx_reset_routes(0,&a,&u,&pages,&bank,1);assert(matrix_show(0,&a,&u,&pages,&bank,-1,-1));matrix_history_clear();
    matrix_click(1130,66,SDL_BUTTON_LEFT);assert(w->effect_menu);
    matrix_click(860,750,SDL_BUTTON_LEFT);assert(!w->effect_menu && w->effect_open==1);
    assert(w->controls.effect[0].type==TS_SISTER_FX_TRANSIENT && w->rows[w->selected].kind==4);
    assert(w->column_count==TS_MATRIX_SHARED_DESTINATIONS+2+a.speakers.count && w->destination==17);
    uint32_t first=w->effect_open;float old=w->controls.effect[0].a;
    matrix_wheel(90,755,3,0,0);assert(w->controls.effect[0].a>old);
    matrix_shot(directory,"matrix-transient-instance");
    render(energy);matrix_click(1130,66,SDL_BUTTON_LEFT);matrix_click(1060,750,SDL_BUTTON_LEFT);
    assert(w->effect_open!=first && w->controls.effect[1].type==TS_MATRIX_FX_COMPRESSOR);
    assert(w->column_count==TS_MATRIX_SHARED_DESTINATIONS+4+a.speakers.count);
    uint32_t second=w->effect_open;
    matrix_click(470,710,SDL_BUTTON_LEFT);assert(w->controls.effect[1].compressor.external);
    assert(w->controls.effect[0].a>old && !w->controls.effect[0].compressor.external);
    matrix_shot(directory,"matrix-compressor-instance");
    /* Leaving the effect panel exposes the ordinary route controls. */
    matrix_click(1150,710,SDL_BUTTON_LEFT);assert(!w->effect_open);
    int row=w->selected;matrix_click(80,MATRIX_Y+(row-w->scroll)*MATRIX_RH+12,SDL_BUTTON_LEFT);
    assert(w->effect_open==second);matrix_click(1150,710,SDL_BUTTON_LEFT);
    /* Move both added ports and their rows; identities stay attached. */
    w->destination=34;mx_reveal_column(34);
    int x=MATRIX_X+(mx_column_position(34)-w->column)*MATRIX_CW+20;
    matrix_click(x,140,SDL_BUTTON_LEFT);matrix_key(SDLK_LEFT,KMOD_SHIFT);
    assert(w->destination==34 && w->controls.effect[1].id==second);
    matrix_click(80,MATRIX_Y+(w->selected-w->scroll)*MATRIX_RH+12,SDL_BUTTON_LEFT);
    matrix_key(SDLK_UP,KMOD_SHIFT);assert(w->rows[w->selected].tile==second);
    /* Remove all source references, even hidden tracks, tiles and cards. */
    TsSourceRoute route={.matrix_enabled=1};route.matrix_gain[18]=1000;route.matrix_gain[34]=500;
    pages.tracker.lanes[TS_TRACKER_LANES-1].output_route=route;
    bank.bank[0].output_route=route;
    TsInstrument *other=ts_sample_pages_page_mut(&pages,&bank,ts_sample_pages_count(&pages)-1);
    assert(other);other->bank[0].output_route=route;
    if(w->mosaic)for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(w->mosaic->events[i].id)w->mosaic->events[i].output_route=route;
    render(energy);matrix_click(950,710,SDL_BUTTON_LEFT);
    assert(!w->controls.effect[1].id && !w->effect_open && w->column_count==TS_MATRIX_SHARED_DESTINATIONS+2+a.speakers.count);
    assert(!pages.tracker.lanes[TS_TRACKER_LANES-1].output_route.matrix_gain[34]);
    assert(!bank.bank[0].output_route.matrix_gain[18] && !other->bank[0].output_route.matrix_gain[34]);
    render(energy);matrix_key(SDLK_z,KMOD_CTRL);
    assert(w->controls.effect[1].id==second && bank.bank[0].output_route.matrix_gain[18]==1000);
    assert(other->bank[0].output_route.matrix_gain[34]==500);
    render(energy);matrix_key(SDLK_y,KMOD_CTRL);assert(!w->controls.effect[1].id);
    render(energy);matrix_click(1130,66,SDL_BUTTON_LEFT);matrix_click(1060,750,SDL_BUTTON_LEFT);
    assert(w->controls.effect[1].id>second && !bank.bank[0].output_route.matrix_gain[18]);
    /* Native audio callback sees the new graph, while the five-slot board is unchanged. */
    TsSisterFxControls pedalboard=a.sister.parameters.fx;
    w->controls.effect[0].a=w->controls.effect[0].b=.5f;
    w->controls.row[17].matrix_gain[TS_MATRIX_MAIN]=1000;mx_commit_controls(0,&a,&u);
    bank.bank[0].output_route=(TsSourceRoute){.matrix_enabled=1};bank.bank[0].output_route.matrix_gain[17]=1000;
    source_route_update_voices(&a,bank.bank[0].tile_id,bank.bank[0].output_route);
    assert(ts_note_bank_start(&a.notes,&bank,TS_AUDITION_CURRENT,12,0,48000)==TS_NOTE_STARTED);
    render(energy);render(energy);assert(energy[0]>0 && energy[1]>0);
    assert(!memcmp(&pedalboard,&a.sister.parameters.fx,sizeof(pedalboard)));
    ts_note_bank_clear(&a.notes);render(energy);
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(w->controls.effect[i].id){mx_effect_remove(0,&a,&u,&pages,&bank,i);render(energy);}
    mx_collect_effects(0,&a);assert(!ts_matrix_effects_memory(&a.sister.matrix_effects));
    /* A deliberately excessive 16-stage transient patch still reaches the
       actual callback's linked hardware guard, including its first sample. */
    TsMatrixControls chain=w->controls;
    for(int i=0;i<16;++i){assert(ts_matrix_effect_add(&chain,TS_SISTER_FX_TRANSIENT)==i);chain.effect[i].a=chain.effect[i].b=1;chain.row[17+i].matrix_gain[i==15?TS_MATRIX_MAIN:18+i]=1000;}
    assert(ts_matrix_effects_restore(&a.sister.matrix_effects,&chain,48000));assert(ts_matrix_set(&a.sister.matrix,&chain,48000));
    for(int i=0;i<8;++i)render(energy);
    bank.bank[0].output_route.matrix_gain[17]=1000;
    assert(ts_note_bank_start(&a.notes,&bank,TS_AUDITION_CURRENT,12,0,48000)==TS_NOTE_STARTED);
    float output[512*4],hardware_peak=0;float internal_peak=0;
    for(int block=0;block<8;++block) {
        audio_callback(&a,(Uint8 *)output,sizeof(output));
        for(int i=0;i<512*4;++i){assert(isfinite(output[i]) && fabsf(output[i])<=.981f);hardware_peak=fmaxf(hardware_peak,fabsf(output[i]));}
        internal_peak=fmaxf(internal_peak,a.sister.matrix.output_peak[32]);
    }
    assert(hardware_peak>0 && internal_peak>1);ts_note_bank_clear(&a.notes);
    for(int i=0;i<16;++i)ts_matrix_effect_remove(&chain,i);
    assert(ts_matrix_effects_restore(&a.sister.matrix_effects,&chain,48000));assert(ts_matrix_set(&a.sister.matrix,&chain,48000));
    w->controls=u.config.matrix=chain;matrix_history_clear();
    puts("Matrix instances UI: add, independent controls, sidechain ports, layout, remove hidden routes, undo/redo and native audio passed");
}
