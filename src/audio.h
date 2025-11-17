/**
 * audio.h - Handles playing, loading and management of audio for the application
 */

#ifndef ETSUKO_AUDIO_H
#define ETSUKO_AUDIO_H

#include <stdbool.h>

void audio_init(void);
void audio_finish(void);
void audio_loop(void);
void audio_load(const unsigned char *data, int data_size);
void audio_resume(void);
void audio_pause(void);
void audio_seek(double time);
void audio_seek_relative(double diff);

double audio_elapsed_time(void);
double audio_total_time(void);
bool audio_is_paused(void);

#endif // ETSUKO_AUDIO_H
