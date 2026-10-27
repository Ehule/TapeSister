#include "test_cockpit_font.inc"
static void test_cockpit(const char *directory)
{
    test_cockpit_font(directory);
    TsMosaic *m=a.mosaic;assert(m);
    PerformanceStatus original=performance_status(0,&a);
    assert(original.tracker.ready);
    tapeheadActionPerformanceUnmuteAll();
    if(!(original.tracker.score_mutes&1))assert(tapeheadActionTrackMuteToggle(0));
    assert(tapeheadActionTrackPerformanceSoloToggle(0));
    PerformanceStatus state=performance_status(0,&a);
    /* A performance solo does not cancel a saved score mute on that lane. */
    assert((state.tracker.score_mutes&1) && !(state.tracker.performance_mutes&1));
    assert(state.tracker.performance_solo==0);
    m->markers[0]=(TsMosaicMarker){.stored=1,.time=2};snprintf(m->markers[0].name,sizeof(m->markers[0].name),"FIRST");
    m->markers[1]=(TsMosaicMarker){.stored=1,.time=5};snprintf(m->markers[1].name,sizeof(m->markers[1].name),"NEXT");
    m->events[0].muted=m->events[0].solo=1;
    performance_arrangement.updated=SDL_GetTicks()-300;performance_arrangement_refresh(m,3);
    assert(performance_arrangement.cards==1 && performance_arrangement.muted==1 && performance_arrangement.solo==1 && performance_arrangement.here==1);
    m->markers[1].time=30;assert(performance_cue_range(m,1).end==30);m->markers[1].time=5;
    int row=song.row;unsigned running=performance_running(&a);
    perf_input_click(230,118,SDL_BUTTON_LEFT);assert(performance_transport.page==PERF_CUES);
    perf_input_click(32,200,SDL_BUTTON_LEFT);assert(m->marker_active==1 && m->time==2);
    assert(song.row==row && performance_running(&a)==running);
    perf_input_click(40,380,SDL_BUTTON_LEFT);assert(m->marker_loop);
    m->time=3.25;perf_input_click(40,380,SDL_BUTTON_LEFT);assert(!m->marker_loop && m->time==3.25);
    perf_input_shot(directory,"control-center-cues");
    perf_input_click(40,118,SDL_BUTTON_LEFT);assert(performance_transport.page==PERF_OVERVIEW);
    unsigned selected=a.sister.performance_transport;
    perf_input_click(210,192,SDL_BUTTON_LEFT);assert(performance_running(&a)==running && a.sister.performance_transport==(selected^PERF_MOSAIC));
    perf_input_click(210,192,SDL_BUTTON_LEFT);assert(a.sister.performance_transport==selected);
    /* The cockpit must not become a second consuming reader of F9 meters. */
    a.notes.voices[0].output_route.activity_source=.75f;
    a.notes.voices[0].output_route.activity_send=.5f;
    performance_transport.painted=SDL_GetTicks()-100;performance_update(0,&a);
    assert(a.notes.voices[0].output_route.activity_source==.75f && a.notes.voices[0].output_route.activity_send==.5f);
    assert(ts_note_bank_start(&a.notes,&bank,TS_AUDITION_CURRENT,12,0,48000)==TS_NOTE_STARTED);
    int voices=performance_status(0,&a).notes;assert(voices>0);
    perf_input_click(230,64,SDL_BUTTON_LEFT);assert(performance_status(0,&a).notes==voices);
    ts_note_bank_clear(&a.notes);
    a.sister.performance_transport=PERF_MOSAIC;
    assert(ts_tapehead_transport_locked(1));m->playing=1;
    performance_transport.selected=0;perf_input_shot(directory,"control-center-overview");
    SDL_FlushEvent(SDL_KEYDOWN);perf_input_click(40,618,SDL_BUTTON_LEFT);
    SDL_Event routing_request;
    assert(SDL_PeepEvents(&routing_request,1,SDL_GETEVENT,SDL_KEYDOWN,SDL_KEYDOWN)==1);
    assert(routing_request.key.keysym.sym==SDLK_F9 && routing_request.key.windowID==SDL_GetWindowID(window));
    /* Explicit software-UI measurement, not a timing-sensitive CI assertion.
       Dense sample-card metadata exercises the UI cache, not pattern audio. */
    if(getenv("TS_COCKPIT_BENCHMARK")) {
        for(int density=0;density<2;++density) {
            if(density)for(int i=1;i<TS_MOSAIC_EVENTS;++i)assert(ts_mosaic_add(m,m->events[0].source,0,i));
            for(int cold=0;cold<2;++cold) {
                Uint64 start=SDL_GetPerformanceCounter();
                for(int paint=0;paint<100;++paint) {
                    performance_transport.painted=SDL_GetTicks()-100;
                    if(cold)performance_arrangement.updated=SDL_GetTicks()-300;
                    performance_update(0,&a);
                }
                double ms=(SDL_GetPerformanceCounter()-start)*1000.0/SDL_GetPerformanceFrequency()/100;
                printf("F11 software paint / %d sample cards / %s metadata: %.3f ms per paint\n",density?TS_MOSAIC_EVENTS:1,cold?"refreshed":"cached",ms);
            }
        }
        for(int i=1;i<TS_MOSAIC_EVENTS;++i)ts_mosaic_delete(m,m->events[i].id);
    }
    /* Window/logical coordinate conversion also covers drag and page targets. */
    SDL_SetWindowSize(performance_transport.window,1200,800);
    SDL_RenderSetLogicalSize(performance_transport.renderer,PERF_W,PERF_H);
    perf_input_shot(directory,"control-center-overview-125");
    int x,y;SDL_RenderLogicalToWindow(performance_transport.renderer,430,118,&x,&y);
    perf_input_click(x,y,SDL_BUTTON_LEFT);assert(performance_transport.page==PERF_INPUTS);
    SDL_SetWindowSize(performance_transport.window,PERF_W,PERF_H);
    SDL_RenderSetLogicalSize(performance_transport.renderer,PERF_W,PERF_H);
    performance_page_set(&a,PERF_OVERVIEW);
    a.sister.performance_transport=selected;ts_tapehead_stop();m->playing=0;m->events[0].muted=m->events[0].solo=0;
    tapeheadActionPerformanceUnmuteAll();
    for(int i=0;i<song.numChannels && i<32;++i) {
        if(editor.channelMuted[i]!=((original.tracker.score_mutes>>i)&1))assert(tapeheadActionTrackMuteToggle(i));
        if((original.tracker.performance_mutes>>i)&1)assert(tapeheadActionTrackPerformanceMuteToggle(i));
    }
}
