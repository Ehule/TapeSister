#ifndef TAPESISTER_ASIO_BACKEND_H
#define TAPESISTER_ASIO_BACKEND_H
#include <SDL.h>
#ifdef __cplusplus
extern "C" {
#endif
/* One ASIO driver, one duplex stream. Endpoint handles are logical views of
   that stream; capture is delivered immediately before playback each period. */
int ts_asio_count(int capture);
const char *ts_asio_name(int index, int capture);
int ts_asio_spec(int index, int capture, SDL_AudioSpec *spec);
int ts_asio_default_info(char **name, SDL_AudioSpec *spec, int capture);
void ts_asio_rescan(void);
SDL_AudioDeviceID ts_asio_open(const char *name, int capture,
    const SDL_AudioSpec *wanted, SDL_AudioSpec *got);
int ts_asio_owns(SDL_AudioDeviceID id);
void ts_asio_close(SDL_AudioDeviceID id);
void ts_asio_lock(void);
void ts_asio_unlock(void);
void ts_asio_pause(SDL_AudioDeviceID id, int paused);
void ts_asio_poll(void);
void ts_asio_quit(void);
unsigned ts_asio_xruns(void);
const float *ts_asio_input_block(unsigned *frames, unsigned *channels);
#ifdef __cplusplus
}
#endif
#endif
