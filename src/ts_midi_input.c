#include "tapesister/midi_input.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef TAPESISTER_HAS_MIDI
#include "rtmidi/rtmidi_c.h"
#endif

enum { TS_MIDI_QUEUE_CAPACITY = 256, TS_MIDI_NAME_MAX = 1024 };

struct TsMidiInput {
#ifdef TAPESISTER_HAS_MIDI
    RtMidiInPtr device;
#endif
    char **port_names;
    int port_count;
    char active_name[TS_MIDI_NAME_MAX];
    TsMidiEvent queue[TS_MIDI_QUEUE_CAPACITY];
    atomic_uint read_index;
    atomic_uint write_index;
    atomic_uint dropped;
    atomic_int accepting;
    atomic_int channel;
    int active;
};

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0u)
        snprintf(error, error_size, "%s", message != NULL ? message : "");
}

static void free_ports(TsMidiInput *input)
{
    if (input == NULL) return;
    for (int i = 0; i < input->port_count; ++i) free(input->port_names[i]);
    free(input->port_names);
    input->port_names = NULL;
    input->port_count = 0;
}

#ifdef TAPESISTER_HAS_MIDI
static void clear_queue(TsMidiInput *input)
{
    if (input == NULL) return;
    atomic_store_explicit(&input->read_index, 0u, memory_order_relaxed);
    atomic_store_explicit(&input->write_index, 0u, memory_order_relaxed);
    atomic_store_explicit(&input->dropped, 0u, memory_order_relaxed);
}

static char *port_name(RtMidiPtr device, unsigned int port)
{
    int length = 0;
    char *name;
    rtmidi_get_port_name(device, port, NULL, &length);
    if (!device->ok || length <= 0) return NULL;
    name = (char *)malloc((size_t)length + 1u);
    if (name == NULL) return NULL;
    rtmidi_get_port_name(device, port, name, &length);
    if (!device->ok) {
        free(name);
        return NULL;
    }
    name[length] = '\0';
    return name;
}

static void close_device(TsMidiInput *input)
{
    if (input == NULL) return;
    atomic_store_explicit(&input->accepting, 0, memory_order_release);
    if (input->device != NULL) {
        if (input->active) {
            rtmidi_in_cancel_callback(input->device);
            rtmidi_close_port(input->device);
        }
        rtmidi_in_free(input->device);
        input->device = NULL;
    }
    input->active = 0;
    input->active_name[0] = '\0';
    clear_queue(input);
}

static void midi_callback(double timestamp, const unsigned char *message,
                          size_t message_size, void *user_data)
{
    TsMidiInput *input = (TsMidiInput *)user_data;
    TsMidiEvent event;
    unsigned write_index;
    unsigned next;
    unsigned read_index;
    int wanted_channel;
    (void)timestamp;
    if (input == NULL || message == NULL || message_size < 2u ||
        !atomic_load_explicit(&input->accepting, memory_order_acquire))
        return;
    if (!ts_midi_decode_short_message(
            message[0], message[1], message_size >= 3u ? message[2] : 0u,
            &event))
        return;
    wanted_channel = atomic_load_explicit(&input->channel, memory_order_relaxed);
    if (wanted_channel > 0 && event.channel + 1 != wanted_channel) return;
    write_index = atomic_load_explicit(&input->write_index, memory_order_relaxed);
    next = (write_index + 1u) % TS_MIDI_QUEUE_CAPACITY;
    read_index = atomic_load_explicit(&input->read_index, memory_order_acquire);
    if (next == read_index) {
        atomic_fetch_add_explicit(&input->dropped, 1u, memory_order_relaxed);
        return;
    }
    input->queue[write_index] = event;
    atomic_store_explicit(&input->write_index, next, memory_order_release);
}
#endif

TsMidiInput *ts_midi_input_create(void)
{
    TsMidiInput *input = (TsMidiInput *)calloc(1u, sizeof(*input));
    if (input == NULL) return NULL;
    atomic_init(&input->read_index, 0u);
    atomic_init(&input->write_index, 0u);
    atomic_init(&input->dropped, 0u);
    atomic_init(&input->accepting, 0);
    atomic_init(&input->channel, 0);
    return input;
}

void ts_midi_input_destroy(TsMidiInput *input)
{
    if (input == NULL) return;
#ifdef TAPESISTER_HAS_MIDI
    close_device(input);
#endif
    free_ports(input);
    free(input);
}

int ts_midi_input_rescan(TsMidiInput *input, char *error, size_t error_size)
{
    if (input == NULL) {
        set_error(error, error_size, "Invalid MIDI input state");
        return 0;
    }
#ifdef TAPESISTER_HAS_MIDI
    {
        RtMidiInPtr probe = rtmidi_in_create_default();
        unsigned int available;
        char **names;
        int kept = 0;
        if (probe == NULL || !probe->ok) {
            if (probe != NULL) rtmidi_in_free(probe);
            set_error(error, error_size, "MIDI backend unavailable");
            return 0;
        }
        available = rtmidi_get_port_count(probe);
        names = available > 0u ?
            (char **)calloc((size_t)available, sizeof(*names)) : NULL;
        if (available > 0u && names == NULL) {
            rtmidi_in_free(probe);
            set_error(error, error_size, "Out of memory while scanning MIDI inputs");
            return 0;
        }
        for (unsigned int port = 0u; port < available; ++port) {
            char *name = port_name(probe, port);
            if (name != NULL) names[kept++] = name;
        }
        rtmidi_in_free(probe);
        free_ports(input);
        input->port_names = names;
        input->port_count = kept;
    }
    set_error(error, error_size, "");
    return 1;
#else
    free_ports(input);
    set_error(error, error_size, "MIDI support is unavailable in this build");
    return 0;
#endif
}

int ts_midi_input_configure(TsMidiInput *input, const char *device_name,
                            int channel, char *error, size_t error_size)
{
    if (input == NULL || channel < 0 || channel > 16) {
        set_error(error, error_size, "Invalid MIDI input configuration");
        return 0;
    }
    atomic_store_explicit(&input->channel, channel, memory_order_relaxed);
#ifdef TAPESISTER_HAS_MIDI
    int selected = -1;
    close_device(input);
    if (device_name != NULL && strcmp(device_name, "OFF") == 0) {
        snprintf(input->active_name, sizeof(input->active_name), "OFF");
        set_error(error, error_size, "");
        return 1;
    }
    if (!ts_midi_input_rescan(input, error, error_size)) return 0;
    if (input->port_count == 0) {
        set_error(error, error_size, "No MIDI input devices found");
        return device_name == NULL || device_name[0] == '\0';
    }
    if (device_name == NULL || device_name[0] == '\0') selected = 0;
    else {
        for (int i = 0; i < input->port_count; ++i)
            if (strcmp(device_name, input->port_names[i]) == 0) {
                selected = i;
                break;
            }
    }
    if (selected < 0) {
        set_error(error, error_size, "Configured MIDI input was not found");
        return 0;
    }
    input->device = rtmidi_in_create_default();
    if (input->device == NULL || !input->device->ok) {
        close_device(input);
        set_error(error, error_size, "Could not initialize MIDI input");
        return 0;
    }
    rtmidi_open_port(input->device, (unsigned int)selected,
                     "TapeSister MIDI Input");
    if (!input->device->ok) {
        close_device(input);
        set_error(error, error_size, "Could not open the selected MIDI input");
        return 0;
    }
    rtmidi_in_ignore_types(input->device, true, true, true);
    rtmidi_in_set_callback(input->device, midi_callback, input);
    if (!input->device->ok) {
        close_device(input);
        set_error(error, error_size, "Could not start the MIDI input callback");
        return 0;
    }
    input->active = 1;
    snprintf(input->active_name, sizeof(input->active_name), "%s",
             input->port_names[selected]);
    atomic_store_explicit(&input->accepting, 1, memory_order_release);
    set_error(error, error_size, "");
    return 1;
#else
    (void)device_name;
    set_error(error, error_size, "MIDI support is unavailable in this build");
    return 0;
#endif
}

int ts_midi_input_poll(TsMidiInput *input, TsMidiEvent *event)
{
    unsigned read_index;
    unsigned write_index;
    if (input == NULL || event == NULL) return 0;
    if (atomic_exchange_explicit(&input->dropped, 0u, memory_order_acq_rel) > 0u) {
        /* Discard everything that preceded the overflow. Keeping those events
           after panic could replay a Note On whose matching Note Off was the
           message that got dropped. Events published after this snapshot stay
           queued and are handled normally on the next poll. */
        unsigned current_write = atomic_load_explicit(
            &input->write_index, memory_order_acquire);
        atomic_store_explicit(&input->read_index, current_write,
                              memory_order_release);
        memset(event, 0, sizeof(*event));
        event->action = TS_MIDI_ACTION_PANIC;
        event->channel = -1;
        return 1;
    }
    read_index = atomic_load_explicit(&input->read_index, memory_order_relaxed);
    write_index = atomic_load_explicit(&input->write_index, memory_order_acquire);
    if (read_index == write_index) return 0;
    *event = input->queue[read_index];
    atomic_store_explicit(&input->read_index,
                          (read_index + 1u) % TS_MIDI_QUEUE_CAPACITY,
                          memory_order_release);
    return 1;
}

int ts_midi_input_port_count(const TsMidiInput *input)
{
    return input != NULL ? input->port_count : 0;
}

const char *ts_midi_input_port_name(const TsMidiInput *input, int index)
{
    if (input == NULL || index < 0 || index >= input->port_count) return NULL;
    return input->port_names[index];
}

const char *ts_midi_input_active_name(const TsMidiInput *input)
{
    return input != NULL ? input->active_name : "";
}

int ts_midi_input_is_active(const TsMidiInput *input)
{
    return input != NULL && input->active;
}
