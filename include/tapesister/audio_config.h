#ifndef TAPESISTER_AUDIO_CONFIG_H
#define TAPESISTER_AUDIO_CONFIG_H

#include <stddef.h>

#include "tapesister/config.h"

/*
 * Audio-aware wrappers preserve the existing TsConfig parser/writer and layer
 * SDL playback and MIDI input settings on top. Older INI files remain valid.
 */
int ts_audio_config_load(TsConfig *config, const char *path,
                         char *error, size_t error_size);
int ts_audio_config_save(const TsConfig *config, const char *path,
                         char *error, size_t error_size);
int ts_audio_buffer_frames_valid(int frames);

#endif
