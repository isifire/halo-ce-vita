#ifndef HALO_VITA_PREFIX_H
#define HALO_VITA_PREFIX_H
#if !defined(__arm__) || __SIZEOF_POINTER__ != 4
#error Vita game units require ARMv7 with 32-bit pointers
#endif
#define HALO_VITA 1
#define _WINSOCKAPI_ 1
#define __thread
#include "halo_linux_prefix.h"
/* Newlib pulls limits.h into these headers; Halo uses enum constants with
 * these names. Load the headers before clearing just the conflicting macros. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#undef LONG_MAX
#undef LONG_MIN
#undef CHAR_MAX
#undef CHAR_MIN
#endif
