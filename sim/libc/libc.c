/* Minimal freestanding libc for the wasm32 simulator build.
 * Heap = LVGL's built-in allocator (same 48 KB pool the firmware uses). */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "lvgl.h"

void *memcpy(void *d, const void *s, size_t n)
{
    uint8_t *dd = d; const uint8_t *ss = s;
    while (n--) *dd++ = *ss++;
    return d;
}
void *memmove(void *d, const void *s, size_t n)
{
    uint8_t *dd = d; const uint8_t *ss = s;
    if (dd < ss) { while (n--) *dd++ = *ss++; }
    else { dd += n; ss += n; while (n--) *--dd = *--ss; }
    return d;
}
void *memset(void *d, int c, size_t n)
{
    uint8_t *dd = d;
    while (n--) *dd++ = (uint8_t)c;
    return d;
}
int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    for (; n; n--, x++, y++) if (*x != *y) return *x - *y;
    return 0;
}
size_t strlen(const char *s) { const char *p = s; while (*p) p++; return p - s; }
int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}
int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b) return (uint8_t)*a - (uint8_t)*b;
        if (!*a) return 0;
    }
    return 0;
}
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++) != 0) {} return r; }
char *strncpy(char *d, const char *s, size_t n)
{
    char *r = d;
    for (; n && *s; n--) *d++ = *s++;
    while (n--) *d++ = 0;
    return r;
}
char *strcat(char *d, const char *s) { strcpy(d + strlen(d), s); return d; }
char *strchr(const char *s, int c)
{
    for (;; s++) { if (*s == (char)c) return (char *)s; if (!*s) return NULL; }
}
char *strrchr(const char *s, int c)
{
    const char *r = NULL;
    for (;; s++) { if (*s == (char)c) r = s; if (!*s) return (char *)r; }
}

void *malloc(size_t n) { return lv_mem_alloc(n); }
void free(void *p) { lv_mem_free(p); }
void *realloc(void *p, size_t n) { return lv_mem_realloc(p, n); }
void *calloc(size_t n, size_t sz)
{
    void *p = lv_mem_alloc(n * sz);
    if (p) memset(p, 0, n * sz);
    return p;
}
int abs(int x) { return x < 0 ? -x : x; }
void abort(void) { sim_abort(); }

void qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *))
{
    /* insertion sort: only used on tiny arrays by stb_truetype */
    uint8_t *b = base, tmp[64];
    for (size_t i = 1; i < n; i++) {
        for (size_t j = i; j > 0 && cmp(b + (j - 1) * sz, b + j * sz) > 0; j--) {
            for (size_t k = 0; k < sz; k += sizeof(tmp)) {
                size_t m = sz - k < sizeof(tmp) ? sz - k : sizeof(tmp);
                memcpy(tmp, b + (j - 1) * sz + k, m);
                memcpy(b + (j - 1) * sz + k, b + j * sz + k, m);
                memcpy(b + j * sz + k, tmp, m);
            }
        }
    }
}

int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap) { return lv_vsnprintf(buf, n, fmt, ap); }
int snprintf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int r = lv_vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}
int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int r = lv_vsnprintf(buf, 1 << 20, fmt, ap);
    va_end(ap);
    return r;
}
