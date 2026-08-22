#ifndef TAPESISTER_MIDI_INPUT_H
#define TAPESISTER_MIDI_INPUT_H

#include <stddef.h>

#include "tapesister/note_event.h"

typedef struct TsMidiInput TsMidiInput;

TsMidiInput *ts_midi_input_create(void);
void ts_midi_input_destroy(TsMidiInput *input);
int ts_midi_input_rescan(TsMidiInput *input, char *error, size_t error_size);
int ts_midi_input_configure(TsMidiInput *input, const char *device_name,
                            int channel, char *error, size_t error_size);
int ts_midi_input_poll(TsMidiInput *input, TsMidiEvent *event);
int ts_midi_input_port_count(const TsMidiInput *input);
const char *ts_midi_input_port_name(const TsMidiInput *input, int index);
const char *ts_midi_input_active_name(const TsMidiInput *input);
int ts_midi_input_is_active(const TsMidiInput *input);

#endif
