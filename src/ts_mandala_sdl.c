#define _POSIX_C_SOURCE 200809L
#include "tapesister/mandala_sdl.h"
#include "tapesister/ui.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

/* SPSC IPC ring, same executable/ABI on both ends. C atomics must be lock-free
   in this build AND on the mapped addresses; otherwise launch is refused.
   Windows and POSIX shared mappings supply coherent ordinary RAM on the
   supported x86/x64 targets. No atomic_flag/spinlock or process-shared mutex. */
typedef struct { TsMandalaConfig config; TsMandalaPacket audio; unsigned serial; } VisualMessage;
typedef struct {
    atomic_uint write, read, quit, ready, freeze, quality, display, fullscreen;
    VisualMessage message[4];
} VisualShared;
static TsMandalaQueue audio_queue;
static struct {
    VisualShared *shared;
    char name[96];
    Uint32 sent, launched, closing;
    int force_stopped, close_requested;
    unsigned serial;
    TsMandalaPacket last_audio;
    TsMandalaConfig sent_config;
#ifdef _WIN32
    HANDLE mapping, process;
#else
    pid_t pid;
#endif
} host;

static int shared_lock_free(VisualShared *s)
{
    return atomic_is_lock_free(&s->write) && atomic_is_lock_free(&s->read) &&
        atomic_is_lock_free(&s->quit) && atomic_is_lock_free(&s->ready) &&
        atomic_is_lock_free(&s->freeze) && atomic_is_lock_free(&s->quality) &&
        atomic_is_lock_free(&s->display) && atomic_is_lock_free(&s->fullscreen);
}
static void shared_init(VisualShared *s, const TsMandalaPreferences *p)
{
    memset(s, 0, sizeof(*s));
    atomic_init(&s->write, 0); atomic_init(&s->read, 0);
    atomic_init(&s->quit, 0); atomic_init(&s->ready, 0); atomic_init(&s->freeze, 0);
    atomic_init(&s->quality, p->quality >= 0 && p->quality <= 2 ? p->quality : 0);
    atomic_init(&s->display, p->display >= 0 ? (unsigned)p->display : 0);
    atomic_init(&s->fullscreen, p->fullscreen != 0);
}
static int shared_send(VisualShared *s, const VisualMessage *m)
{
    unsigned w = atomic_load_explicit(&s->write, memory_order_relaxed);
    unsigned r = atomic_load_explicit(&s->read, memory_order_acquire);
    if (w-r >= 4) return 0;
    s->message[w%4] = *m;
    atomic_store_explicit(&s->write, w+1, memory_order_release);
    return 1;
}
static int shared_latest(VisualShared *s, VisualMessage *m)
{
    unsigned r = atomic_load_explicit(&s->read, memory_order_relaxed);
    unsigned w = atomic_load_explicit(&s->write, memory_order_acquire);
    if (r == w) return 0;
    *m = s->message[(w-1)%4];
    atomic_store_explicit(&s->read, w, memory_order_release);
    return 1;
}
void ts_mandala_host_init(void) { ts_mandala_queue_init(&audio_queue); }
void ts_mandala_audio(const float *out, unsigned frames, unsigned channels)
{ ts_mandala_push(&audio_queue, out, frames, channels); }
int ts_mandala_active(void) { return host.shared && !atomic_load(&host.shared->quit); }
int ts_mandala_frozen(void) { return host.shared && atomic_load(&host.shared->freeze); }
void ts_mandala_freeze(void)
{
    if (host.shared) atomic_fetch_xor(&host.shared->freeze, 1);
    atomic_store(&audio_queue.enabled, ts_mandala_active() && !ts_mandala_frozen());
}
void ts_mandala_close(void)
{
    atomic_store(&audio_queue.enabled, 0);
    if (host.shared) {
        atomic_store(&host.shared->quit, 1);
        if (!host.close_requested) { host.closing = SDL_GetTicks(); host.close_requested = 1; }
    }
}
static void host_force_stop(void)
{
    if (host.force_stopped) return;
#ifdef _WIN32
    if (host.process) TerminateProcess(host.process, 0);
#else
    /* SIGKILL also retires a suspended child. Never wait for graphics exit. */
    if (host.pid > 0) kill(host.pid, SIGKILL);
#endif
    host.force_stopped = 1;
}
static void host_release(void)
{
#ifdef _WIN32
    if (host.shared) UnmapViewOfFile(host.shared);
    if (host.mapping) CloseHandle(host.mapping);
    if (host.process) CloseHandle(host.process);
#else
    if (host.shared) munmap(host.shared, sizeof(VisualShared));
    if (*host.name) shm_unlink(host.name);
#endif
    memset(&host, 0, sizeof(host));
}
void ts_mandala_shutdown(void)
{
    ts_mandala_close();
    host_force_stop();
#ifndef _WIN32
    if (host.pid > 0) waitpid(host.pid, NULL, WNOHANG);
#endif
    host_release();
}
int ts_mandala_open(const TsMandalaConfig *config, const TsMandalaPreferences *prefs, char *error, size_t size)
{
    if (host.shared) { snprintf(error, size, "Visualizer is open or closing"); return 0; }
    if (!ts_mandala_lock_free(&audio_queue) || !ts_mandala_valid(config)) {
        snprintf(error, size, "Visualizer requires lock-free atomics and a valid seed"); return 0;
    }
    char *base = SDL_GetBasePath(); char executable[2048];
    if (!base) { snprintf(error, size, "Cannot locate TapeSister executable"); return 0; }
    int n = snprintf(executable, sizeof(executable), "%s%s", base,
#ifdef _WIN32
        "TapeSister.exe"
#else
        "tapesister"
#endif
    ); SDL_free(base);
    if (n < 0 || n >= (int)sizeof(executable)) return 0;
#ifdef _WIN32
    snprintf(host.name, sizeof(host.name), "Local\\TapeSisterMandala-%lu-%llu", (unsigned long)GetCurrentProcessId(), (unsigned long long)SDL_GetPerformanceCounter());
    host.mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(VisualShared), host.name);
    if (host.mapping) host.shared = MapViewOfFile(host.mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(VisualShared));
#else
    snprintf(host.name, sizeof(host.name), "/tapesister-mandala-%ld-%llu", (long)getpid(), (unsigned long long)SDL_GetPerformanceCounter());
    int fd = shm_open(host.name, O_RDWR|O_CREAT|O_EXCL, 0600);
    if (fd >= 0) {
        if (!ftruncate(fd, sizeof(VisualShared))) {
            void *mapping = mmap(NULL, sizeof(VisualShared), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
            if (mapping != MAP_FAILED) host.shared = mapping;
        }
        close(fd);
    }
#endif
    if (!host.shared) goto failed;
    shared_init(host.shared, prefs);
    if (!shared_lock_free(host.shared)) goto failed;
    VisualMessage initial = {0}; initial.config = *config;
    shared_send(host.shared, &initial);
    host.sent_config = *config;
#ifdef _WIN32
    char command[2300];
    snprintf(command, sizeof(command), "\"%s\" --mandala-renderer %s %lu", executable, host.name, (unsigned long)GetCurrentProcessId());
    STARTUPINFOA startup; PROCESS_INFORMATION process;
    memset(&startup, 0, sizeof(startup)); startup.cb = sizeof(startup);
    if (!CreateProcessA(executable, command, NULL, NULL, FALSE, BELOW_NORMAL_PRIORITY_CLASS, NULL, NULL, &startup, &process)) goto failed;
    host.process = process.hProcess; CloseHandle(process.hThread);
#else
    char parent[32]; snprintf(parent, sizeof(parent), "%ld", (long)getpid());
    char *args[] = {executable, "--mandala-renderer", host.name, parent, NULL};
    int spawn_error = posix_spawn(&host.pid, executable, NULL, NULL, args, environ);
    if (spawn_error) { SDL_SetError("%s", strerror(spawn_error)); goto failed; }
#endif
    host.launched = SDL_GetTicks(); host.sent = host.launched - 100;
    /* Do not enable sampling until the renderer has initialized successfully. */
    return 1;
failed:
    snprintf(error, size, "Cannot launch visualizer: %.100s", SDL_GetError());
    host_release(); return 0;
}
int ts_mandala_poll(const TsMandalaConfig *config, TsMandalaPreferences *prefs)
{
    if (!host.shared) return 0;
    int alive;
#ifdef _WIN32
    DWORD code; alive = GetExitCodeProcess(host.process, &code) && code == STILL_ACTIVE;
#else
    alive = waitpid(host.pid, NULL, WNOHANG) == 0;
#endif
    prefs->quality = (int)atomic_load(&host.shared->quality);
    prefs->display = (int)atomic_load(&host.shared->display);
    prefs->fullscreen = (int)atomic_load(&host.shared->fullscreen);
    if (!alive) { atomic_store(&audio_queue.enabled, 0); host_release(); return 0; }
    int ready = atomic_load(&host.shared->ready);
    if (atomic_load(&host.shared->quit) && !host.close_requested) ts_mandala_close();
    if (!ready && SDL_GetTicks() - host.launched > 10000) ts_mandala_close();
    if (host.close_requested && SDL_GetTicks() - host.closing > 1000) host_force_stop();
#ifndef _WIN32
    if (ready && *host.name) { shm_unlink(host.name); *host.name = 0; }
#endif
    atomic_store(&audio_queue.enabled, ready && ts_mandala_active() && !ts_mandala_frozen());
    Uint32 now = SDL_GetTicks();
    if (now - host.sent >= 30) {
        VisualMessage m; m.config = *config;
        int fresh = ts_mandala_latest(&audio_queue, &host.last_audio);
        if (fresh) ++host.serial;
        m.audio = host.last_audio; m.serial = host.serial;
        if (fresh || memcmp(config, &host.sent_config, sizeof(*config))) {
            if (shared_send(host.shared, &m)) host.sent_config = *config;
        }
        host.sent = now;
    }
    return ts_mandala_active();
}

/* Native SDL renderer implementation; kept separate from the host/audio path. */
#include "ts_mandala_render.inc"

int ts_mandala_dispatch(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--mandala-capture"))
        return mandala_capture(argc, argv);
    if (argc != 4 || strcmp(argv[1], "--mandala-renderer")) return -1;
    VisualShared *shared = NULL;
#ifdef _WIN32
    HANDLE mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, argv[2]);
    if (mapping) shared = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*shared));
    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)strtoul(argv[3], NULL, 10));
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
#else
    int fd = shm_open(argv[2], O_RDWR, 0600);
    if (fd >= 0) {
        void *p = mmap(NULL, sizeof(*shared), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
        if (p != MAP_FAILED) shared = p;
        close(fd);
    }
    pid_t parent = (pid_t)strtol(argv[3], NULL, 10);
    setpriority(PRIO_PROCESS, 0, 10);
#endif
    int result = 1;
    if (shared && shared_lock_free(shared)) result = mandala_run(shared, parent);
#ifdef _WIN32
    if (shared) UnmapViewOfFile(shared);
    if (mapping) CloseHandle(mapping);
    if (parent) CloseHandle(parent);
#else
    if (shared) munmap(shared, sizeof(*shared));
#endif
    return result;
}
