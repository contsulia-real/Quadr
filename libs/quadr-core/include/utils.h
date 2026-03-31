//
// Created by Why23 on 2026/3/29.
//

#ifndef QUADR_UTILS_H
#define QUADR_UTILS_H

#ifdef _WIN32
#include <windows.h>

inline double now() {
    static LARGE_INTEGER freq;
    static int init = 0;
    if (!init) {
        QueryPerformanceFrequency(&freq);
        init = 1;
    }
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / freq.QuadPart;
}

#else
#include <time.h>

double now() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
#endif

#endif //QUADR_UTILS_H