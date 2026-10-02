#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>
static SDL_Window *window;
static AudioState *a;
static TsUiState *ui;
static TsInstrument *instrument;
static TsSamplePages *pages;
static void key(SDL_Keycode sym)
{
    SDL_Event e={0};e.type=SDL_KEYDOWN;e.key.windowID=SDL_GetWindowID(window);e.key.keysym.sym=sym;
    assert(tracker_event(&e,window,0,a,ui,pages,instrument,48000));
}
static void click(int x,int y)
{
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=SDL_GetWindowID(window);
    e.button.button=SDL_BUTTON_LEFT;e.button.x=x;e.button.y=y;
    assert(tracker_event(&e,window,0,a,ui,pages,instrument,48000));
}
int main(int argc,char **argv)
{
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER));
    window=SDL_CreateWindow("Tracker",0,0,640,400,0);assert(window);
    a=calloc(1,sizeof(*a));ui=malloc(sizeof(*ui));instrument=malloc(sizeof(*instrument));pages=malloc(sizeof(*pages));
    assert(a && ui && instrument && pages);
    char error[256];
    ts_ui_init(ui);ts_instrument_init(instrument);assert(ts_sample_pages_init(pages,error,sizeof(error)));ui->tracker=&pages->tracker;
    ts_tracker_playback_init(&a->tracker);ts_note_bank_init(&a->notes);
    ts_performance_init(&a->performance);ts_performance_init(&a->tile_launchers);
    ts_keyboard_sequence_init(&a->keyboard_sequence);ts_capture_init(&a->capture);ts_audio_mixer_init(&a->mixer);
    ts_sister_runtime_init(&a->sister);a->output_rate=48000;a->fm_output_gain=a->fm_output_target=1;
    assert(ts_sister_runtime_reconfigure(&a->sister,48000,2,error,sizeof(error)));
    assert(ts_instrument_select_bank(instrument,0,error,sizeof(error)));
    assert(ts_instrument_activate_silence(instrument,48000,48000,error,sizeof(error)));
    for(size_t i=0;i<instrument->current.frames;++i)instrument->current.data[i]=.2f;
    instrument->has_loop=1;instrument->loop_first=0;instrument->loop_last=48000;
    assert(ts_instrument_sync_selected(instrument,error,sizeof(error)));
    key(SDLK_F10);assert(ui->tracker_open && pages->tracker.pattern_count==1 && !a->tracker.running);
    TsTrackerPattern *p=ts_sister_tracker_pattern(&pages->tracker,pages->tracker.editor_pattern);
    click(20,342);assert(p->cells[0][0].tile_id==instrument->bank[0].tile_id);
    key(SDLK_z);assert(p->cells[0][0].note==ui->keyboard_base_note && p->cells[0][0].note_kind==TS_TRACKER_NOTE_PITCH);
    assert(pages->tracker.editor_row==1 && !ts_note_bank_count(&a->notes));
    /* Two hex digits must retain the high nibble even for an unbound prefix. */
    pages->tracker.aliases[0x12]=pages->tracker.aliases[1];pages->tracker.aliases[1]=0;
    click(67,110);assert(ui->tracker_field==1);key(SDLK_1);key(SDLK_2);
    assert(p->cells[1][0].tile_id==instrument->bank[0].tile_id);
    click(88,122);assert(ui->tracker_field==2);key(SDLK_2);key(SDLK_0);
    assert(p->cells[2][0].has_volume && p->cells[2][0].volume==0x20);
    click(38,134);click(150,342);assert(p->cells[3][0].note_kind==TS_TRACKER_NOTE_OFF);
    click(38,146);click(186,342);assert(p->cells[4][0].note_kind==TS_TRACKER_NOTE_CUT);
    p->cells[0][1]=p->cells[0][0];click(180,14);assert(a->tracker.running);
    float block[1024];audio_callback(a,(Uint8*)block,sizeof(block));
    assert(a->tracker.lanes[0].voice.active && a->tracker.lanes[1].voice.active);
    assert(fabs(a->mixer.buses.capture.l-.4)<1e-5 && fabs(block[1022]-.32)<1e-5);
    double phase=a->tracker.lanes[0].voice.position;
    key(SDLK_SPACE);assert(a->tracker.paused);audio_callback(a,(Uint8*)block,sizeof(block));
    assert(a->tracker.lanes[0].voice.position==phase && fabs(block[1022])<1e-5);
    key(SDLK_SPACE);assert(!a->tracker.paused);audio_callback(a,(Uint8*)block,sizeof(block));
    key(SDLK_F10);assert(!ui->tracker_open && a->tracker.running);
    /* Stop belongs to tracker; an existing held manual note keeps its phase. */
    TsNoteEvent n;TsTuning tuning={60,0};assert(ts_note_event_qwerty(&n,0,60));
    assert(ts_note_bank_start_sample_event(&a->notes,&instrument->current,&tuning,&n,1,48000)==TS_NOTE_STARTED);
    ts_tracker_playback_stop(&a->tracker);assert(ts_note_bank_count(&a->notes)==1);
    audio_callback(a,(Uint8*)block,sizeof(block));assert(ts_note_bank_count(&a->notes)==1 && block[1022]>.1f);
    ts_note_bank_clear(&a->notes);tracker_play(0,a,ui,pages,instrument,48000);
    audio_callback(a,(Uint8*)block,sizeof(block));
    /* FILE OUT receives the exact final audible Main mix. */
    TsPerformanceRecorder rec;ts_performance_recorder_init(&rec);
    assert(ts_performance_recorder_start(&rec,"tracker-file-out.wav",48000,2,2048,error,sizeof(error)));
    a->sister_file_recorder=&rec;atomic_init(&a->sister_file_tap,TS_SISTER_TAP_MIX);
    audio_callback(a,(Uint8*)block,sizeof(block));assert(atomic_load(&rec.accepted_frames)==512);
    for(int i=0;i<512;++i){assert(rec.ring[i].l==block[i*2] && rec.ring[i].r==block[i*2+1]);}
    a->sister_file_recorder=NULL;assert(ts_performance_recorder_request_stop(&rec));
    while(ts_performance_recorder_pump(&rec,2048)){}
    ts_performance_recorder_free(&rec);remove("tracker-file-out.wav");
    /* TRACK is a selectable Sister input; an unselected Main bus cannot leak. */
    assert(ts_sister_runtime_enable(&a->sister,48000,2,2,5,error,sizeof(error)));
    ts_sister_runtime_set_sources(&a->sister,TS_SISTER_SOURCE_TRACK);
    audio_callback(a,(Uint8*)block,sizeof(block));audio_callback(a,(Uint8*)block,sizeof(block));
    assert(fabs(a->sister.last_frame.input.l-.4)<1e-5);
    ts_sister_runtime_set_sources(&a->sister,0);
    audio_callback(a,(Uint8*)block,sizeof(block));audio_callback(a,(Uint8*)block,sizeof(block));
    assert(fabs(a->sister.last_frame.input.l)<1e-6 && a->mixer.buses.tracker.l==0);
    TsSisterUiModel model;ts_sister_ui_model_init(&model,&ui->config);
    TsSisterUiHit hit=ts_sister_ui_hit_test_model(&model,345,180);assert(hit.action==TS_SISTER_UI_ACTION_SOURCE_TRACK && hit.index==5);
    char target[96];assert(ts_sister_ui_midi_target(hit,target,sizeof(target)));
    assert(!strcmp(target,"sister.source.tracker"));
    TsSisterUiHit mapped;assert(midi_sister_hit_from_target(target,.5f,&mapped));assert(mapped.index==5);
    /* A different editor pattern cannot switch the heard pattern until Play. */
    TsPatternId heard=a->tracker.prepared->pattern.id,new_id;
    assert(ts_sister_tracker_add_pattern(&pages->tracker,64,&new_id,error,sizeof(error)));
    pages->tracker.editor_pattern=new_id;ui->tracker_open=1;tracker_refresh(0,a,ui,pages,instrument,48000);
    assert(a->tracker.prepared->pattern.id==heard);
    tracker_play(0,a,ui,pages,instrument,48000);assert(a->tracker.prepared->pattern.id==new_id);
    pages->tracker.editor_pattern=heard;ui->tracker_scroll=0;
    tracker_refresh(0,a,ui,pages,instrument,48000);
    if(argc>1) {
        TsFramebuffer *fb=malloc(sizeof(*fb));assert(fb);ts_ui_render(fb,ui,instrument);
        SDL_Surface *s=SDL_CreateRGBSurfaceFrom(fb->pixels,640,400,32,640*4,0xff0000,0xff00,0xff,0xff000000);
        assert(s && !SDL_SaveBMP(s,argv[1]));SDL_FreeSurface(s);free(fb);
    }
    stop_all_force(0,a,ui);assert(!a->tracker.running && !ts_note_bank_count(&a->notes));
    ts_tracker_playback_free(&a->tracker);ts_sister_runtime_free(&a->sister);
    ts_sample_pages_free(pages);ts_instrument_free(instrument);free(pages);free(instrument);free(ui);free(a);
    SDL_DestroyWindow(window);SDL_Quit();puts("SisterTracker controller checks passed");return 0;
}
