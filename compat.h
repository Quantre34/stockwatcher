#ifndef COMPAT_H
#define COMPAT_H

#ifdef _WIN32
#include <windows.h>
#include <string.h>

#define strcasecmp  _stricmp
#define strncasecmp _strnicmp

static inline char *strcasestr(const char *h, const char *n) {
    if (!*n) return (char *)h;
    size_t nl = strlen(n);
    for (; *h; h++)
        if (_strnicmp(h, n, nl) == 0)
            return (char *)h;
    return NULL;
}

#define sleep(s) Sleep((DWORD)((s) * 1000))

static inline void platform_init(void) {
    SetConsoleOutputCP(CP_UTF8);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(out, &mode))
            SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
}

#else

static inline void platform_init(void) {}

#endif /* _WIN32 */

#endif /* COMPAT_H */
