#pragma once

#ifdef DEBUG
#define DEBUG_PRINT(fmt, ...) (printf(fmt, __VA_ARGS__))
#else
inline void ____ignore____(...) {}
#define DEBUG_PRINT(fmt, ...) (____ignore____(__VA_ARGS__))
#endif

#define EXIT_PRINT(fmt, ...) (fprintf(stderr, "Error: \033[1;31m" fmt "\033[0m\n" __VA_OPT__(,) __VA_ARGS__), exit(EXIT_FAILURE))

