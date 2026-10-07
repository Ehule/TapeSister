#ifndef TAPESISTER_UI_SCHEDULE_H
#define TAPESISTER_UI_SCHEDULE_H
#include <stdint.h>
/* Rendering and service cadence are independent of the device audio clock.
   Input wakes SDL's event wait; these are upper bounds for unattended refresh. */
/* Integer 17 ms gating can miss every other 60 Hz VSync. A 16 ms service
   window keeps that presentation cadence; VSync remains the final limiter. */
enum { TS_UI_ACTIVE_MS=16u };
static inline unsigned ts_ui_refresh_ms(int visible, int editing, int animated)
{ return !visible ? 100u : editing ? TS_UI_ACTIVE_MS : animated ? 33u : 100u; }
static inline int ts_ui_refresh_due(uint32_t now, uint32_t last, unsigned period, int invalid)
{ return invalid || (uint32_t)(now-last) >= period; }
#endif
