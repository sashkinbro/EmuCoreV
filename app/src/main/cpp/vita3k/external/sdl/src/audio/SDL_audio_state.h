// EmuCoreV-local hooks for restoring SDL3 AudioStream queues in save states.
#pragma once

#include <SDL3/SDL_audio.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

SDL_DECLSPEC size_t SDLCALL SDL_GetAudioStreamStateSize(SDL_AudioStream *stream);
SDL_DECLSPEC bool SDLCALL SDL_SaveAudioStreamState(SDL_AudioStream *stream, void *buffer, size_t size);
SDL_DECLSPEC bool SDLCALL SDL_LoadAudioStreamState(SDL_AudioStream *stream, const void *buffer, size_t size);
SDL_DECLSPEC bool SDLCALL SDL_ValidateAudioStreamState(const void *buffer, size_t size);

#ifdef __cplusplus
}
#endif
