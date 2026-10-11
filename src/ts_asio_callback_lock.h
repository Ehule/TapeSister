#ifndef TS_ASIO_CALLBACK_LOCK_H
#define TS_ASIO_CALLBACK_LOCK_H
#include <cstdint>

enum class TsAsioLock { acquired, recovered, skipped };

/* Spend at most 1/8 of a period, capped at 0.5 ms, on a short UI overlap.
   Compute this when opening the stream, not for every rendered block. */
inline uint64_t ts_asio_lock_budget(unsigned frames, unsigned rate, uint64_t frequency)
{
    if (!rate) return 0;
    const double period = double(frames) / rate / 8;
    return uint64_t(frequency * (period < .0005 ? period : .0005));
}

/* Ops supplies a nonblocking try_lock (0=owned, 1=busy, -1=error), a monotonic
   clock and a CPU pause hint. No scheduler sleep, allocation or unbounded wait.
   Injection keeps deadline tests deterministic even on overloaded CI hosts. */
template<class Ops> TsAsioLock ts_asio_callback_lock(Ops &ops, uint64_t budget)
{
    int result = ops.try_lock();
    if (!result) return TsAsioLock::acquired;
    if (result < 0 || !budget) return TsAsioLock::skipped;
    const uint64_t start = ops.now();
    while (ops.now() - start < budget) {
        ops.pause();
        result = ops.try_lock();
        if (!result) return TsAsioLock::recovered;
        if (result < 0) break;
    }
    return TsAsioLock::skipped;
}
#endif
