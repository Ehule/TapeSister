#ifndef TAPESISTER_SOURCE_ROUTE_UI_H
#define TAPESISTER_SOURCE_ROUTE_UI_H
#include "tapesister/ui.h"
#include "tapesister/source_route.h"
typedef struct {
    TsSourceRoute route;
    TsSpatialControls array;
    TsRouterControls router;
    unsigned enabled;
    unsigned available, channels;
    int track, slot, can_rename;
    int page, bus; /* Output / Sends / Shared Returns; selected return bus. */
    char name[80];
} TsSourceRouteUi;
void ts_source_route_ui_render(TsFramebuffer *fb,const TsSourceRouteUi *m,const TsPalette *palette);
#endif
