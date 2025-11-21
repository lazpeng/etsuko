#include "etsuko.h"

#include <stdio.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "error.h"
#include "renderer.h"

static void error_callback(const int error, const char *description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

int global_init(void) {
    glfwSetErrorCallback(error_callback);

    if ( !glfwInit() ) {
        error_abort("glfwInit failed");
    }

    render_init();

    return 0;
}

void global_finish(void) {
    render_finish();
    glfwTerminate();
}
