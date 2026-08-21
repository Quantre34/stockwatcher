#include <stdio.h>
#include <string.h>
#include "notifier.h"

static void make_safe(const char *src, char *dst, size_t max) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < max - 1; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c > 127
                || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                || (c >= '0' && c <= '9')
                || c == ' ' || c == '-' || c == '_'
                || c == '.' || c == ',' || c == '!' || c == '(')
            dst[j++] = (char)c;
    }
    dst[j] = '\0';
}

#ifdef _WIN32

#include <stdlib.h>
#include <windows.h>

void notify_found(const char *keyword, const char *url) {
    (void)url;
    char safe[256];
    make_safe(keyword, safe, sizeof(safe));

    printf("\a");
    fflush(stdout);

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "powershell -WindowStyle Hidden -Command \""
        "[void][System.Reflection.Assembly]::LoadWithPartialName('System.Windows.Forms');"
        "$n=[System.Windows.Forms.NotifyIcon]::new();"
        "$n.Icon=[System.Drawing.SystemIcons]::Information;"
        "$n.Visible=$true;"
        "$n.ShowBalloonTip(4000,'StockWatcher','%s tespit edildi!',"
        "[System.Windows.Forms.ToolTipIcon]::Info);"
        "Start-Sleep 4;$n.Dispose()\"",
        safe);
    system(cmd);
}

#else  /* macOS */

#include <unistd.h>

static void run_async(const char *path, char *const argv[]) {
    pid_t pid = fork();
    if (pid == 0) {
        execvp(path, argv);
        _exit(127);
    }
}

void notify_found(const char *keyword, const char *url) {
    (void)url;
    char safe[256];
    make_safe(keyword, safe, sizeof(safe));

    char *afplay_args[] = { "afplay", "/System/Library/Sounds/Glass.aiff", NULL };
    run_async("afplay", afplay_args);

    char script[512];
    snprintf(script, sizeof(script),
        "display notification \"%s tespit edildi!\""
        " with title \"StockWatcher\""
        " subtitle \"Urun Bulundu\""
        " sound name \"Glass\"",
        safe);
    char *osa_args[] = { "osascript", "-e", script, NULL };
    run_async("osascript", osa_args);

    char voice_msg[300];
    snprintf(voice_msg, sizeof(voice_msg), "Urun bulundu: %s", safe);
    char *say_args[] = { "say", voice_msg, NULL };
    run_async("say", say_args);
}

#endif
