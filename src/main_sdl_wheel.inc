/* UI-thread wheel decoding. Continuous controls use precise deltas; legacy
   discrete controls receive accumulated detents without losing partial turns. */
typedef struct {
    double x, y;
    Uint32 window, device, timestamp;
    int mouse_x, mouse_y, modifiers, valid;
} WheelAccumulator;

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

static int wheel_detents(double *remainder, double delta)
{
    if ((*remainder < 0 && delta > 0) || (*remainder > 0 && delta < 0)) *remainder = 0;
    *remainder += delta;
    int whole = (int)trunc(*remainder);
    *remainder -= whole;
    return whole;
}

static int wheel_event_prepare(WheelAccumulator *state, SDL_Event *e, int modifiers)
{
    if (e->type != SDL_MOUSEWHEEL) {
        /* Do not transfer a partial turn across clicks, keys, focus changes. */
        if (e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_KEYDOWN ||
            e->type == SDL_KEYUP || e->type == SDL_WINDOWEVENT)
            state->valid = 0;
        return 1;
    }
    double dx = wheel_event_delta(e, 1), dy = wheel_event_delta(e, 0);
    if (dx == 0 && dy == 0) return 0;
    int x, y; wheel_event_mouse(e, &x, &y);
    if (!state->valid || state->window != e->wheel.windowID || state->device != e->wheel.which ||
        state->mouse_x != x || state->mouse_y != y || state->modifiers != modifiers ||
        e->wheel.timestamp - state->timestamp > 500) state->x = state->y = 0;
    state->valid = 1; state->window = e->wheel.windowID; state->device = e->wheel.which;
    state->mouse_x = x; state->mouse_y = y; state->modifiers = modifiers;
    state->timestamp = e->wheel.timestamp;
    e->wheel.x = wheel_detents(&state->x, dx);
    e->wheel.y = wheel_detents(&state->y, dy);
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
    for (int i = 1; i < 32; ++i) {
        SDL_Event next;
        if (SDL_PeepEvents(&next, 1, SDL_PEEKEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) != 1 ||
            next.type != SDL_MOUSEWHEEL || next.wheel.windowID != e->wheel.windowID ||
            next.wheel.which != e->wheel.which || next.wheel.mouseX != e->wheel.mouseX ||
            next.wheel.mouseY != e->wheel.mouseY || next.wheel.direction != e->wheel.direction)
            break;
        double nx = wheel_event_delta(&next, 1), ny = wheel_event_delta(&next, 0);
        if (dx * nx < 0 || dy * ny < 0 || fabs(dx + nx) > 128 || fabs(dy + ny) > 128) break;
        if (SDL_PeepEvents(&next, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) != 1) break;
        dx += nx; dy += ny; e->wheel.timestamp = next.wheel.timestamp;
    }
    e->wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    e->wheel.x = (int)dx; e->wheel.y = (int)dy;
    e->wheel.preciseX = (float)dx; e->wheel.preciseY = (float)dy;
#else
    (void)e;
#endif
}
