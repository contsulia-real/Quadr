//
// Created by Why23 on 2026/3/29.
//
// Wrapper around quadr_platform.h::quadr_now_ms() for backward compatibility.

#ifndef QUADR_UTILS_H
#define QUADR_UTILS_H

#include "quadr_platform.h"

static inline double now(void) {
    return quadr_now_ms();
}

#endif //QUADR_UTILS_H
