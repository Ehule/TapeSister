/* UI-thread wheel decoding. SDL owns fractional-to-integer accumulation.
   Keep its detents independent of precise deltas used by continuous controls. */
static void wheel_event_mouse(const SDL_Event *e, int *x, int *y)
{
#if SDL_VERSION_ATLEAST(2, 26, 0)
    *x = e->wheel.mouseX; *y = e->wheel.mouseY;
#else
    (void)e; SDL_GetMouseState(x, y);
#endif
}

static double wheel_event_delta(const SDL_Event *e, int horizontal)
{
    double value = horizontal ? e->wheel.x : e->wheel.y;
#if SDL_VERSION_ATLEAST(2, 0, 18)
    double precise = horizontal ? e->wheel.preciseX : e->wheel.preciseY;
    if (isfinite(precise) && precise != 0) value = precise;
#endif
    value = fmax(-128, fmin(128, value));
    return e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -value : value;
}

static int wheel_event_detents(const SDL_Event *e, int horizontal)
{
    int value = horizontal ? e->wheel.x : e->wheel.y;
    value = SDL_clamp(value, -128, 128);
    return e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -value : value;
}

static int wheel_event_prepare(SDL_Event *e)
{
    if (e->type != SDL_MOUSEWHEEL) return 1;
    double dx = wheel_event_delta(e, 1), dy = wheel_event_delta(e, 0);
    int ix = wheel_event_detents(e, 1), iy = wheel_event_detents(e, 0);
    if (dx == 0 && dy == 0 && ix == 0 && iy == 0) return 0;
    e->wheel.x = ix; e->wheel.y = iy;
    e->wheel.direction = SDL_MOUSEWHEEL_NORMAL;
#if SDL_VERSION_ATLEAST(2, 0, 18)
    e->wheel.preciseX = (float)dx; e->wheel.preciseY = (float)dy;
#endif
    return 1;
}

/* Merge only adjacent wheel events with the same owner, pointer and direction.
   Never scan past a key/button/motion event or reorder input. A bounded batch
   prevents high-resolution wheels repeating expensive control work per packet. */
static void wheel_event_coalesce(SDL_Event *e)
{
#if SDL_VERSION_ATLEAST(2, 26, 0)
    if (e->type != SDL_MOUSEWHEEL) return;
    double dx = wheel_event_delta(e, 1), dy = wheel_event_delta(e, 0);
    int ix = wheel_event_detents(e, 1), iy = wheel_event_detents(e, 0);
    for (int i = 1; i < 32; ++i) {
        SDL_Event next;
        if (SDL_PeepEvents(&next, 1, SDL_PEEKEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) != 1 ||
            next.type != SDL_MOUSEWHEEL || next.wheel.windowID != e->wheel.windowID ||
            next.wheel.which != e->wheel.which || next.wheel.mouseX != e->wheel.mouseX ||
            next.wheel.mouseY != e->wheel.mouseY || next.wheel.direction != e->wheel.direction)
            break;
        double nx = wheel_event_delta(&next, 1), ny = wheel_event_delta(&next, 0);
        int nix = wheel_event_detents(&next, 1), niy = wheel_event_detents(&next, 0);
        if (abs(ix + nix) > 128 || abs(iy + niy) > 128) break;
        if (dx * nx < 0 || dy * ny < 0 || fabs(dx + nx) > 128 || fabs(dy + ny) > 128) break;
        if (SDL_PeepEvents(&next, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) != 1) break;
        ix += nix; iy += niy;
        dx += nx; dy += ny; e->wheel.timestamp = next.wheel.timestamp;
    }
    e->wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    e->wheel.x = ix; e->wheel.y = iy;
    e->wheel.preciseX = (float)dx; e->wheel.preciseY = (float)dy;
#else
    (void)e;
#endif
}
