#ifndef TAPESISTER_SPATIAL_UI_H
#define TAPESISTER_SPATIAL_UI_H
#include "tapesister/ui.h"
typedef struct {
    TsSpatialControls controls;
    TsSpatialView view;
    int page, speaker, learn, matrix;
    TsSpeakerSetup speakers;
    float input_peak;
    char message[104];
} TsSpatialUi;
typedef enum {
    TS_SPATIAL_CAPTURE_EMPTY, TS_SPATIAL_CAPTURE_READY,
    TS_SPATIAL_CAPTURE_EDITING, TS_SPATIAL_CAPTURE_CHANGED
} TsSpatialCaptureVisual;
TsSpatialCaptureVisual ts_spatial_ui_capture_visual(const TsSpatialUi *model, int state);
void ts_spatial_ui_capture_status(const TsSpatialUi *model, char *out, size_t size);
void ts_spatial_ui_render(TsFramebuffer *fb, const TsSpatialUi *model, const TsPalette *palette);
typedef struct {
    TsSpeakerSetup setup;
    unsigned channels;
    TsAudioChannelRange range;
    int selected, editing, caret, test;
    char edit[TS_SPEAKER_NAME], message[104];
    float peak[16];
} TsSpeakersUi;
void ts_speakers_ui_render(TsFramebuffer *fb,const TsSpeakersUi *model,const TsPalette *palette);
int ts_spatial_ui_parameter(int x, int y);
#endif
