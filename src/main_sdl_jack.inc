/* Native JACK endpoints. Loaded at runtime on Linux, independently of SDL's
   optional JACK driver. No JACK headers or link-time dependency are required.
   ABI types/constants below correspond to jack/types.h and jack/jack.h.
   Routing belongs to the JACK graph: never auto-connect to physical ports. */
#if defined(__linux__)
typedef struct _jack_client TsJackClient;
typedef struct _jack_port TsJackPort;
typedef uint32_t TsJackFrames;
typedef struct {
    TsJackClient *(*client_open)(const char *,unsigned,unsigned *,...);
    int (*client_close)(TsJackClient *);
    int (*activate)(TsJackClient *);
    int (*deactivate)(TsJackClient *);
    TsJackFrames (*get_sample_rate)(TsJackClient *);
    TsJackFrames (*get_buffer_size)(TsJackClient *);
    TsJackPort *(*port_register)(TsJackClient *,const char *,const char *,unsigned long,unsigned long);
    void *(*port_get_buffer)(TsJackPort *,TsJackFrames);
    int (*set_process_callback)(TsJackClient *,int (*)(TsJackFrames,void *),void *);
    int (*set_buffer_size_callback)(TsJackClient *,int (*)(TsJackFrames,void *),void *);
    int (*set_sample_rate_callback)(TsJackClient *,int (*)(TsJackFrames,void *),void *);
    void (*on_shutdown)(TsJackClient *,void (*)(void *),void *);
} TsJackApi;
static TsJackApi ts_jack;
static void *ts_jack_library;
static int ts_jack_selected;
enum { TS_JACK_SLOTS=8, TS_JACK_MAX_FRAMES=16384, TS_JACK_BLOCKS=4, TS_JACK_ID_BASE=0x40000000 };
typedef struct {
    TsJackClient *client;
    TsJackPort *ports[8];
    SDL_AudioSpec spec;
    float *interleaved, *blocks;
    SDL_Thread *worker;
    SDL_mutex *mutex;
    SDL_sem *wake;
    int capture, role, notified;
    _Atomic int paused, stopping, lost;
    _Atomic unsigned read_block, write_block, xruns;
} TsJackEndpoint;
static TsJackEndpoint ts_jack_endpoints[TS_JACK_SLOTS];
static const char *const ts_jack_names[2][2] = {
    {"TapeSister Master", "TapeSister Insert Send"},
    {"TapeSister Input", "TapeSister Insert Return"}
};

static int ts_jack_load(void)
{
    if (ts_jack_library) return 1;
    ts_jack_library=SDL_LoadObject("libjack.so.0");
    if (!ts_jack_library) {
        SDL_SetError("JACK runtime unavailable: install libjack.so.0, then start JACK or use pw-jack");
        return 0;
    }
#define TS_JACK_LOAD(fn) do { \
    void *symbol=SDL_LoadFunction(ts_jack_library,"jack_" #fn); \
    if (!symbol) goto failed; \
    memcpy(&ts_jack.fn,&symbol,sizeof(symbol)); \
} while (0)
    TS_JACK_LOAD(client_open);TS_JACK_LOAD(client_close);
    TS_JACK_LOAD(activate);TS_JACK_LOAD(deactivate);
    TS_JACK_LOAD(get_sample_rate);TS_JACK_LOAD(get_buffer_size);
    TS_JACK_LOAD(port_register);TS_JACK_LOAD(port_get_buffer);
    TS_JACK_LOAD(set_process_callback);TS_JACK_LOAD(set_buffer_size_callback);
    TS_JACK_LOAD(set_sample_rate_callback);TS_JACK_LOAD(on_shutdown);
#undef TS_JACK_LOAD
    return 1;
failed:
    SDL_UnloadObject(ts_jack_library);ts_jack_library=NULL;memset(&ts_jack,0,sizeof(ts_jack));
    return 0;
}

static TsJackEndpoint *ts_jack_endpoint(SDL_AudioDeviceID id)
{
    if (id<TS_JACK_ID_BASE || id>=TS_JACK_ID_BASE+TS_JACK_SLOTS) return NULL;
    return &ts_jack_endpoints[id-TS_JACK_ID_BASE];
}

/* SDL-style worker keeps the large existing DSP callback and its UI exclusion
   off JACK's deadline thread. JACK only copies a bounded SPSC block and wakes
   the worker. Ports are not disconnected by a UI lock or pause. */
static int ts_jack_worker(void *userdata)
{
    TsJackEndpoint *e=userdata;
    (void)SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);
    while (!atomic_load(&e->stopping)) {
        if (SDL_SemWait(e->wake)) break;
        if (atomic_load(&e->stopping)) break;
        /* Collapse missed playback deadlines instead of accumulating latency. */
        if (!e->capture) while (SDL_SemTryWait(e->wake)==0) {}
        SDL_LockMutex(e->mutex);
        unsigned read_at=atomic_load(&e->read_block),write_at=atomic_load(&e->write_block);
        if (!atomic_load(&e->paused) && !atomic_load(&e->lost)) {
            if (e->capture && read_at!=write_at) {
                memcpy(e->interleaved,e->blocks+(read_at%TS_JACK_BLOCKS)*e->spec.size/sizeof(float),e->spec.size);
                atomic_store(&e->read_block,read_at+1);
                e->spec.callback(e->spec.userdata,(Uint8 *)e->interleaved,(int)e->spec.size);
            } else if (!e->capture && write_at-read_at<2) {
                memset(e->interleaved,0,e->spec.size);
                e->spec.callback(e->spec.userdata,(Uint8 *)e->interleaved,(int)e->spec.size);
                memcpy(e->blocks+(write_at%TS_JACK_BLOCKS)*e->spec.size/sizeof(float),e->interleaved,e->spec.size);
                atomic_store(&e->write_block,write_at+1);
            }
        } else if (e->capture) atomic_store(&e->read_block,write_at);
        SDL_UnlockMutex(e->mutex);
    }
    return 0;
}

static int ts_jack_process(TsJackFrames frames,void *userdata)
{
    TsJackEndpoint *e=userdata;
    float *ports[8]={0};
    for (unsigned ch=0;ch<e->spec.channels;++ch) {
        ports[ch]=ts_jack.port_get_buffer(e->ports[ch],frames);
        if (!e->capture && ports[ch]) memset(ports[ch],0,frames*sizeof(float));
    }
    if (frames!=e->spec.samples) {atomic_store(&e->lost,1);return 0;}
    unsigned read_at=atomic_load(&e->read_block),write_at=atomic_load(&e->write_block);
    if (atomic_load(&e->paused) || atomic_load(&e->lost)) {
        if (!e->capture) atomic_store(&e->read_block,write_at);
        return 0;
    }
    if (e->capture) {
        if (write_at-read_at<TS_JACK_BLOCKS) {
            float *block=e->blocks+(write_at%TS_JACK_BLOCKS)*e->spec.size/sizeof(float);
            for (unsigned f=0;f<frames;++f) for (unsigned ch=0;ch<e->spec.channels;++ch)
                block[f*e->spec.channels+ch]=ports[ch]?ports[ch][f]:0;
            atomic_store(&e->write_block,write_at+1);SDL_SemPost(e->wake);
        } else atomic_fetch_add(&e->xruns,1);
    } else {
        if (read_at!=write_at) {
            const float *block=e->blocks+(read_at%TS_JACK_BLOCKS)*e->spec.size/sizeof(float);
            for (unsigned f=0;f<frames;++f) for (unsigned ch=0;ch<e->spec.channels;++ch)
                if (ports[ch]) ports[ch][f]=block[f*e->spec.channels+ch];
            atomic_store(&e->read_block,read_at+1);
        } else atomic_fetch_add(&e->xruns,1);
        SDL_SemPost(e->wake);
    }
    return 0;
}

static void ts_jack_shutdown(void *userdata)
{ atomic_store(&((TsJackEndpoint *)userdata)->lost,1); }
static int ts_jack_rate_changed(TsJackFrames value,void *userdata)
{
    TsJackEndpoint *e=userdata;
    if (value!=(unsigned)e->spec.freq) atomic_store(&e->lost,1);
    return 0;
}
static int ts_jack_buffer_changed(TsJackFrames value,void *userdata)
{
    TsJackEndpoint *e=userdata;
    if (value!=e->spec.samples) atomic_store(&e->lost,1);
    return 0;
}

static void ts_jack_lock(TsJackEndpoint *e)
{ if (e->mutex) SDL_LockMutex(e->mutex); }
static void ts_jack_close(TsJackEndpoint *e)
{
    if (!e->client) return;
    atomic_store(&e->paused,1);atomic_store(&e->stopping,1);
    ts_jack.deactivate(e->client); /* joins JACK before freeing its port buffers */
    if (e->wake) SDL_SemPost(e->wake);
    if (e->worker) SDL_WaitThread(e->worker,NULL);
    ts_jack.client_close(e->client);
    if (e->wake) SDL_DestroySemaphore(e->wake);
    if (e->mutex) SDL_DestroyMutex(e->mutex);
    free(e->interleaved);free(e->blocks);
    e->interleaved=e->blocks=NULL;e->worker=NULL;e->wake=NULL;e->mutex=NULL;e->client=NULL;
}

static SDL_AudioDeviceID ts_jack_open(const char *name,int capture,
    const SDL_AudioSpec *wanted,SDL_AudioSpec *got)
{
    int role=0,slot;
    if (name && *name) {
        if (!strcmp(name,ts_jack_names[capture][1])) role=1;
        else if (strcmp(name,ts_jack_names[capture][0])) {
            SDL_SetError("JACK device '%s' is unavailable; choose a TapeSister JACK endpoint",name);return 0;
        }
    }
    if (!wanted || !wanted->callback || wanted->format!=AUDIO_F32SYS) {
        SDL_SetError("JACK requires a float audio callback");return 0;
    }
    if (!ts_jack_load()) return 0;
    for (slot=0;slot<TS_JACK_SLOTS && ts_jack_endpoints[slot].client;++slot) {}
    if (slot==TS_JACK_SLOTS) {SDL_SetError("Too many TapeSister JACK streams");return 0;}
    TsJackEndpoint *e=&ts_jack_endpoints[slot];memset(e,0,sizeof(*e));
    e->capture=capture;e->role=role;e->spec=*wanted;
    atomic_init(&e->paused,1);atomic_init(&e->stopping,0);atomic_init(&e->lost,0);
    atomic_init(&e->read_block,0);atomic_init(&e->write_block,0);atomic_init(&e->xruns,0);
    unsigned status=0;
    e->client=ts_jack.client_open(ts_jack_names[capture][role],1u,&status); /* JackNoStartServer */
    if (!e->client) {
        SDL_SetError("JACK server unavailable (status 0x%x); start JACK or use pw-jack",status);return 0;
    }
    e->spec.freq=(int)ts_jack.get_sample_rate(e->client);
    unsigned frames=ts_jack.get_buffer_size(e->client);
    e->spec.channels=role?2:8;e->spec.samples=(Uint16)frames;
    e->spec.size=frames*e->spec.channels*sizeof(float);e->spec.silence=0;
    if (!frames || frames>TS_JACK_MAX_FRAMES || e->spec.freq<=0) goto failed;
    e->interleaved=calloc(1,e->spec.size);e->blocks=calloc(TS_JACK_BLOCKS,e->spec.size);
    e->mutex=SDL_CreateMutex();e->wake=SDL_CreateSemaphore(0);
    if (!e->interleaved || !e->blocks || !e->mutex || !e->wake) goto failed;
    e->worker=SDL_CreateThread(ts_jack_worker,"TapeSister JACK audio",e);
    if (!e->worker) goto failed;
    const char *port_prefix=capture?(role?"return":"input"):(role?"send":"master");
    for (unsigned ch=0;ch<e->spec.channels;++ch) {
        char port[32];snprintf(port,sizeof(port),"%s_%u",port_prefix,ch+1);
        e->ports[ch]=ts_jack.port_register(e->client,port,"32 bit float mono audio",capture?1u:2u,0);
        if (!e->ports[ch]) goto failed;
    }
    if (ts_jack.set_process_callback(e->client,ts_jack_process,e) ||
        ts_jack.set_buffer_size_callback(e->client,ts_jack_buffer_changed,e) ||
        ts_jack.set_sample_rate_callback(e->client,ts_jack_rate_changed,e)) goto failed;
    ts_jack.on_shutdown(e->client,ts_jack_shutdown,e);
    if (ts_jack.activate(e->client)) goto failed;
    *got=e->spec;return (SDL_AudioDeviceID)(TS_JACK_ID_BASE+slot);
failed:
    ts_jack_close(e);SDL_SetError("Could not create JACK ports or unsupported server buffer");return 0;
}

static void ts_jack_poll(void)
{
    if (!ts_jack_selected) return;
    for (int i=0;i<TS_JACK_SLOTS;++i) {
        TsJackEndpoint *e=&ts_jack_endpoints[i];
        if (e->client && atomic_load(&e->lost) && !e->notified) {
            SDL_Event event;SDL_zero(event);event.type=SDL_AUDIODEVICEREMOVED;
            event.adevice.which=(Uint32)(TS_JACK_ID_BASE+i);event.adevice.iscapture=(Uint8)e->capture;
            if (SDL_PushEvent(&event)>0) e->notified=1;
        }
    }
}
#else
static int ts_jack_selected;
static void ts_jack_poll(void) {}
#endif

#if defined(TAPESISTER_HAS_ASIO)
#include "tapesister/asio_backend.h"
#endif
static int ts_asio_selected;

/* Uniform endpoint API for the existing main/capture/Insert lifecycle. */
static const char *ts_native_current_driver(void)
{ return ts_asio_selected?"asio":ts_jack_selected?"jack":SDL_GetCurrentAudioDriver(); }
static int ts_native_device_count(int capture)
{
#if defined(TAPESISTER_HAS_ASIO)
    if (ts_asio_selected) return ts_asio_count(capture);
#endif
    return ts_jack_selected?2:SDL_GetNumAudioDevices(capture);
}
static const char *ts_native_device_name(int index,int capture)
{
#if defined(TAPESISTER_HAS_ASIO)
    if (ts_asio_selected) return ts_asio_name(index,capture);
#endif
#if defined(__linux__)
    if (ts_jack_selected) return index>=0 && index<2?ts_jack_names[capture!=0][index]:NULL;
#endif
    return SDL_GetAudioDeviceName(index,capture);
}
static int ts_native_device_spec(int index,int capture,SDL_AudioSpec *spec)
{
#if defined(TAPESISTER_HAS_ASIO)
    if (ts_asio_selected) return ts_asio_spec(index,capture,spec);
#endif
#if defined(__linux__)
    if (ts_jack_selected) {
        if (index<0 || index>1) return SDL_SetError("Invalid JACK device");
        SDL_zero(*spec);spec->channels=index?2:8;spec->format=AUDIO_F32SYS;
        return 0; /* Actual rate/quantum are read from JACK on open. */
    }
#endif
#if SDL_VERSION_ATLEAST(2,0,16)
    return SDL_GetAudioDeviceSpec(index,capture,spec);
#else
    (void)index;(void)capture;(void)spec;return -1;
#endif
}
static int ts_native_default_info(char **name,SDL_AudioSpec *spec,int capture)
{
#if defined(TAPESISTER_HAS_ASIO)
    if (ts_asio_selected) {
        return ts_asio_default_info(name,spec,capture);
    }
#endif
#if defined(__linux__)
    if (ts_jack_selected) {
        if (name) *name=SDL_strdup(ts_jack_names[capture!=0][0]);
        if (spec) return ts_native_device_spec(0,capture,spec);
        return 0;
    }
#endif
#if SDL_VERSION_ATLEAST(2,24,0)
    return SDL_GetDefaultAudioInfo(name,spec,capture);
#else
    (void)name;(void)spec;(void)capture;return -1;
#endif
}
static SDL_AudioDeviceID ts_native_open(const char *name,int capture,
    const SDL_AudioSpec *wanted,SDL_AudioSpec *got,int flags)
{
#if defined(TAPESISTER_HAS_ASIO)
    if (ts_asio_selected) return ts_asio_open(name,capture,wanted,got);
#endif
#if defined(__linux__)
    if (ts_jack_selected) return ts_jack_open(name,capture!=0,wanted,got);
#endif
    return SDL_OpenAudioDevice(name,capture,wanted,got,flags);
}
static void ts_native_close(SDL_AudioDeviceID id)
{
#if defined(TAPESISTER_HAS_ASIO)
    if (ts_asio_owns(id)) {ts_asio_close(id);return;}
#endif
#if defined(__linux__)
    TsJackEndpoint *e=ts_jack_endpoint(id);if (e) {ts_jack_close(e);return;}
#endif
    SDL_CloseAudioDevice(id);
}
static void ts_native_lock(SDL_AudioDeviceID id)
{
#if defined(TAPESISTER_HAS_ASIO)
    if (ts_asio_owns(id)) {ts_asio_lock();return;}
#endif
#if defined(__linux__)
    TsJackEndpoint *e=ts_jack_endpoint(id);if (e) {ts_jack_lock(e);return;}
#endif
    SDL_LockAudioDevice(id);
}
static void ts_native_unlock(SDL_AudioDeviceID id)
{
#if defined(TAPESISTER_HAS_ASIO)
    if (ts_asio_owns(id)) {ts_asio_unlock();return;}
#endif
#if defined(__linux__)
    TsJackEndpoint *e=ts_jack_endpoint(id);if (e) {SDL_UnlockMutex(e->mutex);return;}
#endif
    SDL_UnlockAudioDevice(id);
}
static void ts_native_pause(SDL_AudioDeviceID id,int paused)
{
#if defined(TAPESISTER_HAS_ASIO)
    if (ts_asio_owns(id)) {ts_asio_pause(id,paused);return;}
#endif
#if defined(__linux__)
    TsJackEndpoint *e=ts_jack_endpoint(id);
    if (e) {
        SDL_LockMutex(e->mutex);atomic_store(&e->paused,paused!=0);SDL_UnlockMutex(e->mutex);
        if (!paused && !e->capture) SDL_SemPost(e->wake);
        return;
    }
#endif
    SDL_PauseAudioDevice(id,paused);
}

static unsigned ts_native_worker_xruns(SDL_AudioDeviceID id)
{
#if defined(TAPESISTER_HAS_ASIO)
    if (ts_asio_owns(id)) return ts_asio_xruns();
#endif
#if defined(__linux__)
    TsJackEndpoint *e=ts_jack_endpoint(id);
    if (e && e->client) return atomic_load(&e->xruns);
#else
    (void)id;
#endif
    return 0;
}
