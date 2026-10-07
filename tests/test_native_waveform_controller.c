/* Actual native texture uploads, overlays, and workspace transitions. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>
#include "test_waveform_native.inc"
#include "test_waveform_workspaces.inc"

int main(void)
{
    SDL_SetMainReady();
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);
    assert(!SDL_Init(SDL_INIT_AUDIO|SDL_INIT_VIDEO|SDL_INIT_TIMER));
    test_native_waveform_and_window();
    test_workspace_waveforms();
    SDL_Quit();
    return 0;
}
