/**
 * karaoke.h - Creates and runs the karaoke part of the application, managing state and loading resources specific to this module
 */

#ifndef ETSUKO_KARAOKE_H
#define ETSUKO_KARAOKE_H

typedef struct Karaoke_t Karaoke_t;

Karaoke_t *karaoke_init(void);
int karaoke_load_loop(Karaoke_t *state);
void karaoke_setup(Karaoke_t *state);
int karaoke_loop(const Karaoke_t *state);
void karaoke_finish(const Karaoke_t *state);

#endif // ETSUKO_KARAOKE_H
