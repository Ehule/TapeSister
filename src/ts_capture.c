#include "tapesister/capture.h"

#include "tapesister/note_bank.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0)
        snprintf(error, error_size, "%s", message != NULL ? message : "");
}

static int bit_count(uint32_t value)
{
    int count = 0;
    while (value != 0u) {
        count += (int)(value & 1u);
        value >>= 1u;
    }
    return count;
}

void ts_capture_init(TsCaptureRecorder *recorder)
{
    if (recorder == NULL) return;
    recorder->buffer = NULL;
    recorder->capacity_frames = 0u;
    recorder->recorded_frames = 0u;
    recorder->sample_rate = 0u;
    recorder->staged_notes = 0u;
    recorder->destination_slot = -1;
    recorder->source_slot = -1;
    recorder->stopped_early = 0;
    atomic_init(&recorder->state, TS_CAPTURE_IDLE);
}

void ts_capture_free(TsCaptureRecorder *recorder)
{
    if (recorder == NULL) return;
    free(recorder->buffer);
    ts_capture_init(recorder);
}

int ts_capture_arm(TsCaptureRecorder *recorder, int destination_slot,
                   size_t capacity_frames, uint32_t sample_rate,
                   char *error, size_t error_size)
{
    float *buffer;
    if (recorder == NULL || destination_slot < 0 ||
        destination_slot >= TS_BANK_SLOT_COUNT || capacity_frames == 0u ||
        sample_rate == 0u || capacity_frames > SIZE_MAX / sizeof(*buffer)) {
        set_error(error, error_size, "Invalid Capture destination or duration");
        return 0;
    }
    buffer = (float *)calloc(capacity_frames, sizeof(*buffer));
    if (buffer == NULL) {
        set_error(error, error_size, "Out of memory preparing Capture tape");
        return 0;
    }
    free(recorder->buffer);
    ts_capture_init(recorder);
    recorder->buffer = buffer;
    recorder->capacity_frames = capacity_frames;
    recorder->sample_rate = sample_rate;
    recorder->destination_slot = destination_slot;
    recorder->state = TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER;
    set_error(error, error_size, "");
    return 1;
}

int ts_capture_set_source(TsCaptureRecorder *recorder, int source_slot,
                          char *error, size_t error_size)
{
    if (recorder == NULL || recorder->state != TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER) {
        set_error(error, error_size, "Capture is not waiting for a source");
        return 0;
    }
    if (source_slot < 0 || source_slot >= TS_BANK_SLOT_COUNT) {
        set_error(error, error_size, "Invalid Capture source tile");
        return 0;
    }
    if (source_slot == recorder->destination_slot) {
        set_error(error, error_size, "Capture source cannot be its destination");
        return 0;
    }
    recorder->source_slot = source_slot;
    set_error(error, error_size, "");
    return 1;
}

int ts_capture_toggle_staged_note(TsCaptureRecorder *recorder, int note,
                                  char *error, size_t error_size)
{
    uint32_t bit;
    if (recorder == NULL || recorder->state != TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER) {
        set_error(error, error_size, "Arm Capture before staging a chord");
        return 0;
    }
    if (note < 0 || note >= 24) {
        set_error(error, error_size, "Invalid staged keyboard note");
        return 0;
    }
    bit = 1u << note;
    if ((recorder->staged_notes & bit) != 0u) {
        recorder->staged_notes &= ~bit;
        set_error(error, error_size, "");
        return 1;
    }
    if (bit_count(recorder->staged_notes) >= TS_NOTE_VOICE_LIMIT) {
        set_error(error, error_size, "Staged chord reached the five-note limit");
        return 0;
    }
    recorder->staged_notes |= bit;
    set_error(error, error_size, "");
    return 1;
}

void ts_capture_clear_staged_notes(TsCaptureRecorder *recorder)
{
    if (recorder != NULL) recorder->staged_notes = 0u;
}

int ts_capture_trigger(TsCaptureRecorder *recorder,
                       char *error, size_t error_size)
{
    if (recorder == NULL || recorder->state != TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER) {
        set_error(error, error_size, "Capture is not armed");
        return 0;
    }
    if (recorder->source_slot < 0 ||
        recorder->source_slot == recorder->destination_slot) {
        set_error(error, error_size, "Select an occupied source tile first");
        return 0;
    }
    recorder->recorded_frames = 0u;
    recorder->stopped_early = 0;
    recorder->staged_notes = 0u;
    recorder->state = TS_CAPTURE_RECORDING;
    set_error(error, error_size, "");
    return 1;
}

int ts_capture_write_sample(TsCaptureRecorder *recorder, float sample)
{
    if (recorder == NULL || recorder->state != TS_CAPTURE_RECORDING ||
        recorder->buffer == NULL ||
        recorder->recorded_frames >= recorder->capacity_frames)
        return 0;
    recorder->buffer[recorder->recorded_frames++] = sample;
    if (recorder->recorded_frames == recorder->capacity_frames) {
        recorder->state = TS_CAPTURE_COMPLETED;
        recorder->stopped_early = 0;
        return 1;
    }
    return 0;
}

int ts_capture_stop(TsCaptureRecorder *recorder,
                    char *error, size_t error_size)
{
    if (recorder == NULL || recorder->state != TS_CAPTURE_RECORDING) {
        set_error(error, error_size, "Capture is not recording");
        return 0;
    }
    if (recorder->recorded_frames == 0u) {
        set_error(error, error_size, "Capture has not recorded a frame yet");
        return 0;
    }
    recorder->stopped_early = recorder->recorded_frames < recorder->capacity_frames;
    recorder->state = TS_CAPTURE_COMPLETED;
    set_error(error, error_size, "");
    return 1;
}

int ts_capture_cancel(TsCaptureRecorder *recorder)
{
    if (recorder == NULL ||
        (recorder->state != TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER &&
         recorder->state != TS_CAPTURE_RECORDING))
        return 0;
    recorder->state = TS_CAPTURE_CANCELED;
    recorder->staged_notes = 0u;
    return 1;
}

float ts_capture_progress(const TsCaptureRecorder *recorder)
{
    if (recorder == NULL || recorder->capacity_frames == 0u) return 0.0f;
    if (recorder->recorded_frames >= recorder->capacity_frames) return 1.0f;
    return (float)((double)recorder->recorded_frames /
                   (double)recorder->capacity_frames);
}

const char *ts_capture_state_name(TsCaptureState state)
{
    if (state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER) return "ARMED";
    if (state == TS_CAPTURE_RECORDING) return "RECORDING";
    if (state == TS_CAPTURE_COMPLETED) return "COMPLETE";
    if (state == TS_CAPTURE_CANCELED) return "CANCELED";
    return "IDLE";
}
