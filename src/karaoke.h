/**
 * karaoke.h - Creates and runs the karaoke part of the application, managing state and loading resources specific to this module
 */

#ifndef ETSUKO_KARAOKE_H
#define ETSUKO_KARAOKE_H

int karaoke_load_async(void);

void karaoke_init(void);
int karaoke_loop(void);
void karaoke_finish(void);

#endif // ETSUKO_KARAOKE_H
