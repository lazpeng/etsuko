/**
 * constants.h - A few defined constants that can be shared throughout the codebase
 */

#ifndef ETSUKO_CONSTANTS_H
#define ETSUKO_CONSTANTS_H

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define VERSION "0.6.2b"
#define APP_NAME "etsuko"
#define DEFAULT_TITLE APP_NAME " - Karaoke v" VERSION

/**
 * This annotates, very poorly, that the struct owns the following pointer and is responsible for freeing it
 * along with its own destroy() function
 */
#define OWNING
/**
 * This annotates that the struct holds only a weak reference to the pointer and is not responsible for
 * freeing its associated memory
 */
#define WEAK

/**
 * This annotates that the pointer parameter following this keyword can be null and will be treated accordingly by
 * the callee, such as not reading values from it or not writing results to it
 */
#define MAYBE_NULL

#endif // ETSUKO_CONSTANTS_H
