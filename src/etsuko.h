/**
 * etsuko.h - Main part of the application that handles global initialization and cleanup
 */

#ifndef ETSUKO_ETSUKO_H
#define ETSUKO_ETSUKO_H

// Initializes stuff that is used through all the operating modes and should not get re-initialized more than once
int global_init(void);
// Frees the stuff initialized by init. This should only be called once per run (same with init)
void global_finish(void);

#endif // ETSUKO_ETSUKO_H
