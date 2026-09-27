#pragma once

#if defined(__APPLE__)

#include <chrono>
#include <time.h>

namespace Hyprutils::OS::Darwin {
    inline std::chrono::steady_clock::time_point awakeNow() {
        return std::chrono::steady_clock::time_point{std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::nanoseconds{clock_gettime_nsec_np(CLOCK_UPTIME_RAW)})};
    }
}

#endif
