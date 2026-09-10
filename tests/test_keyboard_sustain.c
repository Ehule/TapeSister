/* Shared release semantics and actual workspace MIDI/shortcut routes. */
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>

static void shot(const char *name,TsUiState *ui,TsInstrument *instrument)
{
    const char *dir=getenv("TS_TEST_SUSTAIN_SHOTS");if(!dir)return;
    char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",dir,name);
    static TsFramebuffer fb;ts_ui_render(&fb,ui,instrument);ts_ui_render_file_recording(&fb,ui);assert(ts_ui_write_ppm(&fb,path));
}
static void test_voice_policy(TsInstrument *instrument,const TsSample *sample)
{
    TsNoteBank bank;TsTuning unity={60,0};TsNoteEvent a,b;
    for(int midi=0;midi<2;++midi)for(int mode=0;mode<3;++mode)for(int loop=0;loop<2;++loop) {
        ts_note_bank_init(&bank);
        if(midi) {assert(ts_note_event_midi(&a,60,100,0));assert(ts_note_event_midi(&b,64,100,1));}
        else {assert(ts_note_event_qwerty(&a,0,60));assert(ts_note_event_qwerty(&b,4,60));}
        instrument->has_loop=loop;instrument->loop_first=0;instrument->loop_last=loop?instrument->current.frames:0;
#define START(ev,latch) (mode==0?ts_note_bank_start_tuned_event(&bank,instrument,&unity,TS_AUDITION_CURRENT,ev,latch,44100):mode==1?ts_note_bank_start_sample_event(&bank,sample,&unity,ev,latch,44100):ts_note_bank_start_preview_event(&bank,sample,&unity,ev,100,1000,loop,44100))
        assert(START(&a,0)==TS_NOTE_STARTED);
        ts_note_bank_release_event(&bank,&a);assert(ts_note_bank_count(&bank)==0);
        ts_note_bank_set_sustain(&bank,1);
        assert(START(&a,0)==TS_NOTE_STARTED);assert(START(&b,0)==TS_NOTE_STARTED);
        ts_note_bank_release_event(&bank,&a);assert(ts_note_bank_count(&bank)==2);
        ts_note_bank_set_sustain(&bank,0);assert(ts_note_bank_count(&bank)==1);
        assert(ts_note_bank_display_voice(&bank)->midi_note==64);
        ts_note_bank_release_event(&bank,&b);assert(ts_note_bank_count(&bank)==0);
        ts_note_bank_set_sustain(&bank,1);assert(START(&a,0)==TS_NOTE_STARTED);
        ts_note_bank_release_event(&bank,&a);
        assert(START(&a,0)==TS_NOTE_STARTED);assert(ts_note_bank_count(&bank)==1); /* Retrigger, never latch-toggle. */
        ts_note_bank_set_sustain(&bank,0);assert(ts_note_bank_count(&bank)==1);
        ts_note_bank_release_event(&bank,&a);assert(ts_note_bank_count(&bank)==0);
        if(mode!=2) {
            assert(START(&a,1)==TS_NOTE_STARTED);ts_note_bank_release_event(&bank,&a);
            ts_note_bank_set_sustain(&bank,0);assert(ts_note_bank_count(&bank)==1);
        }
        ts_note_bank_set_sustain(&bank,1);ts_note_bank_clear(&bank);
        assert(bank.sustain && !ts_note_bank_count(&bank));
#undef START
    }
    /* A sustained one-shot ends naturally; sustain does not turn it into a loop. */
    instrument->has_loop=0;ts_note_bank_init(&bank);ts_note_bank_set_sustain(&bank,1);
    assert(ts_note_event_qwerty(&a,0,60));
    assert(ts_note_bank_start_tuned_event(&bank,instrument,&unity,TS_AUDITION_CURRENT,&a,0,44100)==TS_NOTE_STARTED);
    ts_note_bank_release_event(&bank,&a);
    for(size_t i=0;i<instrument->current.frames+2;++i)(void)ts_note_bank_read(&bank);
    assert(!ts_note_bank_count(&bank));
}

#include "test_playback_continuity.inc"
#include "test_waveform_native.inc"
#include "test_waveform_workspaces.inc"

int main(void)
{
    static AudioState audio;static TsUiState ui;static TsInstrument instrument;static SisterWindow sister;
    TsSample sample;char error[160];SDL_SetMainReady();
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);SDL_setenv("SDL_VIDEODRIVER","dummy",1);
    assert(!SDL_Init(SDL_INIT_AUDIO|SDL_INIT_VIDEO|SDL_INIT_TIMER));
    SDL_AudioSpec spec={0};spec.freq=44100;spec.channels=2;spec.format=AUDIO_F32SYS;spec.samples=256;
    SDL_AudioDeviceID device=SDL_OpenAudioDevice(NULL,0,&spec,NULL,0);assert(device);ts_real_output=device;
    SDL_Window *window=SDL_CreateWindow("Sustain",0,0,640,400,0);assert(window);
    ts_ui_init(&ui);ts_instrument_init(&instrument);ts_sample_init(&sample);
    ts_note_bank_init(&audio.notes);ts_performance_init(&audio.performance);ts_performance_init(&audio.tile_launchers);
    ts_sister_runtime_init(&audio.sister);ts_capture_init(&audio.capture);
    sample.frames=4096;sample.sample_rate=44100;sample.channels=1;sample.data=malloc(sample.frames*sizeof(float));assert(sample.data);
    for(size_t i=0;i<sample.frames;++i)sample.data[i]=.2f*sinf((float)i*.07f);
    assert(ts_instrument_import_sample(&instrument,&sample,0,0,sample.frames,TS_LOOP_FORWARD,error,sizeof(error)));
    test_voice_policy(&instrument,&sample);
    test_keyboard_continuity(window,device,&audio,&ui,&instrument);
    test_start_loops(device,&audio,&ui);
    test_continuity_ui(&ui,&instrument);
    test_native_waveform_and_window();
    test_workspace_waveforms();
    /* Reproduce Set Loop -> click current tile -> launch, then revisit and Undo. */
    ui.bank_view_slot=-1;ts_instrument_set_selection(&instrument,500,2500);
    set_loop(device,&audio,&ui,&instrument);assert(instrument.has_loop);
    size_t loop_first=instrument.loop_first,loop_last=instrument.loop_last;
    int undo_count=instrument.undo_count;
    uint64_t hash=ts_sample_hash(&instrument.current);
    assert(ts_ui_execute_bank_action(&instrument,instrument.selected_slot,TS_UI_BANK_ACTION_AUDITION,error,sizeof(error)));
    assert(instrument.has_loop && instrument.loop_first==loop_first && instrument.loop_last==loop_last);
    assert(instrument.undo_count==undo_count && ts_sample_hash(&instrument.current)==hash);
    assert(instrument.bank[instrument.selected_slot].has_loop);
    toggle_tile_launcher(device,&audio,&ui,&instrument,instrument.selected_slot,44100);
    const TsPerformanceVoice *launched=ts_performance_source_display_voice(&audio.tile_launchers,instrument.selected_slot);
    assert(launched && launched->looping && launched->range_first==loop_first && launched->range_last==loop_last);
    ts_performance_clear(&audio.tile_launchers);
    assert(ts_ui_execute_bank_action(&instrument,1,TS_UI_BANK_ACTION_AUDITION,error,sizeof(error)));
    assert(ts_ui_execute_bank_action(&instrument,0,TS_UI_BANK_ACTION_AUDITION,error,sizeof(error)));
    assert(instrument.has_loop && instrument.loop_first==loop_first && instrument.loop_last==loop_last);
    assert(ts_instrument_undo(&instrument,error,sizeof(error)) && !instrument.has_loop);
    assert(ts_instrument_redo(&instrument,error,sizeof(error)) && instrument.has_loop);
    clear_loop(device,&audio,&ui,&instrument);
    assert(ts_ui_execute_bank_action(&instrument,0,TS_UI_BANK_ACTION_AUDITION,error,sizeof(error)));
    assert(!instrument.has_loop && !instrument.bank[0].has_loop);

    const TsCdpRecipe *scrub=ts_cdp_recipe_find("scrub");assert(scrub);
    assert(!strcmp(scrub->controls[3].value_names[0],"BOUNCE"));
    assert(!strcmp(scrub->controls[3].value_names[1],"FORWARD"));
    for(int forward=0;forward<2;++forward) {
        TsCdpRecipeValues values;ts_cdp_recipe_values_default(scrub,&values);values.controls[3]=(float)forward;
        TsCdpCommand commands[TS_CDP_MAX_STAGES];size_t count=0;
        assert(ts_cdp_recipe_build_commands(scrub,&values,44100,44100,commands,&count,error,sizeof(error)));
        int flag=0;for(int arg=0;arg<commands[0].argc;++arg)flag|=!strcmp(commands[0].arguments[arg],"-f");
        assert(flag==forward);
    }

    SDL_Event key={0};key.type=SDL_KEYDOWN;key.key.windowID=SDL_GetWindowID(window);
    key.key.keysym.sym=SDLK_s;key.key.keysym.mod=KMOD_SHIFT;
    assert(keyboard_sustain_event(&key,window,device,&audio,&ui,&sister,&instrument));
    assert(ui.keyboard_sustain && audio.notes.sustain && audio.performance.sustain && audio.sister.performance.sustain);
    key.key.repeat=1;assert(keyboard_sustain_event(&key,window,device,&audio,&ui,&sister,&instrument));assert(ui.keyboard_sustain);key.key.repeat=0;
    shot("sustain-canvas",&ui,&instrument);
    ui.config_open=1;assert(!keyboard_sustain_event(&key,window,device,&audio,&ui,&sister,&instrument));ui.config_open=0;
    ui.fm_open=1;shot("sustain-fm",&ui,&instrument);ui.fm_open=0;
    /* Group/Sister use the same pedal policy, including MIDI channel identity. */
    TsNoteEvent a,b;assert(ts_note_event_midi(&a,60,100,0));assert(ts_note_event_midi(&b,64,100,1));
    for(int which=0;which<2;++which) {
        TsPerformanceBank *bank=which?&audio.sister.performance:&audio.performance;
        assert(ts_performance_trigger_group_event(bank,&instrument,1,&a,0,44100)==1);
        assert(ts_performance_trigger_group_event(bank,&instrument,1,&b,0,44100)==1);
        ts_performance_release_event(bank,&a);assert(ts_performance_count(bank)==2);
    }
    assert(keyboard_sustain_event(&key,window,device,&audio,&ui,&sister,&instrument));
    assert(!ui.keyboard_sustain && ts_performance_count(&audio.performance)==1 && ts_performance_count(&audio.sister.performance)==1);
    runtime_note_release_event(&audio,&b);assert(!ts_performance_count(&audio.performance) && !ts_performance_count(&audio.sister.performance));
    keyboard_sustain_toggle(device,&audio,&ui,&sister);
    runtime_note_clear(&audio);assert(audio.notes.sustain && audio.performance.sustain && audio.sister.performance.sustain);
    TsMidiEvent midi={0};midi.action=TS_MIDI_ACTION_NOTE_ON;midi.note=a;
    /* Main tile and FM MIDI routes obey the same setting as QWERTY. */
    for(int fm=0;fm<2;++fm) {
        ui.fm_open=fm;
        midi.action=TS_MIDI_ACTION_NOTE_ON;handle_midi_event(device,&audio,&ui,&instrument,&sample,&midi,44100);
        assert(ts_note_bank_count(&audio.notes)==1);
        midi.action=TS_MIDI_ACTION_NOTE_OFF;handle_midi_event(device,&audio,&ui,&instrument,&sample,&midi,44100);
        assert(ts_note_bank_count(&audio.notes)==1);
        keyboard_sustain_toggle(device,&audio,&ui,&sister);assert(!ts_note_bank_count(&audio.notes));
        midi.action=TS_MIDI_ACTION_NOTE_ON;handle_midi_event(device,&audio,&ui,&instrument,&sample,&midi,44100);
        midi.action=TS_MIDI_ACTION_NOTE_OFF;handle_midi_event(device,&audio,&ui,&instrument,&sample,&midi,44100);
        assert(!ts_note_bank_count(&audio.notes));keyboard_sustain_toggle(device,&audio,&ui,&sister);
    }
    ui.fm_open=0;midi.action=TS_MIDI_ACTION_NOTE_ON;
    /* MIDI reaches the Portal's active waveform, selected range and transpose. */
    PortalController c;portal_init(&c,&ui.portal);ui.portal.open=1;
    assert(portal_source(device,&audio,&ui,&instrument,&c));
    ui.portal.waves[0].has_selection=1;ui.portal.waves[0].selection_first=100;ui.portal.waves[0].selection_last=1000;ui.portal.loop=1;
    handle_midi_event(device,&audio,&ui,&instrument,NULL,&midi,44100);
    assert(ts_note_bank_count(&audio.notes)==1);
    const TsNoteVoice *v=ts_note_bank_display_voice(&audio.notes);
    assert(v->sample==ui.portal.source && v->range_first==100 && v->range_last==1000 && v->looping);
    midi.action=TS_MIDI_ACTION_NOTE_OFF;handle_midi_event(device,&audio,&ui,&instrument,NULL,&midi,44100);
    assert(ts_note_bank_count(&audio.notes)==1);
    ui.portal.name_focus=1;assert(!keyboard_sustain_event(&key,window,device,&audio,&ui,&sister,&instrument));
    midi.action=TS_MIDI_ACTION_NOTE_ON;midi.note=b;handle_midi_event(device,&audio,&ui,&instrument,NULL,&midi,44100);assert(ts_note_bank_count(&audio.notes)==1);
    ui.portal.name_focus=0;ui.portal.number_focus=0;assert(!keyboard_sustain_event(&key,window,device,&audio,&ui,&sister,&instrument));ui.portal.number_focus=-1;
    shot("sustain-portal",&ui,&instrument);
    portal_clear_notes(&audio,&ui.portal,&c);assert(!ts_note_bank_count(&audio.notes));
    ui.portal.result=&sample;ui.portal.listen_result=1;ui.portal.loop=0;
    handle_midi_event(device,&audio,&ui,&instrument,NULL,&midi,44100);
    v=ts_note_bank_display_voice(&audio.notes);assert(v && v->sample==&sample && v->midi_note==64 && v->step>1 && !v->looping);
    midi.action=TS_MIDI_ACTION_PANIC;midi.channel=1;handle_midi_event(device,&audio,&ui,&instrument,NULL,&midi,44100);assert(!ts_note_bank_count(&audio.notes));
    portal_close(device,&audio,&ui,&c);portal_free(&c);
    /* File preview supports stereo MIDI and both note-release modes. */
    TsSample stereo;ts_sample_init(&stereo);stereo.frames=sample.frames;stereo.sample_rate=44100;stereo.channels=2;
    stereo.data=malloc(stereo.frames*2*sizeof(float));assert(stereo.data);
    for(size_t i=0;i<stereo.frames;++i) {stereo.data[2*i]=sample.data[i];stereo.data[2*i+1]=-sample.data[i];}
    ui.import_preview_open=1;ui.import_preview_sample=&stereo;ui.import_preview_loop=1;
    midi.action=TS_MIDI_ACTION_NOTE_ON;midi.note=a;
    handle_midi_event(device,&audio,&ui,&instrument,NULL,&midi,44100);assert(ts_note_bank_count(&audio.notes)==1);
    midi.action=TS_MIDI_ACTION_NOTE_OFF;handle_midi_event(device,&audio,&ui,&instrument,NULL,&midi,44100);assert(ts_note_bank_count(&audio.notes)==1);
    TsStereoFrame heard={0};for(int i=0;i<200;++i)heard=ts_note_bank_read_stereo(&audio.notes);
    assert(fabsf(heard.l)>.001 && fabsf(heard.l+heard.r)<.00001);
    shot("sustain-import",&ui,&instrument);
    SDL_Event click={0};click.type=SDL_MOUSEBUTTONDOWN;click.button.windowID=SDL_GetWindowID(window);
    click.button.button=SDL_BUTTON_LEFT;click.button.x=550;click.button.y=320;
    assert(keyboard_sustain_event(&click,window,device,&audio,&ui,&sister,&instrument));assert(!ui.keyboard_sustain && !ts_note_bank_count(&audio.notes));
    midi.action=TS_MIDI_ACTION_NOTE_ON;handle_midi_event(device,&audio,&ui,&instrument,NULL,&midi,44100);
    midi.action=TS_MIDI_ACTION_NOTE_OFF;handle_midi_event(device,&audio,&ui,&instrument,NULL,&midi,44100);assert(!ts_note_bank_count(&audio.notes));
    ui.import_preview_open=0;
    /* Canvas and Portal buttons are actual click targets; hidden keyboard is not. */
    ui.show_keyboard=0;assert(!keyboard_sustain_event(&click,window,device,&audio,&ui,&sister,&instrument));
    ui.show_keyboard=1;assert(keyboard_sustain_event(&click,window,device,&audio,&ui,&sister,&instrument));assert(ui.keyboard_sustain);
    ui.portal.open=1;ui.portal.number_focus=-1;click.button.x=565;click.button.y=316;
    assert(keyboard_sustain_event(&click,window,device,&audio,&ui,&sister,&instrument));assert(!ui.keyboard_sustain);ui.portal.open=0;
    /* Same shortcut in the companion window; preset text remains text. */
    sister.window=SDL_CreateWindow("Sister",0,0,640,400,0);assert(sister.window);sister.window_id=SDL_GetWindowID(sister.window);
    key.key.windowID=sister.window_id;
    assert(keyboard_sustain_event(&key,window,device,&audio,&ui,&sister,&instrument));assert(sister.model.keyboard_sustain);
    sister.model.preset_manage_open=1;assert(!keyboard_sustain_event(&key,window,device,&audio,&ui,&sister,&instrument));sister.model.preset_manage_open=0;
    const char *dir=getenv("TS_TEST_SUSTAIN_SHOTS");if(dir) {
        static TsFramebuffer fb;char path[1024];ts_sister_ui_render(&fb,&sister.model,&ui.palette);
        snprintf(path,sizeof(path),"%s/sustain-sister.ppm",dir);assert(ts_ui_write_ppm(&fb,path));
    }
    ts_sample_free(&stereo);ts_sample_free(&sample);ts_instrument_free(&instrument);
    ts_performance_free(&audio.performance);ts_performance_free(&audio.tile_launchers);ts_sister_runtime_free(&audio.sister);ts_capture_free(&audio.capture);
    SDL_DestroyWindow(sister.window);SDL_DestroyWindow(window);SDL_CloseAudioDevice(device);ts_real_output=0;SDL_Quit();
    puts("Shared Sustain and preview MIDI tests passed");return 0;
}
