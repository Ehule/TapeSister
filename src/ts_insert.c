#include "tapesister/insert.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

void ts_insert_default(TsInsertControls *c)
{ memset(c, 0, sizeof(*c)); }

int ts_insert_valid(const TsInsertControls *c)
{
    return c && c->send_pair >= 0 && c->send_pair <= 4 &&
        c->return_pair >= 0 && c->return_pair <= 3 &&
        isfinite(c->send_db) && c->send_db >= -24 && c->send_db <= 12 &&
        isfinite(c->return_db) && c->return_db >= -24 && c->return_db <= 12;
}

void ts_insert_init(TsInsert *s)
{
    memset(s, 0, sizeof(*s));
    ts_insert_default(&s->controls);
    ts_input_monitor_init(&s->return_monitor);
    ts_input_monitor_init(&s->send_monitor);
    atomic_init(&s->send_request, 0);
    atomic_init(&s->dedicated_return, 0);
    atomic_init(&s->port_request, 0);
    atomic_init(&s->port_ack, 0);
    atomic_init(&s->monitor_port_ack, 0);
    atomic_init(&s->input_channels, 0);
    atomic_init(&s->return_buffer_frames, 0);
    atomic_init(&s->output_buffer_frames, 0);
    s->send_gain = s->return_gain = s->send_target = s->return_target = 1;
    ts_insert_prepare(s, 48000);
}

void ts_insert_prepare(TsInsert *s, unsigned rate)
{
    if (!rate) return;
    s->sample_rate = rate;
    s->decay = expf(-1.0f / (rate * .15f));
    s->slew = 1.0f - expf(-1.0f / (rate * .01f));
}

static unsigned bridge_prime(unsigned producer_frames, unsigned consumer_frames,
                             unsigned producer_rate, unsigned consumer_rate)
{
    /* The ring is measured at the producer rate. Either callback may be the
       larger burst, regardless of the requested buffer size. Two bursts cover
       their scheduling phase and one late delivery without four-block latency. */
    uint64_t demand = consumer_rate ?
        ((uint64_t)consumer_frames * producer_rate + consumer_rate - 1u) / consumer_rate : 0;
    uint64_t burst = producer_frames > demand ? producer_frames : demand;
    return burst > TS_INPUT_MONITOR_MAX_PRIME_FRAMES / 2u ?
        TS_INPUT_MONITOR_MAX_PRIME_FRAMES : (unsigned)burst * 2u;
}

void ts_insert_begin_output_block(TsInsert *s, unsigned frames)
{
    atomic_store_explicit(&s->output_buffer_frames, frames, memory_order_release);
    unsigned rate = atomic_load_explicit(&s->return_monitor.input_rate, memory_order_acquire);
    unsigned capture = atomic_load_explicit(&s->return_buffer_frames, memory_order_acquire);
    ts_input_monitor_set_prime_frames(&s->return_monitor,
        bridge_prime(capture, frames, rate, s->sample_rate));
    ts_input_monitor_recover_backlog(&s->return_monitor);
    if (s->separate_send && s->send_channels)
        ts_input_monitor_note_capture_block(&s->send_monitor, frames);
}

void ts_insert_set(TsInsert *s, const TsInsertControls *c)
{
    TsInsertControls next = *c;
    if (!ts_insert_valid(&next)) ts_insert_default(&next);
    if (next.return_pair != s->controls.return_pair ||
        next.send_pair != s->controls.send_pair) {
        unsigned generation = (atomic_load(&s->port_request) & ~7u) + 8u;
        unsigned port = next.send_pair ? (unsigned)next.return_pair + 1u : 0u;
        atomic_store_explicit(&s->port_request, generation | port, memory_order_release);
        generation = (atomic_load(&s->send_request) & ~7u) + 8u;
        atomic_store_explicit(&s->send_request, generation | (unsigned)next.send_pair, memory_order_release);
    }
    s->controls = next;
    s->send_target = powf(10, next.send_db / 20);
    s->return_target = powf(10, next.return_db / 20);
}

void ts_insert_capture_prepare(TsInsert *s, unsigned channels,
                               unsigned rate, unsigned buffer_frames)
{ ts_insert_capture_prepare_from(s, channels, rate, buffer_frames, 0); }

void ts_insert_capture_prepare_from(TsInsert *s, unsigned channels,
    unsigned rate, unsigned buffer_frames, int dedicated)
{
    if (atomic_load_explicit(&s->dedicated_return, memory_order_acquire) != dedicated) return;
    if (channels > 255) channels = 0;
    atomic_store_explicit(&s->input_channels, 0, memory_order_release);
    atomic_store_explicit(&s->return_monitor.enabled, 0, memory_order_release);
    atomic_store_explicit(&s->return_monitor.input_rate, rate, memory_order_release);
    atomic_store_explicit(&s->return_buffer_frames, buffer_frames, memory_order_release);
    ts_input_monitor_set_prime_frames(&s->return_monitor,
        bridge_prime(buffer_frames, atomic_load_explicit(&s->output_buffer_frames,
            memory_order_acquire), rate, s->sample_rate));
    atomic_store(&s->return_monitor.underrun_count, 0);
    atomic_store(&s->return_monitor.dropped_frame_count, 0);
    atomic_store(&s->return_monitor.capture_callback_count, 0);
    atomic_store(&s->return_monitor.captured_frame_count, 0);
    atomic_store(&s->return_monitor.largest_capture_block_frames, 0);
    unsigned request=atomic_load_explicit(&s->port_request,memory_order_relaxed);
    atomic_store_explicit(&s->port_request,request+8u,memory_order_release);
    atomic_store_explicit(&s->return_monitor.enabled, channels >= 2, memory_order_release);
    atomic_store_explicit(&s->input_channels, channels, memory_order_release);
}

void ts_insert_capture(TsInsert *s, const float *input, size_t frames,
                       unsigned channels)
{ ts_insert_capture_from(s, input, frames, channels, 0); }

void ts_insert_capture_from(TsInsert *s, const float *input, size_t frames,
                            unsigned channels, int dedicated)
{
    if (!s || !input || channels < 2 || channels > 255) return;
    if (atomic_load_explicit(&s->dedicated_return, memory_order_acquire) != dedicated) return;
    if(atomic_load_explicit(&s->input_channels,memory_order_acquire)!=channels)return;
    unsigned request = atomic_load_explicit(&s->port_request, memory_order_acquire);
    unsigned port = request & 7u;
    atomic_store_explicit(&s->port_ack, request, memory_order_release);
    if (!port || port * 2u > channels) return;
    unsigned first = (port - 1u) * 2u;
    if (frames > atomic_load_explicit(&s->return_buffer_frames, memory_order_relaxed))
        atomic_store_explicit(&s->return_buffer_frames, (unsigned)frames, memory_order_release);
    ts_input_monitor_note_capture_block(&s->return_monitor, (uint32_t)frames);
    for (size_t i = 0; i < frames; ++i) {
        TsStereoFrame sample = {input[i * channels + first], input[i * channels + first + 1]};
        ts_input_monitor_push_frame(&s->return_monitor, sample);
    }
}

TsStereoFrame ts_insert_external_frame(const TsInsert *s,
    const float *frame, unsigned channels, int mode)
{
    if (!s) return ts_input_channel_select(frame, channels, mode);
    unsigned port = ts_insert_reserved_port(s);
    if (!port) return ts_input_channel_select(frame, channels, mode);
    return ts_input_channel_select_excluding(frame, channels, mode, 3u << ((port - 1u) * 2u));
}

int ts_insert_send_available(const TsInsert *s)
{
    if (s->separate_send) return s->controls.send_pair > 0 &&
        s->send_channels <= 255 &&
        (unsigned)s->controls.send_pair * 2u <= s->send_channels;
    return s->controls.send_pair > 0 && s->output_channels <= 255 &&
        (unsigned)(s->controls.send_pair + 1) * 2u <= s->output_channels;
}

int ts_insert_return_available(const TsInsert *s)
{
    return s->controls.send_pair > 0 &&
        (unsigned)(s->controls.return_pair + 1) * 2u <= atomic_load_explicit(&s->input_channels, memory_order_acquire);
}

static TsStereoFrame bounded(TsStereoFrame f, float gain)
{
    f = ts_stereo_frame_sanitize(f);
    f.l *= gain; f.r *= gain;
    float peak = fmaxf(fabsf(f.l), fabsf(f.r));
    if (!isfinite(peak)) return (TsStereoFrame){0, 0};
    if (peak > 1) { f.l /= peak; f.r /= peak; }
    return f;
}

TsStereoFrame ts_insert_process(TsInsert *s, TsStereoFrame input, float route_gain)
{
    s->send_gain += (s->send_target - s->send_gain) * s->slew;
    s->return_gain += (s->return_target - s->return_gain) * s->slew;
    s->send = ts_insert_send_available(s) ? bounded(input, s->send_gain * route_gain) : (TsStereoFrame){0, 0};
    s->send_peak = fmaxf(fmaxf(fabsf(s->send.l), fabsf(s->send.r)), s->send_peak * s->decay);
    unsigned request = atomic_load_explicit(&s->port_request, memory_order_acquire);
    TsStereoFrame result = {0, 0};
    if (s->port_seen != request) {
        unsigned acknowledged=atomic_load_explicit(&s->port_ack,memory_order_acquire);
        ts_input_monitor_discard(&s->return_monitor);
        if (acknowledged == request) s->port_seen = request;
    } else if (ts_insert_return_available(s)) {
        result = bounded(ts_input_monitor_read_frame(&s->return_monitor, s->sample_rate), s->return_gain);
    }
    s->return_peak = fmaxf(fmaxf(fabsf(result.l), fabsf(result.r)), s->return_peak * s->decay);
    /* An active but unavailable Insert is silent. Never substitute dry here. */
    if (!ts_insert_send_available(s)) result = (TsStereoFrame){0, 0};
    return result;
}

void ts_insert_write_output(TsInsert *s, float *frame, unsigned channels, TsStereoFrame master)
{
    for (unsigned i = 0; i < channels; ++i) frame[i] = 0;
    if (channels == 1) frame[0] = ts_stereo_frame_fold_mono(master);
    if (channels < 2) return;
    frame[0] = master.l; frame[1] = master.r;
    if (s->separate_send) {
        if (s->send_channels) ts_input_monitor_push_frame(&s->send_monitor, s->send);
        return;
    }
    if (ts_insert_send_available(s) && (unsigned)(s->controls.send_pair + 1) * 2u <= channels) {
        unsigned first = (unsigned)s->controls.send_pair * 2u;
        frame[first] = s->send.l; frame[first + 1] = s->send.r;
    }
}

unsigned ts_insert_reserved_port(const TsInsert *s)
{
    return atomic_load_explicit(&s->dedicated_return, memory_order_acquire) ? 0u :
        atomic_load_explicit(&s->port_request, memory_order_acquire) & 7u;
}

void ts_insert_io_mode(TsInsert *s, int separate_send, int dedicated_return)
{
    s->separate_send = separate_send != 0;
    atomic_store_explicit(&s->dedicated_return, dedicated_return != 0, memory_order_release);
    atomic_store_explicit(&s->input_channels, 0, memory_order_release);
    atomic_fetch_add_explicit(&s->port_request, 8u, memory_order_release);
}

void ts_insert_send_prepare(TsInsert *s, unsigned channels, unsigned rate, unsigned buffer_frames)
{
    s->send_channels = channels >= 2 && channels <= 255 ? channels : 0;
    ts_input_monitor_set_enabled(&s->send_monitor, s->send_channels != 0, rate);
    atomic_store_explicit(&s->output_buffer_frames, buffer_frames, memory_order_release);
    ts_input_monitor_set_prime_frames(&s->send_monitor,
        bridge_prime(buffer_frames, 0, rate, rate));
    ts_input_monitor_discard(&s->send_monitor);
    unsigned generation = (atomic_load(&s->send_request) & ~7u) + 8u;
    atomic_store_explicit(&s->send_request, generation | (unsigned)s->controls.send_pair, memory_order_release);
}

void ts_insert_render_send(TsInsert *s, float *out, size_t frames, unsigned channels, unsigned rate)
{
    memset(out, 0, frames * channels * sizeof(*out));
    unsigned producer_rate = atomic_load_explicit(&s->send_monitor.input_rate, memory_order_acquire);
    unsigned producer_frames = atomic_load_explicit(&s->output_buffer_frames, memory_order_acquire);
    ts_input_monitor_set_prime_frames(&s->send_monitor,
        bridge_prime(producer_frames, (unsigned)frames, producer_rate, rate));
    unsigned request = atomic_load_explicit(&s->send_request, memory_order_acquire);
    if (request != s->send_seen) {
        ts_input_monitor_discard(&s->send_monitor);
        s->send_seen = request;
        return;
    }
    unsigned pair = request & 7u;
    if (!pair || pair * 2u > channels) return;
    unsigned first = (pair - 1u) * 2u;
    ts_input_monitor_recover_backlog(&s->send_monitor);
    for (size_t i = 0; i < frames; ++i) {
        TsStereoFrame f = ts_input_monitor_read_frame(&s->send_monitor, rate);
        out[i * channels + first] = f.l;
        out[i * channels + first + 1] = f.r;
    }
}

int ts_insert_write(FILE *f, const TsInsertControls *controls)
{
    TsInsertControls c = *controls;
    if (!ts_insert_valid(&c)) ts_insert_default(&c);
    return fprintf(f, "Insert.SendPair=%d\nInsert.ReturnPair=%d\nInsert.SendDb=%.9g\nInsert.ReturnDb=%.9g\n",
                   c.send_pair, c.return_pair, c.send_db, c.return_db) >= 0;
}

int ts_insert_read(TsInsertControls *c, const char *key, const char *value)
{
    char *end; errno = 0; double n = strtod(value, &end);
    if (errno || end == value || *end || !isfinite(n)) return -1;
    TsInsertControls next = *c;
    if (!strcmp(key, "Insert.SendPair") || !strcmp(key, "Insert.ReturnPair")) {
        int maximum = !strcmp(key, "Insert.SendPair") ? 4 : 3;
        if (n < 0 || n > maximum || n != floor(n)) return -1;
        if (!strcmp(key, "Insert.SendPair")) next.send_pair = (int)n;
        else next.return_pair = (int)n;
    } else if (!strcmp(key, "Insert.SendDb")) next.send_db = (float)n;
    else if (!strcmp(key, "Insert.ReturnDb")) next.return_db = (float)n;
    else return 0;
    if (!ts_insert_valid(&next)) return -1;
    *c = next; return 1;
}
