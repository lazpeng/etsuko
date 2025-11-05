/**
 * karaoke.h - Creates and runs the karaoke part of the application, managing state and loading resources specific to this module
 */

#ifndef ETSUKO_KARAOKE_H
#define ETSUKO_KARAOKE_H

int karaoke_load_async();

void karaoke_init();
int karaoke_loop();
void karaoke_finish();

#endif // ETSUKO_KARAOKE_H
