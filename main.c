#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <pthread.h>
#include <curl/curl.h>
#include "scanner.h"
#include "keywords.h"
#include "zara.h"

#define ZARA_LOCALE    "tr/tr"
#define ZARA_CATS_URL  "https://www.zara.com/" ZARA_LOCALE \
                       "/categories?languageId=-27"

static ScannerState          g_scanner;
static KeywordList           g_keywords;
static volatile sig_atomic_t g_quit = 0;

static void on_signal(int sig) {
    (void)sig;
    g_quit = 1;
    g_scanner.running = 0;
}

static void print_banner(void) {
    printf("\n");
    printf(" ____  _             _   __        __    _       _               \n");
    printf("/ ___|| |_ ___   ___| | _\\ \\      / /_ _| |_ ___| |__   ___ _ __ \n");
    printf("\\___ \\| __/ _ \\ / __| |/ /\\ \\ /\\ / / _` | __/ __| '_ \\ / _ \\ '__|\n");
    printf(" ___) | || (_) | (__|   <  \\ V  V / (_| | || (__| | | |  __/ |   \n");
    printf("|____/ \\__\\___/ \\___|_|\\_\\  \\_/\\_/ \\__,_|\\__\\___|_| |_|\\___|_|   \n");
    printf("\n");
    printf("  ─────────────────────────────────────────────────────────\n");
    printf("   Sana istedigin urunu bulalim!            v2.1\n");
    printf("  ─────────────────────────────────────────────────────────\n");
    printf("\n");
}

static void print_help(void) {
    printf(
        "\n"
        "  Komutlar:\n"
        "    add <kelime>         Anahtar kelime ekle\n"
        "    rm  <kelime>         Anahtar kelime sil\n"
        "    list                 Kelimeleri listele\n"
        "    size XS S M L ...   Beden filtresi koy (bos = temizle)\n"
        "    color Siyah Ekru ... Renk filtresi koy (bos = temizle)\n"
        "    filters              Aktif filtreleri goster\n"
        "    pause                Taramayi duraklat\n"
        "    resume               Taramayi devam ettir\n"
        "    status               Durum goster\n"
        "    help                 Bu yardim\n"
        "    quit                 Cikis\n"
        "\n"
    );
}

static int read_int(const char *prompt) {
    printf("%s", prompt);
    fflush(stdout);
    char buf[64];
    if (!fgets(buf, sizeof(buf), stdin)) return -1;
    return atoi(buf);
}

/* Split `str` on spaces into `words[]`. Returns count. */
static int split_words(const char *str,
                       char words[][MAX_SIZE_NAME], int max_words) {
    int count = 0;
    while (*str && count < max_words) {
        while (*str == ' ') str++;
        if (!*str) break;
        int j = 0;
        while (*str && *str != ' ' && j < MAX_SIZE_NAME - 1)
            words[count][j++] = *str++;
        words[count][j] = '\0';
        if (words[count][0]) count++;
    }
    return count;
}

#define RED   "\033[91m"
#define RESET "\033[0m"

static int is_sale_cat(const char *name) {
    return strcasestr(name, "indirim") || strcasestr(name, "sale") ||
           strcasestr(name, "kampanya");
}

typedef struct { const char *label; const char *section; } SectionMap;

static const SectionMap SECTIONS[] = {
    { "Kadin",  "WOMAN"  },
    { "Erkek",  "MAN"    },
    { "Cocuk",  "KID"    },
    { "Home",   "HOME"   },
    { "Beauty", "BEAUTY" },
};
#define SECTION_COUNT ((int)(sizeof(SECTIONS)/sizeof(SECTIONS[0])))

static int setup_zara_from_url(char *api_url_out, size_t api_url_size) {
    printf("\nZara sayfa URL'sini yapistirin\n");
    printf("(Ornek: https://www.zara.com/tr/tr/s-woman-editorial-ou8989.html?v1=2440352)\n\n");
    printf("URL: ");
    fflush(stdout);

    char url_buf[2048] = "";
    if (!fgets(url_buf, sizeof(url_buf), stdin)) return -1;
    size_t len = strlen(url_buf);
    while (len > 0 && (url_buf[len-1] == '\n' || url_buf[len-1] == '\r'))
        url_buf[--len] = '\0';

    const char *v1 = strstr(url_buf, "v1=");
    if (!v1) {
        fprintf(stderr, "[!] URL'de v1= parametresi bulunamadi.\n"
                        "    Sayfayi tarayicida acarken adres cubugundaki URL'yi kullanin.\n");
        return -1;
    }
    int cat_id = atoi(v1 + 3);
    if (cat_id <= 0) {
        fprintf(stderr, "[!] Gecersiz kategori ID.\n");
        return -1;
    }
    printf("[i] Kategori ID: %d\n", cat_id);
    zara_make_api_url(ZARA_LOCALE, cat_id, api_url_out, api_url_size);
    return 0;
}

static int setup_zara(char *api_url_out, size_t api_url_size) {
    printf("\n  Kategori secim yontemi:\n");
    printf("    1. Listeden sec\n");
    printf("    2. Zara URL'sini yapistir  " RED "(indirim/editorial sayfalar icin)" RESET "\n\n");

    int mode;
    for (;;) {
        mode = read_int("Secim: ");
        if (mode == 1 || mode == 2) break;
        printf("[!] 1 veya 2 girin.\n");
    }

    if (mode == 2)
        return setup_zara_from_url(api_url_out, api_url_size);

    printf("\n[Zara TR] Kategori listesi cekiliyor...\n");
    fflush(stdout);

    char *cats_json = http_get(ZARA_CATS_URL);
    if (!cats_json) {
        fprintf(stderr, "[!] Kategori listesi alinamadi.\n");
        return -1;
    }

    ZaraCategory all_cats[MAX_CATEGORIES];
    int total = zara_list_categories(cats_json, all_cats, MAX_CATEGORIES);
    free(cats_json);

    if (total == 0) {
        fprintf(stderr, "[!] Kategori bulunamadi.\n");
        return -1;
    }

    printf("\n  Bolum secin:\n");
    for (int i = 0; i < SECTION_COUNT; i++)
        printf("    %d. %s\n", i + 1, SECTIONS[i].label);
    printf("\n");

    int sec_choice;
    for (;;) {
        sec_choice = read_int("Bolum (numara): ");
        if (sec_choice >= 1 && sec_choice <= SECTION_COUNT) break;
        printf("[!] 1-%d arasi bir sayi girin.\n", SECTION_COUNT);
    }
    const char *target_section = SECTIONS[sec_choice - 1].section;

    printf("\nKategori ara (bos birakmak: Enter): ");
    fflush(stdout);
    char search_term[128] = "";
    if (fgets(search_term, sizeof(search_term), stdin)) {
        size_t len = strlen(search_term);
        while (len > 0 && (search_term[len-1] == '\n' || search_term[len-1] == '\r'))
            search_term[--len] = '\0';
    }

    ZaraCategory *filtered[MAX_CATEGORIES];
    int nf = 0;

    for (int i = 0; i < total; i++) {
        if (strcmp(all_cats[i].section, target_section) != 0) continue;
        if (search_term[0] &&
            !strcasestr(all_cats[i].name, search_term)) continue;
        filtered[nf++] = &all_cats[i];
        if (nf >= 80) break;
    }

    if (nf == 0) {
        printf("[!] Eslesme bulunamadi. Tum kategoriler gosteriliyor.\n");
        for (int i = 0; i < total; i++) {
            if (strcmp(all_cats[i].section, target_section) == 0)
                filtered[nf++] = &all_cats[i];
            if (nf >= 80) break;
        }
    }

    printf("\n  [%s] %d kategori bulundu:\n\n",
           SECTIONS[sec_choice-1].label, nf);
    for (int i = 0; i < nf; i++) {
        if (is_sale_cat(filtered[i]->name))
            printf("    %3d. " RED "%s" RESET "\n", i + 1, filtered[i]->name);
        else
            printf("    %3d. %s\n", i + 1, filtered[i]->name);
    }
    printf("\n");

    int choice;
    for (;;) {
        choice = read_int("Kategori numarasi: ");
        if (choice >= 1 && choice <= nf) break;
        printf("[!] 1-%d arasi bir sayi girin.\n", nf);
    }

    ZaraCategory *sel = filtered[choice - 1];
    printf("[i] Secilen: %s (cat_id=%d)\n", sel->name, sel->cat_id);

    zara_make_api_url(ZARA_LOCALE, sel->cat_id, api_url_out, api_url_size);
    return 0;
}

static int site_selection(char *api_url_out, size_t api_url_size,
                          char *locale_out, size_t locale_size,
                          int *zara_mode_out) {
    printf("\n");
    printf("╔════════════════════════════════════╗\n");
    printf("║         Site Scanner v2.0          ║\n");
    printf("╚════════════════════════════════════╝\n");
    printf("\n  Taramak istediginiz site:\n");
    printf("    1. Zara Turkiye\n");
    printf("\n");

    int choice;
    for (;;) {
        choice = read_int("Secim: ");
        if (choice == 1) break;
        printf("[!] Gecersiz secim.\n");
    }

    switch (choice) {
        case 1:
            strncpy(locale_out, ZARA_LOCALE, locale_size - 1);
            locale_out[locale_size - 1] = '\0';
            *zara_mode_out = 1;
            return setup_zara(api_url_out, api_url_size);
        default:
            return -1;
    }
}

int main(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    signal(SIGCHLD, SIG_IGN);

    print_banner();
    keywords_init(&g_keywords);

    char api_url[2048] = "";
    char locale[16]    = "";
    int  zara_mode     = 0;

    if (site_selection(api_url, sizeof(api_url),
                       locale, sizeof(locale), &zara_mode) != 0) {
        fprintf(stderr, "[!] Kurulum basarisiz.\n");
        curl_global_cleanup();
        return 1;
    }

    printf("\nAnahtar kelimeler (bos birakmak icin Enter):\n");
    for (;;) {
        printf("  Kelime (veya Enter ile devam): ");
        fflush(stdout);
        char kw[MAX_KEYWORD_LEN];
        if (!fgets(kw, sizeof(kw), stdin)) break;
        size_t len = strlen(kw);
        while (len > 0 && (kw[len-1] == '\n' || kw[len-1] == '\r'))
            kw[--len] = '\0';
        if (len == 0) break;
        if (keywords_add(&g_keywords, kw) == 0)
            printf("  [+] Eklendi: %s\n", kw);
    }

    scanner_init(&g_scanner, api_url, locale, &g_keywords, 20, zara_mode);

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    pthread_t tid;
    int rc = pthread_create(&tid, NULL, scanner_thread, &g_scanner);
    if (rc != 0) {
        fprintf(stderr, "[!] Thread olusturulamadi (hata %d).\n", rc);
        keywords_destroy(&g_keywords);
        scanner_destroy(&g_scanner);
        curl_global_cleanup();
        return 1;
    }

    printf("\n[i] Tarama basliyor — her 20 saniyede bir\n");
    printf("[i] API: %s\n", api_url);
    keywords_list(&g_keywords);
    print_help();

    char line[512];
    while (!g_quit) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;

        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        if (!len) continue;

        if (!strcmp(line, "quit") || !strcmp(line, "exit") || !strcmp(line, "q")) {
            g_quit = 1;
            g_scanner.running = 0;

        } else if (!strncmp(line, "add ", 4)) {
            const char *w = line + 4;
            int r = keywords_add(&g_keywords, w);
            if      (r == 0) printf("[+] Eklendi: \"%s\"\n", w);
            else if (r == 1) printf("[!] Zaten var: \"%s\"\n", w);
            else if (r == 2) printf("[!] Liste dolu (maks %d)\n", MAX_KEYWORDS);

        } else if (!strncmp(line, "rm ", 3)) {
            const char *w = line + 3;
            if (keywords_remove(&g_keywords, w) == 0)
                printf("[-] Silindi: \"%s\"\n", w);
            else
                printf("[!] Bulunamadi: \"%s\"\n", w);

        } else if (!strcmp(line, "list") || !strcmp(line, "ls")) {
            keywords_list(&g_keywords);

        } else if (!strncmp(line, "size", 4)) {
            const char *arg = line + 4;
            while (*arg == ' ') arg++;
            if (!*arg || !strcmp(arg, "clear")) {
                scanner_set_size_filters(&g_scanner, NULL, 0);
                printf("[~] Beden filtresi temizlendi.\n");
            } else {
                char words[MAX_SIZE_FILTERS][MAX_SIZE_NAME];
                int wc = split_words(arg, words, MAX_SIZE_FILTERS);
                const char *ptrs[MAX_SIZE_FILTERS];
                for (int i = 0; i < wc; i++) ptrs[i] = words[i];
                scanner_set_size_filters(&g_scanner, ptrs, wc);
                printf("[~] Beden filtresi:");
                for (int i = 0; i < wc; i++) printf(" %s", words[i]);
                printf("\n");
            }

        } else if (!strncmp(line, "color", 5)) {
            const char *arg = line + 5;
            while (*arg == ' ') arg++;
            if (!*arg || !strcmp(arg, "clear")) {
                scanner_set_color_filters(&g_scanner, NULL, 0);
                printf("[~] Renk filtresi temizlendi.\n");
            } else {
                /* Color names can have spaces, so we accept the whole arg as one filter */
                const char *ptrs[1] = { arg };
                scanner_set_color_filters(&g_scanner, ptrs, 1);
                printf("[~] Renk filtresi: %s\n", arg);
            }

        } else if (!strcmp(line, "filters") || !strcmp(line, "f")) {
            scanner_show_filters(&g_scanner);

        } else if (!strcmp(line, "pause") || !strcmp(line, "p")) {
            g_scanner.paused = 1;
            printf("[~] Tarama duraklatildi.\n");

        } else if (!strcmp(line, "resume") || !strcmp(line, "r")) {
            g_scanner.paused = 0;
            printf("[~] Tarama devam ediyor.\n");

        } else if (!strcmp(line, "status") || !strcmp(line, "s")) {
            printf("[i] Durum  : %s\n",
                   g_scanner.paused ? "Duraklatildi" : "Taraniyor");
            printf("[i] API    : %s\n", g_scanner.api_url);
            printf("[i] Tarama : #%d\n", (int)g_scanner.scan_count);
            keywords_list(&g_keywords);
            scanner_show_filters(&g_scanner);

        } else if (!strcmp(line, "help") || !strcmp(line, "h") || !strcmp(line, "?")) {
            print_help();

        } else {
            printf("[!] Bilinmeyen komut: \"%s\"  (help yazin)\n", line);
        }
    }

    g_scanner.running = 0;
    pthread_join(tid, NULL);
    keywords_destroy(&g_keywords);
    scanner_destroy(&g_scanner);
    curl_global_cleanup();

    printf("\nHoscakal!\n");
    return 0;
}
