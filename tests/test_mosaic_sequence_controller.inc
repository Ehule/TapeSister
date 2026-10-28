#include <time.h>
static void sequence_circle_measure(TsUiState *u)
{
    TsUiLooperCircle circle={0};assert(ts_ui_mosaic_looper_circle(&circle,u,1280,1024));
    uint32_t *pixels=circle.pixels;uint64_t raster=circle.raster_count;int x=u->config.mosaic_looper_x,y=u->config.mosaic_looper_y;
    u->config.mosaic_looper_x=30;u->config.mosaic_looper_y=60;
    assert(ts_ui_mosaic_looper_circle(&circle,u,1280,1024) && !circle.changed && circle.pixels==pixels && circle.raster_count==raster);
    u->midi_learn_active=1;assert(!ts_ui_mosaic_looper_circle(&circle,u,1280,1024) && !circle.valid);
    u->midi_learn_active=0;assert(ts_ui_mosaic_looper_circle(&circle,u,1280,1024) && circle.pixels==pixels);
    u->config.mosaic_looper_x=x;u->config.mosaic_looper_y=y;
    const char *path=getenv("TS_SEQUENCE_UI_BENCH");
    if(path) {
        TsFramebuffer *fb=malloc(sizeof(*fb));assert(fb);double times[3][3];
        TsMosaicEvent *e=ts_mosaic_find(u->mosaic,u->mosaic_sequence_id);assert(e);int slot=(int)(e-u->mosaic->events);
        for(int trial=0;trial<3;++trial)for(int run=0;run<3;++run) {
            int mode=(run+trial)%3;
            clock_t start=clock();
            for(int frame=0;frame<600;++frame) {
                u->mosaic_live[slot].phase=frame/600.;u->mosaic_live[slot].elapsed=u->mosaic_sequence_duration*frame/600.;
                if(mode!=2)ts_ui_render(fb,u,&instrument);
                if(mode)assert(ts_ui_mosaic_looper_circle(&circle,u,1280,1024));
            }
            times[mode][trial]=1000.*(clock()-start)/CLOCKS_PER_SEC/600;
        }
        FILE *f=fopen(path,"wb");assert(f);fprintf(f,"workload,median_cpu_ms_per_frame,circle_cache_bytes\n");
        for(int mode=0;mode<3;++mode) {
            for(int i=0;i<3;++i)for(int j=i+1;j<3;++j)if(times[mode][j]<times[mode][i]){double t=times[mode][i];times[mode][i]=times[mode][j];times[mode][j]=t;}
            fprintf(f,"%s,%.6f,%zu\n",(const char *[]){"logical_ui","logical_ui_plus_native_circle","native_circle_only"}[mode],times[mode][1],mode?(size_t)circle.width*circle.height*16:0);
        }
        fclose(f);free(fb);
    }
    ts_ui_mosaic_looper_circle_free(&circle);
}
static void test_mosaic_sequence_controller(SDL_Window *window,SDL_AudioDeviceID device)
{
    AudioState *a=routing_audio(0);TsUiState *u=mosaic_record_ui_fixture(a);ExternalInputState *s=mosaic_record_input_fixture(a);
    MosaicController *c=calloc(1,sizeof(*c));assert(c);SDL_AudioDeviceID input=0;
    ts_loop_capture_init(&s->loop_capture);atomic_init(&s->loop_tap,-1);a->loop_capture=&s->loop_capture;a->loop_tap=&s->loop_tap;
    TsMosaic *m=u->mosaic;TsMosaicEvent *one=&m->events[0];one->live=1;one->duration=.01;
    TsMosaicEvent *two=ts_mosaic_copy(m,one->id,0,100),*bg=ts_mosaic_copy(m,one->id,0,200);assert(two && bg);
    two->duration=.015;bg->live_order=-1;uint64_t id=one->id;
    ts_mosaic_live_launch(m,bg->id,0);float output[512];audio_callback(a,(Uint8 *)output,sizeof(output));
    uint64_t background=m->voices[bg-m->events].generation;
    u->mosaic_live_open=1;mosaic_select_only(u,id);mosaic_poll(device,u,&instrument,c);
    int px,py;ts_ui_mosaic_live_origin(u,&px,&py);char target[80];
    assert(ts_ui_midi_target_from_point(u,px+18,py+193,target,sizeof(target)) && !strcmp(target,"mosaic.live.cycle"));
    assert(ts_ui_midi_target_from_point(u,px+150,py+193,target,sizeof(target)) && !strcmp(target,"mosaic.live.finish_sequence"));
    live_performance_transport(window,device,a,s,u,&instrument,c,6);assert(!m->sequence.running);
    live_performance_transport(window,device,a,s,u,&instrument,c,0);audio_callback(a,(Uint8 *)output,sizeof(output));
    assert(m->sequence.running && m->sequence.count==2);
    double elapsed=m->live[0].elapsed;uint64_t generation=m->voices[0].generation;
    live_performance_transport(window,device,a,s,u,&instrument,c,6);assert(m->sequence.finishing);
    live_performance_transport(window,device,a,s,u,&instrument,c,0);
    assert(!m->sequence.finishing && m->live[0].elapsed==elapsed && m->voices[0].generation==generation);
    live_performance_transport(window,device,a,s,u,&instrument,c,6);
    for(int n=0;n<20;++n)audio_callback(a,(Uint8 *)output,sizeof(output));
    assert(!m->sequence.running && m->live[bg-m->events].state && m->voices[bg-m->events].generation==background);
    /* The same two actions work from learned pedals, on press only. */
    assert(ts_midi_map_assign(&u->config.midi_map,"mosaic.live.cycle",(TsMidiSource){TS_MIDI_SOURCE_NOTE,9,65}));
    assert(ts_midi_map_assign(&u->config.midi_map,"mosaic.live.finish_sequence",(TsMidiSource){TS_MIDI_SOURCE_NOTE,9,66}));
    TsMidiEvent pedal;assert(ts_midi_decode_short_message(0x99,65,100,&pedal));
    assert(handle_midi_mapping(device,a,u,&instrument,&sister,&input,s,&pedal,48000,2));assert(m->sequence.running);
    assert(ts_midi_decode_short_message(0x99,66,100,&pedal));
    assert(handle_midi_mapping(device,a,u,&instrument,&sister,&input,s,&pedal,48000,2));assert(m->sequence.finishing);
    assert(ts_midi_decode_short_message(0x89,65,0,&pedal));
    assert(handle_midi_mapping(device,a,u,&instrument,&sister,&input,s,&pedal,48000,2));assert(m->sequence.finishing);
    assert(mosaic_live_command(device,&input,a,s,u,"sequence_stop"));
    for(int n=0;n<3;++n)audio_callback(a,(Uint8 *)output,sizeof(output));
    assert(!m->sequence.running && m->live[bg-m->events].state);
    /* Actual setup controls retain Card IDs and make background membership explicit. */
    live_performance_click(window,device,a,s,u,&instrument,c,px+110,py+252);assert(u->mosaic_live_advanced==1);
    live_performance_click(window,device,a,s,u,&instrument,c,550,92);assert(u->mosaic_live_advanced==2);
    mosaic_select_only(u,two->id);live_performance_click(window,device,a,s,u,&instrument,c,440,114);
    assert(two->live_order==1 && one->live_order==2 && one->id==id);
    mosaic_select_only(u,bg->id);live_performance_click(window,device,a,s,u,&instrument,c,300,114);assert(bg->live_order==0);
    live_performance_click(window,device,a,s,u,&instrument,c,300,114);assert(bg->live_order==-1);
    mosaic_select_only(u,id);live_performance_click(window,device,a,s,u,&instrument,c,200,248);
    assert(fabs(one->duration-ts_mosaic_live_cycle(one))<1e-9);
    live_performance_click(window,device,a,s,u,&instrument,c,500,248);assert(one->fade_out>0);
    live_performance_click(window,device,a,s,u,&instrument,c,200,276);assert(one->finish_fade==.1);
    ts_mosaic_live_launch(m,id,0);audio_callback(a,(Uint8 *)output,sizeof(output));
    live_performance_click(window,device,a,s,u,&instrument,c,340,276);
    for(int n=0;n<24;++n)audio_callback(a,(Uint8 *)output,sizeof(output));
    assert(!m->live[0].state && m->live[bg-m->events].state && m->voices[bg-m->events].generation==background);
    const char *shot=getenv("TS_TEST_LIVE_LOOPS");
    if(shot){char path[1200];snprintf(path,sizeof(path),"%s-sequencer-setup.ppm",shot);mosaic_poll(device,u,&instrument,c);live_controller_screenshot_size(u,path,1280,1024);}
    one->duration=ts_mosaic_live_cycle(one);two->duration=2*ts_mosaic_live_cycle(two);bg->duration=2*ts_mosaic_live_cycle(bg);u->mosaic_scale=50;
    live_performance_click(window,device,a,s,u,&instrument,c,560,318);assert(!u->mosaic_live_advanced);
    live_performance_transport(window,device,a,s,u,&instrument,c,0);
    for(int n=0;n<10;++n)audio_callback(a,(Uint8 *)output,sizeof(output));
    mosaic_poll(device,u,&instrument,c);assert(u->mosaic_sequence_running && u->mosaic_sequence_count==2);
    /* Recording layers during a sequence leaves its frozen membership intact. */
    char folder[160];snprintf(folder,sizeof(folder),"sequence-capture-%lu",(unsigned long)SDL_GetTicks());SDL_setenv("TAPESISTER_CAPTURES",folder,1);
    m->live_capture.source=1;m->live_capture.length=0;
    live_performance_key(window,device,a,s,u,&instrument,c,SDLK_z,1,KMOD_NONE);
    uint64_t next=m->next_id;assert(mosaic_live_command(device,&input,a,s,u,"primary"));
    for(int n=0;n<8;++n)audio_callback(a,(Uint8 *)output,sizeof(output));
    assert(mosaic_live_command(device,&input,a,s,u,"next"));
    for(int n=0;n<8;++n)audio_callback(a,(Uint8 *)output,sizeof(output));
    assert(mosaic_live_command(device,&input,a,s,u,"stop_capture"));live_controller_wait(device,&input,a,s,u,next+2);
    assert(m->sequence.running && m->sequence.count==2 && m->live[bg-m->events].state && m->voices[bg-m->events].generation==background);
    assert(ts_mosaic_find(m,next)->live && ts_mosaic_find(m,next+1)->live);
    live_performance_key(window,device,a,s,u,&instrument,c,SDLK_z,0,KMOD_NONE);mosaic_poll(device,u,&instrument,c);
    u->mosaic_scroll=u->mosaic_xscroll=0;u->mosaic_follow=0;u->mosaic_scale=50;
    one->start=0;one->x=0;two->start=.4;two->x=100;bg->start=.2;bg->x=200;mosaic_select_only(u,id);
    if(shot){char path[1200];snprintf(path,sizeof(path),"%s-sequencer-cycle.ppm",shot);live_controller_screenshot_size(u,path,1280,1024);}
    sequence_circle_measure(u);
    assert(mosaic_live_command(device,&input,a,s,u,"halt"));assert(!m->sequence.running);
    ts_loop_capture_free(&s->loop_capture);ts_external_recorder_free(&s->recorder);free(s);free(u);mosaic_controller_free(c);free(c);routing_free(a);
    SDL_setenv("TAPESISTER_CAPTURES","",1);
}
