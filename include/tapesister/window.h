#ifndef TS_WINDOW_H
#define TS_WINDOW_H
#include <SDL2/SDL.h>

/* Workspaces keep the desktop taskbar and native title-bar controls by default.
   Small utility editors deliberately omit MAXIMIZED at their creation sites. */
#define TS_WORKSPACE_WINDOW_FLAGS (SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_MAXIMIZED)

static inline int ts_window_fullscreen_shortcut(const SDL_Event *event)
{
    if(event->type!=SDL_KEYDOWN)return 0;
    SDL_Keycode key=event->key.keysym.sym;
    return (key==SDLK_RETURN || key==SDLK_KP_ENTER || key==SDLK_F11) &&
        (event->key.keysym.mod&KMOD_ALT) &&
        !(event->key.keysym.mod&(KMOD_CTRL|KMOD_SHIFT|KMOD_GUI));
}
static inline int ts_window_fullscreen(SDL_Window *window,int enabled)
{
    /* Switching between our own windows must not minimize a fullscreen view. */
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS,"0");
    return SDL_SetWindowFullscreen(window,enabled?SDL_WINDOW_FULLSCREEN_DESKTOP:0u);
}
/* SDL2's logical-size renderer already maps queued mouse events. Only polled
   pointer positions (and wheel positions before SDL 2.26) need conversion. */
static inline void ts_window_logical_pointer(SDL_Renderer *renderer,const SDL_Event *e,int *x,int *y)
{
    if(e->type==SDL_MOUSEBUTTONDOWN || e->type==SDL_MOUSEBUTTONUP){*x=e->button.x;*y=e->button.y;return;}
    if(e->type==SDL_MOUSEMOTION){*x=e->motion.x;*y=e->motion.y;return;}
#if SDL_VERSION_ATLEAST(2,26,0)
    if(e->type==SDL_MOUSEWHEEL){*x=e->wheel.mouseX;*y=e->wheel.mouseY;return;}
#endif
    int px,py;float lx,ly;SDL_GetMouseState(&px,&py);
    SDL_RenderWindowToLogical(renderer,px,py,&lx,&ly);*x=(int)lx;*y=(int)ly;
}
#endif
