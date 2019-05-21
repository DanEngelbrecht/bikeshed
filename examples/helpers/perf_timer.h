#pragma once

#include <cstdint>

#if defined(_WIN32)
#    include <Windows.h>
#endif

#if defined(_WIN32)
#    include <Windows.h>
#elif defined(__APPLE__)
#    include <mach/mach_time.h>
#else
#    include <sys/time.h>
#endif

static uint64_t g_TicksPerSecond = 1000000u;

inline void SetupTime()
{
#if defined(_WIN32)
        QueryPerformanceFrequency((LARGE_INTEGER*)&g_TicksPerSecond);
#elif defined(__APPLE__)
        mach_timebase_info_data_t info;
        mach_timebase_info(&info);
        g_TicksPerSecond = (info.denom * (uint64_t)1000000000ul) / info.numer;
#endif
}

inline uint64_t Tick()
{
    uint64_t now;
#if defined(_WIN32)
    QueryPerformanceCounter((LARGE_INTEGER*)&now);
#elif defined(__APPLE__)
    now = mach_absolute_time();
#else
    timeval tv;
    gettimeofday(&tv, 0);
    now = ((uint64_t)tv.tv_sec) * 1000000 + tv.tv_usec;
#endif
    return now;
}

inline double TicksToSeconds(uint64_t ticks)
{
    return (double)ticks / (double)g_TicksPerSecond;
}
