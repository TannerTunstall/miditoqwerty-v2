#pragma once

// Shims for the *_s C runtime functions used throughout the original Windows
// codebase, so settings.cpp / themes.cpp build unchanged on macOS.

#ifndef _WIN32

#include <cstdio>
#include <cstring>

inline int fopen_s(FILE** pFile, const char* path, const char* mode) {
    if (!pFile) return 1;
    *pFile = std::fopen(path, mode);
    return *pFile ? 0 : 1;
}

// Variadic sscanf_s -> sscanf. The Microsoft variant takes extra size args for
// %s/%c, but the existing call sites pass them positionally inside the
// fmt-string varargs — they're harmless to forward, sscanf just ignores them
// after consuming its expected args. For the formats actually used in this
// codebase (%f, %s, %*s) this is safe.
#define sscanf_s sscanf
#define fprintf_s fprintf

#ifndef ERROR_SUCCESS
#define ERROR_SUCCESS 0
#endif

#endif // !_WIN32
