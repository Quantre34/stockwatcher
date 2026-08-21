#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compat.h"
#include "zara.h"

/* ── Forward-declared so zara_fetch_sizes can call it ─────────────── */
char *http_get(const char *url);

/* ── JSON helpers ──────────────────────────────────────────────────── */

static const char *read_str(const char *json, char *dest, size_t dest_size) {
    if (!json || *json != '"') {
        if (dest && dest_size) dest[0] = '\0';
        return json;
    }
    json++;
    size_t i = 0;
    while (*json && i < dest_size - 1) {
        if (*json == '\\') {
            json++;
            switch (*json) {
                case '"':  dest[i++] = '"';  break;
                case '\\': dest[i++] = '\\'; break;
                case '/':  dest[i++] = '/';  break;
                case 'n':  dest[i++] = '\n'; break;
                case 't':  dest[i++] = '\t'; break;
                case 'r':  dest[i++] = '\r'; break;
                default:   dest[i++] = *json; break;
            }
            if (*json) json++;
        } else if (*json == '"') {
            json++;
            break;
        } else {
            dest[i++] = *json++;
        }
    }
    dest[i] = '\0';
    return json;
}

static int find_string_field(const char *start, const char *limit,
                             const char *key, char *dest, size_t dest_size) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":\"", key);
    const char *p = strstr(start, search);
    if (!p || (limit && p >= limit)) return 0;
    p += strlen(search) - 1;
    read_str(p, dest, dest_size);
    return 1;
}

static int find_int_field(const char *start, const char *limit,
                          const char *key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(start, search);
    if (!p || (limit && p >= limit)) return -1;
    p += strlen(search);
    while (*p == ' ') p++;
    return atoi(p);
}

/* Returns a pointer just past the closing '}' of the object starting at '{'.
   Returns NULL if unmatched. Handles nested objects and arrays. */
static const char *skip_object(const char *p) {
    if (!p || *p != '{') return NULL;
    int depth = 1;
    p++;
    while (*p && depth > 0) {
        if      (*p == '{' || *p == '[') depth++;
        else if (*p == '}' || *p == ']') depth--;
        p++;
    }
    return depth == 0 ? p : NULL;
}

/*
 * Finds the "id":N that belongs to the object enclosing `pos`.
 * Scans backward for the opening `{`, then forward for the first
 * "id":N at depth 0 inside that object.
 */
static int find_enclosing_id(const char *json_start, const char *pos) {
    const char *p = pos;
    int depth = 0;
    const char *obj_start = NULL;

    while (p > json_start && pos - p < 5000) {
        p--;
        if (*p == '}' || *p == ']')  depth++;
        else if (*p == '[')           { if (depth > 0) depth--; }
        else if (*p == '{') {
            if (depth > 0) depth--;
            else { obj_start = p; break; }
        }
    }
    if (!obj_start) return -1;

    const char *q = obj_start + 1;
    while (q < pos) {
        const char *f = strstr(q, "\"id\":");
        if (!f || f >= pos) break;

        int inner = 0;
        for (const char *r = obj_start + 1; r < f; r++) {
            if (*r == '{' || *r == '[') inner++;
            else if (*r == '}' || *r == ']') inner--;
        }
        if (inner == 0) {
            const char *v = f + 5;
            while (*v == ' ') v++;
            if (*v >= '0' && *v <= '9') {
                int val = atoi(v);
                if (val > 0) return val;
            }
        }
        q = f + 1;
    }
    return -1;
}

/* ── Category listing ──────────────────────────────────────────────── */

static int is_nav_name(const char *name) {
    static const char *skip[] = {
        "TÜMÜNÜ GÖR", "VIEW ALL", "THE NEW", "HAKKINDA",
        "MAĞAZALAR", "CAREERS", "LOOKBOOK", "EDITORIAL",
        "EDİTORYAL", "EDiTORYAL", "STORE LOCATOR",
        "HEDİYE KARTI", "PRESS", "BASIN BÜLTENİ",
        NULL
    };
    for (int i = 0; skip[i]; i++)
        if (strcasecmp(name, skip[i]) == 0) return 1;
    return 0;
}

int zara_list_categories(const char *json,
                         ZaraCategory *cats, int max) {
    int count = 0;
    const char *p = json;

    while (count < max) {
        const char *seo_pos = strstr(p, "\"seoCategoryId\":");
        if (!seo_pos) break;

        int seo_id = atoi(seo_pos + 16);
        if (seo_id <= 0) { p = seo_pos + 1; continue; }

        const char *next_seo = strstr(seo_pos + 1, "\"seoCategoryId\":");

        /* seoCategoryId is now nested inside "seo":{...}.
           Find the "seo":{ anchor so find_enclosing_id locates the
           category object's "id" field, not the seo sub-object's. */
        const char *anchor = seo_pos;
        {
            const char *search_from = (seo_pos > json + 512) ? seo_pos - 512 : json;
            const char *q = search_from;
            while (q < seo_pos) {
                const char *f = strstr(q, "\"seo\":{");
                if (!f || f >= seo_pos) break;
                anchor = f;
                q = f + 1;
            }
        }

        int cat_id = find_enclosing_id(json, anchor);
        if (cat_id <= 0) { p = seo_pos + 1; continue; }

        /* Find the category object's opening '{' to read name/sectionName
           from the start of the object (they appear within the first ~100 chars). */
        const char *cat_start = NULL;
        {
            const char *q = anchor;
            int d = 0;
            while (q > json && anchor - q < 8000) {
                q--;
                if (*q == '}' || *q == ']') d++;
                else if (*q == '[') { if (d > 0) d--; }
                else if (*q == '{') {
                    if (d > 0) d--;
                    else { cat_start = q; break; }
                }
            }
        }

        char name[256] = "";
        char section[64] = "";
        if (cat_start) {
            find_string_field(cat_start, cat_start + 512, "name",
                              name, sizeof(name));
            find_string_field(cat_start, cat_start + 512, "sectionName",
                              section, sizeof(section));
        } else {
            const char *back = (anchor > json + 3000) ? anchor - 3000 : json;
            const char *q = back, *last_name = NULL;
            while (q < anchor) {
                const char *f = strstr(q, "\"name\":\"");
                if (!f || f >= anchor) break;
                last_name = f;
                q = f + 1;
            }
            if (last_name) read_str(last_name + 7, name, sizeof(name));
            q = back;
            const char *last_sec = NULL;
            while (q < anchor) {
                const char *f = strstr(q, "\"sectionName\":\"");
                if (!f || f >= anchor) break;
                last_sec = f;
                q = f + 1;
            }
            if (last_sec) read_str(last_sec + 14, section, sizeof(section));
        }

        {
            const char *irr = strstr(seo_pos, "\"irrelevant\":");
            if (irr && (!next_seo || irr < next_seo)) {
                const char *v = irr + 13;
                while (*v == ' ') v++;
                if (strncmp(v, "true", 4) == 0) {
                    p = seo_pos + 1;
                    continue;
                }
            }
        }

        if (name[0] == '\0' || is_nav_name(name)) { p = seo_pos + 1; continue; }

        if (strcmp(section, "WOMAN") != 0 &&
            strcmp(section, "MAN")   != 0 &&
            strcmp(section, "KID")   != 0 &&
            strcmp(section, "HOME")  != 0 &&
            strcmp(section, "BEAUTY") != 0) {
            p = seo_pos + 1;
            continue;
        }

        int dup = 0;
        for (int i = 0; i < count; i++)
            if (cats[i].cat_id == cat_id) { dup = 1; break; }
        if (dup) { p = seo_pos + 1; continue; }

        cats[count].cat_id = cat_id;
        cats[count].seo_id = seo_id;
        strncpy(cats[count].name, name, sizeof(cats[count].name) - 1);
        cats[count].name[sizeof(cats[count].name) - 1] = '\0';
        strncpy(cats[count].section, section, sizeof(cats[count].section) - 1);
        cats[count].section[sizeof(cats[count].section) - 1] = '\0';
        count++;

        p = seo_pos + 1;
    }
    return count;
}

void zara_make_api_url(const char *locale, int cat_id,
                       char *out, size_t out_size) {
    snprintf(out, out_size,
        "https://www.zara.com/%s/category/%d/products?ajax=true",
        locale, cat_id);
}

/* ── Product parsing (category scan) ──────────────────────────────── */

int zara_parse_products(const char *json, ZaraProduct *prods,
                        int max, const char *locale) {
    int count = 0;
    const char *p = json;

    while (count < max) {
        const char *tp = strstr(p, "\"type\":\"Product\"");
        const char *tb = strstr(p, "\"type\":\"Bundle\"");

        const char *pos;
        if      (tp && tb) pos = (tp < tb) ? tp : tb;
        else if (tp)       pos = tp;
        else if (tb)       pos = tb;
        else               break;

        const char *next = strstr(pos + 1, "\"type\":\"");

        prods[count].id = find_enclosing_id(json, pos);

        /* name */
        prods[count].name[0] = '\0';
        {
            const char *nk = strstr(pos, "\"name\":\"");
            if (nk && (!next || nk < next))
                read_str(nk + 7, prods[count].name, MAX_PROD_NAME);
        }

        /* price */
        prods[count].price = 0;
        prods[count].old_price = 0;
        {
            int v = find_int_field(pos, next, "price");
            if (v > 0) prods[count].price = v;
            int ov = find_int_field(pos, next, "oldPrice");
            if (ov > 0) prods[count].old_price = ov;
        }

        /* product-level availability */
        prods[count].availability[0] = '\0';
        find_string_field(pos, next, "availability",
                          prods[count].availability,
                          sizeof(prods[count].availability));

        /* familyName */
        prods[count].family[0] = '\0';
        find_string_field(pos, next, "familyName",
                          prods[count].family, sizeof(prods[count].family));

        /* availableColors — simple array: [{"colorName":"..."},...]  */
        prods[count].color_count = 0;
        {
            const char *ac = strstr(pos, "\"availableColors\":[");
            if (ac && (!next || ac < next)) {
                const char *cp = ac + 19; /* just past the '[' */
                while (prods[count].color_count < MAX_COLORS_PER_PROD) {
                    const char *cn = strstr(cp, "\"colorName\":\"");
                    if (!cn) break;
                    /* Stop if we've wandered past the array's ']' */
                    const char *bracket = strchr(cp, ']');
                    if (bracket && cn > bracket) break;
                    if (next && cn >= next) break;
                    int ci = prods[count].color_count;
                    read_str(cn + 12, prods[count].colors[ci], MAX_COLOR_NAME);
                    if (prods[count].colors[ci][0])
                        prods[count].color_count++;
                    cp = cn + 1;
                }
            }
        }

        /* URL */
        prods[count].url[0] = '\0';
        {
            char seo_kw[256] = "", seo_pid[64] = "";
            const char *seo_blk = strstr(pos, "\"seo\":{");
            if (seo_blk && (!next || seo_blk < next)) {
                find_string_field(seo_blk, next, "keyword",
                                  seo_kw, sizeof(seo_kw));
                find_string_field(seo_blk, next, "seoProductId",
                                  seo_pid, sizeof(seo_pid));
            }
            if (seo_kw[0] && seo_pid[0]) {
                snprintf(prods[count].url, sizeof(prods[count].url),
                    "https://www.zara.com/%s/%s-p%s.html?v1=%d",
                    locale, seo_kw, seo_pid, prods[count].id);
            }
        }

        if (prods[count].name[0] != '\0') count++;
        p = pos + 1;
    }
    return count;
}

/* ── Size fetching (product page) ──────────────────────────────────── */

int zara_fetch_sizes(const char *product_url,
                     ZaraColorDetail *out, int max_colors) {
    if (!product_url || !product_url[0]) return 0;

    /* Append &ajax=true to get JSON from product page */
    char url[600];
    snprintf(url, sizeof(url), "%s&ajax=true", product_url);

    char *body = http_get(url);
    if (!body) return 0;

    /* Navigate to product.detail.colors */
    const char *detail = strstr(body, "\"detail\":{");
    if (!detail) { free(body); return 0; }
    const char *colors_arr = strstr(detail, "\"colors\":[");
    if (!colors_arr) { free(body); return 0; }

    int count = 0;
    const char *cp = colors_arr + 10; /* just past '[' */

    while (count < max_colors) {
        /* Skip whitespace/commas to next '{' */
        while (*cp && *cp != '{' && *cp != ']') cp++;
        if (*cp != '{') break;

        /* Find end of this color object */
        const char *ce = skip_object(cp);
        if (!ce) break;

        find_string_field(cp, ce, "name",
                          out[count].color, MAX_COLOR_NAME);

        /* Parse sizes array inside this color */
        out[count].size_count = 0;
        const char *sl = strstr(cp, "\"sizes\":[");
        if (sl && sl < ce) {
            const char *sp = sl + 9; /* just past '[' */
            while (out[count].size_count < MAX_SIZES_PER_COLOR) {
                while (*sp && *sp != '{' && *sp != ']') sp++;
                if (*sp != '{') break;
                const char *se = skip_object(sp);
                if (!se) break;
                int si = out[count].size_count;
                find_string_field(sp, se, "name",
                    out[count].sizes[si].name, MAX_SIZE_NAME);
                find_string_field(sp, se, "availability",
                    out[count].sizes[si].availability, 32);
                if (out[count].sizes[si].name[0])
                    out[count].size_count++;
                sp = se;
            }
        }

        if (out[count].color[0]) count++;
        cp = ce;
    }

    free(body);
    return count;
}
