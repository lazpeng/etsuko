/**
 * error.h - Defines a single, non-returning function for printing out an error and exiting prematurely upon major failure
 */

#ifndef ETSUKO_ERROR_H
#define ETSUKO_ERROR_H

#include <stdnoreturn.h>

noreturn void error_abort(const char *message, ...);

#endif // ETSUKO_ERROR_H
