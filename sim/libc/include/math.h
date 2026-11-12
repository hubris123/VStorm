#pragma once
/* sqrt/floor/ceil/fabs compile to native wasm instructions; the
 * transcendental functions are imported from JavaScript's Math. */
#define M_PI 3.14159265358979323846
#define WASM_IMPORT(n) __attribute__((import_module("env"), import_name(n)))
WASM_IMPORT("cos")   double cos(double);
WASM_IMPORT("sin")   double sin(double);
WASM_IMPORT("acos")  double acos(double);
WASM_IMPORT("atan2") double atan2(double, double);
WASM_IMPORT("pow")   double pow(double, double);
WASM_IMPORT("exp")   double exp(double);
WASM_IMPORT("log")   double log(double);
static inline double sqrt(double x)  { return __builtin_sqrt(x); }
static inline double floor(double x) { return __builtin_floor(x); }
static inline double ceil(double x)  { return __builtin_ceil(x); }
static inline double fabs(double x)  { return __builtin_fabs(x); }
static inline double fmod(double x, double y) { return x - y * __builtin_trunc(x / y); }
static inline float sqrtf(float x)  { return __builtin_sqrtf(x); }
static inline float fabsf(float x)  { return __builtin_fabsf(x); }
static inline float floorf(float x) { return __builtin_floorf(x); }
static inline float cosf(float x)   { return (float)cos(x); }
static inline float sinf(float x)   { return (float)sin(x); }
static inline float tanhf(float x)  { double e = exp(2.0 * x); return (float)((e - 1) / (e + 1)); }
