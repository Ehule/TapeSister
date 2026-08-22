#ifndef TAPESISTER_NOTE_EVENT_H
#define TAPESISTER_NOTE_EVENT_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    TS_NOTE_ORIGIN_QWERTY = 0,
    TS_NOTE_ORIGIN_MIDI
} TsNoteOrigin;

typedef struct {
    TsNoteOrigin origin;
    int key;
    int midi_note;
    int velocity;
    int channel;
} TsNoteEvent;

typedef enum {
    TS_MIDI_ACTION_NONE = 0,
    TS_MIDI_ACTION_NOTE_ON,
    TS_MIDI_ACTION_NOTE_OFF,
    TS_MIDI_ACTION_PANIC
} TsMidiAction;

typedef struct {
    TsMidiAction action;
    TsNoteEvent note;
    int channel;
} TsMidiEvent;

int ts_note_event_qwerty(TsNoteEvent *event, int key, int keyboard_base_note);
int ts_note_event_midi(TsNoteEvent *event, int midi_note, int velocity,
                       int channel);
int ts_note_event_same_trigger(const TsNoteEvent *event,
                               TsNoteOrigin origin, int key, int channel);
float ts_note_event_gain(const TsNoteEvent *event);
int ts_midi_decode_short_message(uint8_t status, uint8_t data1, uint8_t data2,
                                 TsMidiEvent *event);

#endif
