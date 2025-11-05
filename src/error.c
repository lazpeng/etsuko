#include "error.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

void error_abort(const char *message, ...) {
    va_list args;
    va_start(args, message);
    vfprintf(stderr, message, args);
    fprintf(stderr, "\n");
    va_end(args);

#ifdef __EMSCRIPTEN__
    emscripten_cancel_main_loop();
#endif
    exit(EXIT_FAILURE);
}
