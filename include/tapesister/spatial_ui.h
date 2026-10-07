#ifndef TAPESISTER_SPATIAL_UI_H
#define TAPESISTER_SPATIAL_UI_H
#include "tapesister/ui.h"
typedef struct {
    TsSpatialControls controls;
    TsSpatialView view;
    int page, speaker, learn;
    char message[104];
} TsSpatialUi;
void ts_spatial_ui_render(TsFramebuffer *fb, const TsSpatialUi *model, const TsPalette *palette);
int ts_spatial_ui_parameter(int x, int y);
#endif
