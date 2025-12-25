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

// Portable Thread-Local Storage macro
// On single-threaded systems, it expands to nothing (zero overhead)
#if defined(SINGLE_THREADED) || defined(__STDC_NO_THREADS__) || defined(STM32)
    #define THREAD_LOCAL  /* nothing */
#else
    #define THREAD_LOCAL _Thread_local
#endif

#endif // SUBJECTIVE_C_THREAD_LOCAL_H
