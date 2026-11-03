#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>

static AudioState a;
static TsUiState ui;
static TsInstrument instrument;
static SisterWindow sister;
static TsFramebuffer fb;
static void click(int x,int y)
{
    TsSisterUiHit h=ts_sister_ui_hit_test_model(&sister.model,x,y);
    sister_apply_action(0,&a,&ui,&instrument,&sister,NULL,NULL,h,48000,2);
}
int main(int argc,char **argv)
{
    SDL_SetMainReady();SDL_setenv("SDL_VIDEODRIVER","dummy",1);
    assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER));
    ts_ui_init(&ui);ts_instrument_init(&instrument);ts_sister_runtime_init(&a.sister);
    a.output_rate=48000;a.output_device_channels=2;
    char error[256];assert(ts_sister_runtime_enable(&a.sister,48000,2,2,5,error,sizeof(error)));
    ts_sister_ui_model_init(&sister.model,&ui.config);sister.model.fx_page=0;
    sister.model.parameters=a.sister.parameters;
    TsStereoFrame *storage=a.sister.machine.dark.storage;
    click(100,16);assert(a.sister.parameters.memory_mode==TS_SISTER_MEMORY_DARK);
    assert(ui.config.sister_memory_mode==TS_SISTER_MEMORY_DARK);
    for(int i=0;i<7;++i) {
        int id=TS_DARK_PARAM_FEEDBACK+i;
        sister_apply_action(0,&a,&ui,&instrument,&sister,NULL,NULL,
            (TsSisterUiHit){TS_SISTER_UI_ACTION_PARAMETER,id,.7f},48000,2);
        assert(fabsf(sister_parameter_normalized(&a.sister.parameters,id)-(id==TS_DARK_PARAM_REVERSE?1:.7f))<1e-5f);
    }
    assert(ts_sister_ui_parameter_lock_toggle(&sister.model,TS_DARK_PARAM_BLUR));
    float blur=a.sister.parameters.dark.blur;click(200,210);assert(a.sister.parameters.dark.blur==blur);
    assert(ts_sister_ui_parameter_lock_toggle(&sister.model,TS_DARK_PARAM_BLUR));
    click(350,238);assert(!a.sister.parameters.dark.reverse);
    click(350,238);assert(a.sister.parameters.dark.reverse);
    TsSisterUiHit midi;assert(midi_sister_hit_from_target("sister.param.3002",.4f,&midi));
    sister_apply_action(0,&a,&ui,&instrument,&sister,NULL,NULL,midi,48000,2);
    assert(a.sister.parameters.dark.blur==.4f);
    char target[80];assert(ts_sister_ui_midi_target(ts_sister_ui_hit_test_model(&sister.model,100,16),target,sizeof(target)));
    assert(!strcmp(target,"sister.param.3000"));
    /* Algorithm and size are real choices with stable MIDI IDs and locks. */
    click(120,68);assert(a.sister.parameters.dark.algorithm==TS_DARK_TWO_DELAYS);
    click(370,68);assert(a.sister.parameters.dark.size==1);
    click(370,68);assert(a.sister.parameters.dark.size==2);
    click(370,68);assert(a.sister.parameters.dark.size==0);
    assert(ts_sister_ui_parameter_lock_toggle(&sister.model,TS_DARK_PARAM_ALGORITHM));
    click(120,68);assert(a.sister.parameters.dark.algorithm==TS_DARK_TWO_DELAYS);
    assert(ts_sister_ui_parameter_lock_toggle(&sister.model,TS_DARK_PARAM_ALGORITHM));
    assert(midi_sister_hit_from_target("sister.param.3008",1,&midi));
    sister_apply_action(0,&a,&ui,&instrument,&sister,NULL,NULL,midi,48000,2);
    assert(a.sister.parameters.dark.algorithm==TS_DARK_GRANULAR);
    assert(midi_sister_hit_from_target("sister.param.3009",1,&midi));
    sister_apply_action(0,&a,&ui,&instrument,&sister,NULL,NULL,midi,48000,2);
    assert(a.sister.parameters.dark.size==2 && ui.config.sister_dark.algorithm==TS_DARK_GRANULAR);
    assert(sister_parameter_wheel_normalized(&a.sister.parameters,TS_DARK_PARAM_ALGORITHM,-1,0)==.75f);
    assert(sister_parameter_wheel_normalized(&a.sister.parameters,TS_DARK_PARAM_SIZE,-1,0)==.5f);
    assert(ts_sister_ui_midi_target(ts_sister_ui_hit_test_model(&sister.model,120,68),target,sizeof(target)));
    assert(!strcmp(target,"sister.param.3008"));
    assert(midi_sister_hit_from_target("sister.param.3010",.4f,&midi));
    sister_apply_action(0,&a,&ui,&instrument,&sister,NULL,NULL,midi,48000,2);
    click(370,68);assert(a.sister.parameters.dark.size==0);
    click(370,68);click(370,68);
    assert(a.sister.parameters.memory_blend==.4f); /* Program choices preserve the live memory crossfade. */
    for(int i=0;i<48000*6;++i) {
        TsStereoFrame v={.2f*sinf(i*.03f),.15f*sinf(i*.041f)};
        TsSisterOutput out=ts_sister_machine_process_frame(&a.sister.machine,v,v);
        assert(isfinite(out.mix.l));
    }
    assert(a.sister.machine.dark.storage==storage);
    assert(ts_sister_machine_get_snapshot(&a.sister.machine,&sister.model.engine));
    sister.model.routing.enabled=1;sister.model.matrix_routing=1;
    sister.model.power_visual=TS_SISTER_UI_POWER_VISUAL_NONE;
    sister.model.parameters=a.sister.parameters;
    TsPalette palette;ts_palette_default(&palette);
    snprintf(sister.model.status,sizeof(sister.model.status),"DARK MATTER / 12 PROGRAMS + LEGACY / MEMORIES RETAINED");
    ts_sister_ui_render(&fb,&sister.model,&palette);
    if(argc>1) {
        SDL_Surface *surface=SDL_CreateRGBSurfaceFrom(fb.pixels,640,400,32,640*4,0xff0000,0xff00,0xff,0);
        assert(surface && !SDL_SaveBMP(surface,argv[1]));SDL_FreeSurface(surface);
    }
    click(100,16);assert(a.sister.parameters.memory_mode==TS_SISTER_MEMORY_TAPE);
    click(70,50);assert(a.sister.parameters.memory_mode==TS_SISTER_MEMORY_TAPE);
    click(100,16);assert(a.sister.parameters.memory_mode==TS_SISTER_MEMORY_DARK);
    assert(a.sister.machine.dark.storage==storage);
    ts_sister_runtime_disable(&a.sister);ts_instrument_free(&instrument);SDL_Quit();
    puts("Dark Matter native switch, controls, MIDI and rendering passed.");return 0;
}
