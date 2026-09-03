#include "tapesister/capture.h"

#include "tapesister/note_bank.h"

#include <math.h>
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
    recorder->overdub_base = NULL;
    recorder->capacity_frames = 0u;
    recorder->recorded_frames = 0u;
    recorder->overdub_base_frames = 0u;
    recorder->sample_rate = 0u;
    recorder->overdub_base_rate = 0u;
    recorder->channels = 1u;
    recorder->overdub_base_channels = 1u;
    recorder->staged_notes = 0u;
    recorder->destination_slot = -1;
    recorder->source_slot = -1;
    recorder->provenance_slot = -1;
    recorder->stopped_early = 0;
    recorder->auto_resize = 0;
    recorder->overdub = 0;
    atomic_init(&recorder->state, TS_CAPTURE_IDLE);
}

void ts_capture_free(TsCaptureRecorder *recorder)
{
    if (recorder == NULL) return;
    free(recorder->buffer);
    free(recorder->overdub_base);
    ts_capture_init(recorder);
}

int ts_capture_arm(TsCaptureRecorder *recorder, int destination_slot,
                   size_t capacity_frames, uint32_t sample_rate,
                   char *error, size_t error_size)
{
    return ts_capture_arm_channels(recorder, destination_slot, capacity_frames,
                                   sample_rate, 1u, error, error_size);
}

int ts_capture_arm_channels(TsCaptureRecorder *recorder, int destination_slot,
                            size_t capacity_frames, uint32_t sample_rate,
                            uint8_t channels, char *error, size_t error_size)
{
    float *buffer;
    size_t scalar_count;
    if (recorder == NULL || destination_slot < 0 ||
        destination_slot >= TS_BANK_SLOT_COUNT || capacity_frames == 0u ||
        sample_rate == 0u ||
        !ts_sample_dimensions(capacity_frames, channels,
                              &scalar_count, NULL)) {
        set_error(error, error_size, "Invalid Capture destination or duration");
        return 0;
    }
    buffer = (float *)calloc(scalar_count, sizeof(*buffer));
    if (buffer == NULL) {
        set_error(error, error_size, "Out of memory preparing Capture tape");
        return 0;
    }
    free(recorder->buffer);
    free(recorder->overdub_base);
    ts_capture_init(recorder);
    recorder->buffer = buffer;
    recorder->capacity_frames = capacity_frames;
    recorder->sample_rate = sample_rate;
    recorder->channels = channels;
    recorder->destination_slot = destination_slot;
    recorder->state = TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER;
    set_error(error, error_size, "");
    return 1;
}

int ts_capture_arm_overdub(TsCaptureRecorder *recorder, int destination_slot,
                           size_t capacity_frames, uint32_t sample_rate,
                           const float *base, size_t base_frames,
                           uint32_t base_rate,
                           char *error, size_t error_size)
{
    return ts_capture_arm_overdub_channels(
        recorder, destination_slot, capacity_frames, sample_rate, 1u,
        base, base_frames, base_rate, 1u, error, error_size);
}

int ts_capture_arm_overdub_channels(
    TsCaptureRecorder *recorder, int destination_slot,
    size_t capacity_frames, uint32_t sample_rate, uint8_t channels,
    const float *base, size_t base_frames, uint32_t base_rate,
    uint8_t base_channels, char *error, size_t error_size)
{
    float *copy;
    size_t byte_count;
    if (base == NULL || base_frames == 0u || base_rate == 0u ||
        !ts_sample_dimensions(base_frames, base_channels, NULL, &byte_count) ||
        !ts_sample_valid_channels(channels)) {
        set_error(error, error_size, "Invalid Overdub target audio");
        return 0;
    }
    copy = (float *)malloc(byte_count);
    if (copy == NULL) {
        set_error(error, error_size, "Out of memory snapshotting Overdub target");
        return 0;
    }
    memcpy(copy, base, byte_count);
    if (!ts_capture_arm_channels(recorder, destination_slot, capacity_frames,
                                 sample_rate, channels, error, error_size)) {
        free(copy);
        return 0;
    }
    recorder->overdub_base = copy;
    recorder->overdub_base_frames = base_frames;
    recorder->overdub_base_rate = base_rate;
    recorder->overdub_base_channels = base_channels;
    recorder->overdub = 1;
    set_error(error, error_size, "");
    return 1;
}

int ts_capture_set_source(TsCaptureRecorder *recorder, int source_slot,
                          char *error, size_t error_size)
{
    if (recorder == NULL ||
        (recorder->state != TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER &&
         recorder->state != TS_CAPTURE_RECORDING)) {
        set_error(error, error_size, "Capture is not accepting a source");
        return 0;
    }
    if (source_slot != TS_CAPTURE_SOURCE_SYNTH &&
        source_slot != TS_CAPTURE_SOURCE_SISTER &&
        (source_slot < 0 || source_slot >= TS_BANK_SLOT_COUNT)) {
        set_error(error, error_size, "Invalid Capture source tile");
        return 0;
    }
    if (source_slot == recorder->destination_slot && !recorder->overdub) {
        set_error(error, error_size, "Capture source cannot be its destination");
        return 0;
    }
    recorder->source_slot = source_slot;
    recorder->provenance_slot = source_slot;
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

uint32_t ts_capture_shift_staged_notes(TsCaptureRecorder *recorder,
                                       int keyboard_base_delta)
{
    const uint32_t visible_notes = UINT32_C(0x00ffffff);
    if (recorder == NULL) return 0u;
    if (keyboard_base_delta >= 24 || keyboard_base_delta <= -24)
        recorder->staged_notes = 0u;
    else if (keyboard_base_delta > 0)
        recorder->staged_notes >>= keyboard_base_delta;
    else if (keyboard_base_delta < 0)
        recorder->staged_notes =
            (recorder->staged_notes << -keyboard_base_delta) & visible_notes;
    return recorder->staged_notes;
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
    if ((recorder->source_slot < 0 &&
         recorder->source_slot != TS_CAPTURE_SOURCE_SYNTH &&
         recorder->source_slot != TS_CAPTURE_SOURCE_SISTER) ||
        (recorder->source_slot == recorder->destination_slot &&
         !recorder->overdub)) {
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
    return ts_capture_write_frame(recorder, ts_stereo_frame_from_mono(sample));
}

int ts_capture_write_frame(TsCaptureRecorder *recorder, TsStereoFrame sample)
{
    if (recorder == NULL || recorder->state != TS_CAPTURE_RECORDING ||
        recorder->buffer == NULL ||
        recorder->recorded_frames >= recorder->capacity_frames)
        return 0;
    sample = ts_stereo_frame_sanitize(sample);
    if (recorder->channels == 1u)
        recorder->buffer[recorder->recorded_frames] =
            ts_stereo_frame_fold_mono(sample);
    else {
        size_t at = recorder->recorded_frames * 2u;
        recorder->buffer[at] = sample.l;
        recorder->buffer[at + 1u] = sample.r;
    }
    ++recorder->recorded_frames;
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

static size_t frames_from_ms(uint32_t sample_rate, int milliseconds)
{
    if (milliseconds <= 0) return 0u;
    return (size_t)(((uint64_t)sample_rate * (uint64_t)milliseconds + 999u) / 1000u);
}

static void external_store_frame(float *buffer, size_t frame, uint8_t channels,
                                 TsStereoFrame sample)
{
    sample = ts_stereo_frame_sanitize(sample);
    if (channels == 1u)
        buffer[frame] = ts_stereo_frame_fold_mono(sample);
    else {
        buffer[frame * 2u] = sample.l;
        buffer[frame * 2u + 1u] = sample.r;
    }
}

static TsStereoFrame external_load_frame(const float *buffer, size_t frame,
                                         uint8_t channels)
{
    if (channels == 1u) return ts_stereo_frame_from_mono(buffer[frame]);
    return ts_stereo_frame_sanitize((TsStereoFrame){
        buffer[frame * 2u], buffer[frame * 2u + 1u]});
}

static void external_push_pre_roll(TsExternalRecorder *recorder,
                                   TsStereoFrame sample)
{
    if (recorder->pre_roll_capacity == 0u || recorder->pre_roll == NULL) return;
    external_store_frame(recorder->pre_roll, recorder->pre_roll_write,
                         recorder->channels, sample);
    recorder->pre_roll_write = (recorder->pre_roll_write + 1u) % recorder->pre_roll_capacity;
    if (recorder->pre_roll_count < recorder->pre_roll_capacity)
        ++recorder->pre_roll_count;
}

static void external_commit_pre_roll(TsExternalRecorder *recorder)
{
    size_t first;
    size_t count;
    if (recorder->pre_roll_count == 0u || recorder->pre_roll == NULL) return;
    count = recorder->pre_roll_count;
    if (count > recorder->capacity_frames) count = recorder->capacity_frames;
    first = recorder->pre_roll_count == recorder->pre_roll_capacity ?
            recorder->pre_roll_write : 0u;
    for (size_t index = 0; index < count; ++index) {
        size_t source = (first + index) % recorder->pre_roll_capacity;
        TsStereoFrame sample = external_load_frame(
            recorder->pre_roll, source, recorder->channels);
        external_store_frame(recorder->buffer, recorder->recorded_frames++,
                             recorder->channels, sample);
    }
}

void ts_external_recorder_init(TsExternalRecorder *recorder)
{
    if (recorder == NULL) return;
    recorder->buffer = NULL;
    recorder->capacity_frames = 0u;
    recorder->recorded_frames = 0u;
    recorder->pre_roll = NULL;
    recorder->pre_roll_capacity = 0u;
    recorder->pre_roll_count = 0u;
    recorder->pre_roll_write = 0u;
    recorder->silence_frames = 0u;
    recorder->tail_frames = 0u;
    recorder->quiet_frames = 0u;
    recorder->sample_rate = 0u;
    recorder->channels = 1u;
    recorder->threshold_amplitude = 0.0f;
    recorder->threshold_db = 0;
    recorder->destination_slot = -1;
    recorder->stopped_early = 0;
    atomic_init(&recorder->state, TS_EXTERNAL_CAPTURE_IDLE);
}

void ts_external_recorder_free(TsExternalRecorder *recorder)
{
    if (recorder == NULL) return;
    free(recorder->buffer);
    free(recorder->pre_roll);
    ts_external_recorder_init(recorder);
}

int ts_external_recorder_arm(TsExternalRecorder *recorder,
                             int destination_slot,
                             uint32_t sample_rate,
                             int threshold_db,
                             int pre_roll_ms,
                             int silence_ms,
                             int tail_ms,
                             int max_seconds,
                             char *error, size_t error_size)
{
    return ts_external_recorder_arm_channels(
        recorder, destination_slot, sample_rate, 1u, threshold_db,
        pre_roll_ms, silence_ms, tail_ms, max_seconds, error, error_size);
}

int ts_external_recorder_arm_channels(
    TsExternalRecorder *recorder, int destination_slot,
    uint32_t sample_rate, uint8_t channels, int threshold_db,
    int pre_roll_ms, int silence_ms, int tail_ms, int max_seconds,
    char *error, size_t error_size)
{
    float *buffer;
    float *pre_roll = NULL;
    size_t capacity_frames;
    size_t pre_roll_frames;
    size_t capacity_scalars;
    size_t pre_roll_scalars;
    if (recorder == NULL || destination_slot < 0 || destination_slot >= 16 ||
        sample_rate == 0u || threshold_db < -90 || threshold_db > 0 ||
        pre_roll_ms < 0 || silence_ms < 1 || tail_ms < 0 ||
        max_seconds < 1 || max_seconds > 600 ||
        !ts_sample_valid_channels(channels)) {
        set_error(error, error_size, "Invalid external recording settings");
        return 0;
    }
    if ((uint64_t)sample_rate * (uint64_t)max_seconds > SIZE_MAX) {
        set_error(error, error_size, "External recording duration is too large");
        return 0;
    }
    capacity_frames = (size_t)sample_rate * (size_t)max_seconds;
    pre_roll_frames = frames_from_ms(sample_rate, pre_roll_ms);
    if (pre_roll_frames > capacity_frames) pre_roll_frames = capacity_frames;
    if (!ts_sample_dimensions(capacity_frames, channels,
                              &capacity_scalars, NULL) ||
        !ts_sample_dimensions(pre_roll_frames, channels,
                              &pre_roll_scalars, NULL)) {
        set_error(error, error_size, "External recording duration is too large");
        return 0;
    }
    buffer = (float *)calloc(capacity_scalars, sizeof(*buffer));
    if (buffer == NULL) {
        set_error(error, error_size, "Out of memory preparing external recording tape");
        return 0;
    }
    if (pre_roll_frames > 0u) {
        pre_roll = (float *)calloc(pre_roll_scalars, sizeof(*pre_roll));
        if (pre_roll == NULL) {
            free(buffer);
            set_error(error, error_size, "Out of memory preparing recording pre-roll");
            return 0;
        }
    }
    free(recorder->buffer);
    free(recorder->pre_roll);
    ts_external_recorder_init(recorder);
    recorder->buffer = buffer;
    recorder->capacity_frames = capacity_frames;
    recorder->pre_roll = pre_roll;
    recorder->pre_roll_capacity = pre_roll_frames;
    recorder->silence_frames = frames_from_ms(sample_rate, silence_ms);
    recorder->tail_frames = frames_from_ms(sample_rate, tail_ms);
    recorder->sample_rate = sample_rate;
    recorder->channels = channels;
    recorder->threshold_db = threshold_db;
    recorder->threshold_amplitude = powf(10.0f, (float)threshold_db / 20.0f);
    recorder->destination_slot = destination_slot;
    recorder->state = TS_EXTERNAL_CAPTURE_ARMED;
    set_error(error, error_size, "");
    return 1;
}

int ts_external_recorder_write_sample(TsExternalRecorder *recorder, float sample)
{
    return ts_external_recorder_write_frame(
        recorder, ts_stereo_frame_from_mono(sample));
}

int ts_external_recorder_write_frame(TsExternalRecorder *recorder,
                                     TsStereoFrame sample)
{
    float level;
    if (recorder == NULL || recorder->buffer == NULL) return 0;
    sample = ts_stereo_frame_sanitize(sample);
    level = fmaxf(fabsf(sample.l), fabsf(sample.r));
    if (recorder->state == TS_EXTERNAL_CAPTURE_ARMED) {
        int trigger_is_in_pre_roll;
        external_push_pre_roll(recorder, sample);
        if (level < recorder->threshold_amplitude) return 0;
        trigger_is_in_pre_roll = recorder->pre_roll_count > 0u;
        external_commit_pre_roll(recorder);
        if (!trigger_is_in_pre_roll &&
            recorder->recorded_frames < recorder->capacity_frames) {
            external_store_frame(recorder->buffer, recorder->recorded_frames++,
                                 recorder->channels, sample);
        }
        recorder->quiet_frames = 0u;
        recorder->state = TS_EXTERNAL_CAPTURE_RECORDING;
        if (recorder->recorded_frames >= recorder->capacity_frames) {
            recorder->state = TS_EXTERNAL_CAPTURE_COMPLETED;
            return 1;
        }
        return 2;
    }
    if (recorder->state != TS_EXTERNAL_CAPTURE_RECORDING) return 0;
    external_store_frame(recorder->buffer, recorder->recorded_frames++,
                         recorder->channels, sample);
    if (level >= recorder->threshold_amplitude) recorder->quiet_frames = 0u;
    else ++recorder->quiet_frames;
    if (recorder->recorded_frames >= recorder->capacity_frames ||
        recorder->quiet_frames >= recorder->silence_frames + recorder->tail_frames) {
        recorder->state = TS_EXTERNAL_CAPTURE_COMPLETED;
        recorder->stopped_early = recorder->recorded_frames < recorder->capacity_frames;
        return 1;
    }
    return 0;
}

int ts_external_recorder_stop(TsExternalRecorder *recorder,
                              char *error, size_t error_size)
{
    if (recorder == NULL || recorder->state != TS_EXTERNAL_CAPTURE_RECORDING) {
        set_error(error, error_size, "External input is not recording");
        return 0;
    }
    if (recorder->recorded_frames == 0u) {
        set_error(error, error_size, "External input has not recorded a frame yet");
        return 0;
    }
    recorder->stopped_early = recorder->recorded_frames < recorder->capacity_frames;
    recorder->state = TS_EXTERNAL_CAPTURE_COMPLETED;
    set_error(error, error_size, "");
    return 1;
}

int ts_external_recorder_cancel(TsExternalRecorder *recorder)
{
    if (recorder == NULL ||
        (recorder->state != TS_EXTERNAL_CAPTURE_ARMED &&
         recorder->state != TS_EXTERNAL_CAPTURE_RECORDING)) return 0;
    recorder->state = TS_EXTERNAL_CAPTURE_CANCELED;
    return 1;
}

float ts_external_recorder_progress(const TsExternalRecorder *recorder)
{
    if (recorder == NULL || recorder->capacity_frames == 0u) return 0.0f;
    if (recorder->recorded_frames >= recorder->capacity_frames) return 1.0f;
    return (float)((double)recorder->recorded_frames /
                   (double)recorder->capacity_frames);
}

const char *ts_external_capture_state_name(TsExternalCaptureState state)
{
    if (state == TS_EXTERNAL_CAPTURE_ARMED) return "ARMED";
    if (state == TS_EXTERNAL_CAPTURE_RECORDING) return "RECORDING";
    if (state == TS_EXTERNAL_CAPTURE_COMPLETED) return "COMPLETE";
    if (state == TS_EXTERNAL_CAPTURE_CANCELED) return "CANCELED";
    return "IDLE";
}

int ts_external_next_chain_slot(int destination_slot)
{
    return destination_slot >= 0 && destination_slot < 15 ? destination_slot + 1 : -1;
}
