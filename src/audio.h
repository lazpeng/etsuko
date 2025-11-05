/**
 * audio.h - Handles playing, loading and management of audio for the application
 */

#ifndef ETSUKO_AUDIO_H
#define ETSUKO_AUDIO_H

void audio_init();
void audio_finish();
void audio_loop();
void audio_load(const char *file);
void audio_resume();
void audio_pause();
void audio_seek(double time);
void audio_seek_relative(double diff);

double audio_elapsed_time();
double audio_total_time();
bool audio_is_paused();

#endif // ETSUKO_AUDIO_H
