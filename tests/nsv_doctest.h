// Includes doctest.h with its own warnings silenced.
#pragma once

#ifdef __clang__
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wunknown-warning-option"
  #pragma clang diagnostic ignored "-W#warnings"
  #pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wcpp"
#endif

// MSVC 19.51+ (VS 2026) rejects doctest's forward declarations of std types (C5285),
// so have doctest include the real std headers instead.
#if defined(_MSC_VER) && !defined(DOCTEST_CONFIG_USE_STD_HEADERS)
  #define DOCTEST_CONFIG_USE_STD_HEADERS
#endif

#include "doctest.h"

#ifdef __clang__
  #pragma clang diagnostic pop
#elif defined(__GNUC__)
  #pragma GCC diagnostic pop
#endif
