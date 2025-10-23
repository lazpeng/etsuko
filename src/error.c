#include "error.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void error_abort(const char *message, ...) {
    va_list args;
    va_start(args, message);
    vfprintf(stderr, message, args);
    fprintf(stderr, "\n");
    va_end(args);

    exit(EXIT_FAILURE);
}
