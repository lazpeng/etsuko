#include "audio.h"

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdlib.h>

#include "contrib/minimp3_ex.h"

#include "error.h"

#define PCM_BUFFER_SIZE (4096 * 4)

typedef struct {
    uint8_t *mp3_data;
    size_t mp3_size;
    mp3dec_ex_t decoder;
    int16_t pcm_buffer[PCM_BUFFER_SIZE];
    size_t pcm_buffer_fill;
    size_t pcm_buffer_pos;
    int channels;
    int sample_rate;
    double total_time;
    uint64_t total_samples;
    bool paused;
    bool stopped;
} audio_state_t;

static audio_state_t g_audio = {0};
static SDL_AudioDeviceID g_audio_device = 0;
static SDL_mutex *g_audio_mutex = NULL;

static void refill_pcm_buffer(void) {
    if ( g_audio.mp3_data == NULL ) {
        return;
    }

    if ( g_audio.pcm_buffer_pos > 0 && g_audio.pcm_buffer_fill > 0 ) {
        const size_t remaining = g_audio.pcm_buffer_fill - g_audio.pcm_buffer_pos;
        if ( remaining > 0 ) {
            memmove(g_audio.pcm_buffer, &g_audio.pcm_buffer[g_audio.pcm_buffer_pos], remaining * sizeof(int16_t));
        }
        g_audio.pcm_buffer_fill = remaining;
        g_audio.pcm_buffer_pos = 0;
    }

    const size_t space_available = PCM_BUFFER_SIZE - g_audio.pcm_buffer_fill;
    if ( space_available > 0 ) {
        const size_t samples_to_read = space_available;
        const size_t samples_read =
            mp3dec_ex_read(&g_audio.decoder, &g_audio.pcm_buffer[g_audio.pcm_buffer_fill], samples_to_read);
        g_audio.pcm_buffer_fill += samples_read;
    }
}

static void audio_callback(void *_, uint8_t *stream, const int len) {
    memset(stream, 0, len);

    SDL_LockMutex(g_audio_mutex);

    if ( g_audio.paused || g_audio.stopped || g_audio.mp3_data == NULL ) {
        SDL_UnlockMutex(g_audio_mutex);
        return;
    }

    const size_t samples_requested = len / sizeof(int16_t);
    size_t samples_copied = 0;
    int16_t *output = (int16_t *)stream;

    while ( samples_copied < samples_requested ) {
        if ( g_audio.pcm_buffer_pos >= g_audio.pcm_buffer_fill ) {
            refill_pcm_buffer();

            if ( g_audio.pcm_buffer_fill == 0 ) {
                g_audio.stopped = true;
                break;
            }
        }

        const size_t available = g_audio.pcm_buffer_fill - g_audio.pcm_buffer_pos;
        const size_t needed = samples_requested - samples_copied;
        const size_t to_copy = available < needed ? available : needed;

        memcpy(&output[samples_copied], &g_audio.pcm_buffer[g_audio.pcm_buffer_pos], to_copy * sizeof(int16_t));

        g_audio.pcm_buffer_pos += to_copy;
        samples_copied += to_copy;
    }

    SDL_UnlockMutex(g_audio_mutex);
}

void audio_init(void) {
    if ( SDL_InitSubSystem(SDL_INIT_AUDIO) != 0 ) {
        error_abort("SDL_InitSubSystem(AUDIO) failed");
    }

    g_audio_mutex = SDL_CreateMutex();
    if ( g_audio_mutex == NULL ) {
        error_abort("SDL_CreateMutex failed");
    }

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 4096;
    want.callback = audio_callback;
    want.userdata = &g_audio;

    g_audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if ( g_audio_device == 0 ) {
        error_abort("SDL_OpenAudioDevice failed");
    }
}

void audio_finish(void) {
    if ( g_audio_device != 0 ) {
        SDL_CloseAudioDevice(g_audio_device);
        g_audio_device = 0;
    }

    if ( g_audio_mutex != NULL ) {
        SDL_DestroyMutex(g_audio_mutex);
        g_audio_mutex = NULL;
    }

    if ( g_audio.mp3_data != NULL ) {
        mp3dec_ex_close(&g_audio.decoder);
        free(g_audio.mp3_data);
        g_audio.mp3_data = NULL;
    }

    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

static void reset(void) {
    audio_resume();
    audio_pause();
}

void audio_load(const unsigned char *data, const int data_size) {
    SDL_LockMutex(g_audio_mutex);

    if ( g_audio.mp3_data != NULL ) {
        mp3dec_ex_close(&g_audio.decoder);
        free(g_audio.mp3_data);
        g_audio.mp3_data = NULL;
    }

    SDL_RWops *rw = SDL_RWFromConstMem(data, data_size);
    if ( rw == NULL ) {
        SDL_UnlockMutex(g_audio_mutex);
        error_abort("Failed to open MP3 file");
    }

    const Sint64 file_size = SDL_RWsize(rw);
    if ( file_size <= 0 ) {
        SDL_RWclose(rw);
        SDL_UnlockMutex(g_audio_mutex);
        error_abort("Invalid MP3 file size");
    }

    g_audio.mp3_data = malloc(file_size);
    if ( g_audio.mp3_data == NULL ) {
        SDL_RWclose(rw);
        SDL_UnlockMutex(g_audio_mutex);
        error_abort("Failed to allocate memory for MP3");
    }

    if ( SDL_RWread(rw, g_audio.mp3_data, file_size, 1) != 1 ) {
        free(g_audio.mp3_data);
        g_audio.mp3_data = NULL;
        SDL_RWclose(rw);
        SDL_UnlockMutex(g_audio_mutex);
        error_abort("Failed to read MP3 file");
    }

    g_audio.mp3_size = file_size;
    SDL_RWclose(rw);

    if ( mp3dec_ex_open_buf(&g_audio.decoder, g_audio.mp3_data, g_audio.mp3_size, MP3D_SEEK_TO_SAMPLE) != 0 ) {
        free(g_audio.mp3_data);
        g_audio.mp3_data = NULL;
        SDL_UnlockMutex(g_audio_mutex);
        error_abort("Failed to initialize MP3 decoder");
    }

    g_audio.channels = g_audio.decoder.info.channels;
    g_audio.sample_rate = g_audio.decoder.info.hz;
    g_audio.total_samples = g_audio.decoder.samples;
    g_audio.total_time = (double)g_audio.total_samples / (double)g_audio.sample_rate / (double)g_audio.channels;
    g_audio.pcm_buffer_fill = 0;
    g_audio.pcm_buffer_pos = 0;

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = g_audio.sample_rate;
    want.format = AUDIO_S16SYS;
    want.channels = g_audio.channels;
    want.samples = 4096;
    want.callback = audio_callback;
    want.userdata = &g_audio;

    SDL_UnlockMutex(g_audio_mutex);

    if ( g_audio_device != 0 ) {
        SDL_CloseAudioDevice(g_audio_device);
    }

    g_audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if ( g_audio_device == 0 ) {
        error_abort("SDL_OpenAudioDevice failed");
    }

    reset();
}

void audio_resume(void) {
    SDL_LockMutex(g_audio_mutex);

    if ( g_audio.stopped ) {
        mp3dec_ex_seek(&g_audio.decoder, 0);
        g_audio.pcm_buffer_fill = 0;
        g_audio.pcm_buffer_pos = 0;
        g_audio.stopped = false;
        g_audio.paused = false;
        SDL_UnlockMutex(g_audio_mutex);
        SDL_PauseAudioDevice(g_audio_device, 0);
    } else if ( g_audio.paused ) {
        if ( audio_elapsed_time() >= audio_total_time() ) {
            mp3dec_ex_seek(&g_audio.decoder, 0);
            g_audio.pcm_buffer_fill = 0;
            g_audio.pcm_buffer_pos = 0;
        }
        g_audio.paused = false;
        SDL_UnlockMutex(g_audio_mutex);
        SDL_PauseAudioDevice(g_audio_device, 0);
    } else {
        SDL_UnlockMutex(g_audio_mutex);
    }
}

void audio_pause(void) {
    SDL_LockMutex(g_audio_mutex);
    if ( g_audio.paused ) {
        SDL_UnlockMutex(g_audio_mutex);
        return;
    }

    g_audio.paused = true;
    SDL_UnlockMutex(g_audio_mutex);
    SDL_PauseAudioDevice(g_audio_device, 1);
}

void audio_seek(const double time) {
    SDL_LockMutex(g_audio_mutex);

    if ( g_audio.stopped ) {
        SDL_UnlockMutex(g_audio_mutex);
        reset();
        SDL_LockMutex(g_audio_mutex);
    }

    uint64_t sample_pos = (uint64_t)(time * (double)g_audio.sample_rate * (double)g_audio.channels);

    if ( sample_pos >= g_audio.total_samples ) {
        sample_pos = g_audio.total_samples;
    }

    mp3dec_ex_seek(&g_audio.decoder, sample_pos);
    g_audio.pcm_buffer_fill = 0;
    g_audio.pcm_buffer_pos = 0;

    SDL_UnlockMutex(g_audio_mutex);
}

void audio_seek_relative(const double diff) {
    const double new_time = audio_elapsed_time() + diff;
    audio_seek(new_time);
}

double audio_elapsed_time(void) {
    if ( g_audio.mp3_data == NULL ) {
        return 0.0;
    }

    SDL_LockMutex(g_audio_mutex);
    const uint64_t current_sample = g_audio.decoder.cur_sample;
    SDL_UnlockMutex(g_audio_mutex);

    return (double)current_sample / (double)g_audio.sample_rate / (double)g_audio.channels;
}

double audio_total_time(void) { return g_audio.total_time; }

bool audio_is_paused(void) {
    SDL_LockMutex(g_audio_mutex);
    const bool result = g_audio.paused || g_audio.stopped;
    SDL_UnlockMutex(g_audio_mutex);
    return result;
}

void audio_loop(void) {
    if ( audio_elapsed_time() >= audio_total_time() && !g_audio.stopped ) {
        SDL_LockMutex(g_audio_mutex);
        g_audio.stopped = true;
        SDL_UnlockMutex(g_audio_mutex);
    }
}
