/*
 * Thread-Local Storage Macro
 *
 * Portable THREAD_LOCAL macro that expands to nothing on single-threaded
 * systems (zero overhead) and to _Thread_local on multi-threaded systems.
 *
 * Usage:
 *   static THREAD_LOCAL MyType g_my_variable;
 */

#ifndef SUBJECTIVE_C_THREAD_LOCAL_H
#define SUBJECTIVE_C_THREAD_LOCAL_H

// Portable Thread-Local Storage macro.
// On ESP32/single-threaded builds keep zero-overhead storage.
// On toolchains that define __STDC_NO_THREADS__ (e.g. Apple Clang), fall back
// to compiler TLS extension so exception handlers stay thread-local.
#if defined(SINGLE_THREADED) || defined(ESP32_BUILD)
#define THREAD_LOCAL /* nothing */
#elif defined(__STDC_NO_THREADS__)
#if defined(__clang__) || defined(__GNUC__)
#define THREAD_LOCAL __thread
#else
#define THREAD_LOCAL /* nothing */
#endif
#else
#define THREAD_LOCAL _Thread_local
#endif

#endif // SUBJECTIVE_C_THREAD_LOCAL_H
