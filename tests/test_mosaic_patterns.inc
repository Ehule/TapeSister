#include <time.h>
/* Exercises the real embedded FT2 effects/mixer, not the legacy native fixture. */
static void pattern_render(TsMosaic *m,TsStereoFrame *out,int frames,double start)
{
    m->playing=m->was_playing=0;m->last_output=(TsStereoFrame){0};m->rate=48000;
    ts_mosaic_seek(m,start);m->playing=1;
    for(int i=0;i<frames;++i){if(!(i%256))ts_mosaic_begin_block(m,(unsigned)(frames-i<256?frames-i:256));out[i]=ts_mosaic_read(m,48000);}
}
static double pattern_difference(const TsStereoFrame *a,const TsStereoFrame *b,int n)
{
    double d=0;for(int i=0;i<n;++i)d=fmax(d,fmax(fabs(a[i].l-b[i].l),fabs(a[i].r-b[i].r)));return d;
}

#include "test_mosaic_pattern_updates.inc"
static void mosaic_patterns(void)
{
    mosaic_pattern_updates();
    audit_blank_song(16);
    for(size_t i=0;i<test_bank->current.frames;++i){test_bank->current.data[2*i]=.3f*sinf(i*.043f);test_bank->current.data[2*i+1]=.2f*cosf(i*.037f);}
    assert(ts_instrument_sync_selected(test_bank,test_error,sizeof(test_error)));
    assert(ts_tapehead_refresh(test_pages,test_bank,48000,test_error,sizeof(test_error)));
    note_t *raw=pattern[editor.editPattern];assert(raw);
    raw[0]=(note_t){.note=49,.instr=1,.vol=0x50,.efx=0x08,.efxData=48};
    raw[32*4]=(note_t){.note=56,.instr=1,.efx=0x0a,.efxData=1};
    raw[1]=(note_t){.note=61,.instr=1,.efx=0x08,.efxData=208};
    raw[32*8+1]=(note_t){.note=97};export_score();
    TsMosaic *m=ts_mosaic_create();assert(m && m->pattern_engine);m->gain=1;
    TsMosaicSource *source=ts_pattern_source(m,test_pages,test_bank,pat()->id,test_error,sizeof(test_error));
    assert(source && source->pattern && source->pattern->prepared && source->pattern->asset_count==1);
    assert(ts_pattern_source(m,test_pages,test_bank,pat()->id,test_error,sizeof(test_error))==source);
    /* Card cycle follows explicit LEN even when shorter than physical rows. */
    TsSisterTracker *score=&test_pages->tracker;
    int old_bypass=score->length_bypass,old_control=score->control_lane;
    unsigned old_length0=score->lanes[0].length,old_length1=score->lanes[1].length;
    score->length_bypass=0;score->control_lane=-1;score->lanes[0].length=8;score->lanes[1].length=12;
    TsMosaicSource *length_source=ts_pattern_source(m,test_pages,test_bank,pat()->id,test_error,sizeof(test_error));assert(length_source);
    assert(fabs(length_source->pattern->duration-12*score->ticks_per_line*2.5/score->bpm)<1e-9);
    score->control_lane=0;length_source=ts_pattern_source(m,test_pages,test_bank,pat()->id,test_error,sizeof(test_error));assert(length_source);
    assert(fabs(length_source->pattern->duration-8*score->ticks_per_line*2.5/score->bpm)<1e-9);
    score->length_bypass=old_bypass;score->control_lane=old_control;score->lanes[0].length=old_length0;score->lanes[1].length=old_length1;
    TsMosaicEvent *a=ts_mosaic_add(m,source,0,20);assert(a);a->duration=1.2;
    const int n=57600;TsStereoFrame *single=calloc(n,sizeof(*single)),*mixed=calloc(n,sizeof(*mixed)),*other=calloc(n,sizeof(*other));assert(single&&mixed&&other);
    pattern_render(m,single,n,0);double energy=0;for(int i=0;i<n;++i)energy+=fabs(single[i].l)+fabs(single[i].r);assert(energy>100);
    /* Two copies share definitions, but never playback state. */
    TsMosaicEvent *b=ts_mosaic_add(m,source,0,140);assert(b);b->duration=a->duration;
    pattern_render(m,mixed,n,0);for(int i=0;i<n;++i){if(fabs(mixed[i].l-2*single[i].l)>=1e-5){fprintf(stderr,"overlap mismatch frame %d single %.9g mixed %.9g state ids %llu/%llu gen %llu/%llu\n",i,single[i].l,mixed[i].l,(unsigned long long)m->voices[0].id,(unsigned long long)m->voices[1].id,(unsigned long long)m->voices[0].generation,(unsigned long long)m->voices[1].generation);assert(0);}assert(fabs(mixed[i].r-2*single[i].r)<1e-5);}
    b->start=.137;b->duration=.9;
    pattern_render(m,mixed,n,0);a->muted=1;pattern_render(m,other,n,0);a->muted=0;
    for(int i=300;i<n;++i){assert(fabs(mixed[i].l-single[i].l-other[i].l)<1e-5);assert(fabs(mixed[i].r-single[i].r-other[i].r)<1e-5);}
    ts_mosaic_delete(m,b->id);
    /* Seek reconstructs effects and sample phase; the standard seek attack
       intentionally differs for the first 2ms. */
    pattern_render(m,other,6000,.197);double difference=pattern_difference(single+9456+300,other+300,5000);
    fprintf(stderr,"pattern seek peak error %.9g\n",difference);assert(difference<.002);
    pattern_render(m,other,n,0);assert(pattern_difference(single,other,n)<1e-6);
    /* Live fades and balance edit only the combined output, without reset. */
    ts_mosaic_seek(m,0);m->playing=1;for(int i=0;i<5000;++i)ts_mosaic_read(m,48000);
    uint64_t generation=m->voices[0].generation;a->pan=.75f;a->fade_in=.1;a->fade_out=.2;a->gain=.5;
    for(int i=0;i<1000;++i)ts_mosaic_read(m,48000);assert(m->voices[0].generation==generation);
    a->pan=0;a->fade_in=a->fade_out=0;a->gain=1;
    m->playing=0;for(int i=0;i<1000;++i){TsStereoFrame f=ts_mosaic_read(m,48000);if(i>500)assert(fabs(f.l)+fabs(f.r)<1e-9);}

    /* Fractional transport speed must interpolate the running voice, never
       reset it between adjacent fractional sample positions. */
    ts_mosaic_set_speed(m,.5);pattern_render(m,other,6000,0);
    for(int i=500;i<6000;++i){float expected=(single[i/2].l+single[(i+1)/2].l)*.5f;assert(fabs(other[i].l-expected)<.002);}
    ts_mosaic_set_speed(m,1);
    uint64_t source_hash=source->hash;a->notes[0]=72;pattern_render(m,other,6000,0);
    assert(pattern_difference(single+500,other+500,5000)>.01 && source->hash==source_hash);a->notes[0]=60;
    a->output_route.matrix_enabled=1;a->output_route.matrix_gain[3]=1000;
    ts_mosaic_seek(m,0);m->playing=1;
    for(int i=0;i<6000;++i){if(!(i%256))ts_mosaic_begin_block(m,256);TsSourceRouteMix sends={0};TsStereoFrame main=ts_mosaic_read_routed(m,48000,&sends);
        if(i>500){assert(fabs(main.l)<1e-6);assert(fabs(sends.graph[3].l-single[i].l)<1e-5);}}
    a->output_route=(TsSourceRoute){0};
    /* Transport commands inside a snapshot cannot jump the song, stop a
       sibling, or change the host tempo/FasTracks switch. */
    raw[32*2+2]=(note_t){.efx=0x0f,.efxData=0xff};raw[32*2+3]=(note_t){.efx=0x0b,.efxData=1};
    raw[32*2+4]=(note_t){.efx=0x0d,.efxData=0};raw[32*2+5]=(note_t){.efx=0x0e,.efxData=0x6f};raw[32*2+6]=(note_t){.efx=0x23,.efxData=0x20};export_score();
    TsMosaicSource *commands=ts_pattern_source(m,test_pages,test_bank,pat()->id,test_error,sizeof(test_error));assert(commands);
    bool master_enabled=fastTracksPOCMasterIsEnabled();a->source=commands;++a->revision;pattern_render(m,other,n,0);
    assert(pattern_difference(single,other,n)<1e-6 && fastTracksPOCMasterIsEnabled()==master_enabled);
    for(int lane=2;lane<=6;++lane)raw[32*2+lane]=(note_t){0};a->source=source;++a->revision;export_score();
    /* Source edits create a new generation; old cards and undo retain audio. */
    raw[0].note=61;export_score();TsMosaicSource *changed=ts_pattern_source(m,test_pages,test_bank,pat()->id,test_error,sizeof(test_error));
    assert(changed && changed!=source && changed->pattern->assets[0].audio==source->pattern->assets[0].audio);
    pattern_render(m,other,n,0);assert(pattern_difference(single,other,n)<1e-6);
    ts_mosaic_checkpoint(m);a->source=changed;++a->revision;
    assert(ts_mosaic_undo(m,0));assert(m->events[0].source==source);assert(ts_mosaic_undo(m,1));assert(m->events[0].source==changed);
    assert(ts_mosaic_undo(m,0));a=&m->events[0];
    /* Ordinary audio cards mix alongside snapshots. */
    b=ts_mosaic_add(m,source->pattern->assets[0].audio,.1,250);assert(b);b->duration=.2;
    pattern_render(m,mixed,n,0);a->muted=1;pattern_render(m,other,n,0);a->muted=0;
    for(int i=300;i<n;++i)assert(fabs(mixed[i].l-single[i].l-other[i].l)<1e-5);
    ts_mosaic_delete(m,b->id);
    /* Repeat resets all effect memory; one-shot releases at the source end.
       The card boundary always gates remaining tails. */
    a->looping=1;pattern_render(m,mixed,n,0);assert(pattern_difference(single,mixed,n)<1e-6);
    a->looping=0;pattern_render(m,other,n,0);double tail=0;for(int i=30000;i<n;++i)tail+=fabs(other[i].l)+fabs(other[i].r);assert(tail<.01);a->looping=1;
    /* Foreground tracker state is bit-identical around interleaved card work. */
    assert(ts_tapehead_transport_locked(2));assert(ts_tapehead_transport_locked(1));
    ts_tapehead_render(256,48000);song_t saved_song=song;channel_t saved_channels[MAX_CHANNELS];memcpy(saved_channels,channel,sizeof(saved_channels));
    for(int i=0;i<1500;++i)ts_mosaic_read(m,48000);
    assert(!memcmp(&song,&saved_song,sizeof(song)));assert(!memcmp(channel,saved_channels,sizeof(saved_channels)));assert(ts_tapehead_running());ts_tapehead_stop();
    /* Real project round trip includes snapshot score, raw XM volume effects,
       PCM assets, routing and all ordinary instance settings. */
    a->pan=-.25;a->fade_in=.03;a->fade_out=.07;a->x=4000;a->width=130;
    assert(ts_mosaic_save(m,".",test_error,sizeof(test_error)));
    TsMosaic *loaded=ts_mosaic_create();
    assert(ts_mosaic_load(loaded,".",test_error,sizeof(test_error)));
    assert(loaded->events[0].source->hash==source->hash);
    assert(loaded->events[0].source->pattern && loaded->events[0].pan==a->pan && loaded->events[0].fade_out==a->fade_out && loaded->events[0].x==4000);
    pattern_render(m,mixed,n,0);pattern_render(loaded,other,n,0);assert(pattern_difference(mixed,other,n)<1e-6);ts_mosaic_free(loaded);
    /* UI uses the actual source browser, drag model and native renderer. */
    static MosaicController mc;static PortalController pc;static SisterWindow sw;static TransformController tc;
    mc.pages=test_pages;test_ui->mosaic=test_audio->mosaic=m;test_ui->mosaic_open=1;test_ui->tracker_open=0;test_ui->mosaic_pattern_tab=0;
    test_ui->mosaic_scale=200;test_ui->mosaic_hscale=1;test_ui->mosaic_scroll=test_ui->mosaic_xscroll=0;m->playing=0;a->x=20;
    mosaic_poll(0,test_ui,test_bank,&mc);
    if(test_ui->mosaic_pattern_choice)mosaic_pattern_resolve(0,test_ui,&mc,0);
    SDL_Event ev;
    /* Click the visible tabs through the full dispatcher. The transport row
       used to consume their center/bottom before the tab handler ran. */
    const int sizes[][2]={{640,400},{1280,800},{1729,973}};
    const int tab_y[]={37,38,44,51};
    for(unsigned scale=0;scale<3;++scale){
        int width=sizes[scale][0],height=sizes[scale][1];SDL_SetWindowSize(test_window,width,height);
        for(unsigned pos=0;pos<4;++pos)for(int tab=1;tab>=0;--tab){
            SDL_zero(ev);ev.type=SDL_MOUSEBUTTONDOWN;ev.button.windowID=SDL_GetWindowID(test_window);
            ev.button.button=SDL_BUTTON_LEFT;ev.button.clicks=1;
            ev.button.x=((tab?82:32)*width+639)/640;ev.button.y=(tab_y[pos]*height+399)/400;
            assert(mosaic_event(&ev,test_window,0,test_audio,test_ui,test_bank,&mc,&pc,&sw,&tc,48000));
            assert(test_ui->mosaic_pattern_tab==tab && !m->playing);
            assert(test_ui->mosaic_source_count && test_ui->mosaic_sources[0].sample);
            if(tab)assert(test_ui->mosaic_sources[0].source && test_ui->mosaic_sources[0].source->pattern);
            else assert(!test_ui->mosaic_sources[0].source || !test_ui->mosaic_sources[0].source->pattern);
            ev.type=SDL_MOUSEBUTTONUP;mosaic_event(&ev,test_window,0,test_audio,test_ui,test_bank,&mc,&pc,&sw,&tc,48000);
        }
    }
    SDL_SetWindowSize(test_window,640,400);
    SDL_zero(ev);ev.type=SDL_MOUSEBUTTONDOWN;ev.button.windowID=SDL_GetWindowID(test_window);
    ev.button.button=SDL_BUTTON_LEFT;ev.button.clicks=1;ev.button.x=82;ev.button.y=44;
    assert(mosaic_event(&ev,test_window,0,test_audio,test_ui,test_bank,&mc,&pc,&sw,&tc,48000));assert(test_ui->mosaic_pattern_tab);
    ev.type=SDL_MOUSEBUTTONUP;mosaic_event(&ev,test_window,0,test_audio,test_ui,test_bank,&mc,&pc,&sw,&tc,48000);
    /* Select a real source, audition with A, then drag/release it into Mosaic. */
    ev.type=SDL_MOUSEBUTTONDOWN;ev.button.x=32;ev.button.y=86;
    assert(mosaic_event(&ev,test_window,0,test_audio,test_ui,test_bank,&mc,&pc,&sw,&tc,48000));assert(mc.drag==3);
    ev.type=SDL_MOUSEBUTTONUP;mosaic_event(&ev,test_window,0,test_audio,test_ui,test_bank,&mc,&pc,&sw,&tc,48000);
    SDL_zero(ev);ev.type=SDL_KEYDOWN;ev.key.windowID=SDL_GetWindowID(test_window);ev.key.keysym.sym=SDLK_a;
    assert(mosaic_event(&ev,test_window,0,test_audio,test_ui,test_bank,&mc,&pc,&sw,&tc,48000));assert(m->audition_id);
    double preview_power=0;for(int i=0;i<1000;++i){TsStereoFrame f=ts_mosaic_read(m,48000);preview_power+=fabs(f.l)+fabs(f.r);}assert(preview_power>1);ts_mosaic_audition(m,0);
    SDL_zero(ev);ev.type=SDL_MOUSEBUTTONDOWN;ev.button.windowID=SDL_GetWindowID(test_window);ev.button.button=SDL_BUTTON_LEFT;ev.button.clicks=1;ev.button.x=32;ev.button.y=86;
    assert(mosaic_event(&ev,test_window,0,test_audio,test_ui,test_bank,&mc,&pc,&sw,&tc,48000));assert(mc.drag==3);
    ev.type=SDL_MOUSEMOTION;ev.motion.windowID=SDL_GetWindowID(test_window);ev.motion.x=450;ev.motion.y=166;
    assert(mosaic_event(&ev,test_window,0,test_audio,test_ui,test_bank,&mc,&pc,&sw,&tc,48000));assert(test_ui->mosaic_ghost_count==1);
    SDL_zero(ev);ev.type=SDL_MOUSEBUTTONUP;ev.button.windowID=SDL_GetWindowID(test_window);ev.button.button=SDL_BUTTON_LEFT;ev.button.x=450;ev.button.y=166;
    assert(mosaic_event(&ev,test_window,0,test_audio,test_ui,test_bank,&mc,&pc,&sw,&tc,48000));assert(!mc.drag);
    TsMosaicEvent *dropped=ts_mosaic_find(m,test_ui->mosaic_selected);assert(dropped && dropped->source->pattern && dropped->notes[0]==60 && dropped->start==.5);
    dropped->solo=1;pattern_render(m,other,n,0);double dropped_power=0;for(int i=0;i<n;++i)dropped_power+=fabs(other[i].l)+fabs(other[i].r);assert(dropped_power>10);
    SDL_zero(ev);ev.type=SDL_KEYDOWN;ev.key.windowID=SDL_GetWindowID(test_window);ev.key.keysym.sym=SDLK_DELETE;
    assert(mosaic_event(&ev,test_window,0,test_audio,test_ui,test_bank,&mc,&pc,&sw,&tc,48000));assert(!dropped->id);m->playing=0;
    mosaic_select_only(test_ui,a->id);double hx,hy;assert(ts_ui_mosaic_fade_handle(test_ui,a,1,&hx,&hy));
    SDL_zero(ev);ev.type=SDL_MOUSEBUTTONDOWN;ev.button.windowID=SDL_GetWindowID(test_window);ev.button.button=SDL_BUTTON_LEFT;ev.button.clicks=1;ev.button.x=round(hx);ev.button.y=round(hy);
    assert(mosaic_event(&ev,test_window,0,test_audio,test_ui,test_bank,&mc,&pc,&sw,&tc,48000));assert(mc.drag==7);
    ev.type=SDL_MOUSEMOTION;ev.motion.windowID=SDL_GetWindowID(test_window);ev.motion.x=round(hx);ev.motion.y=round(hy)+20;
    mosaic_event(&ev,test_window,0,test_audio,test_ui,test_bank,&mc,&pc,&sw,&tc,48000);assert(a->fade_in>.12);
    ev.type=SDL_MOUSEBUTTONUP;ev.button.windowID=SDL_GetWindowID(test_window);ev.button.button=SDL_BUTTON_LEFT;ev.button.x=round(hx);ev.button.y=round(hy)+20;
    mosaic_event(&ev,test_window,0,test_audio,test_ui,test_bank,&mc,&pc,&sw,&tc,48000);assert(!mc.drag);
    double span;assert(ts_ui_mosaic_pan_handle(test_ui,a,&hx,&hy,&span));float opening_pan=a->pan;
    ev.type=SDL_MOUSEBUTTONDOWN;ev.button.windowID=SDL_GetWindowID(test_window);ev.button.button=SDL_BUTTON_LEFT;ev.button.clicks=1;ev.button.x=round(hx);ev.button.y=round(hy);
    mosaic_event(&ev,test_window,0,test_audio,test_ui,test_bank,&mc,&pc,&sw,&tc,48000);assert(mc.drag==9);
    ev.type=SDL_MOUSEMOTION;ev.motion.windowID=SDL_GetWindowID(test_window);ev.motion.x=round(hx)+20;ev.motion.y=round(hy);
    mosaic_event(&ev,test_window,0,test_audio,test_ui,test_bank,&mc,&pc,&sw,&tc,48000);assert(a->pan>opening_pan);
    SDL_zero(ev);ev.type=SDL_KEYDOWN;ev.key.windowID=SDL_GetWindowID(test_window);ev.key.keysym.sym=SDLK_ESCAPE;
    mosaic_event(&ev,test_window,0,test_audio,test_ui,test_bank,&mc,&pc,&sw,&tc,48000);assert(a->pan==opening_pan && !mc.drag);
    b=ts_mosaic_add(m,source,.15,160);b->duration=.9;b->fade_out=.2;b->pan=.5;b->notes[0]=67;
    TsMosaicEvent *wave=ts_mosaic_add(m,source->pattern->assets[0].audio,.3,300);wave->duration=.6;wave->fade_in=.1;wave->fade_out=.1;
    /* Group bottom-edge resizing changes each instance boundary without
       restarting its playhead; Alt equalizes audio and pattern card lengths. */
    mosaic_select_only(test_ui,a->id);mosaic_select_add(test_ui,b->id);mosaic_select_add(test_ui,wave->id);
    uint64_t resize_before=ts_mosaic_hash(m);double duration_a=a->duration,duration_b=b->duration,duration_wave=wave->duration;
    pattern_render(m,other,6000,0);uint64_t resize_generation=m->voices[0].generation;
    SDL_zero(ev);ev.type=SDL_MOUSEBUTTONDOWN;ev.button.windowID=SDL_GetWindowID(test_window);
    ev.button.button=SDL_BUTTON_LEFT;ev.button.clicks=1;ev.button.x=190;
    int resize_y=(int)floor(66+(a->start+a->duration)*test_ui->mosaic_scale)-2;ev.button.y=resize_y;
    assert(mosaic_event(&ev,test_window,0,test_audio,test_ui,test_bank,&mc,&pc,&sw,&tc,48000) && mc.drag==2);
    SDL_zero(ev);ev.type=SDL_MOUSEMOTION;ev.motion.windowID=SDL_GetWindowID(test_window);ev.motion.x=190;ev.motion.y=resize_y+20;
    assert(mosaic_event(&ev,test_window,0,test_audio,test_ui,test_bank,&mc,&pc,&sw,&tc,48000));
    assert(fabs(a->duration-duration_a-.1)<1e-9 && fabs(b->duration-duration_b-.1)<1e-9 && fabs(wave->duration-duration_wave-.1)<1e-9);
    SDL_zero(ev);ev.type=SDL_KEYDOWN;ev.key.keysym.sym=SDLK_LALT;ev.key.keysym.mod=KMOD_ALT;
    assert(mosaic_event(&ev,test_window,0,test_audio,test_ui,test_bank,&mc,&pc,&sw,&tc,48000));
    assert(a->duration==b->duration && a->duration==wave->duration);
    ts_mosaic_begin_block(m,256);ts_mosaic_read(m,48000);assert(m->voices[0].generation==resize_generation);
    SDL_zero(ev);ev.type=SDL_MOUSEBUTTONUP;ev.button.button=SDL_BUTTON_LEFT;ev.button.windowID=SDL_GetWindowID(test_window);ev.button.x=190;ev.button.y=resize_y+20;
    assert(mosaic_event(&ev,test_window,0,test_audio,test_ui,test_bank,&mc,&pc,&sw,&tc,48000));
    assert(ts_mosaic_undo(m,0) && ts_mosaic_hash(m)==resize_before);
    /* Scatter mix applies to both kinds of card without resetting live pattern
       state. Copies reserve enough engines before publishing the whole batch. */
    mosaic_select_only(test_ui,a->id);mosaic_select_add(test_ui,b->id);mosaic_select_add(test_ui,wave->id);
    pattern_render(m,other,6000,0);uint64_t live_generation=m->voices[0].generation;
    double live_time=m->time;uint64_t before_mix=ts_mosaic_hash(m);int mix_history=m->history_cursor;
    mosaic_scatter_open(test_ui,&mc);
    SDL_zero(ev);ev.type=SDL_MOUSEBUTTONDOWN;ev.button.button=SDL_BUTTON_LEFT;
    mosaic_scatter_event(&ev,test_window,0,test_ui,&mc,430,45,KMOD_NONE);
    assert(test_ui->mosaic_scatter_keep_positions);
    for(int control=0;control<3;++control) {
        test_ui->mosaic_scatter_mix[control].mode=1;
        test_ui->mosaic_scatter_mix[control].low=control==2?-60:.15;
    }
    mosaic_scatter_apply(0,test_ui,&mc);
    assert(!test_ui->mosaic_scatter_open && m->history_cursor==mix_history+1);
    assert(a->pan==-.6f && b->fade_in==.15 && wave->fade_out==.15);
    ts_mosaic_begin_block(m,256);ts_mosaic_read(m,48000);
    assert(m->voices[0].generation==live_generation && m->time>live_time);
    assert(ts_mosaic_undo(m,0) && ts_mosaic_hash(m)==before_mix);
    assert(ts_mosaic_undo(m,1));
    mosaic_select_only(test_ui,a->id);mosaic_scatter_open(test_ui,&mc);
    test_ui->mosaic_scatter_copies=1;test_ui->mosaic_scatter_count=24;
    test_ui->mosaic_scatter_start=0;test_ui->mosaic_scatter_end=.001;test_ui->mosaic_scatter_overlap=1;
    test_ui->mosaic_scatter_mix[2].mode=2;test_ui->mosaic_scatter_mix[2].low=-100;test_ui->mosaic_scatter_mix[2].high=100;
    mosaic_scatter_apply(0,test_ui,&mc);assert(!test_ui->mosaic_scatter_open);
    int instances=0;for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id)++instances;assert(instances==27);
    pattern_render(m,other,6000,0);assert(!ts_pattern_engine_overflow(m->pattern_engine));
    for(int i=3;i<27;++i)assert(m->voices[i].active && m->events[i].source==a->source);
    assert(ts_mosaic_undo(m,0) && ts_mosaic_undo(m,0) && ts_mosaic_hash(m)==before_mix);
    m->playing=0;mosaic_select_only(test_ui,a->id);
    snprintf(test_ui->status,sizeof(test_ui->status),"PATTERN SNAPSHOTS / INDEPENDENT PLAYHEADS / SHARED SAMPLES");
    snapshot("mosaic-pattern-cards.bmp");
    /* Native pass at fractional scale, used by the desktop renderer. */
    TsFramebuffer *fb=malloc(sizeof(*fb));ts_ui_render(fb,test_ui,test_bank);
    SDL_Renderer *renderer=SDL_CreateRenderer(test_window,-1,SDL_RENDERER_SOFTWARE);assert(renderer);SDL_SetWindowSize(test_window,1280,800);
    SDL_Texture *texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,640,400);assert(texture);
    TsUiWaveformDetail details[2]={0};SDL_Texture *textures[2]={0};
    for(int pass=0;pass<2;++pass){
        int width=pass?1729:1280,height=pass?973:800;SDL_SetWindowSize(test_window,width,height);
        if(pass){test_ui->mosaic_time=0;test_ui->mosaic_hscale=.83;test_ui->mosaic_scale=173.5;test_ui->mosaic_scroll=.037;test_ui->mosaic_xscroll=3.1;}
        native_waveform_prepare(renderer,details,1);ts_audio_ui_render(fb,test_ui,test_bank);
        SDL_UpdateTexture(texture,NULL,fb->pixels,640*4);SDL_RenderCopy(renderer,texture,NULL,NULL);native_waveform_copy(renderer,textures,details,fb);
        SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_ARGB8888);assert(surface);
        assert(!SDL_RenderReadPixels(renderer,NULL,surface->format->format,surface->pixels,surface->pitch));
        assert(!SDL_SaveBMP(surface,pass?"mosaic-pattern-cards-fractional.bmp":"mosaic-pattern-cards-native.bmp"));SDL_FreeSurface(surface);
        /* Card pixels must translate together at the native pixel scale. */
        test_ui->mosaic_time=-1;native_waveform_prepare(renderer,details,1);ts_audio_ui_render(fb,test_ui,test_bank);
        TsUiWaveformDetail *d=&details[0];size_t bytes=(size_t)d->width*d->height*4;uint32_t *prior=malloc(bytes);assert(prior);memcpy(prior,d->pixels,bytes);
        test_ui->mosaic_scroll+=1.0/(test_ui->mosaic_scale*d->height/310.0);
        native_waveform_prepare(renderer,details,1);ts_audio_ui_render(fb,test_ui,test_bank);
        for(int y=0;y<d->height-1;++y)for(int x=0;x<d->width;++x)assert(d->pixels[y*d->width+x]==prior[(y+1)*d->width+x]);
        free(prior);
    }
SDL_DestroyTexture(texture);for(int i=0;i<2;++i){if(textures[i])SDL_DestroyTexture(textures[i]);ts_ui_waveform_detail_free(&details[i]);}SDL_DestroyRenderer(renderer);ts_ui_waveform_detail_begin(NULL);free(fb);
    /* Real PCM mixer workload: 1 / 4 / 8 / 16 copies of an eight-track
       pattern, 48kHz, including modulation and voice overlap. */
    for(int lane=0;lane<8;++lane)raw[lane]=(note_t){.note=49+(lane%3)*7,.instr=1,.efx=4,.efxData=0x47};export_score();
    TsMosaicSource *dense=ts_pattern_source(m,test_pages,test_bank,pat()->id,test_error,sizeof(test_error));assert(dense);
    for(int copies=1;copies<=16;copies*=2){
        memset(m->events,0,sizeof(m->events));m->next_id=1;
        for(int i=0;i<copies;++i){TsMosaicEvent *e=ts_mosaic_add(m,dense,0,i*20);e->duration=10;}
        clock_t cpu_start=clock();uint64_t start=SDL_GetPerformanceCounter();pattern_render(m,mixed,n,0);
        double elapsed=(double)(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency();
        double cpu=(double)(clock()-cpu_start)/CLOCKS_PER_SEC;
        fprintf(stderr,"pattern CPU: %d cards x 8 tracks, 48000 Hz: %.3f ms CPU / 1.2 s (%.2f%% core), %.3f ms wall\n",copies,cpu*1000,cpu/1.2*100,elapsed*1000);
        assert(!ts_pattern_engine_overflow(m->pattern_engine));
        ts_mosaic_seek(m,.197);m->playing=1;double worst=0,total=0,worst_cpu=0;
        for(int block=0;block<225;++block){clock_t cpu_at=clock();uint64_t at=SDL_GetPerformanceCounter();ts_mosaic_begin_block(m,256);for(int j=0;j<256;++j)ts_mosaic_read(m,48000);
            double ms=1000.0*(SDL_GetPerformanceCounter()-at)/SDL_GetPerformanceFrequency();total+=ms;if(ms>worst)worst=ms;double cpu_ms=1000.0*(clock()-cpu_at)/CLOCKS_PER_SEC;if(cpu_ms>worst_cpu)worst_cpu=cpu_ms;}
        fprintf(stderr,"pattern blocks: %d cards, 256 frames, mean wall %.3f ms, max CPU %.3f ms, max wall %.3f ms (includes seek), deadline 5.333 ms\n",copies,total/225,worst_cpu,worst);

    }
    /* More than one source page; real source drag/copy uses the common event
       model. Loading an unrelated module will not resolve a saved editor by
       coincidentally reusing a pattern number. */
    for(int i=1;i<=16;++i){assert(allocatePattern(i));patternNumRows[i]=16;}
    export_score();
    mc.pattern_ready=0;test_ui->mosaic_source_page=1;mosaic_poll(0,test_ui,test_bank,&mc);
    assert(test_ui->mosaic_source_pages==2 && test_ui->mosaic_sources[0].source->pattern);
    assert(mosaic_pattern_editor(0,test_ui,&mc,source->pattern));test_ui->tracker_open=0;test_ui->mosaic_open=1;
    test_pages->mosaic=m;
    TsSamplePages *restored_pages=malloc(sizeof(*restored_pages));TsInstrument *restored_bank=malloc(sizeof(*restored_bank)),*record=malloc(sizeof(*record));assert(restored_pages&&restored_bank&&record);
    ts_instrument_init(restored_bank);ts_instrument_init(record);assert(ts_sample_pages_init(restored_pages,test_error,sizeof(test_error)));restored_pages->mosaic=ts_mosaic_create();
    TsSisterProjectState *project_state=malloc(sizeof(*project_state));assert(project_state);ts_sister_project_state_init(project_state,48000);project_state->page_count=test_pages->page_count;project_state->active_page=test_pages->active_page;
    assert(ts_sample_pages_save_project(test_pages,test_bank,record,project_state,"pattern-project/pattern-project.tsr",test_error,sizeof(test_error)));
    assert(ts_sample_pages_load_project(restored_pages,restored_bank,record,"pattern-project/pattern-project.tsr",test_error,sizeof(test_error)));
    assert(restored_pages->mosaic->events[0].source->pattern && ts_mosaic_hash(restored_pages->mosaic)==ts_mosaic_hash(m));
    pattern_render(m,mixed,n,0);pattern_render(restored_pages->mosaic,other,n,0);assert(pattern_difference(mixed,other,n)<1e-6);
    /* A freshly loaded TSR can browse its saved patterns without first
       opening TrackSister, even while the embedded editor owns another model. */
    static MosaicController restored_mc;restored_mc.pages=restored_pages;
    test_ui->mosaic=test_audio->mosaic=restored_pages->mosaic;test_ui->mosaic_pattern_tab=0;SDL_SetWindowSize(test_window,640,400);
    SDL_zero(ev);ev.type=SDL_MOUSEBUTTONDOWN;ev.button.windowID=SDL_GetWindowID(test_window);ev.button.button=SDL_BUTTON_LEFT;ev.button.clicks=1;ev.button.x=82;ev.button.y=44;
    assert(mosaic_event(&ev,test_window,0,test_audio,test_ui,restored_bank,&restored_mc,&pc,&sw,&tc,48000));
    assert(test_ui->mosaic_pattern_tab && test_ui->mosaic_source_pages==2);
    TsMosaicSource *restored_source=test_ui->mosaic_sources[0].source;
    assert(restored_source && restored_source->pattern && restored_source->pattern->id==restored_pages->tracker.patterns[0]->id);
    assert(!memcmp(restored_source->pattern->cells,dense->pattern->cells,sizeof(dense->pattern->cells)));
    test_ui->mosaic=test_audio->mosaic=m;mosaic_sources_refresh(test_ui,test_bank,&mc);
    ts_mosaic_free(restored_pages->mosaic);restored_pages->mosaic=NULL;ts_sample_pages_free(restored_pages);ts_instrument_free(restored_bank);ts_instrument_free(record);free(restored_pages);free(restored_bank);free(record);free(project_state);test_pages->mosaic=NULL;
    /* Existing XM import, including 32 channels, keymaps, Amiga periods,
       envelopes and raw volume-column commands. Replacement cannot retarget
       the already-placed snapshots. */
    for(int amiga=0;amiga<=1;++amiga){
        xm_fixture("pattern-source.xm",32,amiga);TsSamplePages imported;
        assert(ts_tapehead_module_prepare("pattern-source.xm",&imported,test_error,sizeof(test_error)));
        mc.pages=&imported;assert(!mosaic_pattern_editor(0,test_ui,&mc,source->pattern));mc.pages=test_pages;
        TsMosaic *xm=ts_mosaic_create();TsMosaicSource *xs=ts_pattern_source(xm,&imported,NULL,imported.tracker.patterns[0]->id,test_error,sizeof(test_error));
        assert(xs && xs->pattern->asset_count==2 && xs->pattern->linear==!amiga && xs->pattern->cells[0][31][2]==0x50);
        TsMosaicEvent *xe=ts_mosaic_add(xm,xs,0,0);assert(xe);xe->duration=.2;
        pattern_render(xm,mixed,9600,0);double power=0;for(int j=0;j<9600;++j)power+=fabs(mixed[j].l)+fabs(mixed[j].r);assert(power>10);
        pattern_render(xm,other,6000,.05);assert(pattern_difference(mixed+2400+300,other+300,5000)<.002);
        ts_sample_pages_free(&imported);pattern_render(xm,other,9600,0);assert(pattern_difference(mixed,other,9600)<1e-6);
        assert(ts_mosaic_save(xm,".",test_error,sizeof(test_error)));TsMosaic *copy=ts_mosaic_create();assert(ts_mosaic_load(copy,".",test_error,sizeof(test_error)));
        pattern_render(copy,other,9600,0);assert(pattern_difference(mixed,other,9600)<1e-6);ts_mosaic_free(copy);ts_mosaic_free(xm);
    }
    free(single);free(mixed);free(other);ts_mosaic_free(m);test_ui->mosaic=test_audio->mosaic=NULL;
    fprintf(stderr,"Mosaic pattern integration passed\n");
}
