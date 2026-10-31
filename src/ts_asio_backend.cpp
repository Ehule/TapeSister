/* RtAudio handles ASIO driver discovery, native sample formats and the Windows
   driver ABI. This adapter keeps TapeSister on a single synchronous stream. */
#include "tapesister/asio_backend.h"
#include "RtAudio.h"
#include "ts_asio_callback_lock.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr SDL_AudioDeviceID base = 0x50000000;
struct Endpoint { SDL_AudioSpec spec{}; SDL_AudioDeviceID id{}; bool paused{true}; } endpoints[2];
std::unique_ptr<RtAudio> driver;
std::vector<RtAudio::DeviceInfo> devices;
SDL_mutex *mutex;
std::atomic<unsigned> xruns{0}, driverXruns{0}, controlSkips{0}, controlRecoveries{0};
std::atomic<bool> lost{false};
bool started, notified;
unsigned inputs, outputs, frames, rate, serial, nativeInputs, nativeOutputs;
TsAudioChannelRange ranges[2] = {{1,0},{1,0}};
std::vector<float> inputBuffer, outputBuffer;
uint64_t lockRetryTicks;
std::string active;
const float *currentInput;

void errorCallback(RtAudioErrorType type, const std::string &message)
{
    /* Callback may run on a driver thread. No UI/driver calls here. */
    if (type != RTAUDIO_WARNING && type != RTAUDIO_NO_ERROR) lost.store(true);
    (void)message;
}
void initialize()
{
    if (!mutex) mutex = SDL_CreateMutex();
    if (!driver) driver.reset(new RtAudio(RtAudio::WINDOWS_ASIO, errorCallback));
}
int process(void *out, void *in, unsigned n, double, RtAudioStreamStatus status, void *)
{
    if (out) std::memset(out, 0, size_t(n) * nativeOutputs * sizeof(float));
    if (status) { ++xruns; ++driverXruns; }
    if (lost.load() || n != frames) { lost.store(true); return 0; }
    /* Note edits normally hold this mutex for a fraction of a millisecond.
       Recover those overlaps instead of replacing a whole period with silence.
       Long edits still stop at the retry budget and remain visible as skips. */
    struct LockOps {
        int try_lock() { return SDL_TryLockMutex(mutex); }
        uint64_t now() { return SDL_GetPerformanceCounter(); }
        void pause() {
#ifdef SDL_CPUPauseInstruction
            SDL_CPUPauseInstruction();
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__))
            /* SDL 2.0.20 on the Linux release baseline lacks this macro. */
            __asm__ __volatile__("pause");
#else
            std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
        }
    } ops;
    const auto lock = ts_asio_callback_lock(ops, lockRetryTicks);
    if (lock == TsAsioLock::skipped) { ++xruns; ++controlSkips; return 0; }
    if (lock == TsAsioLock::recovered) ++controlRecoveries;
    Endpoint &capture = endpoints[1], &playback = endpoints[0];
    std::fill(inputBuffer.begin(), inputBuffer.end(), 0.f);
    std::fill(outputBuffer.begin(), outputBuffer.end(), 0.f);
    if (in && nativeInputs) for (unsigned f=0; f<n; ++f)
        std::memcpy(inputBuffer.data()+f*inputs+ranges[1].first-1,
                    static_cast<float *>(in)+f*nativeInputs, nativeInputs*sizeof(float));
    currentInput = capture.id && !capture.paused && in ? inputBuffer.data() : nullptr;
    if (currentInput && capture.spec.callback)
        capture.spec.callback(capture.spec.userdata, reinterpret_cast<Uint8 *>(inputBuffer.data()),
                              int(n * inputs * sizeof(float)));
    if (out && playback.id && !playback.paused && playback.spec.callback)
        playback.spec.callback(playback.spec.userdata, reinterpret_cast<Uint8 *>(outputBuffer.data()),
                               int(n * outputs * sizeof(float)));
    if (out) for (unsigned f=0; f<n; ++f)
        std::memcpy(static_cast<float *>(out)+f*nativeOutputs,
                    outputBuffer.data()+f*outputs+ranges[0].first-1, nativeOutputs*sizeof(float));
    currentInput = nullptr;
    SDL_UnlockMutex(mutex);
    return 0;
}
const RtAudio::DeviceInfo *deviceAt(int index, int capture)
{
    for (const auto &d : devices)
        if ((capture ? d.inputChannels : d.outputChannels) && index-- == 0) return &d;
    return nullptr;
}
void closeStream()
{
    /* Do not hold the application mutex across a driver stop/join. */
    if (driver && driver->isStreamOpen()) driver->closeStream();
    started = false; active.clear(); currentInput = nullptr;
}
}
extern "C" {
int ts_asio_set_ranges(TsAudioChannelRange output, TsAudioChannelRange input)
{
    if (!ts_audio_range_valid(output) || !ts_audio_range_valid(input) ||
        (driver && driver->isStreamOpen())) return 0;
    ranges[0]=output; ranges[1]=input; return 1;
}
void ts_asio_rescan(void)
{
    try {
        initialize();
        /* Probing other ASIO drivers while one is open can unload the active
           driver. Keep its cached catalog until all endpoint views close. */
        if (driver->isStreamOpen()) return;
        devices.clear();
        for (unsigned id : driver->getDeviceIds()) devices.push_back(driver->getDeviceInfo(id));
    } catch (...) { SDL_SetError("ASIO driver enumeration failed"); }
}
int ts_asio_count(int capture)
{
    if (!driver) ts_asio_rescan();
    int n = 0;
    for (const auto &d : devices) if (capture ? d.inputChannels : d.outputChannels) ++n;
    return n;
}
const char *ts_asio_name(int index, int capture)
{ const auto *d = deviceAt(index, capture); return d ? d->name.c_str() : nullptr; }
int ts_asio_spec(int index, int capture, SDL_AudioSpec *spec)
{
    const auto *d = deviceAt(index, capture);
    if (!d) return SDL_SetError("ASIO device unavailable");
    SDL_zero(*spec); spec->format = AUDIO_F32SYS;
    spec->channels = Uint8(std::min(255u, capture ? d->inputChannels : d->outputChannels));
    spec->freq = int(d->currentSampleRate ? d->currentSampleRate : d->preferredSampleRate);
    return 0;
}
int ts_asio_default_info(char **name, SDL_AudioSpec *spec, int capture)
{
    int count = ts_asio_count(capture), index = 0;
    if (!count) return SDL_SetError("No ASIO drivers installed");
    if (!active.empty()) for (int i=0;i<count;++i)
        if (active == ts_asio_name(i,capture)) { index=i; break; }
    if (name) *name = SDL_strdup(ts_asio_name(index,capture));
    if (!spec) return 0;
    int result = ts_asio_spec(index,capture,spec);
    if (!active.empty()) { spec->freq=int(rate); spec->samples=Uint16(frames); }
    return result;
}
int ts_asio_owns(SDL_AudioDeviceID id) { return id >= base && id < base + 0x1000000; }
SDL_AudioDeviceID ts_asio_open(const char *name, int capture,
    const SDL_AudioSpec *wanted, SDL_AudioSpec *got)
{
    capture = capture != 0;
    try {
        initialize();
        if (!mutex || !wanted || !got || !wanted->callback || wanted->format != AUDIO_F32SYS) {
            SDL_SetError("Invalid ASIO float callback request"); return 0;
        }
        if (devices.empty()) ts_asio_rescan();
        const RtAudio::DeviceInfo *choice = nullptr;
        std::string requested = name && *name ? name : (driver->isStreamOpen() ? active : "");
        for (const auto &d : devices) {
            if (!(capture ? d.inputChannels : d.outputChannels)) continue;
            if (requested.empty() || requested == d.name) { choice = &d; break; }
        }
        if (!choice) { SDL_SetError("ASIO driver unavailable; choose an installed 64-bit driver in CFG"); return 0; }
        if (driver->isStreamOpen() && choice->name != active) {
            SDL_SetError("ASIO uses one driver for main I/O and Insert; set INPUT to SYSTEM DEFAULT"); return 0;
        }
        if (endpoints[capture].id) {
            SDL_SetError("ASIO endpoint already open; close both endpoint views before reopening"); return 0;
        }
        if (!driver->isStreamOpen()) {
            if (capture) { SDL_SetError("Open ASIO output before its shared input"); return 0; }
            if (!ts_audio_range_fits(ranges[0], choice->outputChannels) ||
                (choice->inputChannels ? !ts_audio_range_fits(ranges[1], choice->inputChannels) :
                 (ranges[1].first != 1 || ranges[1].last != 0))) {
                SDL_SetError("ASIO channel range exceeds this driver's available inputs or outputs"); return 0;
            }
            outputs = ts_audio_range_end(ranges[0], std::min(255u, choice->outputChannels));
            inputs = ts_audio_range_end(ranges[1], std::min(255u, choice->inputChannels));
            nativeOutputs = outputs - unsigned(ranges[0].first-1);
            nativeInputs = inputs ? inputs - unsigned(ranges[1].first-1) : 0;
            rate = choice->currentSampleRate ? choice->currentSampleRate : choice->preferredSampleRate;
            if (!rate) rate = wanted->freq > 0 ? unsigned(wanted->freq) : 48000;
            frames = wanted->samples ? wanted->samples : 256;
            RtAudio::StreamParameters op, ip;
            op.deviceId = ip.deviceId = choice->ID;
            op.nChannels = nativeOutputs; ip.nChannels = nativeInputs;
            op.firstChannel = unsigned(ranges[0].first-1); ip.firstChannel = unsigned(ranges[1].first-1);
            RtAudio::StreamOptions options; options.streamName = "TapeSister";
            lost.store(false); notified = false; xruns.store(0);
            driverXruns.store(0); controlSkips.store(0); controlRecoveries.store(0);
            if (driver->openStream(&op, inputs ? &ip : nullptr, RTAUDIO_FLOAT32, rate,
                                   &frames, process, nullptr, &options) != RTAUDIO_NO_ERROR) {
                closeStream(); SDL_SetError("ASIO open failed; check driver availability, rate and control panel"); return 0;
            }
            rate = driver->getStreamSampleRate();
            if (!frames || frames > 32768 || !rate) {
                closeStream(); SDL_SetError("Unsupported ASIO buffer size or sample rate"); return 0;
            }
            inputBuffer.assign(size_t(frames)*inputs, 0.f);
            outputBuffer.assign(size_t(frames)*outputs, 0.f);
            lockRetryTicks = ts_asio_lock_budget(frames, rate, SDL_GetPerformanceFrequency());
            active = choice->name;
        }
        SDL_LockMutex(mutex);
        Endpoint &e = endpoints[capture];
        e.spec = *wanted; e.spec.freq = int(rate); e.spec.samples = Uint16(frames);
        e.spec.channels = Uint8(capture ? inputs : outputs);
        e.spec.size = frames * e.spec.channels * sizeof(float);
        e.paused = true; e.id = base + (++serial % 0xfffffe) + 1;
        *got = e.spec; auto id = e.id;
        SDL_UnlockMutex(mutex);
        return id;
    } catch (...) { if (!endpoints[0].id && !endpoints[1].id) closeStream(); SDL_SetError("ASIO driver failed while opening"); return 0; }
}
void ts_asio_lock(void) { if (mutex) SDL_LockMutex(mutex); }
void ts_asio_unlock(void) { if (mutex) SDL_UnlockMutex(mutex); }
void ts_asio_close(SDL_AudioDeviceID id)
{
    ts_asio_lock();
    for (auto &e : endpoints) if (e.id == id) e = Endpoint{};
    bool empty = !endpoints[0].id && !endpoints[1].id;
    ts_asio_unlock();
    if (empty) closeStream();
}
void ts_asio_pause(SDL_AudioDeviceID id, int paused)
{
    ts_asio_lock();
    bool found = false;
    for (auto &e : endpoints) if (e.id == id) { e.paused = paused != 0; found = true; }
    ts_asio_unlock();
    if (found && !paused && !started && driver && driver->isStreamOpen()) {
        started = true;
        if (driver->startStream() != RTAUDIO_NO_ERROR) lost.store(true);
    }
}
int ts_asio_running(void)
{ return driver && started && !lost.load() && driver->isStreamRunning(); }
void ts_asio_poll(void)
{
    if (!driver || notified || !started) return;
    if (!lost.load() && driver->isStreamRunning()) return;
    notified = true;
    /* Existing endpoint recovery runs on the main thread after these events. */
    for (int side = 1; side >= 0; --side) if (endpoints[side].id) {
        SDL_Event event; SDL_zero(event); event.type = SDL_AUDIODEVICEREMOVED;
        event.adevice.which = endpoints[side].id; event.adevice.iscapture = Uint8(side);
        SDL_PushEvent(&event);
    }
}
unsigned ts_asio_xruns(void) { return xruns.load(); }
unsigned ts_asio_driver_xruns(void) { return driverXruns.load(); }
unsigned ts_asio_control_skips(void) { return controlSkips.load(); }
unsigned ts_asio_control_recoveries(void) { return controlRecoveries.load(); }
const float *ts_asio_input_block(unsigned *n, unsigned *ch)
{ *n = frames; *ch = inputs; return currentInput; }
void ts_asio_quit(void)
{
    closeStream(); endpoints[0] = Endpoint{}; endpoints[1] = Endpoint{};
    driver.reset(); devices.clear();
    if (mutex) SDL_DestroyMutex(mutex);
    mutex = nullptr;
}
}
