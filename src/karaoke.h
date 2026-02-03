/**
 * karaoke.h - Creates and runs the karaoke part of the application, managing state and loading resources specific to this module
 */

#ifndef ETSUKO_KARAOKE_H
#define ETSUKO_KARAOKE_H

typedef struct Karaoke_t Karaoke_t;

// Initializes stuff the karaoke operating mode needs
Karaoke_t *karaoke_init(void);
// Asynchronously loads all the assets needed for the karaoke mode. Returns non-zero upon completion
int karaoke_load_loop(Karaoke_t *state);
// Once all the assets are loaded, this should only be called once to build the karaoke ui
void karaoke_setup(Karaoke_t *state);
// Loops the karaoke logic and all its dependents to update the screen, audio and logic
int karaoke_loop(const Karaoke_t *state);
// Frees all the stuff related to the karaoke operating mode
void karaoke_finish(const Karaoke_t *state);

#endif // ETSUKO_KARAOKE_H
