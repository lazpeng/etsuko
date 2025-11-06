/**
 * karaoke.h - Creates and runs the karaoke part of the application, managing state and loading resources specific to this module
 */

#ifndef ETSUKO_KARAOKE_H
#define ETSUKO_KARAOKE_H

typedef struct etsuko_Karaoke_t etsuko_Karaoke_t;

etsuko_Karaoke_t *karaoke_init();
int karaoke_load_async(etsuko_Karaoke_t *state);
void karaoke_setup(etsuko_Karaoke_t *state);
int karaoke_loop(const etsuko_Karaoke_t *state);
void karaoke_finish(const etsuko_Karaoke_t *state);

#endif // ETSUKO_KARAOKE_H
