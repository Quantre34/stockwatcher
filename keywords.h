#ifndef KEYWORDS_H
#define KEYWORDS_H

#include <stddef.h>
#include <pthread.h>

#define MAX_KEYWORDS    64
#define MAX_KEYWORD_LEN 256

typedef struct {
    char            words[MAX_KEYWORDS][MAX_KEYWORD_LEN];
    int             count;
    pthread_mutex_t mutex;
} KeywordList;

void keywords_init(KeywordList *kl);
int  keywords_add(KeywordList *kl, const char *word);
int  keywords_remove(KeywordList *kl, const char *word);
void keywords_list(KeywordList *kl);

/* Tek bir ürün adında eşleşen ilk kelimeyi döner. 0=eşleşme yok */
int  keywords_match(KeywordList *kl, const char *product_name,
                    char *matched_kw, size_t kw_size);

void keywords_destroy(KeywordList *kl);

#endif
