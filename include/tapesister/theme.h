#ifndef TAPESISTER_THEME_H
#define TAPESISTER_THEME_H
#include <stdint.h>
/* Visual-only state. Never read by an audio callback or saved as musical state. */
typedef enum {
    TS_THEME_OVERALL,
    TS_THEME_CANVAS,
    TS_THEME_SISTER,
    TS_THEME_PEDALBOARD,
    TS_THEME_FALLOUT,
    TS_THEME_PRISM,
    TS_THEME_MOSAIC,
    TS_THEME_TRACKER,
    TS_THEME_ARP,
    TS_THEME_ROUTER,
    TS_THEME_EQ,
    TS_THEME_SPATIAL,
    TS_THEME_PORTAL,
    TS_THEME_BROWSER,
    TS_THEME_HEALTH,
    TS_THEME_MIDI,
    TS_THEME_TRANSPORT,
    TS_THEME_GROUP_COUNT
} TsThemeGroup;
typedef enum {
    TS_COLOR_BACKGROUND,
    TS_COLOR_PANEL,
    TS_COLOR_CONTROL,
    TS_COLOR_TEXT,
    TS_COLOR_DIM,
    TS_COLOR_EDGE,
    TS_COLOR_GRID,
    TS_COLOR_SELECTION,
    TS_COLOR_SELECTEDTEXT,
    TS_COLOR_FOCUS,
    TS_COLOR_ACTIVITY,
    TS_COLOR_WARNING,
    TS_COLOR_ERROR,
    TS_COLOR_DATA1,
    TS_COLOR_DATA2,
    TS_COLOR_DATA3,
    TS_COLOR_DATA4,
    TS_COLOR_DATA5,
    TS_COLOR_DATA6,
    TS_COLOR_PLAYHEAD,
    TS_COLOR_MUTED,
    TS_COLOR_SOLO,
    TS_COLOR_RECORD,
    TS_THEME_ROLE_COUNT
} TsThemeRole;
enum { TS_THEME_PRESET_COUNT = 8 };
typedef struct {
    uint32_t overrides[TS_THEME_GROUP_COUNT][TS_THEME_ROLE_COUNT]; /* zero = inherit */
    uint8_t preset, text_clarity, grid_visibility, selection_strength;
} TsTheme;
#endif
