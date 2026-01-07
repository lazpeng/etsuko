#include "audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#endif

#include "contrib/minimp3_ex.h"

#include "error.h"
#include "constants.h"

#define NUM_BUFFERS 4
// TODO: Rename to something else
#define BUFFER_SIZE (4096 * 4)

typedef struct {
    uint8_t *mp3_data;
    size_t mp3_size;
    mp3dec_ex_t decoder;
    ALCdevice *device;
    ALCcontext *context;
    ALuint source;
    ALuint buffers[NUM_BUFFERS];
    int16_t pcm_buffer[BUFFER_SIZE];
    int channels;
    int sample_rate;
    double total_time;
    uint64_t total_samples;
    uint64_t last_first_decoded_sample;
    bool paused;
    bool stopped;
} audio_state_t;

static audio_state_t g_audio = {0};

static void check_al_error(const char *msg) {
    const ALenum error = alGetError();
    if ( error != AL_NO_ERROR ) {
        fprintf(stderr, "OpenAL Error at %s: %d\n", msg, error);
    }
}

void audio_init(void) {
    // TODO: Maybe try to clear any buffers left on openal?
    g_audio.device = alcOpenDevice(NULL);
    if ( !g_audio.device ) {
        error_abort("Failed to open OpenAL device");
    }

    g_audio.context = alcCreateContext(g_audio.device, NULL);
    if ( !g_audio.context ) {
        error_abort("Failed to create OpenAL context");
    }

    if ( !alcMakeContextCurrent(g_audio.context) ) {
        error_abort("Failed to make OpenAL context current");
    }

    alGenSources(1, &g_audio.source);
    alGenBuffers(NUM_BUFFERS, g_audio.buffers);
    check_al_error("init");
}

void audio_finish(void) {
    alDeleteSources(1, &g_audio.source);
    alDeleteBuffers(NUM_BUFFERS, g_audio.buffers);

    alcMakeContextCurrent(NULL);
    alcDestroyContext(g_audio.context);
    alcCloseDevice(g_audio.device);

    if ( g_audio.mp3_data != NULL ) {
        mp3dec_ex_close(&g_audio.decoder);
        free(g_audio.mp3_data);
        g_audio.mp3_data = NULL;
    }
}

static void reset(void) {
    audio_resume();
    audio_pause();
}

void audio_load(const unsigned char *data, const int data_size) {
    alSourceStop(g_audio.source);

    // Clear queued buffers
    ALint queued;
    alGetSourcei(g_audio.source, AL_BUFFERS_QUEUED, &queued);
    while ( queued > 0 ) {
        ALuint buffer;
        alSourceUnqueueBuffers(g_audio.source, 1, &buffer);
        queued--;
    }

    if ( g_audio.mp3_data != NULL ) {
        mp3dec_ex_close(&g_audio.decoder);
        free(g_audio.mp3_data);
        g_audio.mp3_data = NULL;
    }

    g_audio.mp3_data = malloc(data_size);
    if ( g_audio.mp3_data == NULL ) {
        error_abort("Failed to allocate memory for MP3");
    }
    memcpy(g_audio.mp3_data, data, data_size);
    g_audio.mp3_size = data_size;

    if ( mp3dec_ex_open_buf(&g_audio.decoder, g_audio.mp3_data, g_audio.mp3_size, MP3D_SEEK_TO_SAMPLE) != 0 ) {
        free(g_audio.mp3_data);
        g_audio.mp3_data = NULL;
        error_abort("Failed to initialize MP3 decoder");
    }

    g_audio.channels = g_audio.decoder.info.channels;
    g_audio.sample_rate = g_audio.decoder.info.hz;
    g_audio.total_samples = g_audio.decoder.samples;
    g_audio.total_time = (double)g_audio.total_samples / (double)g_audio.sample_rate / (double)g_audio.channels;

    // Preload buffers
    for ( int i = 0; i < NUM_BUFFERS; i++ ) {
        const size_t read = mp3dec_ex_read(&g_audio.decoder, g_audio.pcm_buffer, BUFFER_SIZE / sizeof(int16_t));
        if ( read > 0 ) {
            alBufferData(g_audio.buffers[i], g_audio.channels == 2 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16, g_audio.pcm_buffer,
                         (ALsizei)(read * sizeof(int16_t)), g_audio.sample_rate);
            alSourceQueueBuffers(g_audio.source, 1, &g_audio.buffers[i]);
        }
    }

    reset();
}

void audio_resume(void) {
    if ( g_audio.stopped ) {
        mp3dec_ex_seek(&g_audio.decoder, 0);
        g_audio.stopped = false;
        g_audio.paused = false;
        alSourcePlay(g_audio.source);
    } else if ( g_audio.paused ) {
        if ( audio_elapsed_time() >= audio_total_time() ) {
            mp3dec_ex_seek(&g_audio.decoder, 0);
        }
        g_audio.paused = false;
        alSourcePlay(g_audio.source);
    }
}

void audio_pause(void) {
    if ( g_audio.paused ) {
        return;
    }

    g_audio.paused = true;
    alSourcePause(g_audio.source);
}

void audio_seek(double time) {
    if ( g_audio.stopped ) {
        reset();
    }
    time = MAX(0.0, time);

    uint64_t sample_pos = (uint64_t)(time * (double)g_audio.sample_rate * (double)g_audio.channels);

    if ( sample_pos >= g_audio.total_samples ) {
        sample_pos = g_audio.total_samples;
    }

    mp3dec_ex_seek(&g_audio.decoder, sample_pos);

    // Clear buffers and refill
    alSourceStop(g_audio.source);
    ALint queued;
    alGetSourcei(g_audio.source, AL_BUFFERS_QUEUED, &queued);
    while ( queued > 0 ) {
        ALuint buffer;
        alSourceUnqueueBuffers(g_audio.source, 1, &buffer);
        queued--;
    }

    for ( int i = 0; i < NUM_BUFFERS; i++ ) {
        const size_t read = mp3dec_ex_read(&g_audio.decoder, g_audio.pcm_buffer, BUFFER_SIZE / sizeof(int16_t));
        if ( i == 0 ) {
            g_audio.last_first_decoded_sample = g_audio.decoder.cur_sample;
        }
        if ( read > 0 ) {
            const int format = g_audio.channels == 2 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;
            alBufferData(g_audio.buffers[i], format, g_audio.pcm_buffer, (ALsizei)(read * sizeof(int16_t)), g_audio.sample_rate);
            alSourceQueueBuffers(g_audio.source, 1, &g_audio.buffers[i]);
        }
    }

    if ( !g_audio.paused ) {
        alSourcePlay(g_audio.source);
    }
}

void audio_seek_relative(const double diff) {
    const double new_time = audio_elapsed_time() + diff;
    audio_seek(new_time);
}

double audio_elapsed_time(void) {
    if ( g_audio.mp3_data == NULL ) {
        return 0.0;
    }

    return (double)g_audio.last_first_decoded_sample / (double)g_audio.sample_rate / (double)g_audio.channels;
}

double audio_total_time(void) { return g_audio.total_time; }

bool audio_is_paused(void) { return g_audio.paused || g_audio.stopped; }

void audio_loop(void) {
    if ( g_audio.mp3_data == NULL || g_audio.paused || g_audio.stopped ) {
        return;
    }

    ALint processed = 0;
    alGetSourcei(g_audio.source, AL_BUFFERS_PROCESSED, &processed);
    bool first = true;

    while ( processed > 0 ) {
        ALuint buffer;
        alSourceUnqueueBuffers(g_audio.source, 1, &buffer);
        check_al_error("alSourceUnqueueBuffers");
        if ( first ) {
            g_audio.last_first_decoded_sample = g_audio.decoder.cur_sample;
            first = false;
        }
        const size_t read = mp3dec_ex_read(&g_audio.decoder, g_audio.pcm_buffer, BUFFER_SIZE / sizeof(int16_t));

        if ( read > 0 ) {
            const int format = g_audio.channels == 2 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;
            alBufferData(buffer, format, g_audio.pcm_buffer, (ALsizei)(read * sizeof(int16_t)), g_audio.sample_rate);
            check_al_error("alBufferData");
            alSourceQueueBuffers(g_audio.source, 1, &buffer);
            check_al_error("alSourceQueueBuffers");
        } else {
            // End of stream
            g_audio.stopped = true;
        }

        processed--;
    }

    ALint state;
    alGetSourcei(g_audio.source, AL_SOURCE_STATE, &state);
    if ( state != AL_PLAYING && !g_audio.paused && !g_audio.stopped ) {
        alSourcePlay(g_audio.source);
        check_al_error("alSourcePlay restart");
    }
}
