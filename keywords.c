#include <stdio.h>
#include <string.h>
#include "compat.h"
#include "keywords.h"

void keywords_init(KeywordList *kl) {
    kl->count = 0;
    pthread_mutex_init(&kl->mutex, NULL);
}

int keywords_add(KeywordList *kl, const char *word) {
    if (!word || !*word) return -1;

    pthread_mutex_lock(&kl->mutex);

    for (int i = 0; i < kl->count; i++) {
        if (strcasecmp(kl->words[i], word) == 0) {
            pthread_mutex_unlock(&kl->mutex);
            return 1;
        }
    }

    if (kl->count >= MAX_KEYWORDS) {
        pthread_mutex_unlock(&kl->mutex);
        return 2;
    }

    strncpy(kl->words[kl->count], word, MAX_KEYWORD_LEN - 1);
    kl->words[kl->count][MAX_KEYWORD_LEN - 1] = '\0';
    kl->count++;

    pthread_mutex_unlock(&kl->mutex);
    return 0;
}

int keywords_remove(KeywordList *kl, const char *word) {
    pthread_mutex_lock(&kl->mutex);

    for (int i = 0; i < kl->count; i++) {
        if (strcasecmp(kl->words[i], word) == 0) {
            for (int j = i; j < kl->count - 1; j++)
                memcpy(kl->words[j], kl->words[j + 1], MAX_KEYWORD_LEN);
            kl->count--;
            pthread_mutex_unlock(&kl->mutex);
            return 0;
        }
    }

    pthread_mutex_unlock(&kl->mutex);
    return 1;
}

void keywords_list(KeywordList *kl) {
    pthread_mutex_lock(&kl->mutex);

    if (kl->count == 0) {
        printf("[i] Anahtar kelime listesi bos.\n");
    } else {
        printf("[i] Anahtar kelimeler (%d adet):\n", kl->count);
        for (int i = 0; i < kl->count; i++)
            printf("    %d. %s\n", i + 1, kl->words[i]);
    }
    fflush(stdout);

    pthread_mutex_unlock(&kl->mutex);
}

int keywords_match(KeywordList *kl, const char *product_name,
                   char *matched_kw, size_t kw_size) {
    pthread_mutex_lock(&kl->mutex);
    int found = 0;

    for (int i = 0; i < kl->count; i++) {
        if (strcasestr(product_name, kl->words[i])) {
            if (matched_kw && kw_size) {
                strncpy(matched_kw, kl->words[i], kw_size - 1);
                matched_kw[kw_size - 1] = '\0';
            }
            found = 1;
            break;
        }
    }

    pthread_mutex_unlock(&kl->mutex);
    return found;
}

void keywords_destroy(KeywordList *kl) {
    pthread_mutex_destroy(&kl->mutex);
}
