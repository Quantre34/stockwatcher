#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "notifier.h"

static void run_async(const char *path, char *const argv[]) {
    pid_t pid = fork();
    if (pid == 0) {
        execvp(path, argv);
        _exit(127);
    }
    /* Parent returns immediately; SIGCHLD=SIG_IGN reaps the child */
}

void notify_found(const char *keyword, const char *url) {
    (void)url;

    /* Whitelist filter: allow UTF-8 multibyte (>127) and safe ASCII only.
       No shell is involved, but AppleScript strings are delimited by "
       so we still exclude " and \. */
    char safe[256];
    size_t j = 0;
    for (size_t i = 0; keyword[i] && j < sizeof(safe) - 1; i++) {
        unsigned char c = (unsigned char)keyword[i];
        if (c > 127
                || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                || (c >= '0' && c <= '9')
                || c == ' ' || c == '-' || c == '_'
                || c == '.' || c == ',' || c == '!' || c == '(') {
            safe[j++] = (char)c;
        }
    }
    safe[j] = '\0';

    /* Sound — async, no blocking */
    char *afplay_args[] = {
        "afplay", "/System/Library/Sounds/Glass.aiff", NULL
    };
    run_async("afplay", afplay_args);

    /* Desktop banner via osascript -e (no temp file, no shell) */
    char script[512];
    snprintf(script, sizeof(script),
        "display notification \"%s tespit edildi!\""
        " with title \"StockWatch\""
        " subtitle \"Urun Bulundu\""
        " sound name \"Glass\"",
        safe);
    char *osa_args[] = { "osascript", "-e", script, NULL };
    run_async("osascript", osa_args);

    /* Voice */
    char voice_msg[300];
    snprintf(voice_msg, sizeof(voice_msg), "Urun bulundu: %s", safe);
    char *say_args[] = { "say", voice_msg, NULL };
    run_async("say", say_args);
}
