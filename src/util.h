#pragma once

#include <stdio.h>

#ifdef DEBUG
#define DEBUG_PRINT(fmt, ...) (printf("Debug: " fmt "\n" __VA_OPT__(,) __VA_ARGS__))
#else
#define DEBUG_PRINT(fmt, ...)
#endif

#define EXIT_PRINT(fmt, ...) (fprintf(stderr, "Error: \033[1;31m" fmt "\033[0m" " (%s on line %d)\n" __VA_OPT__(,) __VA_ARGS__, __func__, __LINE__), fflush(stderr), exit(EXIT_FAILURE))

