#include "tapesister/sister_wave_snapshot.h"

#include <float.h>
#include <string.h>

static uint32_t float_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static float bits_float(uint32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static TsSisterWaveBin empty_bin(void)
{
    return (TsSisterWaveBin){FLT_MAX, -FLT_MAX, FLT_MAX, -FLT_MAX};
}

static void publish_bin(TsSisterWavePublisher *publisher, size_t bin)
{
    TsSisterWaveBin value = publisher->current;
    if (value.left_minimum == FLT_MAX) value = (TsSisterWaveBin){0};
    atomic_fetch_add_explicit(&publisher->revision, 1u, memory_order_acq_rel);
    atomic_store_explicit(&publisher->values[bin][0],
                          float_bits(value.left_minimum), memory_order_relaxed);
    atomic_store_explicit(&publisher->values[bin][1],
                          float_bits(value.left_maximum), memory_order_relaxed);
    atomic_store_explicit(&publisher->values[bin][2],
                          float_bits(value.right_minimum), memory_order_relaxed);
    atomic_store_explicit(&publisher->values[bin][3],
                          float_bits(value.right_maximum), memory_order_relaxed);
    atomic_store_explicit(&publisher->write_bin, bin, memory_order_relaxed);
    atomic_store_explicit(&publisher->valid_bins, publisher->valid_bins_writer,
                          memory_order_relaxed);
    atomic_fetch_add_explicit(&publisher->revision, 1u, memory_order_release);
}

void ts_sister_wave_publisher_init(TsSisterWavePublisher *publisher)
{
    if (publisher == NULL) return;
    memset(publisher, 0, sizeof(*publisher));
    atomic_init(&publisher->revision, 0u);
    atomic_init(&publisher->write_bin, 0u);
    atomic_init(&publisher->valid_bins, 0u);
    atomic_init(&publisher->channels, 2u);
    for (size_t bin = 0u; bin < TS_SISTER_WAVE_BIN_COUNT; ++bin)
        for (size_t value = 0u; value < 4u; ++value)
            atomic_init(&publisher->values[bin][value], float_bits(0.0f));
    publisher->current = empty_bin();
}

void ts_sister_wave_publisher_clear(TsSisterWavePublisher *publisher,
                                    uint8_t channels)
{
    if (publisher == NULL) return;
    atomic_fetch_add_explicit(&publisher->revision, 1u, memory_order_acq_rel);
    for (size_t bin = 0u; bin < TS_SISTER_WAVE_BIN_COUNT; ++bin)
        for (size_t value = 0u; value < 4u; ++value)
            atomic_store_explicit(&publisher->values[bin][value],
                                  float_bits(0.0f), memory_order_relaxed);
    publisher->current = empty_bin();
    publisher->current_bin = 0u;
    publisher->valid_bins_writer = 0u;
    publisher->frames_since_publish = 0u;
    publisher->initialized = 0;
    atomic_store_explicit(&publisher->write_bin, 0u, memory_order_relaxed);
    atomic_store_explicit(&publisher->valid_bins, 0u, memory_order_relaxed);
    atomic_store_explicit(&publisher->channels, channels == 1u ? 1u : 2u,
                          memory_order_relaxed);
    atomic_fetch_add_explicit(&publisher->revision, 1u, memory_order_release);
}

void ts_sister_wave_publisher_push(TsSisterWavePublisher *publisher,
                                   TsStereoFrame frame, size_t frame_position,
                                   size_t capacity_frames, uint8_t channels,
                                   int written)
{
    size_t bin;
    if (publisher == NULL || !written || capacity_frames == 0u) return;
    bin = frame_position * TS_SISTER_WAVE_BIN_COUNT / capacity_frames;
    if (bin >= TS_SISTER_WAVE_BIN_COUNT) bin = TS_SISTER_WAVE_BIN_COUNT - 1u;
    if (!publisher->initialized || publisher->current_bin != bin) {
        if (publisher->initialized) publish_bin(publisher, publisher->current_bin);
        publisher->current = empty_bin();
        publisher->current_bin = bin;
        publisher->initialized = 1;
        if (publisher->valid_bins_writer < TS_SISTER_WAVE_BIN_COUNT)
            ++publisher->valid_bins_writer;
    }
    if (frame.l < publisher->current.left_minimum) publisher->current.left_minimum = frame.l;
    if (frame.l > publisher->current.left_maximum) publisher->current.left_maximum = frame.l;
    if (frame.r < publisher->current.right_minimum) publisher->current.right_minimum = frame.r;
    if (frame.r > publisher->current.right_maximum) publisher->current.right_maximum = frame.r;
    atomic_store_explicit(&publisher->channels, channels == 1u ? 1u : 2u,
                          memory_order_relaxed);
    if (++publisher->frames_since_publish >= 64u) {
        publisher->frames_since_publish = 0u;
        publish_bin(publisher, bin);
    }
}

int ts_sister_wave_snapshot_get(const TsSisterWavePublisher *publisher,
                                TsSisterWaveSnapshot *snapshot)
{
    if (publisher == NULL || snapshot == NULL) return 0;
    for (int attempt = 0; attempt < 4; ++attempt) {
        uint64_t before = atomic_load_explicit(&publisher->revision, memory_order_acquire);
        uint64_t after;
        if ((before & 1u) != 0u) continue;
        for (size_t bin = 0u; bin < TS_SISTER_WAVE_BIN_COUNT; ++bin) {
            snapshot->bins[bin].left_minimum = bits_float((uint32_t)atomic_load_explicit(&publisher->values[bin][0], memory_order_relaxed));
            snapshot->bins[bin].left_maximum = bits_float((uint32_t)atomic_load_explicit(&publisher->values[bin][1], memory_order_relaxed));
            snapshot->bins[bin].right_minimum = bits_float((uint32_t)atomic_load_explicit(&publisher->values[bin][2], memory_order_relaxed));
            snapshot->bins[bin].right_maximum = bits_float((uint32_t)atomic_load_explicit(&publisher->values[bin][3], memory_order_relaxed));
        }
        snapshot->write_bin = (size_t)atomic_load_explicit(&publisher->write_bin, memory_order_relaxed);
        snapshot->valid_bins = (size_t)atomic_load_explicit(&publisher->valid_bins, memory_order_relaxed);
        snapshot->channels = (uint8_t)atomic_load_explicit(&publisher->channels, memory_order_relaxed);
        after = atomic_load_explicit(&publisher->revision, memory_order_acquire);
        if (before == after && (after & 1u) == 0u) {
            snapshot->revision = after;
            return 1;
        }
    }
    return 0;
}
