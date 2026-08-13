#include <SDL2/SDL.h>
#include "tapesister/pr13.h"

#define TS_PR13_SYNTHETIC 0x50523133u
#define TS_PR13_NEW_PARENT_EVENT (SDL_USEREVENT + 13)

static int pr13_active_slot = -1;
static int pr13_fine_active = 0;
static int pr13_fine_x = 0;
static int pr13_fine_y = 0;
static int pr13_fine_min = 0;
static int pr13_fine_max = 0;
static int pr13_restore_mods = 0;
static SDL_Keymod pr13_saved_mods = KMOD_NONE;
static int pr13_parent_reseed = 0;

static int pr13_bank_slot_at(int x, int y)
{
    int column, row;
    if (x < 10 || x >= 626 || y < 330 || y >= 380) return -1;
    column = (x - 10) / 77; row = (y - 330) / 25;
    if (column < 0 || column >= 8 || row < 0 || row >= 2) return -1;
    if ((x - 10) % 77 >= 72) return -1;
    return row * 8 + column;
}

static void pr13_push_left_click(int x, int y, Uint32 which)
{
    SDL_Event queued; SDL_zero(queued);
    queued.type = SDL_MOUSEBUTTONDOWN; queued.button.type = SDL_MOUSEBUTTONDOWN;
    queued.button.which = which; queued.button.button = SDL_BUTTON_LEFT;
    queued.button.state = SDL_PRESSED; queued.button.clicks = 1;
    queued.button.x = x; queued.button.y = y; SDL_PushEvent(&queued);
}

static void pr13_remember_fine_control(int x, int y)
{
    pr13_fine_active = 0;
    if (y >= 233 && y < 257) {
        if (x >= 10 && x < 110) { pr13_fine_min = 10; pr13_fine_max = 109; }
        else if (x >= 120 && x < 220) { pr13_fine_min = 120; pr13_fine_max = 219; }
        else if (x >= 230 && x < 330) { pr13_fine_min = 230; pr13_fine_max = 329; }
        else return;
    } else if (y >= 261 && y < 285) {
        pr13_fine_min = x - 48; pr13_fine_max = x + 48;
        if (pr13_fine_min < 10) pr13_fine_min = 10;
        if (pr13_fine_max > 629) pr13_fine_max = 629;
    } else return;
    pr13_fine_x = x; pr13_fine_y = y; pr13_fine_active = 1;
}

static int ts_pr13_poll_event(SDL_Event *event)
{
    int got;
    if (pr13_restore_mods) { SDL_SetModState(pr13_saved_mods); pr13_restore_mods = 0; }
    got = SDL_PollEvent(event);
    if (!got || event == NULL) return got;
    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.which == TS_PR13_SYNTHETIC) return got;

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        int x = event->button.x, y = event->button.y;
        int slot = pr13_bank_slot_at(x, y);
        if (slot >= 0) {
            pr13_active_slot = slot;
            pr13_push_left_click(585, 216, TS_PR13_SYNTHETIC);
            pr13_push_left_click(x, y, TS_PR13_SYNTHETIC);
            return got;
        }
        /* Reuse the obsolete Commit and Reset positions as explicit root-source controls.
           Family Generate/Reseed remain at their original buttons. */
        if (y >= 205 && y < 228 && x >= 247 && x < 325) {
            pr13_parent_reseed = 0; event->type = TS_PR13_NEW_PARENT_EVENT; return got;
        }
        if (y >= 205 && y < 228 && x >= 330 && x < 402) {
            pr13_parent_reseed = 1; event->type = TS_PR13_NEW_PARENT_EVENT; return got;
        }
        if (y >= 205 && y < 228 && x >= 407 && x < 535) { event->type = SDL_USEREVENT; return got; }
        if (y >= 205 && y < 228 && x >= 540 && x < 630) {
            if (pr13_active_slot >= 0) {
                int column = pr13_active_slot % 8, row = pr13_active_slot / 8;
                ts_pr13_set_lock_request(1);
                pr13_saved_mods = SDL_GetModState();
                SDL_SetModState((SDL_Keymod)(pr13_saved_mods | KMOD_SHIFT));
                pr13_restore_mods = 1;
                event->button.button = SDL_BUTTON_RIGHT;
                event->button.x = 10 + column * 77 + 36;
                event->button.y = 330 + row * 25 + 12;
            } else event->type = SDL_USEREVENT;
            return got;
        }
        pr13_remember_fine_control(x, y);
    }

    if (event->type == SDL_MOUSEWHEEL && pr13_fine_active) {
        int amount = event->wheel.y > 0 ? 1 : event->wheel.y < 0 ? -1 : 0;
        if (amount != 0) {
            pr13_fine_x += amount;
            if (pr13_fine_x < pr13_fine_min) pr13_fine_x = pr13_fine_min;
            if (pr13_fine_x > pr13_fine_max) pr13_fine_x = pr13_fine_max;
            event->type = SDL_MOUSEBUTTONDOWN; event->button.type = SDL_MOUSEBUTTONDOWN;
            event->button.which = TS_PR13_SYNTHETIC; event->button.button = SDL_BUTTON_LEFT;
            event->button.state = SDL_PRESSED; event->button.clicks = 1;
            event->button.x = pr13_fine_x; event->button.y = pr13_fine_y;
        }
        return got;
    }
    if (event->type == SDL_KEYDOWN && pr13_fine_active &&
        (event->key.keysym.sym == SDLK_LEFT || event->key.keysym.sym == SDLK_RIGHT)) {
        pr13_fine_x += event->key.keysym.sym == SDLK_RIGHT ? 1 : -1;
        if (pr13_fine_x < pr13_fine_min) pr13_fine_x = pr13_fine_min;
        if (pr13_fine_x > pr13_fine_max) pr13_fine_x = pr13_fine_max;
        event->type = SDL_MOUSEBUTTONDOWN; event->button.type = SDL_MOUSEBUTTONDOWN;
        event->button.which = TS_PR13_SYNTHETIC; event->button.button = SDL_BUTTON_LEFT;
        event->button.state = SDL_PRESSED; event->button.clicks = 1;
        event->button.x = pr13_fine_x; event->button.y = pr13_fine_y;
        return got;
    }
    return got;
}

#define SDL_PollEvent ts_pr13_poll_event
#define SDL_USEREVENT TS_PR13_NEW_PARENT_EVENT
#include "main_sdl.c"
#undef SDL_USEREVENT
#undef SDL_PollEvent
