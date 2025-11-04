/**
 * constants.h - A few defined constants that can be shared throughout the codebase
 */

#ifndef ETSUKO_CONSTANTS_H
#define ETSUKO_CONSTANTS_H

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define VERSION "0.4.6"
#define APP_NAME "etsuko"
#define DEFAULT_TITLE APP_NAME " - Karaoke v" VERSION
#define DEFAULT_WIDTH (1280)
#define DEFAULT_HEIGHT (720)
#define DEFAULT_PT (16)
#define MAX_TEXT_SIZE (1024)
#define BUFFER_SIZE (1024)
#define MAX_TIMINGS_PER_LINE (24)
#define MAX_SONG_LINES (1024)
#define DEFAULT_VEC_CAPACITY (16)

#endif // ETSUKO_CONSTANTS_H
