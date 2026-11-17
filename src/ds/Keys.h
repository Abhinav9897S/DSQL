#pragma once

// hashOf / compareKeys for plain integers, used by the generic containers.
// Str and Value define their own versions next to their classes.

inline unsigned hashOf(long long x) {
    unsigned long long v = (unsigned long long)x;
    v ^= v >> 33;
    v *= 0xff51afd7ed558ccdULL;   // mix the bits so sequential ids spread out
    v ^= v >> 33;
    return (unsigned)v;
}

inline unsigned hashOf(int x) { return hashOf((long long)x); }

inline int compareKeys(long long a, long long b) { return a < b ? -1 : (a > b ? 1 : 0); }
inline int compareKeys(int a, int b) { return a < b ? -1 : (a > b ? 1 : 0); }
