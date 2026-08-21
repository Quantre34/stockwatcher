#ifndef ZARA_H
#define ZARA_H

#include <stddef.h>

#define MAX_PRODUCTS        128
#define MAX_PROD_NAME       512
#define MAX_CATEGORIES      256
#define MAX_COLOR_NAME       64
#define MAX_COLORS_PER_PROD   8
#define MAX_SIZE_NAME        16
#define MAX_SIZES_PER_COLOR  15

typedef struct {
    char name[MAX_SIZE_NAME];
    char availability[32]; /* in_stock | low_on_stock | out_of_stock */
} ZaraSizeInfo;

typedef struct {
    char        color[MAX_COLOR_NAME];
    ZaraSizeInfo sizes[MAX_SIZES_PER_COLOR];
    int          size_count;
} ZaraColorDetail;

typedef struct {
    int  id;
    char name[MAX_PROD_NAME];
    int  price;
    int  old_price; /* >0 means on sale */
    char family[128];
    char url[512];
    char availability[32]; /* product-level */
    char colors[MAX_COLORS_PER_PROD][MAX_COLOR_NAME];
    int  color_count;
} ZaraProduct;

typedef struct {
    int  cat_id;
    int  seo_id;
    char name[256];
    char section[64];
} ZaraCategory;

int  zara_list_categories(const char *json,
                          ZaraCategory *cats, int max);
void zara_make_api_url(const char *locale, int cat_id,
                       char *out, size_t out_size);
int  zara_parse_products(const char *json, ZaraProduct *products,
                         int max, const char *locale);

/* Fetches per-color + per-size availability for a product.
   product_url must be the full URL (already constructed by zara_parse_products).
   Returns number of colors filled in `out`. */
int  zara_fetch_sizes(const char *product_url,
                      ZaraColorDetail *out, int max_colors);

#endif
