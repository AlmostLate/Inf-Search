#ifndef GROWBUF_H
#define GROWBUF_H

#include <stdlib.h>
#include <string.h>

typedef struct {
    int   *data;
    int    len;
    int    cap;
} IntBuf;

static inline void ib_init(IntBuf *b) { b->data = NULL; b->len = 0; b->cap = 0; }

static inline void ib_free(IntBuf *b) { free(b->data); b->data = NULL; b->len = 0; b->cap = 0; }

static inline void ib_push(IntBuf *b, int val) {
    if (b->len == b->cap) {
        int nc = b->cap < 8 ? 8 : b->cap + b->cap / 2;
        int *tmp = (int *)realloc(b->data, (size_t)nc * sizeof(int));
        if (!tmp) return;
        b->data = tmp;
        b->cap = nc;
    }
    b->data[b->len++] = val;
}

static inline int ib_get(IntBuf *b, int idx) { return b->data[idx]; }

static inline void _ib_sift_down(int *a, int start, int end) {
    int root = start;
    while (root * 2 + 1 <= end) {
        int child = root * 2 + 1, sw = root;
        if (a[sw] < a[child]) sw = child;
        if (child + 1 <= end && a[sw] < a[child + 1]) sw = child + 1;
        if (sw == root) return;
        int tmp = a[root]; a[root] = a[sw]; a[sw] = tmp;
        root = sw;
    }
}

static inline void ib_sort(IntBuf *b) {
    int n = b->len;
    if (n < 2) return;
    for (int s = (n - 2) / 2; s >= 0; s--) _ib_sift_down(b->data, s, n - 1);
    for (int e = n - 1; e > 0; e--) {
        int tmp = b->data[0]; b->data[0] = b->data[e]; b->data[e] = tmp;
        _ib_sift_down(b->data, 0, e - 1);
    }
}

static inline void ib_unique(IntBuf *b) {
    if (b->len < 2) return;
    int w = 1;
    for (int r = 1; r < b->len; r++)
        if (b->data[r] != b->data[w - 1]) b->data[w++] = b->data[r];
    b->len = w;
}

static inline void ib_intersect(IntBuf *a, IntBuf *b, IntBuf *out) {
    int i = 0, j = 0;
    while (i < a->len && j < b->len) {
        if (a->data[i] == b->data[j]) { ib_push(out, a->data[i]); i++; j++; }
        else if (a->data[i] < b->data[j]) i++;
        else j++;
    }
}

static inline void ib_union(IntBuf *a, IntBuf *b, IntBuf *out) {
    int i = 0, j = 0;
    while (i < a->len && j < b->len) {
        if (a->data[i] < b->data[j]) ib_push(out, a->data[i++]);
        else if (a->data[i] > b->data[j]) ib_push(out, b->data[j++]);
        else { ib_push(out, a->data[i]); i++; j++; }
    }
    while (i < a->len) ib_push(out, a->data[i++]);
    while (j < b->len) ib_push(out, b->data[j++]);
}

static inline void ib_subtract(IntBuf *a, IntBuf *b, IntBuf *out) {
    int i = 0, j = 0;
    while (i < a->len && j < b->len) {
        if (a->data[i] < b->data[j]) ib_push(out, a->data[i++]);
        else if (a->data[i] > b->data[j]) j++;
        else { i++; j++; }
    }
    while (i < a->len) ib_push(out, a->data[i++]);
}

typedef struct { char **data; int len; int cap; } StrBuf;

static inline void sb_init(StrBuf *b) { b->data = NULL; b->len = 0; b->cap = 0; }

static inline void sb_free_deep(StrBuf *b) {
    for (int i = 0; i < b->len; i++) free(b->data[i]);
    free(b->data); b->data = NULL; b->len = 0; b->cap = 0;
}

static inline void sb_push(StrBuf *b, const char *s) {
    if (b->len == b->cap) {
        int nc = b->cap < 8 ? 8 : b->cap + b->cap / 2;
        char **tmp = (char **)realloc(b->data, (size_t)nc * sizeof(char *));
        if (!tmp) return;
        b->data = tmp;
        b->cap = nc;
    }
    b->data[b->len++] = strdup(s);
}

#endif
