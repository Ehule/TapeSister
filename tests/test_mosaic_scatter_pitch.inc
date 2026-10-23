static void test_mosaic_pitch_choices(void)
{
    TsMosaicPitchSettings p={.mode=TS_MOSAIC_PITCH_SPREAD_SCALE,.root=0,
        .scale=TS_FM_PITCH_SCALE_MINOR,.low_octave=4,.high_octave=4};
    int notes[128],intervals[5],voices,primary[]={79,60,67,63,72};
    int expected[]={60,62,63,65,67,68,70};
    assert(ts_mosaic_pitch_choices(&p,primary,5,notes,intervals,&voices)==7 && voices==1);
    assert(!memcmp(notes,expected,sizeof(expected)));
    p.mode=TS_MOSAIC_PITCH_CHORD;p.chord=TS_MOSAIC_CHORD_PRIMARY;p.high_octave=6;
    int roots[]={60,72},voicing[]={0,3,7,12,19};
    assert(ts_mosaic_pitch_choices(&p,primary,5,notes,intervals,&voices)==2 && voices==5);
    assert(!memcmp(notes,roots,sizeof(roots)) && !memcmp(intervals,voicing,sizeof(voicing)));
    p.high_octave=4;assert(!ts_mosaic_pitch_choices(&p,primary,5,notes,intervals,&voices));
    p.mode=TS_MOSAIC_PITCH_SPREAD_CHORD;
    int spread[]={60,63,67};
    assert(ts_mosaic_pitch_choices(&p,primary,5,notes,intervals,&voices)==3 && voices==1);
    assert(!memcmp(notes,spread,sizeof(spread)));
    p.low_octave=-1;p.high_octave=9;
    for(p.mode=1;p.mode<TS_MOSAIC_PITCH_MODE_COUNT;++p.mode)
        for(p.chord=0;p.chord<TS_MOSAIC_CHORD_COUNT;++p.chord)
            for(p.scale=0;p.scale<TS_FM_PITCH_SCALE_COUNT;++p.scale)
                for(p.root=0;p.root<12;++p.root) {
                    int count=ts_mosaic_pitch_choices(&p,primary,5,notes,intervals,&voices);
                    assert(count>0 && count<=128 && voices>=1 && voices<=5);
                    for(int i=0;i<count;++i) {
                        assert(notes[i]>=0 && notes[i]<=127 && (!i || notes[i]>notes[i-1]));
                        for(int n=0;n<voices;++n)assert(notes[i]+intervals[n]<=127);
                    }
                }
    p.mode=TS_MOSAIC_PITCH_CHORD;p.chord=TS_MOSAIC_CHORD_MAJOR;p.root=11;p.low_octave=p.high_octave=9;
    assert(!ts_mosaic_pitch_choices(&p,primary,5,notes,intervals,&voices));
}
static void scatter_pitch_snapshot(const char *variable)
{
    if(getenv(variable)) {
        static TsFramebuffer fb;ts_ui_render(&fb,&ui,&instrument);
        assert(ts_ui_write_ppm(&fb,getenv(variable)));
    }
}
static void test_mosaic_scatter_pitch(SDL_Window *window)
{
    test_mosaic_pitch_choices();
    TsMosaic *saved=ui.mosaic,*scene=ts_mosaic_create();assert(scene);
    ui.mosaic=audio.mosaic=scene;ui.mosaic_open=1;ui.mosaic_scroll=ui.mosaic_xscroll=0;
    ui.mosaic_scale=24;ui.mosaic_hscale=1;ui.mosaic_follow=0;
    char error[160];TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *a=ts_mosaic_add(scene,source,0,10),*b=ts_mosaic_add(scene,source,2,130);
    TsMosaicEvent *other=ts_mosaic_add(scene,source,3,250);assert(a && b && other);
    a->duration=b->duration=8;a->tuning.root_note=b->tuning.root_note=60;
    b->note_count=3;b->notes[0]=67;b->notes[1]=60;b->notes[2]=63;
    a->gain=.31f;a->pan=-.2f;a->fade_in=.2;a->fade_out=.4;
    TsMosaicEvent before_a=*a,before_b=*b,before_other=*other;
    mosaic_poll(0,&ui,&instrument,&mosaic);mosaic_picks_clear(&ui,&mosaic);
    mosaic_select_only(&ui,b->id);mosaic_select_add(&ui,a->id);
    uint64_t before=ts_mosaic_hash(scene);
    key(window,SDLK_r,KMOD_CTRL);assert(ui.mosaic_scatter_open && ui.mosaic_scatter_pitch.mode==TS_MOSAIC_PITCH_KEEP);
    assert(mosaic.scatter_pool[0].id==b->id && ui.mosaic_ghosts[0].notes[0]==67);
    assert(ui.mosaic_ghosts[1].note_count==1 && ts_mosaic_hash(scene)==before);
    mosaic_arrange_test_frame();
    click(window,410,105,1);assert(ui.mosaic_scatter_keep_positions);
    click(window,150,165,1);assert(ui.mosaic_scatter_pitch.mode==TS_MOSAIC_PITCH_CHORD);
    SDL_SetModState(KMOD_SHIFT);click(window,150,195,1);SDL_SetModState(KMOD_NONE);
    assert(ui.mosaic_scatter_pitch.chord==TS_MOSAIC_CHORD_PRIMARY);
    ui.mosaic_scatter_pitch.low_octave=ui.mosaic_scatter_pitch.high_octave=4;
    mosaic_scatter_preview(&ui,&mosaic);
    assert(ui.mosaic_scatter_valid && ui.mosaic_ghosts[0].note_count==3);
    assert(ui.mosaic_ghosts[0].notes[0]==60 && ui.mosaic_ghosts[0].notes[1]==63 && ui.mosaic_ghosts[0].notes[2]==67);
    assert(ui.mosaic_ghosts[0].start==2 && ui.mosaic_ghosts[1].x==10);
    scatter_pitch_snapshot("TS_SCATTER_CHORD_SCREENSHOT");
    /* Cancel does not mutate a card, add history or interrupt playback. */
    int history=scene->history_count;key(window,SDLK_ESCAPE,KMOD_NONE);
    assert(ts_mosaic_hash(scene)==before && scene->history_count==history);
    key(window,SDLK_r,KMOD_CTRL);click(window,410,105,1);
    ui.mosaic_scatter_pitch=(TsMosaicPitchSettings){.mode=TS_MOSAIC_PITCH_CHORD,.root=0,
        .chord=TS_MOSAIC_CHORD_MIN7,.low_octave=4,.high_octave=4};
    mosaic_scatter_preview(&ui,&mosaic);assert(ui.mosaic_scatter_valid);
    scene->playing=1;ts_mosaic_seek(scene,.5);
    ts_mosaic_begin_block(scene,512);ts_mosaic_read(scene,48000);double unity=scene->voices[0].step[0];assert(unity>0);
    click(window,150,342,1);assert(!ui.mosaic_scatter_open);
    assert(a->note_count==4 && a->notes[1]==63 && b->note_count==4);
    assert(a->start==before_a.start && a->x==before_a.x && b->start==before_b.start && b->x==before_b.x);
    assert(a->gain==before_a.gain && a->pan==before_a.pan && a->fade_in==before_a.fade_in && a->fade_out==before_a.fade_out);
    assert(!memcmp(&a->output_route,&before_a.output_route,sizeof(a->output_route)));
    assert(!memcmp(&a->tuning,&before_a.tuning,sizeof(a->tuning)) && !memcmp(other,&before_other,sizeof(*other)));
    ts_mosaic_begin_block(scene,512);ts_mosaic_read(scene,48000);
    assert(scene->voices[0].revision==a->revision && fabs(scene->voices[0].step[1]/unity-pow(2,3.0/12))<1e-9);
    assert(scene->playing && scene->time>.5);
    uint64_t applied=ts_mosaic_hash(scene);assert(scene->history_count==history+1);
    key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(scene)==before);
    key(window,SDLK_y,KMOD_CTRL);assert(ts_mosaic_hash(scene)==applied);
    /* Ordinary project format retains generated chords, tuning and routing. */
    const char *dir="test-scatter-pitch-project";
#ifdef _WIN32
    assert(!_mkdir(dir));
#else
    assert(!mkdir(dir,0700));
#endif
    TsMosaic *loaded=ts_mosaic_create();assert(loaded);
    assert(ts_mosaic_save(scene,dir,error,sizeof(error)) && ts_mosaic_load(loaded,dir,error,sizeof(error)));
    assert(ts_mosaic_hash(loaded)==applied);ts_mosaic_free(loaded);
    assert(!remove("test-scatter-pitch-project/mosaic.tsm"));assert(!remove("test-scatter-pitch-project/mosaic-000.wav"));
#ifdef _WIN32
    assert(!_rmdir(dir));
#else
    assert(!rmdir(dir));
#endif
    key(window,SDLK_z,KMOD_CTRL);scene->playing=0;
    mosaic_select_only(&ui,b->id);key(window,SDLK_r,KMOD_CTRL);
    click(window,260,105,1);assert(ui.mosaic_scatter_copies);
    click(window,140,105,1);key(window,SDLK_a,KMOD_CTRL);
    SDL_Event input;SDL_zero(input);input.type=SDL_TEXTINPUT;strcpy(input.text.text,"12");dispatch(&input,window);
    key(window,SDLK_RETURN,KMOD_NONE);assert(ui.mosaic_scatter_count==12);
    ui.mosaic_scatter_pitch=(TsMosaicPitchSettings){.mode=TS_MOSAIC_PITCH_SPREAD_CHORD,.root=0,
        .chord=TS_MOSAIC_CHORD_MINOR,.order=TS_MOSAIC_PITCH_SHUFFLED,.low_octave=4,.high_octave=4};
    mosaic.scatter_seed=120;mosaic.scatter_note_seed=240;mosaic_scatter_preview(&ui,&mosaic);
    for(int base=0;base<12;base+=3) {
        unsigned mask=0;for(int i=0;i<3;++i)mask|=1u<<(ui.mosaic_ghosts[base+i].notes[0]-60);
        assert(mask==((1u<<0)|(1u<<3)|(1u<<7)));
    }
    TsMosaicEvent preview[12];memcpy(preview,ui.mosaic_ghosts,sizeof(preview));
    click(window,150,310,1);assert(preview[0].start!=ui.mosaic_ghosts[0].start);
    for(int i=0;i<12;++i)assert(preview[i].notes[0]==ui.mosaic_ghosts[i].notes[0]);
    memcpy(preview,ui.mosaic_ghosts,sizeof(preview));click(window,350,310,1);
    int changed=0;for(int i=0;i<12;++i) {
        assert(preview[i].start==ui.mosaic_ghosts[i].start && preview[i].x==ui.mosaic_ghosts[i].x);
        changed+=preview[i].notes[0]!=ui.mosaic_ghosts[i].notes[0];
    }
    assert(changed && ts_mosaic_hash(scene)==before);
    scatter_pitch_snapshot("TS_SCATTER_PITCH_SCREENSHOT");
    memcpy(preview,ui.mosaic_ghosts,sizeof(preview));
    click(window,150,342,1);assert(mosaic_event_count(scene)==15);
    for(int i=0;i<12;++i) {
        const TsMosaicEvent *copy=&scene->events[3+i];
        assert(copy->start==preview[i].start && copy->x==preview[i].x && copy->source==source);
        assert(copy->note_count==1 && copy->notes[0]==preview[i].notes[0]);
    }
    key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(scene)==before);
    mosaic_select_only(&ui,b->id);key(window,SDLK_r,KMOD_CTRL);click(window,260,105,1);
    ui.mosaic_scatter_count=12;ui.mosaic_scatter_pitch.root=0;
    ui.mosaic_scatter_pitch.low_octave=ui.mosaic_scatter_pitch.high_octave=4;
    /* Ascending/descending uses stable selection/copy order, so position rolls
       never reassign notes. Scale choices include every degree, not just triads. */
    ui.mosaic_scatter_pitch.mode=TS_MOSAIC_PITCH_SPREAD_SCALE;ui.mosaic_scatter_pitch.scale=TS_FM_PITCH_SCALE_MINOR;
    ui.mosaic_scatter_pitch.order=TS_MOSAIC_PITCH_ASCENDING;mosaic_scatter_preview(&ui,&mosaic);
    int minor[]={60,62,63,65,67,68,70};for(int i=0;i<12;++i)assert(ui.mosaic_ghosts[i].notes[0]==minor[i%7]);
    ui.mosaic_scatter_pitch.order=TS_MOSAIC_PITCH_DESCENDING;mosaic_scatter_preview(&ui,&mosaic);
    for(int i=0;i<12;++i)assert(ui.mosaic_ghosts[i].notes[0]==minor[6-i%7]);
    /* Invalid ranges/capacity reject atomically and may be cancelled by mouse. */
    ui.mosaic_scatter_pitch.mode=TS_MOSAIC_PITCH_CHORD;ui.mosaic_scatter_pitch.root=11;
    ui.mosaic_scatter_pitch.low_octave=ui.mosaic_scatter_pitch.high_octave=9;
    click(window,150,342,1);assert(ui.mosaic_scatter_open && !ui.mosaic_scatter_valid && !ui.mosaic_ghost_count && ts_mosaic_hash(scene)==before);
    ui.mosaic_scatter_pitch.mode=TS_MOSAIC_PITCH_SPREAD_CHORD;
    ui.mosaic_scatter_pitch.low_octave=-1;ui.mosaic_scatter_pitch.high_octave=9;
    ui.mosaic_scatter_count=TS_MOSAIC_EVENTS;click(window,150,342,1);
    assert(!ui.mosaic_scatter_valid && ts_mosaic_hash(scene)==before);
    ui.mosaic_scatter_count=TS_MOSAIC_EVENTS-3;mosaic_scatter_preview(&ui,&mosaic);
    assert(ui.mosaic_scatter_valid && ui.mosaic_ghost_count==TS_MOSAIC_EVENTS-3);
    for(int i=0;i<ui.mosaic_ghost_count;++i)assert(ui.mosaic_ghosts[i].note_count==1 && ui.mosaic_ghosts[i].notes[0]<=127);
    click(window,150,342,1);assert(mosaic_event_count(scene)==TS_MOSAIC_EVENTS && scene->sources==source && !source->next);
    key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(scene)==before && mosaic_event_count(scene)==3);
    /* Selected sources also support pitches; none has a position to preserve. */
    mosaic_select_only(&ui,0);mosaic_poll(0,&ui,&instrument,&mosaic);
    SDL_SetModState(KMOD_SHIFT);click(window,20,85,1);SDL_SetModState(KMOD_NONE);
    key(window,SDLK_r,KMOD_CTRL);assert(ui.mosaic_scatter_copies && mosaic.scatter_from_sources);
    click(window,410,105,1);assert(!ui.mosaic_scatter_keep_positions);
    ui.mosaic_scatter_count=6;ui.mosaic_scatter_pitch.mode=TS_MOSAIC_PITCH_SPREAD_SCALE;
    mosaic_scatter_preview(&ui,&mosaic);assert(ui.mosaic_scatter_valid);
    click(window,150,342,1);assert(mosaic_event_count(scene)==9 && !mosaic.picked_count);
    key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(scene)==before);
    mosaic_drag_end(&ui,&mosaic);mosaic_picks_clear(&ui,&mosaic);
    ui.mosaic=audio.mosaic=saved;ts_mosaic_free(scene);mosaic_poll(0,&ui,&instrument,&mosaic);
}
