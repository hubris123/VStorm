#pragma once
#include <stddef.h>
void *malloc(size_t n);
void free(void *p);
void *realloc(void *p, size_t n);
void *calloc(size_t n, size_t sz);
void qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *));
int abs(int x);
static inline long labs(long x) { return x < 0 ? -x : x; }
void abort(void);
