/* Actual SDL gesture dispatch helpers and audio callback, without hardware. */
#define SDL_MAIN_HANDLED
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>

int main(void)
{
    static AudioState audio;static TsUiState ui;static TsInstrument instrument;
    static TsFramebuffer frame;
    TsSample source;char error[160];
    SDL_SetMainReady();SDL_setenv("SDL_AUDIODRIVER","dummy",1);SDL_setenv("SDL_VIDEODRIVER","dummy",1);
    assert(!SDL_Init(SDL_INIT_AUDIO|SDL_INIT_VIDEO|SDL_INIT_TIMER));
    SDL_AudioSpec spec={0};spec.freq=48000;spec.channels=2;spec.format=AUDIO_F32SYS;spec.samples=256;
    SDL_AudioDeviceID device=SDL_OpenAudioDevice(NULL,0,&spec,NULL,0);assert(device);ts_real_output=device;
    ts_ui_init(&ui);ts_instrument_init(&instrument);ts_sample_init(&source);
    ts_sister_runtime_init(&audio.sister);ts_note_bank_init(&audio.notes);
    ts_performance_init(&audio.performance);ts_performance_init(&audio.tile_launchers);
    ts_capture_init(&audio.capture);ts_audio_mixer_init(&audio.mixer);
    audio.output_rate=48000;audio.bank_slot=-1;
    source.frames=48000;source.sample_rate=48000;source.channels=2;
    source.data=malloc(source.frames*2*sizeof(float));assert(source.data);
    for(size_t i=0;i<source.frames;++i) {source.data[2*i]=.2f;source.data[2*i+1]=-.6f;}
    snprintf(source.name,sizeof(source.name),"STEREO TAPE GESTURE");
    assert(ts_instrument_import_sample(&instrument,&source,0,0,0,TS_LOOP_FORWARD,error,sizeof(error)));
    ui.show_keyboard=1;ui.show_recipes=ui.show_ingredients=0;
    ts_instrument_set_selection(&instrument,4000,16000);
    instrument.has_loop=1;instrument.loop_first=4000;instrument.loop_last=16000;
    uint64_t original=ts_sample_hash(&instrument.current);
    for(int right=0;right<=1;++right)for(int move=0;move<=1;++move) {
        SDL_Keymod mod=move ? KMOD_CTRL : KMOD_SHIFT;
        int button=right ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
        assert(begin_tape_drag(&ui,&instrument,button,mod,100));
        assert(ui.tape_dragging && ui.tape_drag_kind==(move ?
            (right ? TS_POST_MOVE_OVERWRITE : TS_POST_MOVE_MIX) :
            (right ? TS_POST_COPY_OVERWRITE : TS_POST_COPY_MIX)));
        update_tape_drag(&ui,&instrument,400);
        for(int mode=0;mode<TS_WAVEFORM_DISPLAY_COUNT;++mode) {
            ui.config.waveform_display_mode=mode;
            ts_ui_render(&frame,&ui,&instrument);
            int x=TS_WAVE_X+400;
            int lanes=mode==TS_WAVEFORM_DISPLAY_STEREO ? 2:1;
            for(int lane=0;lane<lanes;++lane) {
                int h=lanes==2 ? (lane==0 ? TS_WAVE_H/2 : TS_WAVE_H-TS_WAVE_H/2) : TS_WAVE_H;
                int middle=TS_WAVE_Y+lane*(TS_WAVE_H/2)+h/2;
                float value=lanes==2 ? (lane==0 ? .2f:-.6f) :
                    mode==TS_WAVEFORM_DISPLAY_RIGHT ? -.6f : mode==TS_WAVEFORM_DISPLAY_MONO_SUM ? -.2f:.2f;
                int y=middle-(int)(value*(h/2-(lanes==2 ? 4:6)));
                assert(frame.pixels[y*640+x]==ui.palette.colors[TS_PALETTE_PATTERN_EFFECT]);
            }
        }
        ui.config.waveform_display_mode=TS_WAVEFORM_DISPLAY_STEREO;
        ts_ui_render(&frame,&ui,&instrument);
        if(getenv("TS_TEST_STEREO_GESTURE_SCREENSHOT"))
            assert(ts_ui_write_ppm(&frame,getenv("TS_TEST_STEREO_GESTURE_SCREENSHOT")));
        /* Paired channels through the held-note callback before and after edit. */
        SDL_Event key={0};key.type=SDL_KEYDOWN;key.key.keysym.sym=SDLK_z;
        assert(canvas_qwerty_event(&key,device,&audio,&ui,&instrument,48000));
        key.key.keysym.sym=SDLK_x;assert(canvas_qwerty_event(&key,device,&audio,&ui,&instrument,48000));
        assert(ts_note_bank_count(&audio.notes)==2);
        float output[512];audio_callback(&audio,(Uint8*)output,sizeof(output));
        finish_tape_drag(device,&audio,&ui,&instrument);
        assert(!ui.tape_dragging && strstr(ui.status,"APPLIED"));
        assert(instrument.current.channels==2 && ts_note_bank_count(&audio.notes)==2);
        audio_callback(&audio,(Uint8*)output,sizeof(output));
        double energy=0;
        for(size_t i=0;i<256;++i) {
            assert(isfinite(output[2*i]) && isfinite(output[2*i+1]));
            assert(fabsf(output[2*i+1]+3*output[2*i])<.00001f);
            energy+=fabsf(output[2*i]);
        }
        /* Move can silence the original loop because it now contains a gap. */
        if(!move)assert(energy>0);
        release_note(device,&audio,&ui,0);release_note(device,&audio,&ui,2);
        assert(ts_note_bank_count(&audio.notes)==0);
        lock_edit(device,&audio);assert(ts_instrument_undo(&instrument,error,sizeof(error)));
        unlock_edit(device,&audio,&ui,&instrument);
        assert(ts_sample_hash(&instrument.current)==original);
    }
    /* Actual DRAW controller interpolates a linked amplitude profile. */
    assert(begin_amplitude_draw(device,&audio,&ui,&instrument,TS_WAVE_X+80,TS_WAVE_Y+30));
    assert(preview_amplitude_draw(device,&audio,&ui,&instrument,TS_WAVE_X+180,TS_WAVE_Y+40));
    end_amplitude_draw(device,&audio,&ui,&instrument,0);
    assert(instrument.current.channels==2 && ts_sample_hash(&instrument.current)!=original);
    for(size_t i=0;i<source.frames;++i)assert(fabsf(instrument.current.data[2*i+1]+3*instrument.current.data[2*i])<.00001f);
    ts_sample_free(&source);ts_instrument_free(&instrument);
    ts_performance_free(&audio.performance);ts_performance_free(&audio.tile_launchers);
    ts_sister_runtime_free(&audio.sister);ts_capture_free(&audio.capture);
    SDL_CloseAudioDevice(device);ts_real_output=0;SDL_Quit();
    puts("Stereo gesture controller, display and playback tests passed");return 0;
}
