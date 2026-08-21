#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <curl/curl.h>
#include "scanner.h"
#include "notifier.h"
#include "zara.h"

/* ── HTTP ──────────────────────────────────────────────────────────── */

typedef struct { char *data; size_t size; } HttpBuf;

static size_t write_cb(void *ptr, size_t sz, size_t nmemb, void *userp) {
    size_t real  = sz * nmemb;
    HttpBuf *buf = userp;
    char *tmp    = realloc(buf->data, buf->size + real + 1);
    if (!tmp) return 0;
    buf->data = tmp;
    memcpy(buf->data + buf->size, ptr, real);
    buf->size += real;
    buf->data[buf->size] = '\0';
    return real;
}

char *http_get(const char *url) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    HttpBuf buf = { malloc(1), 0 };
    if (!buf.data) { curl_easy_cleanup(curl); return NULL; }
    buf.data[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL,           url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &buf);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
        "AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/120.0.0.0 Safari/537.36");

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers,
        "Accept: application/json, text/html, */*");
    headers = curl_slist_append(headers,
        "Referer: https://www.zara.com/tr/tr/");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "\n[!] HTTP hatasi: %s\n> ",
                curl_easy_strerror(res));
        fflush(stderr);
        free(buf.data);
        return NULL;
    }
    return buf.data;
}

/* ── Helpers ───────────────────────────────────────────────────────── */

static void now_str(char *buf, size_t size) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(buf, size, "%H:%M:%S", tm);
}

static int id_in_list(const int *list, int count, int id) {
    for (int i = 0; i < count; i++)
        if (list[i] == id) return 1;
    return 0;
}

static const char *avail_tr(const char *a) {
    if (strcmp(a, "in_stock")         == 0) return "var";
    if (strcmp(a, "low_on_stock")     == 0) return "az";
    if (strcmp(a, "low_availability") == 0) return "az";
    if (strcmp(a, "out_of_stock")     == 0) return "yok";
    return "?";
}

/* ── Filter helpers ────────────────────────────────────────────────── */

void scanner_set_size_filters(ScannerState *s,
                              const char * const words[], int count) {
    pthread_mutex_lock(&s->filter_mutex);
    s->size_filter_count = 0;
    for (int i = 0; i < count && i < MAX_SIZE_FILTERS; i++) {
        strncpy(s->size_filters[i], words[i], MAX_SIZE_NAME - 1);
        s->size_filters[i][MAX_SIZE_NAME - 1] = '\0';
        s->size_filter_count++;
    }
    pthread_mutex_unlock(&s->filter_mutex);
}

void scanner_set_color_filters(ScannerState *s,
                               const char * const words[], int count) {
    pthread_mutex_lock(&s->filter_mutex);
    s->color_filter_count = 0;
    for (int i = 0; i < count && i < MAX_COLOR_FILTERS; i++) {
        strncpy(s->color_filters[i], words[i], MAX_COLOR_NAME - 1);
        s->color_filters[i][MAX_COLOR_NAME - 1] = '\0';
        s->color_filter_count++;
    }
    pthread_mutex_unlock(&s->filter_mutex);
}

void scanner_show_filters(const ScannerState *s) {
    /* Cast away const for mutex — read-only lock is still a lock */
    pthread_mutex_lock((pthread_mutex_t *)&s->filter_mutex);
    if (s->size_filter_count == 0 && s->color_filter_count == 0) {
        printf("[i] Aktif filtre yok.\n");
    } else {
        if (s->size_filter_count > 0) {
            printf("[i] Beden filtresi:");
            for (int i = 0; i < s->size_filter_count; i++)
                printf(" %s", s->size_filters[i]);
            printf("\n");
        }
        if (s->color_filter_count > 0) {
            printf("[i] Renk filtresi:");
            for (int i = 0; i < s->color_filter_count; i++)
                printf(" %s", s->color_filters[i]);
            printf("\n");
        }
    }
    fflush(stdout);
    pthread_mutex_unlock((pthread_mutex_t *)&s->filter_mutex);
}

/* ── Init / destroy ────────────────────────────────────────────────── */

void scanner_init(ScannerState *s, const char *api_url,
                  const char *locale, KeywordList *kl,
                  int interval, int zara_mode) {
    strncpy(s->api_url, api_url, sizeof(s->api_url) - 1);
    s->api_url[sizeof(s->api_url) - 1] = '\0';
    strncpy(s->locale, locale, sizeof(s->locale) - 1);
    s->locale[sizeof(s->locale) - 1] = '\0';
    s->keywords          = kl;
    s->running           = 1;
    s->paused            = 0;
    s->interval          = interval;
    s->scan_count        = 0;
    s->zara_mode         = zara_mode;
    s->prev_count        = 0;
    s->size_filter_count = 0;
    s->color_filter_count= 0;
    pthread_mutex_init(&s->filter_mutex, NULL);
}

void scanner_destroy(ScannerState *s) {
    pthread_mutex_destroy(&s->filter_mutex);
}

/* ── Scanner thread ────────────────────────────────────────────────── */

void *scanner_thread(void *arg) {
    ScannerState *s = arg;
    char ts[32];

    while (s->running) {
        if (s->paused) { sleep(1); continue; }

        now_str(ts, sizeof(ts));
        s->scan_count++;
        printf("\n[%s] Tarama #%d basliyor...\n", ts, s->scan_count);
        fflush(stdout);

        char *body = http_get(s->api_url);
        if (!body) {
            printf("> ");
            fflush(stdout);
            for (int i = 0; i < s->interval && s->running && !s->paused; i++)
                sleep(1);
            continue;
        }

        ZaraProduct products[MAX_PRODUCTS];
        int n = zara_parse_products(body, products, MAX_PRODUCTS, s->locale);

        int curr_ids[MAX_PRODUCTS];
        int curr_count = 0;
        int new_found  = 0;

        now_str(ts, sizeof(ts));

        /* Read filter state once per scan */
        pthread_mutex_lock(&s->filter_mutex);
        int sf_count = s->size_filter_count;
        int cf_count = s->color_filter_count;
        char sf[MAX_SIZE_FILTERS][MAX_SIZE_NAME];
        char cf[MAX_COLOR_FILTERS][MAX_COLOR_NAME];
        for (int i = 0; i < sf_count; i++)
            memcpy(sf[i], s->size_filters[i], MAX_SIZE_NAME);
        for (int i = 0; i < cf_count; i++)
            memcpy(cf[i], s->color_filters[i], MAX_COLOR_NAME);
        pthread_mutex_unlock(&s->filter_mutex);

        for (int i = 0; i < n; i++) {
            char matched_kw[MAX_KEYWORD_LEN];
            if (!keywords_match(s->keywords, products[i].name,
                                matched_kw, sizeof(matched_kw)))
                continue;

            /* ── Color filter (uses category-scan data, no extra request) ── */
            if (cf_count > 0 && products[i].color_count > 0) {
                int passes = 0;
                for (int ci = 0; ci < products[i].color_count && !passes; ci++)
                    for (int fi = 0; fi < cf_count && !passes; fi++)
                        if (strcasestr(products[i].colors[ci], cf[fi]))
                            passes = 1;
                if (!passes) continue;
            }

            /* ── Track ID (must succeed before notification) ── */
            if (curr_count >= MAX_PRODUCTS) continue;
            curr_ids[curr_count++] = products[i].id;

            if (id_in_list(s->prev_matched, s->prev_count, products[i].id))
                continue; /* already notified */

            /* ── Size filter (fetches product page — only for new products) ── */
            ZaraColorDetail detail[MAX_COLORS_PER_PROD];
            int detail_count = 0;

            if (sf_count > 0) {
                detail_count = zara_fetch_sizes(products[i].url,
                                               detail, MAX_COLORS_PER_PROD);
                int passes = 0;
                for (int ci = 0; ci < detail_count && !passes; ci++) {
                    for (int si = 0; si < detail[ci].size_count && !passes; si++) {
                        if (strcmp(detail[ci].sizes[si].availability,
                                   "out_of_stock") == 0) continue;
                        for (int fi = 0; fi < sf_count && !passes; fi++)
                            if (strcasecmp(detail[ci].sizes[si].name,
                                           sf[fi]) == 0)
                                passes = 1;
                    }
                }
                if (!passes) continue;
            }

            /* ── New matching product: print + notify ── */
            new_found++;
            int price_tl     = products[i].price / 100;
            int old_price_tl = products[i].old_price / 100;
            int on_sale      = old_price_tl > 0 && old_price_tl > price_tl;

#define RED_   "\033[91m"
#define RESET_ "\033[0m"

            const char *avail = products[i].availability;
            const char *avail_label;
            if      (strcmp(avail, "in_stock")         == 0) avail_label = "Stokta";
            else if (strcmp(avail, "low_availability") == 0) avail_label = "Son urunler!";
            else if (strcmp(avail, "out_of_stock")     == 0) avail_label = "Stok yok";
            else                                             avail_label = avail;

            if (on_sale)
                printf("[%s] *** YENİ URUN *** " RED_ "%s" RESET_ "\n",
                       ts, products[i].name);
            else
                printf("[%s] *** YENİ URUN *** %s\n", ts, products[i].name);
            printf("         Kategori : %s\n", products[i].family);
            if (on_sale)
                printf("         Fiyat    : " RED_ "%d TL" RESET_
                       " (indirim oncesi: %d TL)\n", price_tl, old_price_tl);
            else
                printf("         Fiyat    : %d TL\n", price_tl);
            printf("         Stok     : %s\n", avail_label);

            /* Colors from category scan */
            if (products[i].color_count > 0) {
                printf("         Renkler  :");
                for (int ci = 0; ci < products[i].color_count; ci++)
                    printf(" %s%s", products[i].colors[ci],
                           ci < products[i].color_count - 1 ? "," : "");
                printf("\n");
            }

            /* Sizes from product page (only fetched when size filter is set) */
            if (detail_count > 0) {
                for (int ci = 0; ci < detail_count; ci++) {
                    printf("         Bedenler [%s]:", detail[ci].color);
                    for (int si = 0; si < detail[ci].size_count; si++) {
                        const char *a = detail[ci].sizes[si].availability;
                        printf(" %s(%s)", detail[ci].sizes[si].name,
                               avail_tr(a));
                    }
                    printf("\n");
                }
            }

            if (products[i].url[0])
                printf("         Link     : %s\n", products[i].url);
            printf("\n");
            notify_found(products[i].name, products[i].url);
        }

        if (new_found == 0 && curr_count > 0)
            printf("[%s] %d urun mevcut, yeni esleme yok.\n", ts, curr_count);
        else if (curr_count == 0)
            printf("[%s] Esleme yok (%d urun taranmadi).\n", ts, n);

        memcpy(s->prev_matched, curr_ids, curr_count * sizeof(int));
        s->prev_count = curr_count;

        free(body);
        printf("> ");
        fflush(stdout);

        for (int i = 0; i < s->interval && s->running && !s->paused; i++)
            sleep(1);
    }

    return NULL;
}
