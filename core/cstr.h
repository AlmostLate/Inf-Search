#ifndef CSTR_H
#define CSTR_H

#include <stdlib.h>

static inline int cstr_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

static inline char *cstr_dup(const char *s) {
    int n = cstr_len(s);
    char *d = (char *)malloc((size_t)(n + 1));
    if (!d) return NULL;
    for (int i = 0; i <= n; i++) d[i] = s[i];
    return d;
}

static inline int cstr_cmp(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return (unsigned char)*a - (unsigned char)*b; a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static inline int cstr_eq(const char *a, const char *b) { return cstr_cmp(a, b) == 0; }

static inline void cstr_tolower(char *s) { for (; *s; s++) if (*s >= 'A' && *s <= 'Z') *s += 32; }

static inline int cstr_endswith(const char *s, int slen, const char *suf, int suflen) {
    if (suflen > slen) return 0;
    return cstr_cmp(s + slen - suflen, suf) == 0;
}

static inline void cstr_setlen(char *s, int newlen) { s[newlen] = '\0'; }
static inline void cstr_copy(char *dst, const char *src) { while ((*dst++ = *src++)); }
static inline void cstr_cat(char *dst, const char *src) { while (*dst) dst++; cstr_copy(dst, src); }
static inline int cstr_is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static inline int cstr_is_digit(char c) { return c >= '0' && c <= '9'; }
static inline int cstr_is_alnum(char c) { return cstr_is_alpha(c) || cstr_is_digit(c); }

#endif
