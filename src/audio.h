/**
 * audio.h - Handles playing, loading and management of audio for the application
 */

#ifndef ETSUKO_AUDIO_H
#define ETSUKO_AUDIO_H

#include <stdbool.h>

// Initializes everything related to audio, like the buffers, openal audio device and stuff
void audio_init(void);
// Frees all the things initialized in _init
void audio_finish(void);
// Updates the elapsed time, and checks if there's any buffers we can refill from openal to continue playback
void audio_loop(void);
// Basically copies the input data for internal use and reads some info from the (now limited to) mp3 stream
void audio_load(const unsigned char *data, int data_size);
// Resumes a paused or stopped stream
void audio_resume(void);
// Pauses a running stream
void audio_pause(void);
// Seeks to a specific time
void audio_seek(double time);
// Seeks to a time relative to the current time
void audio_seek_relative(double diff);
// Returns a estimate of the current elapsed time
double audio_elapsed_time(void);
// The total playback time of the loaded mp3 stream
double audio_total_time(void);
// Whether the stream is currently paused or stopped (in other words, not playing)
bool audio_is_paused(void);

#endif // ETSUKO_AUDIO_H
