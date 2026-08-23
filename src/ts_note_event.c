#include "tapesister/note_event.h"

#include <string.h>

int ts_note_event_qwerty(TsNoteEvent *event, int key, int keyboard_base_note)
{
    if (event == NULL || key < 0 || key >= 24 || keyboard_base_note < 0 ||
        keyboard_base_note + key > 127)
        return 0;
    event->origin = TS_NOTE_ORIGIN_QWERTY;
    event->key = key;
    event->midi_note = keyboard_base_note + key;
    event->velocity = 127;
    event->channel = -1;
    return 1;
}

int ts_note_event_midi(TsNoteEvent *event, int midi_note, int velocity,
                       int channel)
{
    if (event == NULL || midi_note < 0 || midi_note > 127 || velocity < 0 ||
        velocity > 127 || channel < 0 || channel > 15)
        return 0;
    event->origin = TS_NOTE_ORIGIN_MIDI;
    event->key = midi_note;
    event->midi_note = midi_note;
    event->velocity = velocity;
    event->channel = channel;
    return 1;
}

int ts_note_event_same_trigger(const TsNoteEvent *event,
                               TsNoteOrigin origin, int key, int channel)
{
    if (event == NULL || event->origin != origin || event->key != key) return 0;
    return origin != TS_NOTE_ORIGIN_MIDI || event->channel == channel;
}

float ts_note_event_gain(const TsNoteEvent *event)
{
    if (event == NULL) return 1.0f;
    if (event->origin != TS_NOTE_ORIGIN_MIDI) return 1.0f;
    if (event->velocity <= 0) return 0.0f;
    if (event->velocity >= 127) return 1.0f;
    return (float)event->velocity / 127.0f;
}

int ts_midi_decode_short_message(uint8_t status, uint8_t data1, uint8_t data2,
                                 TsMidiEvent *event)
{
    uint8_t kind = status & 0xf0u;
    int channel = status & 0x0fu;
    if (event == NULL || status < 0x80u || status >= 0xf0u) return 0;
    memset(event, 0, sizeof(*event));
    event->channel = channel;
    data1 &= 0x7fu;
    data2 &= 0x7fu;
    if (kind == 0x90u && data2 > 0u) {
        event->action = TS_MIDI_ACTION_NOTE_ON;
        return ts_note_event_midi(&event->note, data1, data2, channel);
    }
    if (kind == 0x80u || (kind == 0x90u && data2 == 0u)) {
        event->action = TS_MIDI_ACTION_NOTE_OFF;
        return ts_note_event_midi(&event->note, data1, 0, channel);
    }
    if (kind == 0xb0u && (data1 == 120u || data1 == 123u)) {
        event->action = TS_MIDI_ACTION_PANIC;
        return 1;
    }
    return 0;
}
