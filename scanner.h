#ifndef SCANNER_H
#define SCANNER_H

#include <stdatomic.h>
#include <pthread.h>
#include "keywords.h"
#include "zara.h"

#define MAX_SIZE_FILTERS  8
#define MAX_COLOR_FILTERS 4

typedef struct {
    char         api_url[2048];
    char         locale[16];
    KeywordList *keywords;
    _Atomic int  running;
    _Atomic int  paused;
    int          interval;
    _Atomic int  scan_count;
    int          zara_mode;   /* read-only after init */

    int          prev_matched[MAX_PRODUCTS];
    int          prev_count;

    /* Size / color filters — protected by filter_mutex */
    pthread_mutex_t filter_mutex;
    char  size_filters[MAX_SIZE_FILTERS][MAX_SIZE_NAME];
    int   size_filter_count;
    char  color_filters[MAX_COLOR_FILTERS][MAX_COLOR_NAME];
    int   color_filter_count;
} ScannerState;

char *http_get(const char *url);

void  scanner_init(ScannerState *s, const char *api_url,
                   const char *locale, KeywordList *kl,
                   int interval, int zara_mode);
void *scanner_thread(void *arg);
void  scanner_destroy(ScannerState *s);

void  scanner_set_size_filters(ScannerState *s,
                               const char * const words[], int count);
void  scanner_set_color_filters(ScannerState *s,
                                const char * const words[], int count);
void  scanner_show_filters(const ScannerState *s);

#endif
