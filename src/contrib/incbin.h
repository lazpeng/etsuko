/**
 * incbin.h - Provides a way to include raw files in to the source code at build time.
 * it would be preferable to use #embed, but it's an extension and some IDEs do not recognize it,
 * so it litters the editor with errors
 *
 * Included from: https://gist.github.com/mmozeiko/ed9655cf50341553d282?permalink_comment_id=4514008#gistcomment-4514008
 */

#ifndef ETSUKO_INCBIN_H
#define ETSUKO_INCBIN_H

#include <stdio.h>

#define STR2(x) #x
#define STR(x) STR2(x)

#ifdef __APPLE__
#define USTR(x) "_" STR(x)
#else
#define USTR(x) STR(x)
#endif

#ifdef _WIN32
#define INCBIN_SECTION ".rdata, \"dr\""
#elif defined __APPLE__
#define INCBIN_SECTION "__TEXT,__const"
#else
#define INCBIN_SECTION ".rodata"
#endif

#define INCBIN(name, file) \
__asm__(".section " INCBIN_SECTION "\n" \
".global " USTR(incbin) "_" STR(name) "\n" \
".balign 16\n" \
USTR(incbin) "_" STR(name) ":\n" \
".incbin \"" file "\"\n" \
\
".global " USTR(incbin) "_" STR(name) "_end\n" \
".balign 1\n" \
USTR(incbin) "_" STR(name) "_end:\n" \
".byte 0\n" \
); \
extern __attribute__((aligned(16))) const char incbin_ ## name []; \
extern                              const char incbin_ ## name ## _end[];

#endif // ETSUKO_INCBIN_H
