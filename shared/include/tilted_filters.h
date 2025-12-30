#pragma once

// Small, reusable filter helpers.
// Header-only; designed for fixed-size arrays (no heap).

#include <stddef.h>

// Median of N values (N must be > 0).
// Uses a simple in-place bubble sort on a copy (fine for small N).
template <size_t N>
static inline float tilted_median(const float (&values)[N])
{
    static_assert(N > 0, "tilted_median requires N > 0");

    float temp[N];
    for (size_t i = 0; i < N; i++)
        temp[i] = values[i];

    for (size_t i = 0; i < N - 1; i++)
    {
        for (size_t j = 0; j < N - i - 1; j++)
        {
            if (temp[j] > temp[j + 1])
            {
                float swap = temp[j];
                temp[j] = temp[j + 1];
                temp[j + 1] = swap;
            }
        }
    }

    if constexpr (N % 2 == 1)
        return temp[N / 2];
    else
        return (temp[N / 2 - 1] + temp[N / 2]) / 2.0f;
}
